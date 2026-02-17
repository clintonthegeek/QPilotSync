# First-Run & Connection UX Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make QPilotSync auto-detect Palm devices via udev, auto-create profiles, auto-sync, and live in the system tray — so the user just presses HotSync on their Palm and everything happens.

**Architecture:** A new `PalmDeviceMonitor` class wraps libudev to watch for USB devices with `idVendor=0830`. When a Palm appears, it collects all ttyUSB ports and hands them to a new `AutoSyncOrchestrator` that races parallel connections, identifies the device, auto-creates a profile if needed, and runs HotSync. A `KStatusNotifierItem` system tray icon keeps the app resident.

**Tech Stack:** Qt6, KF6 (KStatusNotifierItem, KNotification, KConfig), libudev, pilot-link

**Design doc:** `docs/plans/2026-02-17-first-run-and-connection-ux-design.md`

---

## Task 1: Add libudev Dependency to CMake

**Files:**
- Modify: `CMakeLists.txt:20` (add `find_package` for udev)
- Modify: `CMakeLists.txt:56-72` (add udev to link libraries)
- Modify: `src/CMakeLists.txt` (add udev to QPilotCore link libraries)

**Step 1: Add pkg-config lookup for libudev**

In `CMakeLists.txt`, after the KF6 `find_package` calls (around line 32), add:

```cmake
# System libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED libudev)
```

In `src/CMakeLists.txt`, add `${UDEV_LIBRARIES}` to `target_link_libraries(QPilotCore ...)` and `${UDEV_INCLUDE_DIRS}` to `target_include_directories(QPilotCore ...)`.

**Step 2: Build to verify**

Run: `cmake -B build && cmake --build build --parallel $(nproc)`
Expected: PASS (no new sources yet, just linking)

**Step 3: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt
git commit -m "build: add libudev dependency for Palm device detection"
```

---

## Task 2: Add USB Serial Number to DeviceFingerprint

**Files:**
- Modify: `src/profile.h:38-77` (DeviceFingerprint struct)
- Modify: `src/profile.cpp` (serialization of new field)

**Step 1: Add usbSerialNumber field**

In `src/profile.h`, add to the `DeviceFingerprint` struct:

```cpp
QString usbSerialNumber;  // USB descriptor serial (e.g. "L0JG14I11398")
```

Update `matches()` to check serial number first (highest priority), then userId, then userName:

```cpp
bool matches(const DeviceFingerprint &other) const {
    // USB serial number is the most reliable identifier
    if (!usbSerialNumber.isEmpty() && !other.usbSerialNumber.isEmpty()) {
        return usbSerialNumber == other.usbSerialNumber;
    }
    if (userId != 0 && other.userId != 0) {
        return userId == other.userId;
    }
    return !userName.isEmpty() && userName == other.userName;
}
```

Update `registryKey()` and `fromRegistryKey()` to include serial number.

**Step 2: Update Profile serialization**

In `src/profile.cpp`, update `load()` and `save()` to read/write `UsbSerialNumber` alongside `UserId` and `UserName`.

**Step 3: Build and run tests**

Run: `cmake --build build --parallel $(nproc)`
Run: `ctest --test-dir build --output-on-failure -R test_profile`
Expected: PASS

**Step 4: Commit**

```bash
git add src/profile.h src/profile.cpp
git commit -m "feat: add USB serial number to DeviceFingerprint"
```

---

## Task 3: Create PalmDeviceMonitor (udev Watcher)

**Files:**
- Create: `src/palm/palmdevicemonitor.h`
- Create: `src/palm/palmdevicemonitor.cpp`
- Modify: `src/CMakeLists.txt` (add new sources)

**Step 1: Write PalmDeviceMonitor header**

```cpp
#pragma once

#include <QObject>
#include <QStringList>

struct udev;
struct udev_monitor;
class QSocketNotifier;

/**
 * @brief Monitors udev for Palm USB device attach/detach events.
 *
 * Watches for USB devices with idVendor=0830 (Palm, Inc.).
 * When the visor driver creates ttyUSB ports, emits palmDetected()
 * with the list of port paths.
 */
class PalmDeviceMonitor : public QObject
{
    Q_OBJECT

public:
    explicit PalmDeviceMonitor(QObject *parent = nullptr);
    ~PalmDeviceMonitor() override;

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

Q_SIGNALS:
    /** Emitted when a Palm USB device is detected. Ports are all
     *  ttyUSB paths created for this device (typically 2). */
    void palmDetected(const QStringList &ports, const QString &usbSerial);

    /** Emitted when the Palm USB device is disconnected. */
    void palmDisconnected(const QString &usbSerial);

    /** Emitted on monitor errors. */
    void monitorError(const QString &error);

private Q_SLOTS:
    void onUdevEvent();

private:
    void collectPalmPorts(const QString &syspath, QStringList &ports);

    struct udev *m_udev = nullptr;
    struct udev_monitor *m_monitor = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    bool m_running = false;

    // Track detected devices: USB syspath -> serial number
    QMap<QString, QString> m_detectedDevices;
};
```

**Step 2: Implement PalmDeviceMonitor**

Key implementation details:

- Constructor: `udev_new()`, create monitor with `udev_monitor_new_from_netlink(m_udev, "udev")`.
- `start()`: Set filter `udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "tty", nullptr)`. Enable receiving, get fd, create `QSocketNotifier`.
- `onUdevEvent()`: Call `udev_monitor_receive_device()`. If action is "add" and subsystem is "tty", walk parent devices looking for `idVendor` == `"0830"`. If found, collect all ttyUSB siblings, get serial from USB parent, wait 100ms (QTimer::singleShot) for both ports to appear, then emit `palmDetected(ports, serial)`.
- `stop()`: Clean up notifier, unref monitor and udev.

The 100ms delay is needed because the two ttyUSB ports may appear in separate udev events. Use a `QTimer::singleShot` to batch them: on first port detection for a given USB parent, start the timer. When it fires, scan sysfs for all ttyUSB ports under that parent and emit once.

**Step 3: Build**

Run: `cmake --build build --parallel $(nproc)`
Expected: PASS

**Step 4: Commit**

```bash
git add src/palm/palmdevicemonitor.h src/palm/palmdevicemonitor.cpp src/CMakeLists.txt
git commit -m "feat: add PalmDeviceMonitor for udev-based Palm detection"
```

---

## Task 4: Create AutoSyncOrchestrator

**Files:**
- Create: `src/kf6/autosyncorchestrator.h`
- Create: `src/kf6/autosyncorchestrator.cpp`
- Modify: `src/CMakeLists.txt` (add new sources)

**Step 1: Write AutoSyncOrchestrator header**

This class ties together device detection → parallel connection → profile lookup → auto-sync. It's the "brain" of the zero-click flow.

```cpp
#pragma once

#include <QObject>

class PalmDeviceMonitor;
class DeviceSession;
class KPilotDeviceLink;
class Profile;
class LogWidget;

namespace Sync { class SyncEngine; }

/**
 * @brief Orchestrates the auto-detect → connect → sync pipeline.
 *
 * When PalmDeviceMonitor detects a Palm:
 * 1. Races parallel connections on all detected ports
 * 2. Reads device identity (user info, serial number)
 * 3. Looks up or auto-creates a profile
 * 4. Runs HotSync
 * 5. Disconnects cleanly
 */
class AutoSyncOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit AutoSyncOrchestrator(QObject *parent = nullptr);
    ~AutoSyncOrchestrator() override;

    void setDeviceMonitor(PalmDeviceMonitor *monitor);
    void setSyncEngine(Sync::SyncEngine *engine);
    void setLogWidget(LogWidget *logWidget);

    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    void syncStarted(const QString &userName);
    void syncFinished(bool success, const QString &summary);
    void profileCreated(const QString &profilePath, const QString &userName);
    void profileLoaded(Profile *profile);
    void connectionEstablished(const QString &userName, const QString &deviceName);
    void error(const QString &message);
    void statusChanged(const QString &status);

private Q_SLOTS:
    void onPalmDetected(const QStringList &ports, const QString &usbSerial);
    void onConnectionComplete(bool success);
    void onDeviceReady(const QString &userName, const QString &deviceName);
    void onReadyForSync();
    void onSyncFinished(bool success, const QString &summary);

private:
    Profile* findOrCreateProfile(const QString &usbSerial,
                                  const QString &userName, quint32 userId);
    void startParallelConnections(const QStringList &ports);
    void cleanupLosingConnection(int index);

    PalmDeviceMonitor *m_monitor = nullptr;
    Sync::SyncEngine *m_syncEngine = nullptr;
    LogWidget *m_logWidget = nullptr;

    // Parallel connection state
    QList<DeviceSession*> m_racingSessions;
    DeviceSession *m_winningSession = nullptr;
    QString m_currentUsbSerial;
    bool m_busy = false;

    // Profile for current sync
    Profile *m_currentProfile = nullptr;
};
```

**Step 2: Implement AutoSyncOrchestrator**

Key implementation:

- `onPalmDetected()`: If busy, ignore (already syncing). Otherwise, set busy, call `startParallelConnections(ports)`.
- `startParallelConnections()`: Create one `DeviceSession` per port. Connect each session's `connectionComplete` to a lambda that checks if this is the first winner. The first `connectionComplete(true)` wins; set `m_winningSession`, close/delete the rest.
- `onConnectionComplete()`: Winner identified. Read user info. Call `findOrCreateProfile()`.
- `findOrCreateProfile()`: Check `KF6Settings::instance().findProfileForDevice()` by serial + userId. If found, load it. If not found, create `~/PalmSync/<userName>/`, call `profile->initialize()`, register in settings, emit `profileCreated`.
- `onReadyForSync()`: Call `m_winningSession->requestSync(HotSync, m_syncEngine)`.
- `onSyncFinished()`: Disconnect, clean up, set busy=false, emit `syncFinished`.

**Step 3: Build**

Run: `cmake --build build --parallel $(nproc)`
Expected: PASS

**Step 4: Commit**

```bash
git add src/kf6/autosyncorchestrator.h src/kf6/autosyncorchestrator.cpp src/CMakeLists.txt
git commit -m "feat: add AutoSyncOrchestrator for zero-click sync pipeline"
```

---

## Task 5: Add System Tray (KStatusNotifierItem)

**Files:**
- Modify: `CMakeLists.txt` (add KF6StatusNotifierItem if needed — it's part of KF6Notifications which is already linked)
- Modify: `src/kf6/kf6mainwindow.h` (add tray members)
- Modify: `src/kf6/kf6mainwindow.cpp` (create tray, handle close event)

**Step 1: Add KStatusNotifierItem to KF6MainWindow**

In `kf6mainwindow.h`, add forward declaration and member:

```cpp
class KStatusNotifierItem;

// In private members:
KStatusNotifierItem *m_trayIcon = nullptr;
bool m_minimizeToTray = true;
```

**Step 2: Create tray icon in constructor**

In `kf6mainwindow.cpp`, in the constructor after `setupGUI`:

```cpp
// System tray
m_trayIcon = new KStatusNotifierItem(this);
m_trayIcon->setIconByName(QStringLiteral("phone"));
m_trayIcon->setToolTipTitle(i18n("QPilotSync"));
m_trayIcon->setToolTipSubTitle(i18n("Listening for Palm devices"));
m_trayIcon->setCategory(KStatusNotifierItem::ApplicationStatus);
m_trayIcon->setStandardActionsEnabled(true);

// Tray context menu actions
QAction *syncNowAction = new QAction(i18n("Sync Now"), this);
syncNowAction->setEnabled(false);
m_trayIcon->contextMenu()->addAction(syncNowAction);
```

**Step 3: Override closeEvent for minimize-to-tray**

Update `closeEvent()`:

```cpp
void KF6MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_minimizeToTray && m_trayIcon) {
        hide();
        event->ignore();
        return;
    }
    saveWindowState();
    event->accept();
}
```

**Step 4: Add tray state update methods**

Create helpers to update tray tooltip and icon based on sync state:

```cpp
void KF6MainWindow::updateTrayState(const QString &status)
{
    if (!m_trayIcon) return;
    m_trayIcon->setToolTipSubTitle(status);
}
```

Call these from existing connection/sync handlers.

**Step 5: Build and test manually**

Run: `cmake --build build --parallel $(nproc)`
Launch app, verify tray icon appears. Close window, verify it hides to tray. Click tray icon, verify window reappears.

**Step 6: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "feat: add system tray with minimize-to-tray behavior"
```

---

## Task 6: Wire Auto-Detection into KF6MainWindow

**Files:**
- Modify: `src/kf6/kf6mainwindow.h` (add monitor + orchestrator members)
- Modify: `src/kf6/kf6mainwindow.cpp` (create and connect them at startup)

**Step 1: Add members**

In `kf6mainwindow.h`:

```cpp
class PalmDeviceMonitor;
class AutoSyncOrchestrator;

// In private members:
PalmDeviceMonitor *m_deviceMonitor = nullptr;
AutoSyncOrchestrator *m_autoSync = nullptr;
```

**Step 2: Initialize in constructor**

In the constructor, after `initializeConduits()`:

```cpp
// Auto-detection
m_deviceMonitor = new PalmDeviceMonitor(this);
m_autoSync = new AutoSyncOrchestrator(this);
m_autoSync->setDeviceMonitor(m_deviceMonitor);
m_autoSync->setSyncEngine(m_syncEngine);
m_autoSync->setLogWidget(m_logWidget);

// Wire orchestrator signals to UI updates
connect(m_autoSync, &AutoSyncOrchestrator::statusChanged,
        this, &KF6MainWindow::updateTrayState);
connect(m_autoSync, &AutoSyncOrchestrator::connectionEstablished,
        this, [this](const QString &userName, const QString &deviceName) {
            m_logWidget->logInfo(i18n("Auto-connected to %1 (%2)", userName, deviceName));
            updateMenuState(true);
        });
connect(m_autoSync, &AutoSyncOrchestrator::profileCreated,
        this, [this](const QString &path, const QString &userName) {
            m_logWidget->logInfo(i18n("Created profile for %1 at %2", userName, path));
            // Offer to change location via KNotification
            auto *notif = new KNotification(QStringLiteral("profileCreated"),
                                             KNotification::CloseOnTimeout, this);
            notif->setTitle(i18n("Profile Created"));
            notif->setText(i18n("Your Palm data is stored at %1.\nClick to change location.", path));
            notif->sendEvent();
        });
connect(m_autoSync, &AutoSyncOrchestrator::profileLoaded,
        this, [this](Profile *profile) {
            m_currentProfile = profile;
            m_syncEngine->setSyncPath(profile->syncFolderPath());
            m_syncEngine->setStateDir(profile->stateDirectoryPath());
            updateWindowTitle();
            updateProfileMenuState();
            m_dashboardWidget->updateStatus(profile, true);
        });
connect(m_autoSync, &AutoSyncOrchestrator::syncFinished,
        this, [this](bool success, const QString &summary) {
            m_logWidget->logInfo(i18n("Auto-sync %1: %2",
                success ? i18n("complete") : i18n("failed"), summary));
            updateMenuState(false);
            m_dashboardWidget->updateStatus(m_currentProfile, false);
        });
connect(m_autoSync, &AutoSyncOrchestrator::error,
        m_logWidget, &LogWidget::logError);

// Start monitoring
if (!m_deviceMonitor->start()) {
    m_logWidget->logWarning(i18n("Failed to start udev monitor. "
                                  "Use Device → Connect for manual connection."));
}
```

**Step 3: Update Dashboard initial text**

Change the Dashboard welcome text from "Welcome to QPilotSync" to include "Listening for Palm devices..." when the monitor is running.

**Step 4: Build and test**

Run: `cmake --build build --parallel $(nproc)`
Launch app, verify "Listening for Palm devices..." appears in log and dashboard. Connect Palm via USB — should auto-detect, auto-create profile, auto-sync.

**Step 5: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "feat: wire udev auto-detection into main window startup"
```

---

## Task 7: Update Dashboard Widget for New UX

**Files:**
- Modify: `src/widgets/dashboard/dashboardwidget.h`
- Modify: `src/widgets/dashboard/dashboardwidget.cpp`

**Step 1: Add last-sync summary display**

Add members:

```cpp
QLabel *m_lastSyncSummaryLabel;
QLabel *m_listeningStatusLabel;
QPushButton *m_configureDevicesButton;
```

Replace the "Welcome to QPilotSync" header with a status-aware display:
- When listening: "Listening for Palm devices. Press HotSync on your Palm."
- When connected: "Connected to [username]'s Palm"
- When syncing: "Syncing [username]'s Palm..." with progress

Add a "Last Sync" card showing record counts and timestamp.

Replace the "Connect to Device" button with context-aware text:
- Listening: "Waiting for HotSync..." (disabled/pulsing)
- Connected: "Sync Now"
- No monitor: "Connect Manually..."

Add "Configure Devices..." button for serial/IR setup.

**Step 2: Add methods for state updates**

```cpp
void setListening(bool listening);
void setSyncing(bool syncing, const QString &deviceName = QString());
void setLastSyncSummary(const QString &summary);
```

**Step 3: Build**

Run: `cmake --build build --parallel $(nproc)`
Expected: PASS

**Step 4: Commit**

```bash
git add src/widgets/dashboard/dashboardwidget.h src/widgets/dashboard/dashboardwidget.cpp
git commit -m "feat: redesign Dashboard for listen-first auto-sync UX"
```

---

## Task 8: Add KF6Settings Entries for New Features

**Files:**
- Modify: `src/kf6/kf6settings.h`
- Modify: `src/kf6/kf6settings.cpp`

**Step 1: Add settings for tray behavior and device lookup by serial**

```cpp
// System tray
bool minimizeToTray() const;
void setMinimizeToTray(bool enabled);

// Device registry by USB serial number
void registerDeviceBySerial(const QString &usbSerial, const QString &profilePath);
QString findProfileBySerial(const QString &usbSerial) const;
```

**Step 2: Implement**

Use KConfig group `[SystemTray]` for tray settings. Use KConfig group `[DeviceSerials]` for serial→profile mapping.

**Step 3: Build**

Run: `cmake --build build --parallel $(nproc)`
Expected: PASS

**Step 4: Commit**

```bash
git add src/kf6/kf6settings.h src/kf6/kf6settings.cpp
git commit -m "feat: add settings for system tray and USB serial registry"
```

---

## Task 9: Integration Test & Cleanup

**Step 1: Full build from clean**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel $(nproc)
```

**Step 2: Run all tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: All existing tests pass.

**Step 3: Manual integration test**

1. Launch app — tray icon appears, dashboard says "Listening for Palm devices..."
2. Put Palm in cradle, press HotSync
3. App detects, connects to correct port, reads user info
4. Profile auto-created at `~/PalmSync/<username>/`
5. HotSync runs automatically
6. Disconnect, dashboard shows "Last sync: just now"
7. Close window — minimizes to tray
8. Press HotSync again — syncs again automatically from tray
9. Quit from tray menu

**Step 4: Remove old debug traces**

Clean up `qDebug()` traces in `actionmanager.cpp` (line 79) and `kf6mainwindow.cpp` (line 769) added during the previous debugging session.

**Step 5: Commit**

```bash
git add -u
git commit -m "chore: integration test cleanup, remove debug traces"
```

---

## Summary

| Task | Component | New Files | Key Dependency |
|------|-----------|-----------|----------------|
| 1 | CMake libudev | — | — |
| 2 | DeviceFingerprint serial | — | — |
| 3 | PalmDeviceMonitor | 2 | libudev |
| 4 | AutoSyncOrchestrator | 2 | Task 3 |
| 5 | System Tray | — | KStatusNotifierItem |
| 6 | Main Window Wiring | — | Tasks 3-5 |
| 7 | Dashboard Redesign | — | Task 6 |
| 8 | Settings Entries | — | — |
| 9 | Integration & Cleanup | — | All |

Tasks 1, 2, 5, 8 are independent and can be parallelized.
Tasks 3 depends on 1. Task 4 depends on 3.
Task 6 depends on 3, 4, 5. Task 7 depends on 6.
Task 9 depends on all.
