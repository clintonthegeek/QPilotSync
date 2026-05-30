# WildPalms — libkalburator P3 port (neutralize sync core)

**Date:** 2026-05-28
**Status:** Design approved, ready for plan
**Scope:** WildPalms only (no libkalburator changes)
**Upstream campaign:** `libkalburator/docs/campaign/architectural-redress/plans/plan-3-neutralize-sync-core.md` (landed on lib `main` at `cd798b3`, tasks P3.T1–P3.T8)

---

## 1. Problem

The user's local `/home/clinton/dev/libkalburator` is on a branch
(`feature/redress-3-neutralize-sync-core`, ahead of v0.60) where the
**sync core has been neutralized**: non-calendar backends no longer carry
calendar-typed assumptions, and `BackendRegistry` / `fetchItems` / `deleteItems`
traffic in domain-neutral types.

WP builds against this local lib via `WILDPALMS_LIBKALBURATOR_SOURCE_DIR`.
The build currently fails with 5 compile errors:

1. `src/palm/calendar/palmcalendarbackend.h:75` — invalid covariant return
   type on `fetchItems` (returns `FetchOperation*`, base now returns
   `SyncOperation*`; `FetchOperation` is not a complete type at the
   override point).
2. `src/palm/calendar/palmcalendarbackend.h:80` — same for `deleteItems`
   / `DeleteOperation*`.
3. `src/runtime/palmruntime.cpp:93` — `PalmSyncHost::backends()` calls
   `QHash<QString, SyncBackend*>::insert(QString, m_registry->backendInstance(id))`,
   but `backendInstance` now returns `SyncBackendBase*` which is not a
   `SyncBackend*`.
4. `src/runtime/palmruntime.cpp:87` (latent) — same mismatch in
   `PalmSyncHost::backendById`.
5. `src/runtime/synchost_wp.cpp:21,26` — the second `ISyncHost`
   implementation has the same mismatch as `PalmSyncHost`.

Beyond the build errors, six WP backends inherit calendar-typed
`SyncBackend` despite being non-calendar (contacts, memo, todo). They
carry dead calendar pure-virtual stubs (`loadCalendars`, `storeCalendars`,
`startSync`, `removeItem`, calendar-typed `pushItems`) — a parallel
problem the lib's P3 already solved upstream by reparenting its own
non-calendar backends onto `SyncBackendBase`. WP should follow.

## 2. Goals / Non-goals

### Goals

- WP builds clean against the post-P3 local libkalburator (and stays
  green against v0.60 — backwards-compatible).
- The 6 non-calendar WP backends inherit `SyncBackendBase` directly, not
  `SyncBackend`. Their calendar-stub overrides are deleted.
- The 2 `ISyncHost` implementations (`PalmSyncHost`, `SyncHost_WP`)
  bridge the registry's new `SyncBackendBase*` return type to
  `ISyncHost`'s still-calendar-typed `SyncBackend*` contract via
  `dynamic_cast` at the boundary.
- Calendar-typed `PalmCalendarBackend` (palm-level + plugin-level) keeps
  its `SyncBackend` base; gains the missing
  `#include "syncoperation.h"` so its covariant `FetchOperation*` /
  `DeleteOperation*` returns are legal.
- Full WP ctest suite stays green at every task boundary. 117/117 from
  D's landing is the floor.

### Non-goals

- **No libkalburator edits.** Standing cross-repo rule. The lib's P3 work
  is upstream-correct.
- **No `ISyncHost` lift to `SyncBackendBase` in WP-local code.**
  `ISyncHost` lives in `calendar/` in the lib, will stay calendar-typed,
  and WP can't change it. The engine doesn't need non-calendar backends
  through `ISyncHost::backends()` — it talks to `BackendRegistry`
  directly post-P3. Filtering via `dynamic_cast` is the long-term
  answer, not a hack.
- **No reparenting of `PalmCalendarBackend`.** Calendar backends are
  meant to inherit `SyncBackend` (calendar-typed). Both palm-level and
  plugin-level wrappers stay.
- **No new tests written for this port.** The existing WP suite
  (117 binaries) already exercises every backend through the engine and
  the plugin createPalmBackend path. A new "proof of neutrality" test
  would duplicate libkalburator's `tst_neutral_sync_core.cpp` which
  already pins the lib contract.
- **No changes to plugin Q_OBJECT signal/slot wiring.** Backends keep
  their existing signals; reparenting only affects the C++ inheritance
  chain.

## 3. Approach

Sequenced in two layers:

**Layer 1 — Compile fixes.** Single task. Three small changes:
- `src/palm/calendar/palmcalendarbackend.h` — add `#include "syncoperation.h"`
- `src/plugins/calendar/palmcalendarbackend.h` — same include (calendar
  submodule — needs a submodule commit + superproject gitlink bump)
- `src/runtime/palmruntime.cpp` `PalmSyncHost::backendById` + `backends()` —
  `dynamic_cast<SyncBackend*>` each registry entry; skip nulls in
  `backends()`
- `src/runtime/synchost_wp.cpp` — same fix as `PalmSyncHost`

After Layer 1, WP builds against both v0.60 and the post-P3 local lib.

**Layer 2 — Reparent 6 non-calendar backends, one task per backend.**

For each: change base from `Kalburator::Sync::SyncBackend` to
`Kalburator::Sync::SyncBackendBase`; delete now-unneeded calendar
override declarations and bodies; drop `#include <KCalendarCore/...>` and
the calendar `syncbackend.h` include from headers where possible
(replace with `<syncbackendbase.h>`); preserve `IBlobBackend` methods
(`createRecord`/`updateRecord`/`deleteRecord`/`loadRecordsOrError`); if a
backend overrides neutral `fetchItems`/`deleteItems`, keep them but
return `SyncOperation*` (not `FetchOperation*`).

The 6 backends:

| # | Path | Repo | Domain |
|---|---|---|---|
| 2 | `src/palm/contacts/palmcontactsbackend.{h,cpp}` | superproject | contacts |
| 3 | `src/palm/memo/palmmemobackend.{h,cpp}` | superproject | memo |
| 4 | `src/palm/todo/palmtodobackend.{h,cpp}` | superproject | todo |
| 5 | `src/plugins/contacts/palmcontactsbackend.{h,cpp}` | contacts submodule | contacts |
| 6 | `src/plugins/memo/memoblobbackend.{h,cpp}` | memo submodule | memo |
| 7 | `src/plugins/todos/todoblobbackend.{h,cpp}` | todos submodule | todo |

(There are two `PalmContactsBackend` classes — one at `src/palm/contacts/`
and one at `src/plugins/contacts/`. They serve different layers — the
palm-level one is a thin DLP adapter; the plugin-level one is the
backend the plugin's V2 ABI exposes — but both are non-calendar and both
reparent.)

## 4. Architecture

```
                                                 BEFORE                  AFTER
SyncBackend (calendar)        ←──────────────  6 non-calendar       no longer
                                                WP backends           inherits

SyncBackendBase (neutral)     ←──────────────  GenericSqliteBackend,
                                                RawFilesBackend,        ← 6 WP
                                                FilteredCollectionBackend,  backends
                                                + (after this port)        ←───┘
                                                  6 WP non-calendar
                                                  backends
```

### `ISyncHost` adapter (Layer 1)

```cpp
// PalmSyncHost / SyncHost_WP, post-port

Kalburator::Sync::SyncBackend* backendById(const QString &id) override {
    auto *b = m_registry ? m_registry->backendInstance(id) : nullptr;
    return dynamic_cast<Kalburator::Sync::SyncBackend*>(b);
    // Non-calendar backends → nullptr. ISyncHost is a calendar-domain
    // interface; the engine fetches non-calendar backends directly
    // from BackendRegistry post-P3.
}

QHash<QString, Kalburator::Sync::SyncBackend*> backends() override {
    QHash<QString, Kalburator::Sync::SyncBackend*> result;
    if (!m_registry) return result;
    for (const QString &id : m_registry->registeredInstanceIds()) {
        if (auto *cb = dynamic_cast<Kalburator::Sync::SyncBackend*>(
                m_registry->backendInstance(id))) {
            result.insert(id, cb);
        }
    }
    return result;
}
```

### Per-backend reparent template

```cpp
// BEFORE
#include "syncbackend.h"   // calendar

class PalmContactsBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    // calendar pure virtuals (now dead)
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(const QString &, const QList<...> &) override;
    void startSync(...) override;
    void removeItem(const QString &, const QString &) override;
    Kalburator::Sync::PushOperation *pushItems(
        const QString &, const QList<KCalendarCore::Incidence::Ptr> &) override;
    // ... neutral IBlobBackend methods stay
};

// AFTER
#include "syncbackendbase.h"   // neutral

class PalmContactsBackend : public Kalburator::Sync::SyncBackendBase {
    Q_OBJECT
public:
    // calendar-typed overrides deleted entirely
    // (loadCalendars/storeCalendars/startSync/removeItem/calendar pushItems)
    // ... neutral IBlobBackend methods stay
};
```

The body deletions are mechanical: remove the declarations from the
header, remove the implementations from the cpp, drop any
KCalendarCore includes that become unused.

## 5. Components & files

### Modified files (Layer 1 — Task 1)

| Path | Change |
|---|---|
| `src/palm/calendar/palmcalendarbackend.h` | Add `#include "syncoperation.h"` |
| `src/plugins/calendar/palmcalendarbackend.h` | Add `#include "syncoperation.h"` (calendar submodule) |
| `src/runtime/palmruntime.cpp` | `PalmSyncHost::backendById` + `backends()` dynamic_cast bridge |
| `src/runtime/synchost_wp.cpp` | Same fix in second ISyncHost impl |

### Modified files (Layer 2 — Tasks 2–7, one per backend)

For each backend, header + cpp:

| Path | Repo | Task |
|---|---|---|
| `src/palm/contacts/palmcontactsbackend.{h,cpp}` | superproject | 2 |
| `src/palm/memo/palmmemobackend.{h,cpp}` | superproject | 3 |
| `src/palm/todo/palmtodobackend.{h,cpp}` | superproject | 4 |
| `src/plugins/contacts/palmcontactsbackend.{h,cpp}` | contacts submodule | 5 |
| `src/plugins/memo/memoblobbackend.{h,cpp}` | memo submodule | 6 |
| `src/plugins/todos/todoblobbackend.{h,cpp}` | todos submodule | 7 |

### No new files

The port is pure subtraction (deleting calendar stubs) plus include
adjustments. No new headers, classes, or tests.

## 6. Data flow

No data-flow change. The port is a structural refactor of the C++
inheritance hierarchy; all live code paths (engine → registry →
backend, engine → host → backend, plugin → backend instantiation)
preserve their semantics.

The only observable behavior change: `ISyncHost::backends()` no longer
includes non-calendar backends. This is correct because:
- Pre-port, those backends inherited `SyncBackend` and appeared in the
  hash, but the engine only used them via `IBlobBackend` methods anyway.
- Post-port, the engine fetches them directly through `BackendRegistry`
  by id, not by walking `ISyncHost::backends()`.

If any production WP code DID rely on iterating `backends()` to find
non-calendar backends, the test suite would catch it. None observed
during exploration.

## 7. Error handling

No runtime error model change. `dynamic_cast` returning `nullptr` is
silently skipped — there is no error condition because non-calendar
backends in `ISyncHost::backends()` is by definition out-of-domain, not
a fault.

`Q_ASSERT(hub)` in reader constructors (existing) is unaffected.

## 8. Testing strategy

**No new tests.** The existing WP suite (117 binaries from D's landing)
already exercises every backend through:
- Per-domain `tst_<plugin>backendplugin` tests (instantiate each plugin,
  call `createPalmBackend`, exercise the backend)
- Per-domain `tst_<foo>_view_reads_hub` tests (D's integration tests)
- `tst_palm_runtime_modes` / `tst_palm_runtime_route_first_sync` /
  `tst_palm_runtime_route_recategorization` (run hotSync end-to-end with
  the full backend set wired)

The port is "behavior-preserving structural refactor" — same discipline
as the lib's P3. If a test fails, the refactor changed behavior and the
test is the truth.

**Per-task verification:**
- After each task: `ctest --test-dir build-dev --output-on-failure -j$(nproc)` must report 117/117 pass.
- At sub-project end: run a scratch FetchContent build (`build-rcheck` against v0.60 from Codeberg) to confirm backwards-compatibility — Layer 1's includes and Layer 2's reparenting must work against both lib branches.

## 9. Success criteria

- `cmake --build build-dev` produces 0 errors.
- `ctest --test-dir build-dev` reports 117/117 pass (D's baseline preserved).
- `cmake --build build-rcheck` (fresh FetchContent against v0.60) also produces 0 errors and 117/117.
- The 6 non-calendar WP backends inherit `Kalburator::Sync::SyncBackendBase` directly. Verified by `grep -rn "public Kalburator::Sync::SyncBackend\b"` matching only the 2 calendar `PalmCalendarBackend` classes.
- `ISyncHost::backends()` returns only `SyncBackend*`-castable entries; non-calendar backends in `BackendRegistry` are silently skipped at the host boundary.
- No `#include <KCalendarCore/...>` lines remain in the 6 reparented backend headers (cpps may keep them if implementations genuinely need them; expected to be rare for non-calendar backends).

## 10. Out of scope / handoffs

- libkalburator's `ISyncHost` could be reparented onto `SyncBackendBase` upstream (replacing the `calendar/` location). That's a libkalburator-side decision; if the lib team makes it, WP's `dynamic_cast` adapters become candidates for simplification. Not part of this port.
- The `SyncHost_WP` class (separate from `PalmSyncHost` — exists at `src/runtime/synchost_wp.{h,cpp}`) may be redundant with `PalmSyncHost` and a candidate for retirement during the broader "reconcile accreted layers" work (umbrella sub-project F). Not in scope here.
- The calendar `pushItems` (taking `QList<KCalendarCore::Incidence::Ptr>`) override on `PalmCalendarBackend` stays — it's the calendar backend's contract.
- Plucker and Install action plugins are unaffected (Plucker inherits `Kalburator::Plugin`, Install uses `IPluginAction` — neither inherits any `SyncBackend*` variant).
