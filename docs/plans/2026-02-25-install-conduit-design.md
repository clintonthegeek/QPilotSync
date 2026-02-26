# Install Conduit Plugin Design

**Date:** 2026-02-25
**Status:** Approved

## Motivation

File installation to Palm devices currently uses two ad-hoc code paths outside the conduit pipeline:

1. `Sync::InstallConduit` (non-plugin QObject in `src/sync/conduits/`) — scans `<profile>/install/` pre-sync, called manually from KF6MainWindow before each sync type.
2. `DeviceWorker::doInstall()` — threaded immediate-install path used when the device is already connected.

Neither participates in SyncEngine's conduit pipeline. There is no UI showing queued files. The user has no visibility into what will be installed on the next sync.

## Decisions

- **Interface:** IConduit (base). Not ISyncConduit (no Palm database to sync). Not IToolConduit (no external tool).
- **Plugin type:** KDE plugin in `src/plugins/install/`, loaded by ConduitManager.
- **Scope:** User-queued files only. Plucker's `context.installQueue` → `m_pendingInstalls` post-loop stays as-is.
- **View:** Conduit view tab alongside Calendar, Contacts, etc. Shows pending files and installed history.
- **Post-install:** Successfully installed files move to `installed/` subfolder (existing behavior preserved).
- **Execution order:** Runs first in SyncEngine pipeline (no dependencies).
- **Immediate install removed:** When user picks files while connected, they queue to install folder. Optionally triggers sync.

## Architecture

### InstallConduit Plugin

```
src/plugins/install/
  installconduit.h
  installconduit.cpp
  installview.h
  installview.cpp
  install-conduit.json          # KPluginFactory metadata
  CMakeLists.txt
```

Implements IConduit directly. Key methods:

- `conduitId()` → `"install"`
- `displayName()` → `"Install Files"`
- `requiresDevice()` → `true`
- `hasView()` → `true`
- `shouldRun(context)` → `true` only if pending files exist
- `canSync(context)` → `true` if device connected
- `sync(context)` → scans install folder, calls `pi_file_install()` via `context->deviceLink->socketDescriptor()`, moves successes to `installed/`, returns SyncResult with stats
- `createView(parent)` → returns `InstallView` widget

### InstallView Widget

A QWidget with:

- **Pending files list** — QListWidget showing `.prc`/`.pdb` files in install folder
- **Add button** — opens file dialog (`.prc`, `.pdb`), copies selected files to install folder
- **Remove button** — removes selected pending file from install folder
- **Installed history** — QListWidget showing files in `installed/` subfolder
- **Refresh** — rescans both folders
- Drag-and-drop support for adding files

### Install Folder Path

The conduit gets the install folder from `SyncContext::syncFolderPath` + `/install/`. This is the same `<profile>/install/` path used today.

### Execution Order

The install conduit has no Palm creator ID and no dependencies. In the topological sort, it naturally runs before data conduits. No special ordering logic needed.

## Removals

### Old InstallConduit (non-plugin)
- Delete `src/sync/conduits/installconduit.h`
- Delete `src/sync/conduits/installconduit.cpp`
- Remove from `src/CMakeLists.txt`

### KF6MainWindow cleanup
- Remove `m_installConduit` member variable
- Remove `runInstallConduit()` method
- Remove `runInstallConduit()` calls from `onHotSync()`, `onFullSync()`, `onCopyPCToPalm()`, `onRestore()`
- Simplify `onInstallFiles()` — always queues to install folder, optionally triggers sync if connected

### DeviceWorker/DeviceSession install path
- Remove `DeviceWorker::doInstall()` method
- Remove `DeviceSession::requestInstall()` method
- Remove `DeviceSession::installFinished` signal
- Remove `DeviceSession::onWorkerInstallFinished()` slot
- Remove `DeviceWorker::installFinished` signal

## What Stays

- `SyncEngine::m_pendingInstalls` post-loop for tool conduit output (Plucker)
- `SyncContext::installQueue` — tool conduits still push output here
- `KF6MainWindow::onInstallFiles()` action — simplified to always queue + optional sync trigger

## Verification

- Project compiles with no errors
- All existing tests pass
- Install conduit appears in ProfilePropertiesDialog conduit list
- Install conduit view tab shows in main window when enabled
- Add files via view → appear in pending list
- HotSync installs pending files, moves to installed history
- No pending files → conduit silently skips during sync
- Plucker post-loop install still works independently
