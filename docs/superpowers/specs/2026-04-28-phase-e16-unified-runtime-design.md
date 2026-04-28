# Phase E.16 — Unified Runtime: Delete Legacy IConduit ABI

**Status:** approved 2026-04-28.
**Parent:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (E.16 row).
**Plan:** `docs/superpowers/plans/2026-04-28-phase-e16-unified-runtime.md`.

## Goal

Collapse Client-Mode (legacy `IConduit` family) and the new Full-Sync /
plugin-ABI runtime into one runtime. Delete the legacy ABI surface and
its consumers; preserve the user-facing Tools-menu sync actions
(HotSync / FullSync / CopyPalmToPC / CopyPCToPalm / Backup / Restore)
on top of a single code path driven by the new `IBackendPlugin` /
`IPluginAction` ABI plus `Kalburator::Sync::BlobSyncEngine`.

## Decision summary

- **Maximal-E.16 (option A):** ship deletion *and* the app-layer
  call-site migration that the deletion forces. E.17 reduces to
  cosmetic cleanup + manual smoke test.
- **Menu UX preserved (option A):** all six Tools actions stay; their
  six `Sync::SyncMode` values become policy on top of one engine.
  No UX change in this phase.
- **`synctypes.h` survives**, relocated to `src/core/synctypes.h`. It
  holds shared types broadly used across mappers and the new runtime
  (`MapperResult<T>`, `DataLossWarning`, `SyncStats`, `SyncResult`,
  `IDMapping`, `CollectionInfo`, `DatabaseInfo`, `ConflictResolution`,
  `RecordState`, `WarningSeverity`, `WarningCategory`, `SyncMode`).
  `Sync::` namespace preserved verbatim (no semantic change).
- **`WildPalms::FullSync` namespace finishes the E.15b half-rename**
  to `WildPalms::Runtime` as a ride-along.

## Architecture

### New: `WildPalms::Runtime::SyncRunner`

Located at `src/runtime/syncrunner_wp.{h,cpp}` in
`WildPalmsRuntime`. Owns the per-sync-operation orchestration:

```
class SyncRunner : public QObject {
public:
    SyncRunner(BackendPluginManager *plugins,
               PalmDeviceConnection *device,
               SyncHost_WP *host,
               QObject *parent = nullptr);

    // The single entry point. Runs synchronously on the calling thread
    // (intended to be called from DeviceWorker on the device-worker
    // thread). Emits progress() throughout and finished() at the end.
    void run(Sync::SyncMode mode, const QStringList &enabledPluginIds);

    void requestCancel();

signals:
    void started(Sync::SyncMode mode);
    void progress(int current, int total, const QString &message);
    void logMessage(const QString &msg);
    void finished(const Sync::SyncResult &aggregateResult);
};
```

**Algorithm:**

For each enabled `IBackendPlugin`:
- Call `plugin->createBackends(host, device)` — returns
  `ProvidedBackends { IBlobBackend* blob; IIncidenceSource*; ... }`.
- Skip if the plugin offers no blob backend (mirror-only or
  action-driven plugins are no-ops here).
- For each `CollectionInfo` returned by `blob->availableCollections()`:
  pair it with the corresponding "PC-side" collection (per the
  plugin's `viewCollections()` mapping) and call
  `BlobSyncEngine::twoWayWithBaseline(palmCollection, pcCollection,
  conflictHandler, baselineStore, policy)`.
- Aggregate per-plugin `SyncResult` into the top-level result.

**SyncMode → policy translation:**

| Mode             | Policy applied to BlobSyncEngine call               |
|------------------|------------------------------------------------------|
| `HotSync`        | Default: trust baselines, two-way merge.             |
| `FullSync`       | Invalidate baselines first; treat every record as if first sync. |
| `CopyPalmToPC`   | Force-direction policy: all conflicts auto-resolve `PalmWins`; PC-only records deleted. |
| `CopyPCToPalm`   | Force-direction policy: all conflicts auto-resolve `PCWins`; Palm-only records deleted. |
| `Backup`         | Bypasses BlobSyncEngine entirely. Direct pilot-link database dump via `PalmDeviceConnection`. No PC writes. |
| `Restore`        | Bypasses BlobSyncEngine entirely. Direct pilot-link database restore from local archive. |

Backup/Restore live as private methods on `SyncRunner` (or a
helper `PalmBackupRestore` class in `src/runtime/`) and are
independent of the per-plugin loop. Logic ports from
`Sync::Conduit::backup()` / `Sync::Conduit::restore()` against
`PalmDeviceConnection::databaseAccess()`.

### App-layer migration

- `KF6MainWindow`: drop `m_conduitManager`, `m_syncEngine`, drop
  `initializeSyncEngine()`, rename `initializeConduits()` →
  `initializePlugins()`. Construct `m_syncRunner` (owned). Menu
  handlers (`onHotSync`/`onFullSync`/`onCopyPalmToPC`/
  `onCopyPCToPalm`/`onBackup`/`onRestore`) all call
  `m_session->requestSync(mode, m_syncRunner)`.
- `DeviceSession::requestSync(SyncMode, SyncEngine*)` →
  `requestSync(SyncMode, SyncRunner*)`.
- `DeviceWorker::doSync(mode, conduitIds, SyncEngine*, …)` →
  `doSync(mode, enabledPluginIds, SyncRunner*)`. The worker simply
  invokes `runner->run(mode, enabledPluginIds)` on its thread,
  forwards signals, and reports completion.
- Conflict pipeline:
  - `src/app/interactiveconflicthandler.{h,cpp}`: rewrite against
    `Sync::ConflictResolution` (now in `src/core/synctypes.h`) and
    the new `WildPalms::Runtime::ConflictPresenter_wp` /
    `ConflictResolver_wp`. The widgets it presents
    (`conflictdialog`, `conflictreviewwidget`,
    `conflictreviewdialog`) are pure UI and survive.
  - The handler exposes the same callable shape so kf6mainwindow's
    wiring is largely a header swap.

### Deletions

| Path                                         | LOC   |
|----------------------------------------------|-------|
| `src/sync/conduit.{h,cpp}`                   | 2,346 |
| `src/sync/syncengine.{h,cpp}`                | 1,334 |
| `src/sync/syncstate.{h,cpp}`                 | 633   |
| `src/sync/localfilebackend.{h,cpp}`          | 514   |
| `src/sync/syncbackend.h`                     | 194   |
| `src/sync/qsynccore/`                        | (TBD) |
| `src/sync/synctypes.h`                       | (relocated, not deleted) |
| `src/core/iconduit.h`                        | 99    |
| `src/core/isyncconduit.h`                    | 57    |
| `src/core/itoolconduit.h`                    | 18    |
| `src/kf6/conduitmanager.{h,cpp}`             | 577   |
| `src/plugins/{memo,calendar,todos,contacts,webcalendar,plucker,install}/{name}conduit.{h,cpp}` + matching `*-conduit.json` | ~2,000 |
| Legacy tests: `test_syncengine.cpp`, `test_localfilebackend.cpp`, `test_syncstate.cpp`, `test_conflict.cpp`, `test_multidatabase.cpp` | (TBD) |
| Each plugin's `WILDPALMS_*_PLUGIN_V2` toggle (V2 becomes unconditional) | n/a |

Approximate total: ~9,000 LOC delete plus the new SyncRunner
(~600 LOC source + tests).

### Cosmetic ride-along: `WildPalms::FullSync` → `WildPalms::Runtime`

Mechanical sed across `src/runtime/{conflictresolver,conflictpresenter,calendarcollection,synchost,syncconfigstore}_wp.{h,cpp}` and the matching test fixtures (`tests/test_fullsync_*.cpp`). Header guards already correct from E.15b.

### Plugin discovery

`BackendPluginManager` already discovers, loads, and unloads V2
plugins. Wiring stays as-is; only the parallel `ConduitManager`
construction goes away.

### Testing

**Survives unchanged:**
- `tst_memo_v2`, `tst_calendar_v2`, `tst_todo_v2`,
  `tst_contacts_v2`, `tst_webcalendar_v2`, `tst_plucker_v2`,
  `tst_install_v2_e2e`
- `test_fullsync_calendarcollection`,
  `test_fullsync_syncconfigstore`, `test_fullsync_synchost`
- `test_calendarmapper`, `test_categoryinfo`,
  `test_contactmapper`, `test_memomapper`, `test_pluckerconfig`,
  `test_profile`, `test_todomapper`
- All `tests/palm*/`, `tests/runtime/`, `tests/plugins/`
  subdirectory tests

**Deleted:** `test_syncengine.cpp`, `test_localfilebackend.cpp`,
`test_syncstate.cpp`. `test_conflict.cpp` and
`test_multidatabase.cpp` reviewed and either deleted or rewritten
to cover surviving types.

**New:** `tests/runtime/tst_syncrunner.cpp`. Drives `SyncRunner`
against `MockBlobBackend` + `MockPalmDatabaseAccess` fixtures,
covering all six `SyncMode` values:
- HotSync / FullSync round-trip with conflicting edits.
- CopyPalmToPC / CopyPCToPalm with disagreeing records.
- Backup writes an archive directory with one PRC per claimed DB.
- Restore reads that archive and pushes back to a fresh mock.

### Profile / persistence

`Sync::SyncMode` enum's int ordering preserved (HotSync=0,
FullSync=1, CopyPalmToPC=2, CopyPCToPalm=3, Backup=4, Restore=5)
so any QSettings-persisted user preference still reads correctly.

## Risks

1. **`Conduit::backup()` / `Conduit::restore()` logic must be ported intact.** Read the legacy implementations carefully before deleting them. The legacy code is the spec for what these modes do at the wire level.
2. **`SyncMode::FullSync` baseline-invalidation** has no analogue in `BlobSyncEngine`. The pragmatic implementation is: clear the per-plugin baseline store entry before running `twoWayWithBaseline`. Verify `IBlobBackend` exposes a way to do this (or surface as a no-op + log if unavailable, with HotSync semantics as fallback).
3. **`InteractiveConflictHandler` → new types** is the trickiest non-mechanical migration. The handler's call shape (synchronous resolve-this-conflict callback) must align with how `BlobSyncEngine` queries handlers. If the shapes differ, an adapter is required; surface that in the plan.
4. **`DeviceWorker::doSync`** currently needs an open pilot-link socket (`m_socket`) to call `dlp_OpenConduit`. The new SyncRunner uses `PalmDeviceConnection` (which abstracts the socket). The socket-vs-connection mismatch needs reconciliation; favor passing the connection through.
5. **Cross-plugin pairing** for `palm:calendar/<slot>` (Calendar-plugin Palm side ↔ WebCalendar-plugin PC side): the runtime must know about cross-plugin pairings before invoking `twoWayWithBaseline`. If this isn't fully wired, mirror-only plugins (WebCalendar, Plucker bootstrap) need explicit handling in `SyncRunner::run` to skip or invoke a different code path.

## Out of scope (stays in E.17)

- Profile field cleanup: any orphaned legacy keys (`m_conduitOrder`, `m_enabledConduits`, etc.) that survive deletion but no longer load anything.
- Renaming `Sync::` namespace itself.
- Any cosmetic file rename of `_wp` suffix.
- App-layer cosmetic cleanup beyond what's load-bearing for the build to link.
- Manual smoke test against a real Palm device.

## Acceptance

- WP `cmake --build build-dev` succeeds.
- WP `ctest --test-dir build-dev` passes — count is at least
  the post-E.15b 76, less any deleted legacy tests, plus the
  new `tst_syncrunner`.
- `grep -rn "IConduit\|ConduitManager\|isyncconduit\|itoolconduit\|src/sync/" src/ tests/` returns nothing.
- `grep -rn "WildPalms::FullSync" src/ tests/` returns nothing.
- All seven Tools-menu actions still drive a sync (verified by
  unit/integration tests; manual device test deferred to E.17).
- Parent spec row flipped to ✅ with landed-2026-04-28 stamp.
- Memory entry written.
