# Phase 1-3 Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove ~2,833 lines of dead code, unify conduit enable/disable to profile-only, and unify the auto-sync/manual-sync paths into a single session lifecycle.

**Architecture:** Three sequential phases. Each phase produces a compilable, testable commit. Phase 1 is purely subtractive. Phase 2 changes conduit management ownership. Phase 3 changes session lifecycle ownership.

**Tech Stack:** C++20, Qt6, KDE Frameworks 6 (KXmlGuiWindow, KConfig, KPageDialog, kcoreaddons plugins)

---

## Phase 1: Delete Dead Code

### Task 1: Remove dead files from build

**Files:**
- Modify: `src/CMakeLists.txt:8-9,12-15,24-25`
- Delete: `src/app/mainwindow.cpp`, `src/app/mainwindow.h`
- Delete: `src/settings.cpp`, `src/settings.h`
- Delete: `src/app/exporthandler.cpp`, `src/app/exporthandler.h`
- Delete: `src/app/importhandler.cpp`, `src/app/importhandler.h`

**Step 1: Remove entries from CMakeLists.txt**

In `src/CMakeLists.txt`, remove these lines from the source list:
```
    app/mainwindow.cpp
    app/mainwindow.h
```
```
    app/exporthandler.cpp
    app/exporthandler.h
    app/importhandler.cpp
    app/importhandler.h
```
```
    settings.cpp
    settings.h
```

**Step 2: Delete the files**

```bash
rm src/app/mainwindow.cpp src/app/mainwindow.h
rm src/settings.cpp src/settings.h
rm src/app/exporthandler.cpp src/app/exporthandler.h
rm src/app/importhandler.cpp src/app/importhandler.h
```

**Step 3: Try to build**

```bash
cd build && cmake --build . 2>&1 | head -50
```

Expected: Build **fails** because `kf6mainwindow.cpp` still includes and uses `ExportHandler` and `ImportHandler`. This is expected — Task 2 fixes these references.

---

### Task 2: Remove ExportHandler/ImportHandler from KF6MainWindow

**Files:**
- Modify: `src/kf6/kf6mainwindow.h:17-18,177-179`
- Modify: `src/kf6/kf6mainwindow.cpp:9-10,91-92,117-122,153-154,186-187,278-283,417-435,1114-1115,1162-1163,1339-1340`

**Step 1: Remove includes from kf6mainwindow.cpp**

Remove these two lines (lines 9-10):
```cpp
#include "../app/exporthandler.h"
#include "../app/importhandler.h"
```

**Step 2: Remove forward declarations from kf6mainwindow.h**

Remove these two lines (lines 17-18):
```cpp
class ExportHandler;
class ImportHandler;
```

**Step 3: Remove member variables from kf6mainwindow.h**

Remove the section (lines 177-179):
```cpp
    // Export/Import handlers
    ExportHandler *m_exportHandler;
    ImportHandler *m_importHandler;
```

**Step 4: Remove initialization from constructor (kf6mainwindow.cpp)**

Remove from initializer list (lines 91-92):
```cpp
    , m_exportHandler(nullptr)
    , m_importHandler(nullptr)
```

Remove from constructor body (lines 117-122):
```cpp
    // Create export/import handlers (must be before setupConnections)
    m_exportHandler = new ExportHandler(this);
    m_exportHandler->setLogWidget(m_logWidget);

    m_importHandler = new ImportHandler(this);
    m_importHandler->setLogWidget(m_logWidget);
```

**Step 5: Remove signal connections in setupConnections() (kf6mainwindow.cpp)**

Remove the export/import connection block (lines 417-435):
```cpp
    connect(m_actionManager, &ActionManager::exportMemosRequested,
            m_exportHandler, &ExportHandler::exportMemos);
    connect(m_actionManager, &ActionManager::exportContactsRequested,
            m_exportHandler, &ExportHandler::exportContacts);
    connect(m_actionManager, &ActionManager::exportCalendarRequested,
            m_exportHandler, &ExportHandler::exportCalendar);
    connect(m_actionManager, &ActionManager::exportTodosRequested,
            m_exportHandler, &ExportHandler::exportTodos);
    connect(m_actionManager, &ActionManager::exportAllRequested,
            m_exportHandler, &ExportHandler::exportAll);

    connect(m_actionManager, &ActionManager::importMemoRequested,
            m_importHandler, &ImportHandler::importMemo);
    connect(m_actionManager, &ActionManager::importContactRequested,
            m_importHandler, &ImportHandler::importContact);
    connect(m_actionManager, &ActionManager::importEventRequested,
            m_importHandler, &ImportHandler::importEvent);
    connect(m_actionManager, &ActionManager::importTodoRequested,
            m_importHandler, &ImportHandler::importTodo);
```

**Step 6: Remove setDeviceLink(nullptr) calls in auto-sync handler**

In the `connectionEstablished` lambda (~line 153-154), remove:
```cpp
                m_exportHandler->setDeviceLink(m_deviceLink);
                m_importHandler->setDeviceLink(m_deviceLink);
```

In the `syncFinished` lambda (~line 186-187), remove:
```cpp
                m_exportHandler->setDeviceLink(nullptr);
                m_importHandler->setDeviceLink(nullptr);
```

**Step 7: Remove setDeviceLink(nullptr) calls in destructor**

In destructor (~lines 278-283), remove:
```cpp
    if (m_exportHandler) {
        m_exportHandler->setDeviceLink(nullptr);
    }
    if (m_importHandler) {
        m_importHandler->setDeviceLink(nullptr);
    }
```

**Step 8: Remove setDeviceLink calls in disconnect handler**

In the `disconnected` lambda (~lines 1114-1115), remove:
```cpp
                m_exportHandler->setDeviceLink(nullptr);
                m_importHandler->setDeviceLink(nullptr);
```

In `onConnectionComplete()` (~lines 1162-1163), remove:
```cpp
    m_exportHandler->setDeviceLink(m_deviceLink);
    m_importHandler->setDeviceLink(m_deviceLink);
```

In `onDisconnectDevice()` or similar cleanup (~lines 1339-1340), remove:
```cpp
        m_exportHandler->setDeviceLink(nullptr);
        m_importHandler->setDeviceLink(nullptr);
```

**Step 9: Build and verify**

```bash
cd build && cmake --build . 2>&1 | head -50
```

Expected: Build **may fail** if ActionManager still references export/import signals. Task 3 handles that.

---

### Task 3: Remove Export/Import actions from ActionManager and XMLGUI

**Files:**
- Modify: `src/kf6/actionmanager.h:56-67,114-125`
- Modify: `src/kf6/actionmanager.cpp:168-212` (the `setupDataActions()` function)
- Modify: `data/wildpalmsui.rc:44-62`

**Step 1: Remove export/import action accessors from actionmanager.h**

Remove (lines 56-67):
```cpp
    // ========== Export Actions ==========
    QAction* exportMemosAction() const { return action(QStringLiteral("export_memos")); }
    QAction* exportContactsAction() const { return action(QStringLiteral("export_contacts")); }
    QAction* exportCalendarAction() const { return action(QStringLiteral("export_calendar")); }
    QAction* exportTodosAction() const { return action(QStringLiteral("export_todos")); }
    QAction* exportAllAction() const { return action(QStringLiteral("export_all")); }

    // ========== Import Actions ==========
    QAction* importMemoAction() const { return action(QStringLiteral("import_memo")); }
    QAction* importContactAction() const { return action(QStringLiteral("import_contact")); }
    QAction* importEventAction() const { return action(QStringLiteral("import_event")); }
    QAction* importTodoAction() const { return action(QStringLiteral("import_todo")); }
```

**Step 2: Remove export/import signals from actionmanager.h**

Remove (lines 114-125):
```cpp
    // Export operations
    void exportMemosRequested();
    void exportContactsRequested();
    void exportCalendarRequested();
    void exportTodosRequested();
    void exportAllRequested();

    // Import operations
    void importMemoRequested();
    void importContactRequested();
    void importEventRequested();
    void importTodoRequested();
```

**Step 3: Remove export/import action creation from actionmanager.cpp**

Remove the entire export/import block in `setupDataActions()` (lines 168-212), which creates the 9 QAction objects and connects them.

If `setupDataActions()` becomes empty after this, remove the method entirely and its call from `setupActions()`.

**Step 4: Remove Data menu from XMLGUI**

In `data/wildpalmsui.rc`, remove the entire `<Menu name="data">` block (lines 44-62):
```xml
        <Menu name="data">
            <text>D&amp;ata</text>
            <Menu name="export">
                <text>&amp;Export</text>
                <Action name="export_memos"/>
                <Action name="export_contacts"/>
                <Action name="export_calendar"/>
                <Action name="export_todos"/>
                <Separator/>
                <Action name="export_all"/>
            </Menu>
            <Menu name="import">
                <text>&amp;Import</text>
                <Action name="import_memo"/>
                <Action name="import_contact"/>
                <Action name="import_event"/>
                <Action name="import_todo"/>
            </Menu>
        </Menu>
```

**Important:** Increment the `version` attribute in the `<gui>` tag (line 2) from `"2"` to `"3"`. This forces KDE to regenerate the cached menu layout:
```xml
<gui name="wildpalms" version="3">
```

**Step 5: Build and verify**

```bash
cd build && cmake --build . 2>&1 | head -50
```

Expected: Clean build. No warnings about export/import.

---

### Task 4: Remove orphaned signals

**Files:**
- Modify: `src/palm/devicesession.h:149`
- Modify: `src/app/conflictreviewwidget.h:57`

**Step 1: Remove operationCancelled from DeviceSession**

In `src/palm/devicesession.h`, remove line 149:
```cpp
    void operationCancelled();
```

**Step 2: Remove applyResolutionsRequested from ConflictReviewWidget**

In `src/app/conflictreviewwidget.h`, remove the signal declaration (~line 57):
```cpp
    void applyResolutionsRequested();
```

Also in `src/app/conflictreviewwidget.cpp`, remove the connect call (~line 180):
```cpp
    connect(m_applyBtn, &QPushButton::clicked,
            this, &ConflictReviewWidget::applyResolutionsRequested);
```

Check if `m_applyBtn` is used for anything else. If `applyResolutionsRequested` was its only purpose, consider whether the button itself should be removed. If the button has no other connections, remove `m_applyBtn` from the class (declaration, creation, layout add).

**Step 3: Build and run tests**

```bash
cd build && cmake --build . && ctest --output-on-failure
```

Expected: Clean build, all tests pass.

**Step 4: Run the application**

```bash
cd build && ./bin/wildpalms
```

Verify: App launches, no Data menu visible, no crashes.

**Step 5: Commit**

```bash
git add -A
git commit -m "refactor: remove dead code (MainWindow, Settings, Export/Import handlers)

Delete 2,833 lines of unused code:
- Old QMainWindow-based MainWindow (never instantiated, main.cpp uses KF6MainWindow)
- QSettings-based Settings singleton (only used by dead MainWindow)
- ExportHandler/ImportHandler (all methods were 'Not Available' stubs)
- Export/Import menu actions and Data menu from XMLGUI
- Orphaned signals: DeviceSession::operationCancelled, ConflictReviewWidget::applyResolutionsRequested"
```

---

## Phase 2: Unify Conduit Enable/Disable to Profile-Only

### Task 5: Remove enabled state from ConduitManager

**Files:**
- Modify: `src/kf6/conduitmanager.h`
- Modify: `src/kf6/conduitmanager.cpp`

**Step 1: Remove `enabled` field from PluginInfo struct**

In `conduitmanager.h`, remove from `PluginInfo` (line 49):
```cpp
        bool enabled = false;
```

**Step 2: Remove enable/disable methods from header**

Remove these declarations from `conduitmanager.h`:
```cpp
    bool isConduitEnabled(const QString &pluginId) const;
    void setConduitEnabled(const QString &pluginId, bool enabled);
```
```cpp
    QList<IConduit *> enabledConduits() const;
```
```cpp
    QString enabledConduitForCreatorId(const QString &creatorId) const;
```

**Step 3: Remove loadConfig/saveConfig from header**

Remove:
```cpp
    void loadConfig();
    void saveConfig();
```

**Step 4: Change resolveExecutionOrder signature**

Change from:
```cpp
    QStringList resolveExecutionOrder() const;
```
To:
```cpp
    QStringList resolveExecutionOrder(const QStringList &enabledConduitIds) const;
```

**Step 5: Remove implementations from conduitmanager.cpp**

Remove these method bodies entirely:
- `isConduitEnabled()` (~lines 187-194)
- `setConduitEnabled()` (~lines 196-221)
- `enabledConduits()` (~lines 160-169)
- `enabledConduitForCreatorId()` (~lines 234-245)
- `loadConfig()` (~lines 347-356)
- `saveConfig()` (~lines 358-368)

**Step 6: Update resolveExecutionOrder implementation**

Change the method to take a parameter instead of reading internal enabled state. Replace the opening block (~lines 249-258):

From:
```cpp
QStringList ConduitManager::resolveExecutionOrder() const
{
    // Collect enabled conduit IDs
    QStringList conduitIds;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it->enabled) {
            conduitIds.append(it.key());
        }
    }
```

To:
```cpp
QStringList ConduitManager::resolveExecutionOrder(const QStringList &enabledConduitIds) const
{
    QStringList conduitIds = enabledConduitIds;
```

The rest of the method body stays the same — it already works with the `conduitIds` local variable.

**Step 7: Build (expect failures)**

```bash
cd build && cmake --build . 2>&1 | head -80
```

Expected: Build fails because callers reference removed methods. Tasks 6-7 fix these.

---

### Task 6: Update KF6MainWindow conduit initialization

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp:636-661`

**Step 1: Remove loadConfig() call**

In `initializeConduits()` (line 644), remove:
```cpp
    m_conduitManager->loadConfig();
```

**Step 2: Change conduit loading to load ALL discovered conduits**

Replace the enabled-check loop (lines 652-660):

From:
```cpp
    // Load all enabled conduits (creates views & registers with SyncEngine)
    for (const auto &info : m_conduitManager->conduitList()) {
        if (info.enabled) {
            QString conduitId = info.metaData.value(QStringLiteral("X-WildPalms-ConduitId"));
            if (conduitId.isEmpty()) {
                conduitId = info.metaData.pluginId();
            }
            m_conduitManager->loadConduit(conduitId);
        }
    }
```

To:
```cpp
    // Load ALL discovered conduits (profile controls which ones participate in sync)
    for (const auto &info : m_conduitManager->conduitList()) {
        QString conduitId = info.metaData.value(QStringLiteral("X-WildPalms-ConduitId"));
        if (conduitId.isEmpty()) {
            conduitId = info.metaData.pluginId();
        }
        m_conduitManager->loadConduit(conduitId);
    }
```

**Step 3: Build (may still have failures from SettingsDialog)**

---

### Task 7: Remove Conduits page from SettingsDialog

**Files:**
- Modify: `src/settingsdialog.h`
- Modify: `src/settingsdialog.cpp`

**Step 1: Remove conduit-related members from settingsdialog.h**

Remove:
```cpp
    void onConduitToggled(QTreeWidgetItem *item, int column);
```
```cpp
    QWidget* createConduitsPage();
```
```cpp
    void addConduitConfigPages(const QString &conduitId);
    void removeConduitConfigPages(const QString &conduitId);
```
```cpp
    ConduitManager *m_conduitManager;
```
```cpp
    // Conduits page
    QTreeWidget *m_conduitTree;
    QLabel *m_conduitDetailLabel;
    QMap<QString, QList<KPageWidgetItem*>> m_conduitConfigPages;
```

Remove the forward declaration:
```cpp
class ConduitManager;
```

Update the constructor signature to remove ConduitManager parameter:

From:
```cpp
    explicit SettingsDialog(ConduitManager *conduitManager,
                            QWidget *parent = nullptr);
```

To:
```cpp
    explicit SettingsDialog(QWidget *parent = nullptr);
```

**Step 2: Remove conduit-related implementations from settingsdialog.cpp**

Remove these entire methods:
- `createConduitsPage()` (~lines 101-218)
- `onConduitToggled()` (~lines 220-254)
- `addConduitConfigPages()` (~lines 256-284)
- `removeConduitConfigPages()` (~lines 286-294)

In the constructor, remove the call that adds the Conduits page. Keep the calls for Profiles, Devices, and Advanced pages.

Remove `m_conduitManager` initialization from constructor.

**Step 3: Remove includes no longer needed**

Remove `QTreeWidget`, `QTreeWidgetItem`, `ConduitManager` includes from settingsdialog.cpp if no longer used.

**Step 4: Update callers of SettingsDialog constructor**

In `kf6mainwindow.cpp`, find `onSettings()` and update the SettingsDialog construction.

From:
```cpp
    auto *dlg = new SettingsDialog(m_conduitManager, this);
```

To:
```cpp
    auto *dlg = new SettingsDialog(this);
```

**Step 5: Build and run tests**

```bash
cd build && cmake --build . && ctest --output-on-failure
```

Expected: Clean build, all tests pass.

---

### Task 8: Create ProfilePropertiesDialog

**Files:**
- Create: `src/widgets/dialogs/profilepropertiesdialog.h`
- Create: `src/widgets/dialogs/profilepropertiesdialog.cpp`
- Modify: `src/CMakeLists.txt` (add new files)

**Step 1: Create the header**

```cpp
#ifndef PROFILEPROPERTIESDIALOG_H
#define PROFILEPROPERTIESDIALOG_H

#include <KPageDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QGroupBox;
class QFormLayout;
class Profile;
class ConduitManager;

/**
 * @brief Per-profile settings dialog
 *
 * Three pages:
 *   - Device: path, baud rate, connection mode, auto-sync, default sync type
 *   - Conduits: enable/disable per conduit with mutual exclusion per creator ID
 *   - Conflict: auto-resolve strategy, fallback behavior
 */
class ProfilePropertiesDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit ProfilePropertiesDialog(Profile *profile,
                                      ConduitManager *conduitManager,
                                      QWidget *parent = nullptr);

Q_SIGNALS:
    void settingsChanged();

private Q_SLOTS:
    void onApply();

private:
    void loadSettings();
    void saveSettings();
    QWidget* createDevicePage();
    QWidget* createConduitsPage();
    QWidget* createConflictPage();

    Profile *m_profile;
    ConduitManager *m_conduitManager;

    // Device page
    QLineEdit *m_devicePathEdit;
    QComboBox *m_baudRateCombo;
    QComboBox *m_connectionModeCombo;
    QCheckBox *m_autoSyncCheck;
    QComboBox *m_defaultSyncTypeCombo;

    // Conduits page — dynamic checkboxes keyed by conduit ID
    QMap<QString, QCheckBox*> m_conduitChecks;

    // Conflict page
    QComboBox *m_autoResolveCombo;
    QComboBox *m_fallbackCombo;
};

#endif // PROFILEPROPERTIESDIALOG_H
```

**Step 2: Create the implementation**

Create `src/widgets/dialogs/profilepropertiesdialog.cpp` with:

- Constructor: Creates 3 pages via KPageDialog, calls loadSettings()
- `createDevicePage()`: QFormLayout with device path QLineEdit, baud rate QComboBox (9600/19200/38400/57600/115200), connection mode QComboBox (KeepAlive/DisconnectAfterSync), auto-sync QCheckBox, default sync type QComboBox (HotSync/FullSync)
- `createConduitsPage()`: Iterate `m_conduitManager->conduitList()`. For each conduit, create a QCheckBox with the conduit's display name. Group by `palmCreatorId()` using QGroupBox. When a checkbox is toggled, enforce mutual exclusion: if enabling, iterate other checkboxes sharing the same creator ID and uncheck them.
- `createConflictPage()`: QFormLayout with auto-resolve QComboBox (None/Palm Wins/PC Wins/Newer Wins/Older Wins/Duplicate), fallback QComboBox (Defer/Skip/Use Default)
- `loadSettings()`: Read from `m_profile` and populate all widgets
- `saveSettings()`: Write from widgets to `m_profile`, call `m_profile->save()`
- `onApply()`: Call saveSettings(), emit settingsChanged()

For the conduits page mutual exclusion, store a `QMap<QString, QString>` mapping conduit ID to creator ID. On checkbox toggle:
```cpp
void onConduitToggled(const QString &conduitId, bool checked)
{
    if (!checked) return;
    QString creatorId = m_conduitManager->palmCreatorId(conduitId);
    if (creatorId.isEmpty()) return;
    for (auto it = m_conduitChecks.constBegin(); it != m_conduitChecks.constEnd(); ++it) {
        if (it.key() != conduitId
            && m_conduitManager->palmCreatorId(it.key()) == creatorId
            && it.value()->isChecked()) {
            it.value()->setChecked(false);
        }
    }
}
```

**Step 3: Add to CMakeLists.txt**

Add to `src/CMakeLists.txt` source list:
```
    widgets/dialogs/profilepropertiesdialog.cpp
    widgets/dialogs/profilepropertiesdialog.h
```

**Step 4: Wire into KF6MainWindow**

In `kf6mainwindow.cpp`, update `onProfileSettings()` to open `ProfilePropertiesDialog`:

```cpp
void KF6MainWindow::onProfileSettings()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded"));
        return;
    }

    auto *dlg = new ProfilePropertiesDialog(m_currentProfile, m_conduitManager, this);
    connect(dlg, &ProfilePropertiesDialog::settingsChanged, this, [this]() {
        // Re-apply conduit enabled settings to sync engine
        for (const QString &conduitId : m_syncEngine->registeredConduits()) {
            m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));

            QJsonObject conduitSettings = m_currentProfile->conduitSettings(conduitId);
            if (!conduitSettings.isEmpty()) {
                auto *conduit = dynamic_cast<Sync::SyncConduitBase*>(m_syncEngine->conduit(conduitId));
                if (conduit) {
                    conduit->loadSettings(conduitSettings);
                }
            }
        }

        // Update connection mode on active session
        if (m_session) {
            m_session->setConnectionMode(m_currentProfile->connectionMode());
        }

        updateWindowTitle();
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}
```

Add `#include "../widgets/dialogs/profilepropertiesdialog.h"` to kf6mainwindow.cpp.

**Step 5: Build, run, and test**

```bash
cd build && cmake --build . && ctest --output-on-failure
```

Run the app. Open a profile. Click File → Profile Settings. Verify the dialog shows all three pages with correct values. Toggle conduits, verify mutual exclusion. Click OK, verify settings are saved to `.wildpalms.conf`.

**Step 6: Commit**

```bash
git add -A
git commit -m "refactor: unify conduit enable/disable to profile-only

- Remove ConduitManager enabled state (loadConfig, saveConfig, setConduitEnabled, etc.)
- Remove Conduits page from SettingsDialog
- Load ALL discovered conduits at startup (profile controls sync participation)
- Add ProfilePropertiesDialog with Device, Conduits, and Conflict pages
- Mutual exclusion per Palm creator ID enforced in profile dialog
- resolveExecutionOrder() now takes enabledConduitIds parameter"
```

---

## Phase 3: Unify the Sync Path

### Task 9: Slim down AutoSyncOrchestrator

**Files:**
- Modify: `src/kf6/autosyncorchestrator.h`
- Modify: `src/kf6/autosyncorchestrator.cpp`

**Step 1: Rewrite the header**

Replace the entire `autosyncorchestrator.h` with:

```cpp
#ifndef AUTOSYNCORCHESTRATOR_H
#define AUTOSYNCORCHESTRATOR_H

#include <QObject>

class PalmDeviceMonitor;
class Profile;
class LogWidget;

/**
 * @brief Lightweight USB device detection and profile resolution.
 *
 * When PalmDeviceMonitor detects a Palm:
 * 1. Looks up or auto-creates a profile based on USB serial / fingerprint
 * 2. Emits deviceDetected() so KF6MainWindow can handle the session
 *
 * Does NOT create DeviceSessions or manage sync lifecycle.
 */
class AutoSyncOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit AutoSyncOrchestrator(QObject *parent = nullptr);
    ~AutoSyncOrchestrator() override;

    void setDeviceMonitor(PalmDeviceMonitor *monitor);
    void setLogWidget(LogWidget *logWidget);

    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /** Emitted when a device is detected and profile resolved */
    void deviceDetected(Profile *profile, const QStringList &ports);

    /** Emitted when a new profile is auto-created */
    void profileCreated(const QString &profilePath, const QString &userName);

    void error(const QString &message);
    void statusChanged(const QString &status);

private Q_SLOTS:
    void onPalmDetected(const QStringList &ports, const QString &usbSerial);

private:
    Profile* findOrCreateProfile(const QString &usbSerial,
                                  const QString &userName, quint32 userId);

    PalmDeviceMonitor *m_monitor = nullptr;
    LogWidget *m_logWidget = nullptr;
    QString m_currentUsbSerial;
    bool m_busy = false;
};

#endif // AUTOSYNCORCHESTRATOR_H
```

**Step 2: Rewrite the implementation**

Replace `autosyncorchestrator.cpp`. Keep:
- Constructor/destructor (simplified — no session cleanup)
- `setDeviceMonitor()` (unchanged)
- `setLogWidget()` (new setter, just stores pointer)
- `onPalmDetected()` — simplified: checks busy, sets busy, stores serial, emits `statusChanged("Detecting device...")`. Since we can no longer read the device identity without a session, we use USB serial to find/create profile. If the serial is enough to resolve a profile, emit `deviceDetected(profile, ports)`. If not (no serial), emit error.
- `findOrCreateProfile()` — keep the existing logic unchanged, it already works with USB serial + KF6Settings lookups

**Key change in onPalmDetected():**

The current version calls `startConnection()` which creates a DeviceSession, connects, reads user info from device, then calls `findOrCreateProfile()`. In the new version, we can only use the USB serial to resolve the profile (no device connection). If the USB serial is empty and no profile can be found, we emit `deviceDetected(nullptr, ports)` and let KF6MainWindow handle it (it will connect, read user info, and create a profile interactively).

```cpp
void AutoSyncOrchestrator::onPalmDetected(const QStringList &ports, const QString &usbSerial)
{
    if (m_busy) {
        qDebug() << "[AutoSync] Already busy, ignoring detection";
        return;
    }

    m_busy = true;
    m_currentUsbSerial = usbSerial;

    emit statusChanged(i18n("Palm device detected..."));

    if (m_logWidget) {
        m_logWidget->logInfo(i18n("Palm detected on: %1", ports.join(", ")));
    }

    // Try to resolve profile from USB serial
    Profile *profile = nullptr;
    if (!usbSerial.isEmpty()) {
        QString profilePath = KF6Settings::instance().findProfileBySerial(usbSerial);
        if (!profilePath.isEmpty() && QDir(profilePath).exists()) {
            profile = new Profile(profilePath);
            if (!profile->load()) {
                delete profile;
                profile = nullptr;
            }
        }
    }

    // If no profile found by serial, try device registry
    if (!profile && !usbSerial.isEmpty()) {
        DeviceFingerprint fp;
        fp.usbSerialNumber = usbSerial;
        QString profilePath = KF6Settings::instance().findProfileForDevice(fp);
        if (!profilePath.isEmpty() && QDir(profilePath).exists()) {
            profile = new Profile(profilePath);
            if (!profile->load()) {
                delete profile;
                profile = nullptr;
            }
        }
    }

    // Emit result — KF6MainWindow handles the rest
    // profile may be nullptr if no match found (first-time device)
    emit deviceDetected(profile, ports);
    m_busy = false;
}
```

Note: `findOrCreateProfile()` can be simplified or removed if the profile-creation logic moves to KF6MainWindow's handler (recommended). For now, keep it as a helper that KF6MainWindow can call after it connects and reads user info.

Actually, move `findOrCreateProfile()` to remain in AutoSyncOrchestrator but make it public, so KF6MainWindow can call it after reading device identity:

In the header, add to public:
```cpp
    Profile* findOrCreateProfile(const QString &usbSerial,
                                  const QString &userName, quint32 userId);
```

**Step 3: Build (expect failures from KF6MainWindow)**

---

### Task 10: Update KF6MainWindow for unified sync path

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

**Step 1: Add new slot declaration to kf6mainwindow.h**

In the private Q_SLOTS section, add:
```cpp
    void onAutoDeviceDetected(Profile *profile, const QStringList &ports);
```

**Step 2: Replace auto-sync signal connections**

In `kf6mainwindow.cpp`, replace the entire auto-sync connection block (~lines 134-203) with:

```cpp
    // Auto-sync orchestrator — detection and profile resolution only
    connect(m_autoSync, &AutoSyncOrchestrator::deviceDetected,
            this, &KF6MainWindow::onAutoDeviceDetected);
    connect(m_autoSync, &AutoSyncOrchestrator::profileCreated,
            this, [this](const QString &path, const QString &userName) {
                m_logWidget->logInfo(i18n("Created profile for %1 at %2", userName, path));
                auto *notif = new KNotification(QStringLiteral("profileCreated"),
                                                 KNotification::CloseOnTimeout, this);
                notif->setTitle(i18n("Profile Created"));
                notif->setText(i18n("Your Palm data is stored at %1.\nClick to change location.", path));
                notif->sendEvent();
            });
    connect(m_autoSync, &AutoSyncOrchestrator::error,
            m_logWidget, &LogWidget::logError);
    connect(m_autoSync, &AutoSyncOrchestrator::statusChanged,
            this, &KF6MainWindow::updateTrayState);
```

**Step 3: Remove setSyncEngine from auto-sync initialization**

Find where `m_autoSync->setSyncEngine(m_syncEngine)` is called and remove it. The orchestrator no longer has this method.

**Step 4: Implement onAutoDeviceDetected**

```cpp
void KF6MainWindow::onAutoDeviceDetected(Profile *profile, const QStringList &ports)
{
    // Guard: if we're already busy with a sync, ignore
    if (m_session && m_session->isBusy()) {
        m_logWidget->logWarning(i18n("Device detected but sync already in progress — ignoring"));
        delete profile;  // We own it, must clean up
        return;
    }

    // If an idle session exists, disconnect it first
    if (m_session && m_session->isConnected()) {
        m_logWidget->logInfo(i18n("Disconnecting previous session for new device"));
        m_session->disconnectDevice();
        m_session->deleteLater();
        m_session = nullptr;
        m_deviceLink = nullptr;
    }

    if (profile) {
        // Known device — load profile and connect
        loadProfile(profile->syncFolderPath());
        delete profile;  // loadProfile creates its own copy

        // Connect using the detected ports
        // Reuse existing startConnection but with multi-port support
        startConnectionMultiPort(ports);
    } else {
        // Unknown device — connect first, then we'll read identity and create profile
        m_logWidget->logInfo(i18n("New Palm device detected — connecting to identify..."));
        startConnectionMultiPort(ports);
        // Profile will be created in onDeviceReady after we read device identity
    }
}
```

**Step 5: Add startConnectionMultiPort method**

The existing `startConnection(const QString &devicePath)` takes a single path. Add a new method that takes a QStringList:

In `kf6mainwindow.h`, add to private:
```cpp
    void startConnectionMultiPort(const QStringList &devicePaths);
```

In `kf6mainwindow.cpp`:
```cpp
void KF6MainWindow::startConnectionMultiPort(const QStringList &devicePaths)
{
    if (devicePaths.isEmpty()) {
        m_logWidget->logError(i18n("No device paths provided"));
        return;
    }

    // Clean up existing session
    if (m_session) {
        m_session->disconnectDevice();
        m_session->deleteLater();
        m_session = nullptr;
        m_deviceLink = nullptr;
    }

    m_session = new DeviceSession(this);

    if (m_currentProfile) {
        m_session->setConnectionMode(m_currentProfile->connectionMode());
    }

    // Wire up the same signals as startConnection()
    connect(m_session, &DeviceSession::connectionComplete,
            this, &KF6MainWindow::onConnectionComplete);
    connect(m_session, &DeviceSession::deviceReady,
            this, &KF6MainWindow::onDeviceReady);
    connect(m_session, &DeviceSession::logMessage,
            m_logWidget, &LogWidget::logInfo);
    connect(m_session, &DeviceSession::errorOccurred,
            m_logWidget, &LogWidget::logError);
    connect(m_session, &DeviceSession::progressUpdated,
            this, &KF6MainWindow::onSyncProgress);
    connect(m_session, &DeviceSession::palmScreenMessage,
            this, &KF6MainWindow::onSessionPalmScreen);
    connect(m_session, &DeviceSession::installFinished,
            this, &KF6MainWindow::onInstallFinished);
    connect(m_session, &DeviceSession::syncFinished,
            this, [this](bool success, const QString &summary) {
                statusBar()->showMessage(success ? i18n("Sync complete") : i18n("Sync failed: %1", summary));
            });
    connect(m_session, &DeviceSession::syncResultReady,
            this, &KF6MainWindow::onAsyncSyncResult);
    connect(m_session, &DeviceSession::disconnected,
            this, [this]() {
                m_deviceLink = nullptr;
                m_syncEngine->setDeviceLink(nullptr);
                updateMenuState(false);
                if (m_currentProfile) {
                    m_dashboardWidget->updateStatus(m_currentProfile, false);
                }
            });
    connect(m_session, &DeviceSession::readyForSync,
            this, &KF6MainWindow::onReadyForSync);

    m_logWidget->logInfo(i18n("Connecting to %1...", devicePaths.join(", ")));
    m_session->connectDevice(devicePaths);

    updateMenuState(false);
    m_actionManager->cancelConnectionAction()->setEnabled(true);
}
```

Note: This duplicates the signal wiring from the existing `startConnection()`. Consider refactoring `startConnection(const QString&)` to call `startConnectionMultiPort(QStringList{devicePath})` to avoid duplication.

**Step 6: Handle auto-sync in onReadyForSync**

In the existing `onReadyForSync()`, add auto-sync check:

After device link is stored and menus updated, add:
```cpp
    // Auto-sync if profile is configured for it
    if (m_currentProfile && m_currentProfile->autoSyncOnConnect()) {
        m_logWidget->logInfo(i18n("Auto-sync enabled — starting HotSync"));
        onHotSync();
    }
```

**Step 7: Handle unknown device in onDeviceReady**

In `onDeviceReady(userName, deviceName)`, if no profile is loaded, use the orchestrator to create one:

```cpp
    // If no profile loaded yet (auto-detected unknown device), create one
    if (!m_currentProfile) {
        quint32 userId = 0;
        QString usbSerial;
        if (m_deviceLink) {
            // Read user ID from device link
            userId = m_deviceLink->pilotUserId();
            // USB serial would have been passed through — check m_autoSync
        }
        Profile *newProfile = m_autoSync->findOrCreateProfile(usbSerial, userName, userId);
        if (newProfile) {
            loadProfile(newProfile->syncFolderPath());
            delete newProfile;
        }
    }
```

**Step 8: Build and test**

```bash
cd build && cmake --build . && ctest --output-on-failure
```

**Step 9: Manual testing**

1. **Manual sync:** Open profile → Connect → HotSync → verify works
2. **Auto-sync (auto-sync on):** Set profile `autoSyncOnConnect=true`, plug in Palm → should detect, connect, and sync automatically
3. **Auto-sync (auto-sync off):** Set `autoSyncOnConnect=false`, plug in Palm → should detect, connect, but wait for user to click sync
4. **Auto-detect while busy:** Start a manual sync, plug in Palm → should log warning and ignore

**Step 10: Refactor startConnection to use startConnectionMultiPort**

Refactor the existing `startConnection(const QString &devicePath)` to delegate:

```cpp
void KF6MainWindow::startConnection(const QString &devicePath)
{
    startConnectionMultiPort(QStringList{devicePath});
}
```

Remove the duplicated signal wiring from the old `startConnection`. Now there's ONE code path for all connections.

**Step 11: Build and run full test suite**

```bash
cd build && cmake --build . && ctest --output-on-failure
```

**Step 12: Commit**

```bash
git add -A
git commit -m "refactor: unify sync path — single session lifecycle in KF6MainWindow

- AutoSyncOrchestrator becomes thin detection/profile-resolution layer
- Emits deviceDetected() instead of managing sessions
- KF6MainWindow owns ALL DeviceSession creation and lifecycle
- Single startConnectionMultiPort() handles both auto and manual connect
- Auto-sync behavior controlled by profile->autoSyncOnConnect()
- Race condition eliminated: busy session ignored on new detection
- Removed syncStarted/syncFinished/connectionEstablished signals from orchestrator"
```
