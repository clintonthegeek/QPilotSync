# Handoff to libkalburator: clobber-sync surface (RFC)

**Date:** 2026-06-05
**From:** WildPalms (`feature/three-tier-sync`)
**To:** libkalburator maintainer (+ PlanStan as co-consumer for green-gate)
**Status:** open RFC. WP design + plan landed; this is what we need from
the library before the WP-side wiring can build.

> **Process note.** This follows the standard WP-writes-RFC /
> lib-team-lands flow (`feedback_libkalburator_handoff_workflow`). The
> downstream pin bump will be a single dedicated commit after the tag.

---

## 0. TL;DR

WildPalms is adding a "Clobber Palm from PC" Tools-menu mode that
wipes selected Palm-side PIM databases and re-pushes from the hub in
one operation. It unblocks repeatable freshen-Palm test loops and
exposes a general "freshen Palm from authoritative desktop" capability
to end users. To implement it cleanly without leaking Palm specifics
upstream, libkalburator needs three small additions:

1. **`ExecutionOverride::clobber`** — a domain-neutral flag that
   causes the engine to skip baseline+mass-delete-guard, call
   `targetBackend->wipeCollection`, then push.
2. **`SyncRequest` semantics relaxed** so the `clobber` flag applies
   on multi-mapping subset dispatch (today `executionOverride` only
   applies on `isSingleMapping()`).
3. **`IBlobBackend::wipeCollection(collectionId)`** — a default
   implementation provided inline (iterate `loadRecords` +
   `deleteRecord`); Palm-side backends override with a fast path.

All three are domain-neutral; libkalburator gains no Palm knowledge.

---

## 1. Motivation

Today, the only way for a WildPalms user (or test loop) to "wipe the
Palm and re-push from desktop" without triggering the mass-delete
guard's "Palm-side mass delete → propagate to desktop" interpretation
is to recreate the WildPalms profile from scratch. That's fine once;
it's intolerable per-cycle for development.

The complete WildPalms design context is at
`WildPalms/docs/superpowers/specs/2026-06-05-clobber-sync-design.md`
(commit `ae258a1` on `feature/three-tier-sync`). The implementation
plan is `WildPalms/docs/superpowers/plans/2026-06-05-clobber-sync.md`.

The three additions are the smallest libkalburator surface that lets
WP own the Palm specifics inside its conduit backends while the engine
orchestrates the clobber semantics generically.

---

## 2. Change 1: `ExecutionOverride::clobber` flag

### Header diff

`src/types/synctypes.h`:

```cpp
 struct ExecutionOverride {
     enum class Direction {
         Default,
         MirrorAToB,
         MirrorBToA,
     };
     Direction direction = Direction::Default;
+
+    /// When true, the engine treats this mapping as a clobber:
+    ///   1. Skip baseline load (treat as first sync).
+    ///   2. Skip mass-delete-guard hook (no deletes will be computed).
+    ///   3. Call targetBackend->wipeCollection(targetCollectionId).
+    ///   4. Push source records to the now-empty target.
+    ///   5. Write a fresh baseline at end-of-sync as normal.
+    ///
+    /// `direction` is silently ignored when `clobber == true`;
+    /// effective direction is always source → target.
+    ///
+    /// Unlike `direction`, this flag also applies on multi-mapping
+    /// subset dispatch (see SyncRequest doc).
+    bool clobber = false;
 };
```

### Engine semantics

For each mapping dispatched with `clobber == true`:

1. **Skip baseline load.** Engine does not consult `BaselineStore`
   for this mapping. Behaves as if no baseline exists.
2. **Skip mass-delete-guard hook.** Engine does not invoke
   `IMassDeleteGuard::confirmMassDelete` for this mapping, even if a
   guard is installed. (Justification: no deletes will be computed —
   the wipe replaces the diff. The guard exists to prompt the user
   before accidental deletes; clobber IS the user's authorization.)
3. **Pre-push wipe.** Engine calls
   `targetBackend->wipeCollection(targetCollectionId)`. On failure,
   the per-mapping `SyncResult` reports failure and the mapping does
   not proceed (other mappings in the request are unaffected — same
   per-mapping isolation hot-sync already has).
4. **Push.** Engine pushes source records to the (now empty) target,
   using the same push path as first-sync.
5. **End-of-sync baseline write.** Same as a normal sync — a fresh
   baseline is written at completion.

### Tests requested

In `tests/engine/` (or wherever current SyncEngine unit/integration
tests live):

- `tst_syncengine_clobber_single_mapping` — clobber=true with one
  mapping; assert `wipeCollection` was called, baseline not loaded
  pre-run, baseline written post-run, all source records arrive at
  target.
- `tst_syncengine_clobber_multi_mapping` — clobber=true on a
  subset-dispatch `SyncRequest` (2+ mapping IDs); assert each mapping
  runs the clobber semantics independently.
- `tst_syncengine_clobber_mass_delete_guard_silenced` — install an
  `IMassDeleteGuard` mock that fails the test if `confirmMassDelete`
  is called; run a clobber whose pre-clobber diff would have been a
  mass delete; assert the mock is never called.

---

## 3. Change 2: `SyncRequest` semantics relaxed for clobber

### Background

Today `SyncRequest::executionOverride` only applies when
`isSingleMapping()`. From `src/engine/syncrequest.h`:

```cpp
/// Per-call execution override. Only meaningful when
/// mappingIds.size() == 1. Ignored for all-enabled and subset
/// dispatch (the historical API only ever accepted an override
/// on the single-mapping overload).
std::optional<Kalburator::Sync::ExecutionOverride> executionOverride;
```

For clobber to work in the multi-conduit UX (user ticks Calendar +
Contacts + Memo + ToDo), WP wants to send ONE `SyncRequest` with all
four mapping IDs and `clobber=true`. The current rule blocks this.

### Requested change

Relax the rule **specifically for the `clobber` flag**:

- When `executionOverride.clobber == true`, the flag applies to every
  mapping in `mappingIds`, regardless of subset / all-enabled shape.
  Each mapping runs the clobber semantics independently.
- `executionOverride.direction` continues to apply only on
  `isSingleMapping()`. Multi-mapping subset dispatch with a non-Default
  `direction` continues to ignore it (existing behavior).

### Doc-comment update

The doc-comment quoted above should be reworded to say "the historical
API only ever accepted an override on the single-mapping overload"
applies to `direction` specifically, and call out that `clobber`
broadens to subset dispatch.

### Tests

The multi-mapping test in §2 covers this transitively. No separate
test is requested.

---

## 4. Change 3: `IBlobBackend::wipeCollection`

### Header diff

`src/blob/iblobbackend.h`:

```cpp
 class IBlobBackend {
 public:
     virtual ~IBlobBackend() = default;
     // ...

     // Mutation
     virtual QString createRecord(const QString &collectionId, ...) = 0;
     virtual bool    updateRecord(const BackendRecord &record) = 0;
     virtual bool    deleteRecord(const QString &recordId) = 0;
+
+    /// Delete every record in the collection, leaving it empty but
+    /// usable. The default implementation iterates loadRecords +
+    /// deleteRecord (slow but always works). Backends MAY override
+    /// with a fast path (drop+recreate, TRUNCATE, etc.). The
+    /// collection itself MUST still exist after this call returns
+    /// successfully — only its records are gone.
+    ///
+    /// Returns true on success, false if any per-record delete failed
+    /// (in which case the collection is in an indeterminate state).
+    virtual bool wipeCollection(const QString &collectionId) {
+        bool ok = true;
+        for (const auto &rec : loadRecords(collectionId)) {
+            ok = deleteRecord(rec.id) && ok;
+        }
+        return ok;
+    }

     // ...
 };
```

The inline default impl makes this a **non-breaking addition** — every
existing backend compiles unchanged and gets a slow-but-correct
implementation. Backends that want a fast path override.

WildPalms's four Palm-side blob backends
(`PalmCalendarBackend`, `PalmContactsBackend`, `MemoBlobBackend`,
`TodoBlobBackend`) will override with `dlp_DeleteDB` + `dlp_CreateDB`
calls — implementation lives in WP's conduit submodules, not
upstream.

### Test requested

In `tests/blob/` (or wherever IBlobBackend unit tests live):

- `tst_iblobbackend_default_wipeCollection` — populate a
  `MockBlobBackend` with N records; call `wipeCollection`; assert
  `loadRecords` returns empty afterward.

---

## 5. PlanStan-green gate

Per `feedback_planstan_pretest_for_upstream`: please run PlanStan's
full ctest against the change before cutting the tag. The clobber
flag default is `false` and `wipeCollection` default impl matches the
naive existing pattern, so PlanStan should be unaffected — but the
gate exists for a reason.

---

## 6. Tag request

After the change lands on libkalburator `main` and PlanStan-green
passes, please cut a tag — `v0.65` is the natural next number after
the current `v0.64` (LastWriteWins tie-bias fix).

WildPalms will bump its pin (`CMakeLists.txt:63`) v0.64 → v0.65 in a
single dedicated commit and then proceed with the WP-side wiring per
its plan.

---

## 7. References

- WildPalms spec:
  `WildPalms/docs/superpowers/specs/2026-06-05-clobber-sync-design.md`
- WildPalms plan:
  `WildPalms/docs/superpowers/plans/2026-06-05-clobber-sync.md`
- Previous handoff template:
  `WildPalms/docs/2026-05-28-libkalburator-sqlite-thread-safety-handoff.md`
- libkalburator current state at handoff time:
  `main` at `c0d0336` / tag `v0.64` (LastWriteWins tie-bias).

Reach out if any of the above doesn't match what you're seeing, or if
the relax-for-clobber-only semantics in §3 feel surgical in a way that
suggests a broader rule worth picking instead.
