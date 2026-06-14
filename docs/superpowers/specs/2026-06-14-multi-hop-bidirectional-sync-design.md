# Transparent multi-hop bidirectional sync — design

**Date:** 2026-06-14
**Status:** Design approved; implementation plan pending.
**Scope:** WildPalms-only. No libkalburator change required (uses existing public
`Sync::ChangeDetection` mixin and `SyncEngine::setSkipUnchangedMappings`).
**Roadmap:** closes item #2 ("Hub↔remote-only sync gap") for the inbound direction and
makes a single HotSync/FullSync propagate both directions across both hops.

---

## Problem

WildPalms sync is a depth-1 star: `Palm — Hub — Remote` (Akonadi / DAV). A user-initiated
sync runs each mapping exactly once, in `m_mappings` order (palm↔hub legs first, then
hub↔remote routes — `PalmRuntime::runAllMappings`, `src/runtime/palmruntime.cpp:832`). The
engine honors that order (no internal topological sort; `m_queue.next()` is FIFO).

A single pass therefore moves data across **one** hop only. Tracing both directions against
the leg-first order:

- **Outbound (Palm → Remote) already completes in one pass.** The palm↔hub leg writes the
  Palm-side change into the hub, then the later hub↔remote route observes the fresh hub
  data and pushes it to the remote.
- **Inbound (Remote → Palm) does not.** The route fills the hub *after* the leg already
  ran, so remote records reach the hub but never the Palm in that pass.

On-device evidence (2026-06-14): an Akonadi calendar synced **83 events into `hub.db`** but
**0 onto the Palm** — the inbound failure above. (A second HotSync delivers them, which is
the symptom we are removing.)

## Goals

1. One HotSync or FullSync propagates changes **both directions across both hops**
   transparently — no second manual run.
2. Repeat passes are **cheap**: a mapping whose collections are unchanged skips its full
   fetch/diff (and, for the Palm, its serial device read) entirely.
3. Entirely WP-side; preserve TwoWay merge/conflict semantics (no directional-overwrite
   restructuring).

## Non-goals

- Deeper-than-depth-1 topologies (none exist in WP). The loop handles them if they appear,
  but we do not design for them specifically.
- Clobber and Mirror multi-hop. They stay single-hop, deliberate one-shot operations.
- A general Palm delta-sync / record-level change tracking. We add only a **collection-level**
  revision (per-database modification number).
- Any libkalburator change.

---

## Approach: enable change-detection skip + fixpoint loop

Keep the existing leg-first mapping order. Wrap the multi-mapping run in a **fixpoint loop**
that re-runs the enabled set until a pass makes no changes, and enable the engine's
**skip-unchanged** fast path so settled mappings cost nothing. Three layers, all WP-side.

### Layer A — Palm device collection revision

`IPalmDatabaseAccess` (`src/palm/sync/ipalmdatabaseaccess.h`) gains:

```cpp
/// Cheap collection-change token: the Palm database's modification number.
/// Empty string = "cannot answer cheaply" (caller treats the collection as changed).
virtual QString databaseRevision(const QString &dbName) const = 0;
```

- **`KPilotDeviceLink`** implements it via `dlp_FindDBInfo` → `DBInfo.modnum`
  (already used at `src/palm/kpilotdevicelink.cpp:1244-1296` for viewer-app checks).
  One DLP call, **no record traversal**. Marshaled to the link thread like every other DLP
  call (`BlockingQueuedConnection`, per the `runAllMappings` threading note).
- **`MockPalmDatabaseAccess`** implements it from a settable per-db counter that bumps on
  writes (so tests can drive change detection deterministically).
- `modnum` increments on every record change. Caveat: **our own writes bump it**, so after
  a sync that writes a Palm DB, that DB's next-sync revision differs from the cached value
  → the next sync re-diffs it (finds nothing). Correct, mildly less efficient for DBs we
  wrote; read-only DBs get the full skip benefit.

### Layer B — Palm backends implement `Sync::ChangeDetection`

`PalmCalendarBackend`, `PalmContactsBackend`, `PalmMemoBackend`, `PalmTodoBackend`
(`src/palm/{calendar,contacts,memo,todo}/`) additionally inherit the neutral mixin
`Kalburator::Sync::ChangeDetection` (`build/_deps/libkalburator-src/src/sync/changedetection.h`
— the interface doc explicitly names "Palm hot-sync per-database sync anchor" as an intended
use):

- `collectionRevision(collectionId)` — map the collection id (`palm:calendar`) to its DB
  name (`DatebookDB`) and return `databaseRevision(dbName)`. Keep the default
  `collectionRevisions` loop (≤4 PIM dbs; cheaper than the 189-entry `listDatabases`
  enumeration, so prefer per-db `dlp_FindDBInfo` over a full sweep).
- `cachedCollectionRevision(collectionId)` / `primeRevisionCache(cache)` — read/write
  last-synced revisions from a per-profile persistent store: a `QSettings` ini at
  `<profile>/.state/palm-revisions.ini`, keyed by DB name (same pattern as the existing
  `akonadi-calendar-revisions.ini`).
- `persistsCollectionRevisions()` → true.

The engine's existing fast path then works unchanged for the Palm side: `prepareSyncFastPath`
(`syncengine.cpp:660`) batches `collectionRevisions`, compares fresh vs
`cachedCollectionRevision`, and a mapping is skipped only when **both** sides are covered
and unchanged (`syncengine.cpp:725-726`). After each successful mapping the engine primes
the cache from the **pre-pass** snapshot (`m_freshState`, `syncengine.cpp:1109-1133`) — see
Convergence below for why that pre-pass timing is what makes the loop settle.

### Layer C — Runtime: enable skip + fixpoint loop

`PalmRuntime::runAllMappings` (`src/runtime/palmruntime.cpp:832`):

1. `m_engine->setSkipUnchangedMappings(true)` for HotSync; **off** for FullSync (FullSync
   clears baselines to force full re-diffs — `palmruntime.cpp:969-976`).
2. Run the enabled mapping set, then **loop**, re-evaluating skips each pass (each
   `runSync` re-runs `prepareSyncFastPath`, re-sampling revisions):
   - **HotSync**: loop while the previous pass reported changes (skip-driven, so settled
     mappings cost nothing); cap 3.
   - **FullSync**: run a **fixed 2 passes** (skip is OFF, so a 3rd no-change *detection*
     pass would do a full wasteful re-read; 2 passes suffice for the depth-1 star).
3. **Stop** (HotSync) when any of: a pass produced no source/target changes
   (`SyncResult.sourceStats.hasChanges() || targetStats.hasChanges()` false for all —
   `synctypes.h:135`); a mapping **failed**; the run was **cancelled**; or the pass **cap
   (3)** is reached (log a warning if it did not converge). FullSync stops after 2 passes
   or early on failure/cancel.
4. **Hold the device across passes** — teardown (`endSync`/`closeConnection`) runs only
   after the loop completes. `disconnectDevice()` is already blocked while a run is in
   flight (`palmruntime.cpp:620-630`); the loop lives entirely inside one sync session.
5. Aggregate stats across passes into the single `PalmRunResult` the caller observes
   (the existing watcher delivers the final future).

---

## Convergence

For the depth-1 star, the loop settles in **≤3 passes**, driven by the engine priming the
revision cache from the *pre-pass* snapshot:

- **Pass 1** (fresh profile: no cached revisions ⇒ nothing skipped): legs run (hub empty,
  no-op to Palm), then routes pull remote → hub. Hub:calendar now holds the remote records,
  but the cache was primed with the *pre-pass* (empty-hub) revision.
- **Pass 2**: `prepareSyncFastPath` samples the now-changed hub revision ≠ cached ⇒ the
  calendar leg is **not** skipped ⇒ delivers the records to the Palm. Unrelated mappings
  (unchanged both sides) are **skipped** — no device read. (A route may re-run as a 0-op.)
- **Pass 3**: every collection's fresh revision now equals cached ⇒ all skipped ⇒ no
  changes ⇒ loop stops. This pass only samples modnums (cheap), no record reads.

Outbound changes need no extra pass (they complete in pass 1, as shown above). The cap of 3
= star-diameter (2) + 1 settle pass.

---

## Modes & guards

| Mode | Skip-unchanged | Fixpoint loop | Notes |
|---|---|---|---|
| **HotSync** | ON | ON | Main target. Behavior change: unchanged mappings now skip their full diff. First-ever sync still runs everything (no cached revisions). |
| **FullSync** | OFF | ON (fixed 2 passes) | Baseline-clear forces full re-diffs; loop still propagates both hops. Heavier, as today. |
| **Clobber** | — | — | Unchanged. Single hop (wipe Palm, re-push from hub); never skips. Populate the hub via a normal sync first if empty. |
| **Mirror** (Copy Palm↔PC) | — | — | Unchanged. Deliberate one-directional single-mapping overwrite. |

Guards: cancellation breaks the loop and reports partial; a mapping failure stops the loop;
the cap stops a non-converging loop with a logged warning; the device is never torn down
mid-loop.

---

## Testing

- **Device**: `MockPalmDatabaseAccess.databaseRevision` reflects a settable per-db value and
  bumps after writes; unit test.
- **Palm backend**: each backend reports `collectionRevision`, persists/reads cached
  revisions across instances, and yields a stable revision for an unchanged collection.
- **Runtime fixpoint** (key test): fake topology = one palm↔hub leg + one route whose source
  seeds N records into the hub (reuse the `appendConduitForTest` / fake-runtime seam,
  `tst_fifth_conduit`). Assert a single `runAllMappings()` delivers all N to the Palm side
  within ≤3 passes; a no-change run settles in 1–2 passes touching the device minimally;
  outbound (Palm-seeded) records reach the route target in pass 1.
- **Regression**: existing single-hop HotSync / Mirror / Clobber tests stay green;
  skip-unchanged must not break a first sync (no cached revisions ⇒ full run).

---

## Risks / caveats

- **modnum semantics**: relies on standard PIM DBs maintaining `modnum`. A DB that doesn't
  returns a stable/zero value → empty-or-equal revision → treated as changed (safe; just no
  skip benefit). No correctness risk.
- **Self-write bump** (above): reduces cross-sync skip benefit for DBs we write; acceptable.
- **Pre-pass priming lag**: adds one settle pass (baked into the cap of 3). Not a bug —
  it is what guarantees the dependent leg re-runs.
- **Skip-unchanged is a HotSync behavior change**: a fully-settled HotSync now does near-zero
  device I/O instead of a full diff. Intended (perf win); covered by the regression suite.
