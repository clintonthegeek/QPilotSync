# Phase 1-3 Refactor: Dead Code, Conduit Unification, Sync Path Unification

**Date:** 2026-02-25
**Status:** Approved

## Motivation

A codebase audit identified three categories of architectural debt:

1. ~2,833 lines of dead code compiled into WildPalmsCore but never used
2. A two-gate conduit enable/disable system (global + per-profile) with unclear UX
3. Two divergent hot sync paths (auto-sync vs manual) with incompatible session ownership, a race condition at handoff, and differing backend/connection-mode behavior

This design addresses all three as sequential, independently-deployable phases.

## Decisions

- **Dead code:** Delete entirely. No archival.
- **Conduit enable/disable:** Profile-only. Remove global gate. Remove Conduits page from Settings dialog.
- **Conduit toggle UI:** Profile properties dialog (new dialog).
- **Auto-sync behavior:** Configurable per-profile via existing `autoSyncOnConnect` setting.
- **Sync path:** Full unification. AutoSyncOrchestrator becomes a thin detection/signal relay. KF6MainWindow owns all session lifecycle.
- **Execution order:** Strict sequential (Phase 1 -> 2 -> 3), each phase compiles and runs independently.

---

## Phase 1: Delete Dead Code

### Files to delete

| File | Lines | Reason |
|------|-------|--------|
| `src/app/mainwindow.cpp` | 2,110 | Old QMainWindow, never instantiated |
| `src/app/mainwindow.h` | 209 | Header for above |
| `src/settings.cpp` | 185 | QSettings singleton, only caller is dead MainWindow |
| `src/settings.h` | 89 | Header for above |
| `src/app/exporthandler.cpp` | 94 | All methods are "Not Available" stubs |
| `src/app/exporthandler.h` | 50 | Header for above |
| `src/app/importhandler.cpp` | 53 | All methods are "Not Available" stubs |
| `src/app/importhandler.h` | 43 | Header for above |

### CMakeLists.txt

Remove from `src/CMakeLists.txt`:
- `app/mainwindow.cpp`
- `settings.cpp`
- `app/exporthandler.cpp`
- `app/exporthandler.h`
- `app/importhandler.cpp`
- `app/importhandler.h`

### Orphaned signals to remove

- `DeviceSession::operationCancelled()` in `devicesession.h` — declared, never emitted
- `ConflictReviewWidget::applyResolutionsRequested()` in `conflictreviewwidget.h` — declared, connected but never emitted

### Include cleanup

Remove any `#include "settings.h"`, `#include "mainwindow.h"`, `#include "exporthandler.h"`, `#include "importhandler.h"` that appear outside the deleted files.

### Verification

- Project compiles with no errors or warnings
- All tests pass
- App launches, loads a profile, connects to device, syncs

---

## Phase 2: Unify Conduit Enable/Disable to Profile-Only

### Goal

Remove the global conduit enable/disable gate. Only the profile controls which conduits participate in sync.

### ConduitManager changes

**Remove:**
- `loadConfig()` / `saveConfig()` — no more KConfig persistence of enabled state
- `setConduitEnabled()` / `isConduitEnabled()` — manager no longer tracks this
- `enabledConduits()` — sync engine gets enabled list from profile
- `PluginInfo::enabled` field

**Modify:**
- `resolveExecutionOrder()` — takes a `QStringList` of enabled conduit IDs as parameter instead of reading internal enabled state
- `discoverConduits()` — unchanged, still scans plugins
- `loadConduit()` / `unloadConduit()` — unchanged, still handles instantiation

**Keep:**
- `conduitList()` — still returns the full catalogue for UI display
- `conduit()` — still returns live IConduit* by ID
- `conduitMetaData()` — still returns metadata
- `palmCreatorId()` — still returns creator ID
- `enabledConduitForCreatorId()` — removed (no longer relevant at this level)

### KF6Settings changes

- Remove the `[Conduits]` KConfig group (keys like `memoEnabled`, `contactsEnabled`, etc.)
- No code changes to KF6Settings itself — it doesn't have conduit methods. The conduit KConfig usage is in ConduitManager which reads/writes KSharedConfig directly.

### SettingsDialog changes

**Remove:**
- `createConduitsPage()` — entire page
- `addConduitConfigPages()` / `removeConduitConfigPages()`
- `onConduitToggled()` slot
- Member variables: `m_conduitTree`, `m_conduitDetailLabel`, `m_conduitConfigPages`

**Remaining pages:** Profiles, Devices, Advanced (3 pages).

### New: ProfilePropertiesDialog

A `KPageDialog` opened from the profile sidebar (e.g., right-click -> Properties, or a gear icon).

**Pages:**

1. **Device** — device path, baud rate, connection mode (KeepAlive / DisconnectAfterSync), auto-sync on connect, default sync type (HotSync / FullSync)
2. **Conduits** — checkboxes for each discovered conduit, grouped by Palm creator ID. Mutual exclusion enforced: enabling a conduit auto-disables any other conduit sharing the same creator ID. Per-conduit settings widgets (e.g., WebCalendar feed list) appear as expandable sections when a conduit is enabled.
3. **Conflict** — auto-resolve strategy, fallback behavior

**Data flow:**
- Reads from `Profile` on open
- Saves to `Profile::setConduitEnabled()`, `Profile::setConduitSettings()`, etc. on OK/Apply
- Emits `settingsChanged()` so KF6MainWindow can re-sync the engine's conduit registration

### KF6MainWindow changes

- On profile load: iterate `ConduitManager::conduitList()`, check `profile->conduitEnabled()` for each, register enabled conduits with sync engine. (This is largely the existing logic at line ~869, minus the ConduitManager enabled check.)
- Remove `conduitManager->loadConfig()` call from initialization
- Remove `conduitManager->saveConfig()` call from shutdown
- Add action/button to open `ProfilePropertiesDialog` for current profile

### Mutual exclusion migration

The "one conduit per Palm creator ID" enforcement moves from `ConduitManager::setConduitEnabled()` to `ProfilePropertiesDialog`. When user checks a conduit, dialog iterates other conduits sharing the same `palmCreatorId()` and unchecks them, saving to profile.

### Verification

- Project compiles
- All tests pass
- Settings dialog no longer shows Conduits page
- Profile properties dialog shows conduit toggles
- Toggling conduits in profile properties affects which conduits run during sync
- Mutual exclusion works (enabling calendar-A disables calendar-B if same creator ID)

---

## Phase 3: Unify the Sync Path

### Goal

Eliminate the dual sync path. KF6MainWindow owns all session lifecycle. AutoSyncOrchestrator becomes a thin USB-detection/profile-resolution layer.

### AutoSyncOrchestrator changes

**Remove:**
- `DeviceSession *m_session` — no longer creates/owns sessions
- `Sync::SyncEngine *m_syncEngine` and `setSyncEngine()` — doesn't touch sync engine
- `startConnection()` — no longer connects
- `onConnectionComplete()` — no longer manages connection
- `onReadyForSync()` — no longer manages sync
- `onSyncFinished()` — no longer manages sync
- `activeSession()` accessor

**Remove signals:**
- `syncStarted()` — KF6MainWindow handles
- `syncFinished()` — KF6MainWindow handles
- `connectionEstablished()` — replaced by `deviceDetected()`
- `profileLoaded()` — KF6MainWindow loads profile itself

**Keep:**
- `setDeviceMonitor()` — still listens to PalmDeviceMonitor
- `setLogWidget()` — still logs detection events
- `onPalmDetected()` — receives udev signal, calls findOrCreateProfile()
- `findOrCreateProfile()` — resolves profile by USB serial / fingerprint / auto-creation
- `isBusy()` — prevents re-entrancy during profile resolution

**Add signal:**
- `deviceDetected(Profile *profile, const QStringList &ports)` — tells KF6MainWindow a device was found

**Keep signals:**
- `profileCreated(const QString &profilePath, const QString &userName)` — UI notification
- `error(const QString &message)` — detection/resolution failures
- `statusChanged(const QString &status)` — status bar updates

### New signal flow

```
PalmDeviceMonitor::palmDetected(ports, usbSerial)
  -> AutoSyncOrchestrator::onPalmDetected()
     -> findOrCreateProfile(usbSerial, ...)
     -> emit deviceDetected(profile, ports)
        -> KF6MainWindow::onAutoDeviceDetected(profile, ports)
           -> if m_session exists and busy: log warning, ignore
           -> if m_session exists and idle: disconnect first
           -> loadProfile(profile)  [same as manual path]
           -> connectToDevice(ports) [same as manual path]
           -> on readyForSync:
              if profile->autoSyncOnConnect(): requestSync(HotSync)
              else: show notification, wait for user
```

### KF6MainWindow changes

**New slot:** `onAutoDeviceDetected(Profile *profile, const QStringList &ports)`
- Guards against race condition: checks m_session state before acting
- Loads profile via existing `loadProfile()` path
- Connects via existing `connectToDevice()` path (works with both single and multi-port)
- After readyForSync, checks `profile->autoSyncOnConnect()` to decide auto-sync vs wait

**Remove:** All signal connections to AutoSyncOrchestrator that manage session transfer (the block around lines 126-186). Replace with single connection to `deviceDetected`.

**Ensure:** `connectToDevice()` handles both `QStringList` of 1 port (manual) and multiple ports (auto-detect) uniformly. The probe logic in `KPilotDeviceLink` already supports both.

**Ensure:** Backend setup (LocalFileBackend, state directory) happens during profile load, not per-sync-invocation. Both auto and manual paths go through profile load first.

**Ensure:** Connection mode (KeepAlive vs DisconnectAfterSync) always comes from the loaded profile, regardless of how connection was initiated.

### Race condition elimination

No more session ownership transfer. KF6MainWindow always creates and owns m_session. If auto-detect fires while busy, it's ignored with a log message. No pointer overwrite, no mid-operation disconnect.

### Verification

- Project compiles
- All tests pass
- Manual sync works: select profile -> connect -> HotSync/FullSync/etc.
- Auto-sync works: plug in Palm -> device detected -> profile loaded -> syncs (if autoSyncOnConnect) or waits
- Auto-detect while busy: logged and ignored, no crash
- Connection mode respected in both paths
- Tickle management consistent in both paths
