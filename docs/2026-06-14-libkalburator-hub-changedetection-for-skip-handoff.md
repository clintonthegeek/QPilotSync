# libkalburator handoff — hub-side ChangeDetection so the engine's skip-unchanged actually fires for WildPalms

**Date:** 2026-06-14
**Direction:** WildPalms → libkalburator
**Status:** RESOLVED — shipped in libkalburator **v0.77** (commit `5d225d8`, main); WP
re-pinned to `v0.77` (`CMakeLists.txt`), builds clean, ctest 126/126. See §Resolution.
**Severity:** Medium. Without it, every WildPalms HotSync pass does a full Palm serial read
of every leg (no skip), so a settled multi-pass HotSync is ~Nx slower than designed.
**Pinned at:** `493bd804a549e161718986065848f0af301b5667`.

---

## TL;DR

WildPalms just landed a transparent multi-hop sync: one HotSync/FullSync propagates changes
across both hops of the `Palm — Hub — Remote` star (it runs the mapping set in a fixpoint
loop). To make the repeat passes cheap, WP gave its Palm backends collection-level
`Sync::ChangeDetection` (per-database modnum tokens) and turned on
`SyncEngine::setSkipUnchangedMappings(true)`.

But the engine only skips a mapping when **both** sides implement `ChangeDetection`
(`syncengine.cpp:725-726`: `sourceCovered && targetCovered && sourceUnchanged && targetUnchanged`).
In WildPalms the hub side never implements it:

- The hub is `Kalburator::Sinks::GenericSqliteBackend` — base `SyncBackendBase` only, no
  `ChangeDetection` (`src/universal/genericsqlitebackend.h:33`).
- Category routes wrap the hub in `Kalburator::Sinks::FilteredCollectionBackend` — also no
  `ChangeDetection`.

So for **every** WildPalms mapping the hub side is uncovered → `eligibleToSkip` is always
false → nothing is ever skipped. WP's Palm-side revision is computed and then discarded
because its paired hub side fails the AND. The whole device-token path is correct but
runtime-inert.

**Correctness is unaffected** — WP's loop terminates via `shouldContinueSync`, which keys off
`SyncStats::hasChanges()` (created/updated/deleted), not the skip machinery; a settled pass
has zero changes and the loop stops. Only the efficiency optimization is missing.

**This RFC asks libkalburator to implement `Sync::ChangeDetection` on the two hub-side
backends.** Once it does, WP's already-shipped `setSkipUnchangedMappings(true)` activates with
**no further WP change** — settled mappings skip their full fetch (and, for the Palm leg, its
slow serial device read).

---

## Request

### 1. `GenericSqliteBackend` implements `Sync::ChangeDetection`

Add a cheap per-collection revision token. Candidate implementations (lib owns the choice):

- **Preferred — a per-collection change counter.** Maintain a small `collection_revisions`
  table (`collectionId TEXT PRIMARY KEY, rev INTEGER`); bump `rev` on every
  create/update/delete that touches a collection (O(1) per write). `collectionRevision(id)`
  reads the counter (O(1)); `cachedCollectionRevision`/`primeRevisionCache` persist the
  last-synced value (a second column, or reuse the engine's prime call). This is the
  sqlite analogue of the Akonadi backend's revision store and the WP Palm modnum.
- **Alternative — `max(rowid)` + row count + a delete counter**, composed into a token.
  Cheaper to add but must account for updates/deletes (a bare `max(rowid)` misses them).

`persistsCollectionRevisions()` → true (the sqlite file persists across runs).

### 2. `FilteredCollectionBackend` forwards/derives a revision for its virtual collection

A filtered view changes iff its parent collection changes, so the conservative and correct
token is the **parent backend's `collectionRevision(parentCollectionId)`** (requires the
parent to implement `ChangeDetection` — satisfied by #1 when the parent is the sqlite hub).
If the parent doesn't implement `ChangeDetection`, return empty ("can't answer" → treated as
changed, current behavior). Conservative is fine: the filtered route re-syncs if anything in
the parent collection changed, even outside the filter — a settle pass that finds no
in-filter changes still terminates the loop via `hasChanges()`.

### Acceptance criteria

1. A `GenericSqliteBackend` collection reports a stable `collectionRevision` that changes iff
   the collection's records change; `prime`/`cached` round-trip across process restarts.
2. With both sides covered, `SyncEngine::prepareSyncFastPath` skips a settled mapping
   (`eligibleToSkip` true) — verified by a calendar-layer/engine test that syncs once, then
   re-runs with `setSkipUnchangedMappings(true)` and asserts the second run skips (no fetch).
3. On WP @ the new lib pin: a fully-settled HotSync's second/third fixpoint passes skip the
   unchanged palm↔hub legs (the device log shows `SyncEngine: skipping unchanged mapping …`
   instead of a full `DatebookDB` read), with no WP source change beyond the pin bump.
4. lib suite green; PlanStan ctest baseline green before tagging.

---

## Context: what WildPalms already shipped (so the lib side is the only missing half)

On branch `feature/multi-hop-bidirectional-sync` (spec/plan under
`docs/superpowers/{specs,plans}/2026-06-14-multi-hop-bidirectional-sync*`):

- Palm device exposes `databaseRevision(dbName)` → `KPilotLink::databaseModnum` (qint64) via
  `dlp_FindDBInfo` → `DBInfo.modnum`.
- The four Palm conduit backends implement `Sync::ChangeDetection` via a shared
  `WildPalms::PalmSync::PalmChangeDetection` mixin over a per-profile `PalmRevisionStore`
  (`<profile>/.state/palm-revisions.ini`), injected by `PalmRuntime::finishConnect`.
- `PalmRuntime::runAllMappings` runs a fixpoint loop (HotSync: skip ON, cap 3; FullSync:
  skip OFF, 2 passes) via the pure helper `shouldContinueSync`, holding the device across
  passes. `setSkipUnchangedMappings(true)` is already on for HotSync — it simply never fires
  because the hub side is uncovered.

So the engine and the Palm side are ready; implementing #1 + #2 makes the optimization live.

---

## Not libkalburator's

The propagation feature (Goal 1) is complete WP-side and needs nothing here. This RFC is
purely the efficiency half (Goal 2). If libkalburator declines or defers, WP's HotSync still
propagates correctly — it just does full device reads on each fixpoint pass until this lands.

---

## Resolution (CLOSED) — libkalburator v0.77, consumed 2026-06-14

Shipped in **v0.77** (commit `5d225d8` on main; lib ctest 151/151, PlanStan gate clean):

- **`GenericSqliteBackend`** now implements `Sync::ChangeDetection`
  (`src/universal/genericsqlitebackend.{h,cpp}`). `collectionRevision()` is a per-collection
  **content digest** (SHA-256 over ordered `(record_id, content_hash)` tuples), with the
  engine-primed baseline persisted in a new `_collection_revisions` table.
- **`FilteredCollectionBackend`** (moved to `src/universal/filteredcollectionbackend.{h,cpp}`)
  derives its token from the parent collection's revision (conservative; non-capable/null
  parent → empty).

**Design note from the lib (matters for our convergence):** they deliberately did NOT use a
write counter (this RFC's "preferred" option). The engine re-writes records back to both
backends on every TwoWay reconcile, so a counter never settles — it chases its own writes. A
content digest is stable under idempotent re-writes, so settled collections converge.
Practically: the `synced_rev` baseline persists across HotSync sessions, so a settled hub
skips on **pass 1 of the next session**; within a session, propagating a real change costs one
extra confirming pass before it skips again — well within WP's cap-3 fixpoint.

**WP consumption:** pin v0.77 (`CMakeLists.txt`); **no WP source change** — the
already-shipped `setSkipUnchangedMappings(true)` now activates because both mapping sides
(hub + Palm) implement `ChangeDetection`. Build clean, ctest 126/126.

**Verification (RFC criterion 3) — on-device, user step:** a fully-settled HotSync's later
fixpoint passes should log `SyncEngine: skipping unchanged mapping …` instead of a full
`DatebookDB` serial read.

**FYI (not WP's problem):** the lib flagged a pre-existing latent bug — two
`GenericSqliteBackend`s syncing each other accumulate duplicates (collection-prefixed record
ids grow each round-trip). Does NOT affect WP (our hub's peers are Palm / CalDAV with stable
bare ids); tracked lib-side as a future follow-up.
