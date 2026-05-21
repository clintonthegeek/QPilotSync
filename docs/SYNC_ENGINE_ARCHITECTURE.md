# Sync Engine Architecture

**Status:** post-merger reality (2026-05-21).
**Supersedes:** the pre-Phase-E `SyncSession` / `Conduit` model. That model is gone; the body of this document describes what actually runs today.

WildPalms does not own a sync engine. The engine is `Kalburator::Engine::SyncEngine`, lives in libkalburator, and is shared with PlanStan. This document describes how the engine drives a sync end to end, what the contracts at each seam are, and what WildPalms-specific glue makes a Palm device look like an ordinary `SyncBackend` to it.

For the plugin contract see `docs/PLUGIN_ABI.md`. For the application-level overview see `docs/ARCHITECTURE_2026.md`.

---

## The mental model in one paragraph

A **mapping** wires two **backends** together. Each backend is a `Kalburator::Sync::SyncBackend` — an abstract handle to a record collection that supports `loadRecords()`, `pushItems()`, `updateItem()`, and `deleteRecord()`. The engine takes a mapping, asks both sides for their records, consults a per-mapping **baseline** to figure out what changed since the last sync, applies an optional **conflict policy** when both sides changed the same record, and writes the merged result back to both backends — updating the baseline as it goes. The engine does not know that one side is a Palm device any more than it knows that the other side might be a CalDAV server: both sides are just `SyncBackend`s.

---

## The engine's surface (libkalburator)

```cpp
namespace Kalburator::Engine {

class SyncEngine {
public:
    // Run sync for a single mapping. ExecutionOverride lets the caller
    // pin direction (mirror A→B, mirror B→A) or behaviour (force-first-sync).
    QFuture<Kalburator::Sync::SyncResult>
        runSyncFuture(const QString &mappingId,
                      const Kalburator::Sync::ExecutionOverride &ov = {});

    void onCancelObserved();   // called by a QFutureWatcher::canceled
};

} // namespace Kalburator::Engine
```

`runSyncFuture` returns a `QFuture` whose continuation fires when the sync finishes (or is cancelled). The future resolves to a `SyncResult` containing per-side stats (`created`, `updated`, `deleted`, `unchanged`) and a success flag.

The engine internally dispatches to one of two paths:

- **Blob path** — `BlobSyncEngine::twoWayWithBaseline`. The hot path for "regular" sync. Both sides are `IBlobBackend`s; the engine compares record blobs against the baseline and decides per record whether to write.
- **First-sync / mirror path** — `dispatchFirstSync` or the mirror overrides. No baseline; one side is treated as authoritative.

WildPalms always uses the blob path for HotSync / FullSync / CopyPalmToPC / CopyPCToPalm (FullSync clears the baseline first to force first-sync semantics; the mirror modes set `ExecutionOverride::Direction`). Backup and Restore bypass the engine entirely — they are raw device byte transfers.

---

## What a `SyncBackend` must do

```cpp
namespace Kalburator::Sync {

class SyncBackend {
public:
    virtual ~SyncBackend() = default;

    virtual QList<CollectionInfo> availableCollections() const = 0;
    virtual Shape::Shape shapeFor(const QString &collectionId) const = 0;

    virtual QFuture<LoadResult> loadRecords(const QString &collectionId) = 0;

    virtual QFuture<PushResult> pushItems(const QString &collectionId,
                                          const QList<BackendRecord> &items,
                                          const TranscodingPlan &plan) = 0;
    virtual QFuture<UpdateResult> updateItem(const QString &collectionId,
                                             const BackendRecord &item,
                                             const TranscodingPlan &plan) = 0;
    virtual QFuture<DeleteResult> deleteRecord(const QString &collectionId,
                                                const QString &recordId) = 0;
};

} // namespace Kalburator::Sync
```

The exact signature lives in libkalburator's `src/sync/syncbackend.h`. The key thing for WildPalms is that **`PalmBackend` is just an implementation of this interface** — once it returns blobs that the engine knows how to compare, the engine treats it identically to a CalDAV backend or a local file backend.

`PalmBackend` (`src/palm/sync/palmbackend.{h,cpp}`) wraps an `IPalmDatabaseAccess`. Each Palm database is exposed as one `CollectionInfo`; `loadRecords()` translates `dlp_ReadRecordByIndex` results into `BackendRecord`s. As of E.16's deferral (c), `loadPalmRecords` caches its result per DB across the lifetime of a sync, so multi-collection plugins (Calendar, ToDo) don't re-read the same Palm DB N times.

The `Shape` returned by `shapeFor(collectionId)` tells the engine what *kind* of record this collection holds (calendar, contacts, memo, todo, blob, ...). Mismatched shapes across a mapping abort the sync with `dispatchSync` returning "cross-domain mappings not supported". The shape system lives in libkalburator and is registered by the stock plugins (see `Kalburator::registerStockPlugins`).

---

## The two storage layers in WildPalms

The engine reads and writes two stores during a sync. **Both are present in WildPalms, for different reasons:**

### 1. `Kalburator::Storage::BaselineStore` (SQLite) — primary

Location: `<profile>/.state/baselines.sqlite`, table `blob_baselines_v3`, primary key `(mapping_id, record_id)`.

Populated and consulted by `BlobSyncEngine::twoWayWithBaseline` on every record. This is the engine's authoritative record of "what did each side look like the last time they were in sync?" — used to detect mid-sync changes and to drive conflict detection.

A `FullSync` clears all rows for a mapping (`clearMappingV3(mapping.id)`) before kicking off the engine, which makes the engine behave as if it has never seen these records before. That is the only difference between FullSync and HotSync at the engine layer.

### 2. WildPalms-local JSON `BaselineStore` + `IDMappingStore` — legacy, narrow purpose

Location: `<profile>/.state/<username>/<plugin>/{mappings.json, baseline.json}`. Code lives in `src/sync/journal/` under namespace `WildPalms::Sync`. Header guard prefix `WILDPALMS_JOURNAL_*`.

This is **not** the engine baseline. It is used by `SyncState::pendingConflictCount()` (and a handful of related read paths in `KF6MainWindow`) to surface "you have N deferred conflicts to review" in the UI for legacy per-conduit conflict tracking. The hot sync path does not touch these files.

The duplication exists because the deferred-conflict counter predates Phase E and the JSON file format is part of the user's on-disk profile. Migrating it to SQLite is a future cleanup; until then, both stores live side by side. See the conflict-handler port plan (`docs/superpowers/plans/2026-05-21-conflict-handler-port.md`) for the migration that put them in their current shape.

---

## Conflict handling

The engine emits a `conflictDetected` signal whenever both sides changed the same record between syncs (relative to the baseline). The signal carries a `Kalburator::Conflict::ConflictRecord` describing the source side, the target side, and the baseline state.

WildPalms's GUI handler is `KalburatorInteractiveConflictHandler` (`src/app/conflict/`). On a conflict it constructs a modal `ConflictDialog` (`src/app/conflictdialog.{h,cpp}`) on the GUI thread and pauses the engine's worker thread on a `QEventLoop` until the user decides. The dialog returns a `ConflictDecision`; the handler forwards that into the engine via libkalburator's keep-alive callback channel. `KF6MainWindow::onPalmConflictHandlerKeepAlive()` keeps the device alive while the user thinks (the tickle worker would otherwise drop the connection).

Per-plugin conflict **overlays** (e.g. `CalendarConflictHandler`, `ContactsConflictHandler`) extend the base behaviour with domain-specific merge offerings — "merge both sides", "take A's title but B's attendee list", etc. They are constructed by their owning plugin's `createConflictHandler()` factory and chained in front of the application-level handler.

The `AskUser` conflict policy is the only one that emits the signal; other policies (`SourceWins`, `TargetWins`, `Newest`, `Defer`) resolve silently inside the engine.

---

## End-to-end: what happens when the user clicks HotSync

```
KF6MainWindow::onHotSync()
    └─ PalmRuntime::hotSync()                         (GUI thread)
         ├─ emit runStarted("HotSync")
         ├─ for each enabled mapping m:
         │     enqueue ids.append(m.id)
         ├─ PalmDeviceAccess::pauseTickle()           (link thread)
         ├─ futures = []
         ├─ for each id in ids:
         │     futures += m_engine->runSyncFuture(id) (engine worker thread)
         ├─ install QFutureWatcher<void> for cancellation
         └─ on all futures finished:
              ├─ aggregate per-plugin stats from each SyncResult
              ├─ PalmDeviceAccess::resumeTickle()
              └─ emit runFinished(PalmRunResult)

Inside SyncEngine::runSyncFuture(mappingId):
    └─ Engine looks up the mapping in its registry → (source, target).
    └─ For both source and target:
         backend = m_registry->backendInstance(<id>)
         loadRecords(collection) → QFuture<LoadResult>
    └─ Await both loads.
    └─ For each record id seen on either side:
         baseline = baselineStore.lookup(mapping, recordId)
         decide action (create / update / delete / conflict / unchanged)
         if conflict and policy == AskUser:
             emit conflictDetected → KalburatorInteractiveConflictHandler
             (worker thread blocks on QEventLoop until user clicks)
    └─ Apply writes via pushItems / updateItem / deleteRecord.
    └─ Update baseline rows.
    └─ Return SyncResult to the original QFuture.
```

The hot path is asynchronous all the way through. The GUI thread never blocks; the link thread is paused only when the engine is about to issue DLP calls; the engine worker thread blocks only when the user is reviewing a conflict.

`copyPalmToPC` / `copyPCToPalm` follow the same shape but set `ExecutionOverride::Direction = MirrorAToB` (or `MirrorBToA`), which the engine treats as "the source is authoritative, write whatever it says".

---

## Cancellation

The contract:

1. Caller invokes `QFuture::cancel()` on the future returned by `PalmRuntime::hotSync()` (or any other sync mode).
2. The future watcher relays `canceled` into `SyncEngine::onCancelObserved`.
3. The engine sets an atomic flag observed by `SyncEngineWorker`. Any nested `QEventLoop::exec` (the conflict-pause loop, the various `await<Op>` helpers) wakes via the cancellation channel.
4. In-flight backend operations complete; the engine returns a `SyncResult` with `success == false` and `errorMessage == "cancelled"`.

`SyncEngine::runSyncFuture` is safe to call again after a cancellation; the engine resets its state at the start of each call.

Two related Qt6 quirks documented in upstream FINDINGS:

- `QFuture::waitForFinished()` does **not** spin the test event loop. Use `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000)`.
- `QFuture::results()` returns an empty list after `cancel()`. Read results via `future.resultAt(0)` if you need them.

The `kSyncTimeoutMs` constant inside libkalburator is bumped to 30 s (was 5 s) to absorb real-device sync times.

---

## Test seams

WildPalms tests do not exercise the engine via a real device. They use the same harness shape as libkalburator's calendar-layer integration tests:

- **Source side:** the plugin's own `*BlobBackend` constructed with a `MockPalmDatabaseAccess` (`src/palm/sync/mockpalmdatabaseaccess.{h,cpp}`) seeded with `PalmRecord`s.
- **Target side:** `Kalburator::Sinks::MockBlobBackend` (in libkalburator), which keeps records in memory and accepts any shape.
- **Baseline:** an in-memory `BlobBaselineStore` constructed against `:memory:` SQLite.
- **Engine:** real `Kalburator::Engine::SyncEngine` against the two backends and the in-memory baseline.

Per-plugin e2e tests live under `tests/plugins/<plugin>/tst_<plugin>_v2.cpp` and follow this pattern. Tests that need a `MainWindow` use `WILDPALMS_QTEST_MAIN` / `WILDPALMS_QTEST_GUILESS_MAIN` to avoid the `__cxa_finalize` exit crash.

There is no automated integration suite against a POSE64 emulator — Phase E.18 cancelled that ambition because POSE64's DLP timing is too unstable. Real-device verification is manual, against a Palm m505 or similar hardware.

---

## See also

- libkalburator's `src/engine/syncengine.{h,cpp}` and `src/blob/blobsyncengine.{h,cpp}` for the engine implementation.
- libkalburator's `docs/phase0/04l-phase-d0-test-harness-design.md` for the engine-level test harness, including the QTRY / `resultAt` gotchas.
- `docs/PLUGIN_ABI.md` for what plugins must provide to participate in a sync.
- `docs/ARCHITECTURE_2026.md` for the surrounding application architecture (PalmRuntime, runtime layer, GUI).
- `docs/DATA_LOSS_HANDLING.md` for first-sync and conflict invariants the engine respects.
