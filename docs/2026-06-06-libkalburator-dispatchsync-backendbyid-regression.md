# Handoff to libkalburator: `dispatchSync` `backendById` regression (RFC)

**Date:** 2026-06-06
**From:** WildPalms (`feature/three-tier-sync` @ `f826612`)
**To:** libkalburator maintainer (+ PlanStan as co-consumer for green-gate)
**Status:** open RFC. Triage complete; root cause confirmed; requesting
a small surgical fix on the engine side.

> **Process note.** This follows the standard
> WP-writes-RFC / lib-team-lands flow
> (`feedback_libkalburator_handoff_workflow`). PlanStan must stay green
> per `feedback_planstan_pretest_for_upstream` before tagging.

---

## 0. TL;DR

After Plan 3 reparented non-calendar backends onto `SyncBackendBase`,
the engine's `dispatchSync` (and four sibling sites) was NOT migrated
to fetch backends via `BackendRegistry::backendInstance` like the rest
of the engine does. They still call `m_controller->backendById()` which
returns `SyncBackend*` — and a `SyncBackendBase`-only backend (the
canonical hub, CardDAV, post-P3 plugin backends) can't satisfy that
return type. WP's `PalmSyncHost::backendById` does a type-correct
`dynamic_cast<SyncBackend*>` on the registry result and returns
`nullptr` for those backends; the engine then bails with
`"dispatchSync: backend not found"` and the sync silently fails.

Three WildPalms tests have been failing since the v0.63 pin bump as a
direct result:

- `tst_palm_runtime_route_first_sync`
- `tst_palm_runtime_route_recategorization`
- `tst_runtime_carddav_e2e`

The triage doc at
`docs/2026-06-04-v0.63-pin-bump-test-regressions.md` records the full
investigation. This RFC requests a targeted fix on the engine side.

---

## 1. Evidence

Diagnosed by adding `qWarning() << fut.resultAt(0).errorMessage;` to
each failing test before its `QVERIFY`. All three tests print the
same engine error:

```
dispatchSync: backend not found
```

Source: `src/engine/syncengine.cpp:1933-1939` (v0.65 pin).

```cpp
SyncBackend *srcBackend = m_controller->backendById(request.mapping.sourceBackend);
SyncBackend *tgtBackend = m_controller->backendById(request.mapping.targetBackend);
if (!srcBackend || !tgtBackend) {
    m_currentResult.success = false;
    m_currentResult.errorMessage = QStringLiteral("dispatchSync: backend not found");
    m_currentResult.endTime = QDateTime::currentDateTime();
    emit syncCompleted(request.mapping.id, m_currentResult);
    return true;
}
```

`ISyncHost::backendById` returns `SyncBackend*`
(`src/calendar/isynchost.h:29`). WP's impl
(`WildPalms/src/runtime/palmruntime.cpp:92-100`):

```cpp
Kalburator::Sync::SyncBackend* backendById(const QString &id) override {
    if (!m_registry) return nullptr;
    return dynamic_cast<Kalburator::Sync::SyncBackend*>(
        m_registry->backendInstance(id));
}
```

The `dynamic_cast` is forced by the interface — and is type-correct
when applied to a registry entry whose dynamic type is only
`SyncBackendBase`. It returns `nullptr` for:

- `Kalburator::Sinks::GenericSqliteBackend` — the canonical hub
  (registered in WP as `wp-hub`, used as the source backend in EVERY
  mapping that goes through the hub).
- `RemoteContactsBackend` (CardDAV).
- Other post-Plan-3 plugin backends and `FilteredCollectionBackend`
  view backends.

The inline comment in WP's `PalmSyncHost::backendById` reads
"the engine fetches non-calendar backends directly from
BackendRegistry via `SyncBackendBase*` post-P3." That assumption holds
for SIX sites in the engine
(`syncengine.cpp:759, 764, 777, 802, 1107, 1214`) — they all use
`m_registry->backendInstance(id)` returning `SyncBackendBase*`. But
FIVE sites — `dispatchSync` and friends at lines
`1709, 1868, 1931, 2592` — still use `m_controller->backendById()`.
The architectural intent of Plan 3 is implemented partially.

## 2. What the engine actually does with the returned pointer

Greppable on the v0.65 source:

```
1952:    const Kalburator::Shape::Shape srcShape = srcBackend->shapeFor(srcColId);
1953:    const Kalburator::Shape::Shape tgtShape = tgtBackend->shapeFor(tgtColId);
2084:            fetchOpRaw = srcBackend->fetchItems(srcColId);
2182:            fetchOpRaw = tgtBackend->fetchItems(tgtColId);
1778:    const bool tgtWritable = tgtBackend->discoveredWritable(colId);
2609:    const Kalburator::Shape::Shape srcShape = srcBackend->shapeFor(srcColId);
2610:    const Kalburator::Shape::Shape tgtShape = tgtBackend->shapeFor(tgtColId);
```

Every method called on the `srcBackend`/`tgtBackend` pointer in
`dispatchSync` (and the other four sites) is a `SyncBackendBase`
virtual. None of them require the dynamic type to be `SyncBackend` —
the `SyncBackend*` return type is vestigial.

## 3. Proposed fix

In libkalburator's `src/engine/syncengine.cpp`, replace
`m_controller->backendById(...)` with
`m_registry->backendInstance(...)` at these five sites and change the
local typed pointer to `SyncBackendBase*`:

```diff
- SyncBackend *srcBackend = m_controller->backendById(request.mapping.sourceBackend);
- SyncBackend *tgtBackend = m_controller->backendById(request.mapping.targetBackend);
+ SyncBackendBase *srcBackend = m_registry->backendInstance(request.mapping.sourceBackend);
+ SyncBackendBase *tgtBackend = m_registry->backendInstance(request.mapping.targetBackend);
```

Affected line ranges (v0.65):

- `syncengine.cpp:1526-1527` (the `m_controller`-using LossProfile
  prep inside `processSync`)
- `syncengine.cpp:1709-1710`
- `syncengine.cpp:1868`
- `syncengine.cpp:1931-1932` (`dispatchSync`'s null-guard — the one
  the failing tests hit)
- `syncengine.cpp:2592`

The downstream code calls only `SyncBackendBase` virtuals on these
pointers (`shapeFor`, `fetchItems`, `discoveredWritable`,
`pushItems`), so no further code change should be needed in the
function bodies — only the local type and the lookup call.

If the engine still needs `ISyncHost::backendById` for any
calendar-specific reason (the interface lives in `src/calendar/`),
that's fine — keep it for legacy calendar paths. But `dispatchSync`
and its peers should no longer route through it; the registry is the
source of truth post-Plan-3.

### Alternative

If you'd prefer to migrate the interface rather than each call site,
`ISyncHost::backendById` could be changed to return `SyncBackendBase*`
directly (and `ISyncHost` moved out of `src/calendar/` since it no
longer is calendar-specific). That's more invasive — every consumer
of `ISyncHost::backendById` would need to adjust. The five-site
registry switch is the smaller delta.

## 4. Acceptance criteria

- Adjust the five sites in `dispatchSync`/peers to fetch from
  `BackendRegistry`.
- `tests/calendar/` integration tests (the stub-`ISyncHost` harness)
  still pass — those exercise the calendar code path and are the
  contract Plan 3's engine-merger refactor preserved.
- PlanStan ctest baseline remains green
  (`feedback_planstan_pretest_for_upstream` gate).
- WildPalms's three failing tests turn green automatically on the WP
  pin bump after the fix lands:
  - `tst_palm_runtime_route_first_sync`
  - `tst_palm_runtime_route_recategorization`
  - `tst_runtime_carddav_e2e`

WP will pin-bump and verify in a separate commit (the standard pin-bump
ritual). No WP-side code change is required for the fix itself.

## 5. Why not fix this in WP

A WP-side workaround would mean inserting a `SyncBackend`-derived
adapter shim in `PalmSyncHost::backendById` that wraps any
`SyncBackendBase`-only backend on the fly so the `dynamic_cast`
returns non-null. That works locally but:

- It's type-correct only because the engine never actually calls a
  `SyncBackend`-specific virtual on the returned pointer — a fragile
  invariant to depend on long-term.
- It leaks library-side scaffolding into the consumer, which is
  precisely the pattern `feedback_library_vs_backend_responsibility`
  warns against.
- It would have to be removed once libkalburator lands the proper fix.

The engine-side fix is small (five `m_controller->backendById` calls
→ `m_registry->backendInstance`), exactly matches the pattern the rest
of the engine already follows post-Plan-3, and removes a partial
migration smell.

## 6. References

- WP triage doc:
  `WildPalms/docs/2026-06-04-v0.63-pin-bump-test-regressions.md`
- WP impl that hits the symptom:
  `WildPalms/src/runtime/palmruntime.cpp:92-100`
  (`PalmSyncHost::backendById`)
- Engine sites to fix (v0.65 pin):
  `src/engine/syncengine.cpp:1526, 1709, 1868, 1931, 2592`
- Engine sites that already do it right (reference pattern):
  `src/engine/syncengine.cpp:764, 777, 802, 1107, 1214`
