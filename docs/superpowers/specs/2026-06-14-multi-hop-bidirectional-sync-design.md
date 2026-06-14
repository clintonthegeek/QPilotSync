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

### Layer A — Palm device collection revision (all main-repo)

`IPalmDatabaseAccess` (`src/palm/sync/ipalmdatabaseaccess.h`) gains a **non-pure** virtual
(default returns empty so the several implementers that can't answer need no change):

```cpp
/// Cheap collection-change token: the Palm database's modification number as a string.
/// Empty string = "cannot answer cheaply" (caller treats the collection as changed).
virtual QString databaseRevision(const QString &dbName) const { return {}; }
```

Delegation chain (each layer a thin forward, mirroring the existing `availableDatabases` /
`readAllRecords` path):

- **`KPilotLink`** (`src/palm/kpilotlink.h`) gains `virtual long databaseModnum(const QString &dbName) { return -1; }` (-1 = unknown).
- **`KPilotDeviceLink`** (`src/palm/kpilotdevicelink.{h,cpp}`) overrides `databaseModnum` via
  `dlp_FindDBInfo` → `DBInfo.modnum` (mirrors the existing `findDatabase` at
  `kpilotdevicelink.cpp:~1289`). One DLP call, **no record traversal**.
- **`PilotLinkPalmDatabaseAccess`** (`src/palm/device/pilotlinkpalmdatabaseaccess.{h,cpp}`)
  overrides `databaseRevision` → `m_link->databaseModnum(dbName)` to a string (empty if < 0).
- **`PalmDeviceAccess`** (`src/runtime/palmdeviceaccess.{h,cpp}`) overrides `databaseRevision`
  → marshals to its `m_impl` on the link thread (mirror its `availableDatabases` marshaling;
  DLP must run on `m_linkThread`).
- **`PalmBackend`** (`src/palm/sync/palmbackend.{h,cpp}`) — the adapter the four conduit
  backends hold — gains `QString databaseRevision(const QString &dbName) const` forwarding to
  its `m_device->databaseRevision(dbName)`.
- **`MockPalmDatabaseAccess`** (`src/palm/sync/mockpalmdatabaseaccess.{h,cpp}`) overrides it
  from a settable per-db value that bumps on `createRecord`/`deleteRecord`/`updateRecord`
  (so the fixpoint test drives change detection deterministically).

`modnum` increments on every record change. Caveat: **our own writes bump it**, so after a
sync that writes a Palm DB, that DB's next-sync revision differs from the cached value → the
next sync re-diffs it (finds nothing). Correct, mildly less efficient for DBs we wrote;
read-only DBs get the full skip benefit.

### Layer B — Palm backends implement `Sync::ChangeDetection`

**Which classes (corrected after code-mapping):** the *live, registered* Palm backends are
the **plugin submodule** classes returned by each conduit's `createPalmBackend`, NOT the
unused `src/palm/*` legacy classes:

| conduit | registered class | header | DB |
|---|---|---|---|
| calendar | `PalmCalendarBackend` (base `SyncBackend`) | `src/plugins/calendar/palmcalendarbackend.h` | DatebookDB |
| contacts | `PalmContactsBackend` (base `SyncBackendBase`) | `src/plugins/contacts/palmcontactsbackend.h` | AddressDB |
| memo | `MemoBlobBackend` (base `SyncBackendBase`) | `src/plugins/memo/memoblobbackend.h` | MemoDB |
| todo | `TodoBlobBackend` (base `SyncBackendBase`) | `src/plugins/todos/todoblobbackend.h` | ToDoDB |

All four reach the device uniformly through a private `m_palmBackend`
(`WildPalms::PalmSync::PalmBackend*` → its `IPalmDatabaseAccess* m_device`). Each wraps
**exactly one** physical DB, so `collectionRevision` returns that DB's modnum regardless of
which collection id (`palm:calendar`, `palm:calendar/3`, …) the engine asks about — no id
parsing needed.

**Shared mixin (DRY, main-repo).** Add `WildPalms::PalmSync::PalmChangeDetection`
(`src/palm/sync/palmchangedetection.h`) inheriting the neutral
`Kalburator::Sync::ChangeDetection` (the lib interface doc explicitly names "Palm hot-sync
per-database sync anchor" as an intended use). It implements everything generic:
`cachedCollectionRevision`/`primeRevisionCache` (delegate to an injected `PalmRevisionStore`),
`collectionRevision` (returns `currentDbRevision()`), and a `setPalmRevisionStore(...)`
setter. It declares one protected hook `virtual QString currentDbRevision() const = 0`.

Each of the four submodule backends then adds `public PalmChangeDetection` as a base and a
one-line override: `currentDbRevision() { return m_palmBackend ? m_palmBackend->databaseRevision(DatabaseName) : QString(); }`.

**Persistence (`PalmRevisionStore`, main-repo `src/palm/sync/`).** A small `QSettings`-ini
token store (`token`/`setToken`), one instance owned by `PalmRuntime` at
`<profile>/.state/palm-revisions.ini` (keyed by collection id — unique per backend, no
collision; same pattern as the lib's `AkonadiRevisionStore`). `PalmRuntime` injects it into
each registered backend (dynamic_cast to `PalmChangeDetection*`) in `finishConnect`, after
registration and before any sync. Per-profile location prevents two profiles on one device
from wrongly skipping each other.

The engine's existing fast path then works unchanged for the Palm side: `prepareSyncFastPath`
(`syncengine.cpp:660`) batches `collectionRevisions`, compares fresh vs
`cachedCollectionRevision`, and a mapping is skipped only when **both** sides are covered
and unchanged (`syncengine.cpp:725-726`). After each successful mapping the engine primes
the cache from the **pre-pass** snapshot (`m_freshState`, `syncengine.cpp:1109-1133`) — see
Convergence below for why that pre-pass timing is what makes the loop settle.

**Submodule workflow:** the four backend edits land in their submodule repos (commit + push
to each GitHub remote), then the superproject gitlinks are bumped. The device layer
(Layer A) and runtime (Layer C) are all main-repo.

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
- **Loop decision** (key unit test): the fixpoint loop's control logic is extracted into a
  pure function `shouldContinueSync(results, passJustFinished, maxPasses)` and exhaustively
  tested — continues on data change, stops at fixpoint (no changes), stops at the cap, stops
  on failure, stops on cancel. This isolates the only new decision logic. A full
  device-connected multi-pass integration test is **not** feasible in-harness (the Palm side
  needs a live HotSync session), so end-to-end Remote→Hub→Palm delivery is validated by the
  on-device smoke test below — consistent with this project's hardware-gated verification
  model.
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
