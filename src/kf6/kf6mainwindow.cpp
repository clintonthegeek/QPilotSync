#include "kf6mainwindow.h"
#include "kf6settings.h"
#include "actionmanager.h"
#include "conduitmanager.h"
#include "autosyncorchestrator.h"

#include "../app/logwidget.h"
#include "../palm/palmdevicemonitor.h"
#include "../settingsdialog.h"

#include "../wildpalms_version.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"
#include "../profile.h"

#include "../core/iconduit.h"
#include "../runtime/accountcontroller.h"
#include "../runtime/palmruntime.h"
#include "../runtime/palmrunresult.h"
#include "../core/ibackendplugin_v2.h"
#include "../runtime/backendpluginmanager.h"
#include "../sync/syncengine.h"
#include "../core/synctypes.h"
#include "../sync/conduit.h"
#include "../sync/localfilebackend.h"
#include "../sync/qsynccore/conflictstore.h"
#include "../sync/syncstate.h"
#include "../app/interactiveconflicthandler.h"
// M5a: bridge header — safe to include alongside WP-local QSyncCore headers.
// The full KalburatorInteractiveConflictHandler header must NOT appear here;
// see src/app/conflict/CMakeLists.txt for the include-guard collision rationale.
#include "../app/conflict/conflictdialogbridge.h"
#include "../app/mapping/mappingeditordialog.h"
#include "../widgets/dialogs/conflictreviewdialog.h"

// Widget includes
#include "../widgets/dashboard/dashboardwidget.h"
#include "../widgets/dialogs/profilepropertiesdialog.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QApplication>
#include <QStatusBar>
#include <QDockWidget>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDebug>
#include <QFutureWatcher>
#include <cstring>

#include <KActionCollection>
#include <KLocalizedString>
#include <KNotification>
#include <KPageWidget>
#include <KPageWidgetItem>
#include <KStatusNotifierItem>

#include <pi-dlp.h>

KF6MainWindow::KF6MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    // KPageWidget layout
    , m_pageWidget(nullptr)
    , m_logDock(nullptr)
    , m_logWidget(nullptr)
    , m_dashboardWidget(nullptr)
    // Action manager
    , m_actionManager(nullptr)
    // Sync engine and conduits
    , m_syncEngine(nullptr)
    , m_syncPath()
    // Last used connection settings
    , m_lastUsedDevicePath()
    , m_lastUsedBaudRate()
    // Device listening mode (m_devicePollTimer and m_listeningForDevice have in-class initializers)
    , m_listeningDevicePath()
    // Current async operation
    , m_pendingSyncOperationName()
    // Profile
    , m_currentProfile(nullptr)
    // Conflict review
    , m_conflictStore(nullptr)
{
    setObjectName(QStringLiteral("KF6MainWindow"));

    // Setup UI
    setupUI();
    setupActions();

    // Initialize sync engine
    initializeSyncEngine();

    // Initialize conduit manager (discovers and loads conduit plugins)
    initializeConduits();

    // Auto-detection
    m_deviceMonitor = new PalmDeviceMonitor(this);
    m_autoSync = new AutoSyncOrchestrator(this);
    m_autoSync->setDeviceMonitor(m_deviceMonitor);
    m_autoSync->setLogWidget(m_logWidget);

    // Now setup connections after all objects are created
    setupConnections();

    // Auto-sync orchestrator — detection and profile resolution only
    connect(m_autoSync, &AutoSyncOrchestrator::deviceDetected,
            this, &KF6MainWindow::onAutoDeviceDetected);
    connect(m_autoSync, &AutoSyncOrchestrator::profileCreated,
            this, [this](const QString &path, const QString &userName) {
                m_logWidget->logInfo(i18n("Created profile for %1 at %2", userName, path));
            });
    connect(m_autoSync, &AutoSyncOrchestrator::error,
            m_logWidget, &LogWidget::logError);
    connect(m_autoSync, &AutoSyncOrchestrator::statusChanged,
            this, &KF6MainWindow::updateTrayState);

    // In development builds, load the RC file directly from the source tree.
    // In installed builds, setupGUI finds it at the standard KDE location.
#ifdef WILDPALMS_DATA_DIR
    setupGUI(Default, QStringLiteral(WILDPALMS_DATA_DIR "/wildpalmsui.rc"));
#else
    setupGUI(Default, QStringLiteral("wildpalmsui.rc"));
#endif

    // Replace the KDE-branded About dialogs with our own.
    // setupGUI(Default) creates a standard Help menu with "About KDE" and
    // "About Wild Palms" (KAboutApplicationDialog). We use KDE Frameworks as
    // a toolkit, but Wild Palms is not a KDE project.
    if (QAction *aboutApp = actionCollection()->action(QStringLiteral("help_about_app"))) {
        aboutApp->disconnect();
        connect(aboutApp, &QAction::triggered, this, &KF6MainWindow::onAbout);
    }
    if (QAction *aboutKDE = actionCollection()->action(QStringLiteral("help_about_kde"))) {
        aboutKDE->setVisible(false);
    }

    // System tray
    m_trayIcon = new KStatusNotifierItem(this);
    m_trayIcon->setIconByName(QStringLiteral("phone"));
    m_trayIcon->setToolTipTitle(i18n("Wild Palms"));
    m_trayIcon->setToolTipSubTitle(i18n("Listening for Palm devices"));
    m_trayIcon->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_trayIcon->setStatus(KStatusNotifierItem::Active);
    m_trayIcon->setStandardActionsEnabled(true);

    // Load minimize-to-tray preference (default: true = closing minimizes to tray)
    m_minimizeToTray = KF6Settings::instance().minimizeToTray();

    // Status bar
    statusBar()->showMessage(i18n("Ready - No device connected"));

    // Log initial message
    m_logWidget->logInfo(QStringLiteral("Wild Palms %1 initialized").arg(WILDPALMS_VERSION_STRING));

    // Restore window state
    restoreWindowState();

    // Load default profile if set
    QString defaultProfile = KF6Settings::instance().defaultProfilePath();
    if (!defaultProfile.isEmpty() && QDir(defaultProfile).exists()) {
        loadProfile(defaultProfile);
    } else {
        m_logWidget->logInfo(i18n("No default profile set. Use File → New Profile to create one."));
    }

    // Initialize menu state
    updateMenuState(false);
    updateProfileMenuState();

    // Start udev monitoring for Palm devices
    if (!m_deviceMonitor->start()) {
        m_logWidget->logWarning(i18n("Failed to start udev monitor. "
                                      "Use Device → Connect for manual connection."));
    } else {
        m_logWidget->logInfo(i18n("Listening for Palm USB devices..."));
    }
}

KF6MainWindow::~KF6MainWindow()
{
    // Disconnect conduit manager signals. Its destructor emits
    // conduitUnloading() for each plugin, which would try to call our
    // onConduitUnloading() slot. By disconnecting here, the signal fires
    // harmlessly into the void when deleteChildren() eventually destroys
    // the manager (after all base-class destructors have run cleanly).
    if (m_conduitManager) {
        disconnect(m_conduitManager, nullptr, this, nullptr);
    }

    // Stop udev monitor
    if (m_deviceMonitor) {
        m_deviceMonitor->stop();
    }

    // Stop any pending timers first
    if (m_devicePollTimer) {
        m_devicePollTimer->stop();
    }

    // Disconnect device if connected (don't delete - let Qt handle it)
    if (m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        m_palmRuntime->disconnectDevice();
    }

    // Clear sync engine's device link reference
    if (m_syncEngine) {
        m_syncEngine->setDeviceLink(nullptr);
    }

    // Delete non-parented objects only
    // m_syncEngine is a QObject child - Qt will delete it

    // Safety check: verify m_currentProfile is a valid heap pointer
    // before deleting. This guards against memory corruption that could
    // cause the pointer to hold an invalid value (like 'this').
    if (m_currentProfile != nullptr) {
        if (reinterpret_cast<void*>(m_currentProfile) == reinterpret_cast<void*>(this)) {
            qWarning() << "[KF6MainWindow] BUG: m_currentProfile points to 'this' - "
                       << "memory corruption detected, skipping delete";
        } else {
            delete m_currentProfile;
        }
    }
    m_currentProfile = nullptr;
}

void KF6MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_minimizeToTray && m_trayIcon) {
        hide();
        event->ignore();
        return;
    }
    saveWindowState();
    if (m_currentProfile) {
        m_currentProfile->save();
    }
    event->accept();
}

void KF6MainWindow::setupUI()
{
    setWindowTitle(i18n("Wild Palms - Palm Pilot Synchronization"));
    setMinimumSize(1000, 700);

    createCentralLayout();
}

void KF6MainWindow::createCentralLayout()
{
    // Status header strip (120 px, sits between toolbar and conduit pages)
    m_dashboardWidget = new DashboardWidget(this);

    // Conduit page area with icon sidebar
    m_pageWidget = new KPageWidget(this);
    m_pageWidget->setFaceType(KPageWidget::List);
    // Conduit pages are added dynamically by onConduitLoaded()

    // Stack: dashboard header on top, page widget below
    auto *central = new QWidget(this);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_dashboardWidget);
    vbox->addWidget(m_pageWidget, 1);
    setCentralWidget(central);

    // Create log widget inside a QDockWidget at the bottom
    m_logWidget = new LogWidget(this);
    m_logDock = new QDockWidget(i18n("Log"), this);
    m_logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock->setWidget(m_logWidget);
    m_logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
}

void KF6MainWindow::setupActions()
{
    m_actionManager = new ActionManager(this, this);
}

void KF6MainWindow::setupConnections()
{
    // Connect action manager signals
    connect(m_actionManager, &ActionManager::newProfileRequested,
            this, &KF6MainWindow::onNewProfile);
    connect(m_actionManager, &ActionManager::openProfileRequested,
            this, &KF6MainWindow::onOpenProfile);
    connect(m_actionManager, &ActionManager::closeProfileRequested,
            this, &KF6MainWindow::onCloseProfile);
    connect(m_actionManager, &ActionManager::profileSettingsRequested,
            this, &KF6MainWindow::onProfileSettings);
    connect(m_actionManager, &ActionManager::configureMappingsRequested,
            this, &KF6MainWindow::onConfigureMappings);
    connect(m_actionManager, &ActionManager::settingsRequested,
            this, &KF6MainWindow::onSettings);

    connect(m_actionManager, &ActionManager::connectRequested,
            this, &KF6MainWindow::onConnectDevice);
    connect(m_actionManager, &ActionManager::disconnectRequested,
            this, &KF6MainWindow::onDisconnectDevice);
    connect(m_actionManager, &ActionManager::cancelConnectionRequested,
            this, &KF6MainWindow::onCancelConnection);
    connect(m_actionManager, &ActionManager::listDatabasesRequested,
            this, &KF6MainWindow::onListDatabases);
    connect(m_actionManager, &ActionManager::setUserInfoRequested,
            this, &KF6MainWindow::onSetUserInfo);
    connect(m_actionManager, &ActionManager::deviceInfoRequested,
            this, &KF6MainWindow::onDeviceInfo);

    connect(m_actionManager, &ActionManager::hotSyncRequested,
            this, &KF6MainWindow::onHotSync);
    connect(m_actionManager, &ActionManager::fullSyncRequested,
            this, &KF6MainWindow::onFullSync);
    connect(m_actionManager, &ActionManager::copyPalmToPCRequested,
            this, &KF6MainWindow::onCopyPalmToPC);
    connect(m_actionManager, &ActionManager::copyPCToPalmRequested,
            this, &KF6MainWindow::onCopyPCToPalm);
    connect(m_actionManager, &ActionManager::backupRequested,
            this, &KF6MainWindow::onBackup);
    connect(m_actionManager, &ActionManager::restoreRequested,
            this, &KF6MainWindow::onRestore);
    connect(m_actionManager, &ActionManager::changeSyncFolderRequested,
            this, &KF6MainWindow::onChangeSyncFolder);
    connect(m_actionManager, &ActionManager::openSyncFolderRequested,
            this, &KF6MainWindow::onOpenSyncFolder);
    connect(m_actionManager, &ActionManager::installFilesRequested,
            this, &KF6MainWindow::onInstallFiles);

    connect(m_actionManager, &ActionManager::clearLogRequested,
            this, &KF6MainWindow::onClearLog);
    connect(m_actionManager, &ActionManager::showConflictsRequested,
            this, &KF6MainWindow::onShowConflicts);

    // Log dock toggle - sync the action checkmark with dock visibility
    connect(m_actionManager->toggleLogPanelAction(), &QAction::toggled,
            this, &KF6MainWindow::onToggleLogPanel);
    connect(m_logDock, &QDockWidget::visibilityChanged,
            m_actionManager->toggleLogPanelAction(), &QAction::setChecked);

    // Focus log
    connect(m_actionManager, &ActionManager::focusLogRequested,
            this, &KF6MainWindow::onFocusLog);

    // KPageWidget page changes
    connect(m_pageWidget, &KPageWidget::currentPageChanged,
            this, &KF6MainWindow::onPageChanged);
}

void KF6MainWindow::saveWindowState()
{
    KF6Settings &settings = KF6Settings::instance();
    settings.setWindowGeometry(saveGeometry());
    settings.setWindowState(saveState());
    settings.setLogPanelVisible(m_logDock->isVisible());

    // Save current conduit page index
    int pageIndex = 0;
    KPageWidgetItem *current = m_pageWidget->currentPage();
    int idx = 0;
    for (auto it = m_conduitPages.constBegin(); it != m_conduitPages.constEnd(); ++it) {
        if (it.value() == current) {
            pageIndex = idx;
            break;
        }
        ++idx;
    }
    settings.setCurrentTabIndex(pageIndex);

    settings.sync();
}

void KF6MainWindow::restoreWindowState()
{
    KF6Settings &settings = KF6Settings::instance();

    // Restore window geometry
    QByteArray geometry = settings.windowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    QByteArray state = settings.windowState();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    // Restore log dock visibility
    bool logVisible = settings.logPanelVisible();
    m_logDock->setVisible(logVisible);
    m_actionManager->toggleLogPanelAction()->setChecked(logVisible);

    // Restore current conduit page
    int pageIndex = settings.currentTabIndex();
    if (!m_conduitPages.isEmpty()) {
        int idx = 0;
        for (auto it = m_conduitPages.constBegin(); it != m_conduitPages.constEnd(); ++it) {
            if (idx == pageIndex) {
                m_pageWidget->setCurrentPage(it.value());
                break;
            }
            ++idx;
        }
    }
}

void KF6MainWindow::onToggleLogPanel(bool visible)
{
    m_logDock->setVisible(visible);
}

void KF6MainWindow::onFocusLog()
{
    m_logDock->setVisible(true);
    m_logDock->raise();
    m_logWidget->setFocus();
    m_actionManager->toggleLogPanelAction()->setChecked(true);
}

void KF6MainWindow::onPageChanged(KPageWidgetItem *current, KPageWidgetItem *previous)
{
    Q_UNUSED(previous)
    Q_UNUSED(current)
    // Conduit views handle their own data loading via loadFromPath()
    // when a profile is loaded. Lazy per-tab refresh can be added later
    // as an optimization.
}

void KF6MainWindow::updateMenuState(bool connected)
{
    // Also consider connected if the orchestrator has an active session
    if (!connected && m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        connected = true;
    }
    bool hasProfile = m_currentProfile != nullptr;
    m_actionManager->updateConnectionState(connected, hasProfile);
    m_actionManager->updateProfileState(hasProfile);
}

void KF6MainWindow::updateWindowTitle()
{
    QString title = i18n("Wild Palms");
    if (m_currentProfile) {
        title += QStringLiteral(" - ") + m_currentProfile->name();
    }
    setWindowTitle(title);
}

void KF6MainWindow::updateProfileMenuState()
{
    bool hasProfile = m_currentProfile != nullptr;
    m_actionManager->updateProfileState(hasProfile);

    // Update conflicts menu with count
    if (hasProfile) {
        int totalConflicts = 0;
        QString userName = m_currentProfile->deviceFingerprint().userName;
        QString statePath = m_currentProfile->stateDirectoryPath();
        QStringList conduits = {QStringLiteral("memos"), QStringLiteral("contacts"),
                                QStringLiteral("calendar"), QStringLiteral("todos")};

        for (const QString &conduitId : conduits) {
            Sync::SyncState state(userName, conduitId);
            state.setStateDirectory(statePath);
            state.load();
            totalConflicts += state.pendingConflictCount();
        }

        m_actionManager->updateConflictCount(totalConflicts);
    }
}

// ========== Sync Engine ==========

void KF6MainWindow::initializeSyncEngine()
{
    m_syncEngine = new Sync::SyncEngine(this);

    // Conduits are no longer hard-coded here. They are loaded
    // dynamically by ConduitManager via initializeConduits() and
    // registered with SyncEngine in onConduitLoaded().

    // Connect sync engine signals
    connect(m_syncEngine, &Sync::SyncEngine::logMessage,
            m_logWidget, &LogWidget::logInfo);
    connect(m_syncEngine, &Sync::SyncEngine::errorOccurred,
            m_logWidget, &LogWidget::logError);
    connect(m_syncEngine, &Sync::SyncEngine::syncStarted,
            this, &KF6MainWindow::onSyncStarted);
    connect(m_syncEngine, &Sync::SyncEngine::syncFinished,
            this, &KF6MainWindow::onSyncFinished);
    connect(m_syncEngine, &Sync::SyncEngine::progressUpdated,
            this, &KF6MainWindow::onSyncProgress);
}

void KF6MainWindow::initializeConduits()
{
    m_conduitManager = new ConduitManager(this);

    // Discover available conduit plugins from the plugin directory.
    // Until conduits are migrated to .so plugins (Phase 3), this will
    // find nothing -- the app starts with just the Dashboard page.
    m_conduitManager->discoverConduits();

    connect(m_conduitManager, &ConduitManager::conduitLoaded,
            this, &KF6MainWindow::onConduitLoaded);
    connect(m_conduitManager, &ConduitManager::conduitUnloading,
            this, &KF6MainWindow::onConduitUnloading);

    // Load ALL discovered conduits (profile controls which ones participate in sync)
    for (const auto &info : m_conduitManager->conduitList()) {
        QString conduitId = info.metaData.value(QStringLiteral("X-WildPalms-ConduitId"));
        if (conduitId.isEmpty()) {
            conduitId = info.metaData.pluginId();
        }
        m_conduitManager->loadConduit(conduitId);
    }

    // Phase E.9 — new-ABI plugins discovered from wildpalms/plugins/.
    // Runs alongside ConduitManager until E.16. Host/device/coordinator
    // are nullptr here; runtime wiring (which owns the real
    // PalmDeviceConnection) lands in E.15/E.17.
    m_backendPluginManager = new WildPalms::BackendPluginManager(
        /*host=*/nullptr, /*device=*/nullptr, /*coordinator=*/nullptr, this);
    m_backendPluginManager->discoverPlugins();

    connect(m_backendPluginManager, &WildPalms::BackendPluginManager::pluginLoaded,
            this, &KF6MainWindow::onBackendPluginLoaded);
    connect(m_backendPluginManager, &WildPalms::BackendPluginManager::pluginUnloading,
            this, &KF6MainWindow::onBackendPluginUnloading);

    for (const auto &info : m_backendPluginManager->catalogue()) {
        if (info.defaultEnabled) {
            m_backendPluginManager->loadPlugin(info.metaData.pluginId());
        }
    }

}

void KF6MainWindow::onConduitLoaded(IConduit *conduit)
{
    // Create a view page if the conduit provides one
    if (conduit->hasView()) {
        QWidget *view = conduit->createView(this);
        auto *page = new KPageWidgetItem(view, conduit->viewName());
        page->setIcon(conduit->viewIcon());
        page->setHeaderVisible(false);
        m_pageWidget->addPage(page);
        m_conduitPages[conduit->conduitId()] = page;
    }

    // Merge XMLGUI contributions if the conduit provides them
    KXMLGUIClient *guiClient = conduit->createGUIClient();
    if (guiClient) {
        insertChildClient(guiClient);
        m_conduitGUIClients[conduit->conduitId()] = guiClient;
    }

    // Register with the sync engine so it participates in sync operations
    m_syncEngine->registerConduit(conduit);

    // Pass ordering hints from plugin metadata to the sync engine
    // (SyncConduitBase reads these itself, but other conduit types need explicit hints)
    KPluginMetaData md = m_conduitManager->conduitMetaData(conduit->conduitId());
    if (md.isValid()) {
        QStringList runBefore, runAfter;
        const QJsonObject raw = md.rawData();
        const QJsonValue beforeVal = raw.value(QStringLiteral("X-WildPalms-RunBefore"));
        if (beforeVal.isArray()) {
            for (const QJsonValue &v : beforeVal.toArray())
                if (!v.toString().isEmpty()) runBefore << v.toString();
        }
        const QJsonValue afterVal = raw.value(QStringLiteral("X-WildPalms-RunAfter"));
        if (afterVal.isArray()) {
            for (const QJsonValue &v : afterVal.toArray())
                if (!v.toString().isEmpty()) runAfter << v.toString();
        }
        if (!runBefore.isEmpty() || !runAfter.isEmpty()) {
            m_syncEngine->setConduitOrdering(conduit->conduitId(), runBefore, runAfter);
        }
    }

    qDebug() << "[KF6MainWindow] Conduit loaded:" << conduit->conduitId()
             << "(" << conduit->displayName() << ")";
}

void KF6MainWindow::onConduitUnloading(IConduit *conduit)
{
    const QString id = conduit->conduitId();

    // Remove the view page
    if (m_conduitPages.contains(id)) {
        m_pageWidget->removePage(m_conduitPages.take(id));
    }

    // Remove XMLGUI client
    if (m_conduitGUIClients.contains(id)) {
        removeChildClient(m_conduitGUIClients.take(id));
    }

    // Unregister from sync engine
    m_syncEngine->unregisterConduit(id);

    qDebug() << "[KF6MainWindow] Conduit unloading:" << id;
}

// Phase E.9 — new-ABI plugin view loop. Mirrors onConduitLoaded for
// plugins that opt in to `hasMainView()`. Sync-engine registration is
// deliberately absent here; the new-ABI sync path comes online in
// E.15/E.17 once the runtime owns the PalmDeviceConnection.
void KF6MainWindow::onBackendPluginLoaded(WildPalms::IBackendPluginV2 *plugin)
{
    if (!plugin || !plugin->hasMainView()) return;

    QWidget *view = plugin->createMainView(this);
    if (!view) return;
    auto *page = new KPageWidgetItem(view, plugin->mainViewName());
    page->setIcon(plugin->mainViewIcon());
    page->setHeaderVisible(false);
    m_pageWidget->addPage(page);
    m_backendPluginPages[plugin->pluginId()] = page;

    qDebug() << "[KF6MainWindow] Backend plugin loaded:" << plugin->pluginId()
             << "(" << plugin->mainViewName() << ")";
}

void KF6MainWindow::onBackendPluginUnloading(WildPalms::IBackendPluginV2 *plugin)
{
    if (!plugin) return;
    if (auto *page = m_backendPluginPages.take(plugin->pluginId())) {
        m_pageWidget->removePage(page);
        page->deleteLater();
    }
    qDebug() << "[KF6MainWindow] Backend plugin unloading:" << plugin->pluginId();
}

void KF6MainWindow::showSyncResult(const Sync::SyncResult &result, const QString &operationName)
{
    int errorCount = result.palmStats.errors + result.pcStats.errors;

    QString summary = i18n("%1 Complete!\n\n"
                           "Palm: %2\n"
                           "PC: %3\n"
                           "Errors: %4",
                           operationName,
                           result.palmStats.summary(),
                           result.pcStats.summary(),
                           errorCount);

    // Send KNotification
    KNotification *notification = new KNotification(
        result.success ? QStringLiteral("syncComplete") : QStringLiteral("syncError"),
        KNotification::CloseOnTimeout, this);
    notification->setTitle(operationName);
    notification->setText(result.success ? i18n("Sync completed successfully") :
                                           i18n("Sync completed with errors"));
    notification->setIconName(result.success ? QStringLiteral("dialog-ok") :
                                               QStringLiteral("dialog-error"));
    notification->sendEvent();

    if (result.success && errorCount == 0) {
        QMessageBox::information(this, operationName + i18n(" Complete"), summary);
    } else {
        if (!result.errorMessage.isEmpty()) {
            summary += QStringLiteral("\n\nError: ") + result.errorMessage;
        }
        for (const auto &warning : result.warnings) {
            if (warning.severity == Sync::WarningSeverity::Error) {
                summary += QStringLiteral("\n - ") + warning.message;
            }
        }
        QMessageBox::warning(this, operationName + i18n(" Complete"), summary);
    }

    m_logWidget->logInfo(i18n("=== %1 Complete: %2 records, %3 errors ===",
                              operationName,
                              result.palmStats.total() + result.pcStats.total(),
                              errorCount));

    // Record last sync time on successful sync
    if (result.success && m_currentProfile) {
        m_currentProfile->setLastSyncTime(result.endTime);
        m_currentProfile->save();
    }

    // Update dashboard after sync
    m_dashboardWidget->updateStatus(m_currentProfile,
                                    m_palmRuntime && m_palmRuntime->isDeviceConnected());
}

// ========== Profile Management ==========

void KF6MainWindow::loadProfile(const QString &path)
{
    // Phase Ic: AccountController borrows the old profile + runtime; it
    // must be reset BEFORE the old Profile is deleted and the old
    // PalmRuntime is replaced.
    if (m_accountController) {
        m_accountController.reset();
    }

    if (m_currentProfile) {
        m_currentProfile->save();
        delete m_currentProfile;
        m_currentProfile = nullptr;
    }

    m_currentProfile = new Profile(path);

    if (!m_currentProfile->isValid()) {
        m_logWidget->logError(i18n("Invalid profile path: %1", path));
        delete m_currentProfile;
        m_currentProfile = nullptr;
        updateWindowTitle();
        updateProfileMenuState();
        return;
    }

    bool isNewProfile = !m_currentProfile->exists();

    if (isNewProfile) {
        m_currentProfile->initialize();
        m_logWidget->logInfo(i18n("Created new profile at: %1", path));

        if (!m_lastUsedDevicePath.isEmpty()) {
            m_currentProfile->setDevicePath(m_lastUsedDevicePath);
            m_currentProfile->setBaudRate(m_lastUsedBaudRate);
            m_currentProfile->save();
            m_logWidget->logInfo(i18n("Using device settings: %1 at %2 bps",
                                      m_lastUsedDevicePath, m_lastUsedBaudRate));
        }
    }

    m_syncPath = path;

    // Configure sync engine
    m_syncEngine->setStateDirectory(m_currentProfile->stateDirectoryPath());


    // M2 — (re)create PalmRuntime for the new profile path.
    m_palmRuntime = std::make_unique<WildPalms::Runtime::PalmRuntime>(
        m_currentProfile->stateDirectoryPath(), this);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runStarted,
            this, &KF6MainWindow::onPalmRunStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runFinished,
            this, &KF6MainWindow::onPalmRunFinished);

    // Phase Ic: AccountController borrows registry + profile + runtime.
    m_accountController = std::make_unique<WildPalms::Runtime::AccountController>(
        m_currentProfile->syncFolderPath(),
        &m_palmRuntime->backendRegistry(),
        m_currentProfile,
        m_palmRuntime.get(),
        this);

    // M5a — construct and install the libkalburator-side conflict handler.
    // Recreate per profile so it points at the new engine.
    // Direct construction is isolated in palmruntimebridgeinstall.cpp to avoid
    // the include-guard collision between WP-local and libkalburator QSyncCore
    // headers (both share the same guard names QSYNCCORE_CONFLICTPOLICY_H etc).
    ConflictDialogBridge::destroyHandler(m_palmConflictHandler);
    m_palmConflictHandler = nullptr;  // clear dangling pointer before recreating
    m_palmConflictHandler = ConflictDialogBridge::createAndInstall(
        m_palmRuntime.get(),
        this,     // parentWidget for the dialog
        this);    // QObject parent for lifetime management

    // Tickle the device link while the dialog is open (matches legacy handler).
    ConflictDialogBridge::connectKeepAlive(
        m_palmConflictHandler,
        [this]() { onPalmConflictHandlerKeepAlive(); });

    m_syncEngine->setConflictAutoResolve(m_currentProfile->conflictAutoResolve());
    m_syncEngine->setConflictFallback(m_currentProfile->conflictFallback());
    m_syncEngine->setConflictPromptStrategy(m_currentProfile->conflictPromptStrategy());
    m_syncEngine->setConflictConnectionBehavior(m_currentProfile->conflictConnectionBehavior());
    m_syncEngine->setConflictTimeoutSeconds(m_currentProfile->conflictTimeoutSeconds());

    // Set up interactive conflict handler
    delete m_interactiveConflictHandler;
    if (!m_conflictStore) {
        m_conflictStore = new QSyncCore::ConflictStore(this);
    }
    m_interactiveConflictHandler = new InteractiveConflictHandler(
        m_conflictStore, this, this);
    m_syncEngine->setConflictHandler(m_interactiveConflictHandler);

    // Set up conduit lookup for rich conflict display
    m_interactiveConflictHandler->setConduitLookup(
        [this](const QString &conduitId) -> const ISyncConduit* {
            if (!m_conduitManager) return nullptr;
            IConduit *c = m_conduitManager->conduit(conduitId);
            return dynamic_cast<const ISyncConduit*>(c);
        });

    // Connect keepAlive signal — tickle pause/resume is now driven by
    // PalmTickle inside PalmDeviceAccess; refresh it when the user is
    // reading a conflict dialog for a long time.
    connect(m_interactiveConflictHandler, &InteractiveConflictHandler::keepAliveRequested,
            this, [this]() {
                if (m_palmRuntime && m_palmRuntime->deviceLink()) {
                    m_palmRuntime->deviceLink()->resumeTickle();
                }
            });

    Sync::LocalFileBackend *backend = new Sync::LocalFileBackend(m_syncPath);
    m_syncEngine->setBackend(backend);

    // Apply profile's conduit enabled settings
    for (const QString &conduitId : m_syncEngine->registeredConduits()) {
        if (m_conduitManager && m_conduitManager->hasDatabaseClaims(conduitId)) {
            // Sync conduit: enabled if it's the active handler for any of its claimed databases
            QStringList activeDBs = m_conduitManager->activeDatabasesForConduit(conduitId, m_currentProfile);
            m_syncEngine->setConduitEnabled(conduitId, !activeDBs.isEmpty());
        } else {
            // Standalone conduit: use simple enable/disable toggle
            m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));
        }

        QJsonObject conduitSettings = m_currentProfile->conduitSettings(conduitId);
        if (!conduitSettings.isEmpty()) {
            auto *conduit = dynamic_cast<Sync::SyncConduitBase*>(m_syncEngine->conduit(conduitId));
            if (conduit) {
                conduit->loadSettings(conduitSettings);
            }
        }
    }

    // Set up database reference resolver for @ sigil in dependency ordering
    if (m_conduitManager && m_currentProfile) {
        m_syncEngine->setDatabaseResolver([this](const QString &dbName) -> QString {
            return m_conduitManager->activeConduitForDatabase(dbName, m_currentProfile);
        });
    }

    // Connection mode (USB serial vs network) is now encoded in the
    // devicePaths passed to PalmRuntime::connectDevice().

    // Add to recent profiles
    KF6Settings::instance().addRecentProfile(path);

    // Auto-set as default if none exists
    if (KF6Settings::instance().defaultProfilePath().isEmpty()) {
        KF6Settings::instance().setDefaultProfilePath(path);
        m_logWidget->logInfo(i18n("Set as default profile"));
    }

    KF6Settings::instance().sync();

    // Tell conduit views to load data from the profile's sync folder
    for (auto it = m_conduitPages.constBegin(); it != m_conduitPages.constEnd(); ++it) {
        QWidget *view = it.value()->widget();
        QMetaObject::invokeMethod(view, "loadFromPath", Q_ARG(QString, m_syncPath));
    }

    // Update UI — check for active connection (manual or auto-sync)
    bool connected = m_palmRuntime && m_palmRuntime->isDeviceConnected();
    updateWindowTitle();
    updateMenuState(connected);
    m_dashboardWidget->updateStatus(m_currentProfile, connected);

    m_logWidget->logInfo(i18n("Loaded profile: %1", m_currentProfile->name()));
    m_logWidget->logInfo(i18n("Sync folder: %1", m_syncPath));
}

void KF6MainWindow::closeProfile()
{
    // Phase Ic: AccountController teardown precedes Profile teardown.
    if (m_accountController) {
        m_accountController.reset();
    }

    if (m_currentProfile) {
        m_currentProfile->save();
        delete m_currentProfile;
        m_currentProfile = nullptr;
    }

    m_syncPath.clear();

    updateWindowTitle();
    updateProfileMenuState();

    m_logWidget->logInfo(i18n("Profile closed"));
}

// ========== Device Connection ==========

void KF6MainWindow::onConnectDevice()
{
    if (m_listeningForDevice) {
        stopListening();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Connect to Palm Device"));
    dialog.setMinimumWidth(400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *infoLabel = new QLabel(
        i18n("Enter the device path and press Connect.\n"
             "Press the HotSync button on your Palm - Wild Palms will wait for it."));
    layout->addWidget(infoLabel);

    QFormLayout *formLayout = new QFormLayout();

    QComboBox *deviceCombo = new QComboBox();
    deviceCombo->setEditable(true);
    deviceCombo->addItem(QStringLiteral("/dev/ttyUSB0"));
    deviceCombo->addItem(QStringLiteral("/dev/ttyUSB1"));
    deviceCombo->addItem(QStringLiteral("/dev/pilot"));
    deviceCombo->addItem(QStringLiteral("usb:"));

    if (m_currentProfile && !m_currentProfile->devicePath().isEmpty()) {
        deviceCombo->setCurrentText(m_currentProfile->devicePath());
    } else if (!m_lastUsedDevicePath.isEmpty()) {
        deviceCombo->setCurrentText(m_lastUsedDevicePath);
    }

    formLayout->addRow(i18n("Device:"), deviceCombo);

    QComboBox *baudCombo = new QComboBox();
    baudCombo->addItem(QStringLiteral("115200"));
    baudCombo->addItem(QStringLiteral("57600"));
    baudCombo->addItem(QStringLiteral("38400"));
    baudCombo->addItem(QStringLiteral("19200"));
    baudCombo->addItem(QStringLiteral("9600"));

    if (m_currentProfile && !m_currentProfile->baudRate().isEmpty()) {
        baudCombo->setCurrentText(m_currentProfile->baudRate());
    } else if (!m_lastUsedBaudRate.isEmpty()) {
        baudCombo->setCurrentText(m_lastUsedBaudRate);
    }

    formLayout->addRow(i18n("Baud Rate:"), baudCombo);
    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(i18n("Connect"));
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString devicePath = deviceCombo->currentText();
    QString baudRate = baudCombo->currentText();

    m_lastUsedDevicePath = devicePath;
    m_lastUsedBaudRate = baudRate;

    if (m_currentProfile) {
        m_currentProfile->setDevicePath(devicePath);
        m_currentProfile->setBaudRate(baudRate);
        m_currentProfile->save();
    }

    bool deviceExists = devicePath.startsWith(QStringLiteral("usb:")) || QFile::exists(devicePath);

    if (deviceExists) {
        startConnection(devicePath);
    } else {
        startListening(devicePath);
    }
}

void KF6MainWindow::startListening(const QString &devicePath)
{
    m_listeningForDevice = true;
    m_listeningDevicePath = devicePath;

    m_logWidget->logInfo(i18n("Waiting for HotSync on %1...", devicePath));
    m_logWidget->logInfo(i18n("Press the HotSync button on your Palm device."));
    statusBar()->showMessage(i18n("Waiting for HotSync on %1...", devicePath));

    if (!m_devicePollTimer) {
        m_devicePollTimer = new QTimer(this);
        connect(m_devicePollTimer, &QTimer::timeout, this, &KF6MainWindow::onDevicePoll);
    }

    m_devicePollTimer->start(500);

    // Note: cancelConnectionAction triggers PalmRuntime::cancelConnect
    // which only cancels the open handshake. Mid-sync cancel is currently
    // unsupported (would require PalmRuntime::cancelSync via QFutureWatcher
    // cancellation propagation — TODO for follow-up).
    m_actionManager->cancelConnectionAction()->setEnabled(true);
    updateMenuState(false);
}

void KF6MainWindow::stopListening()
{
    if (!m_listeningForDevice) {
        return;
    }

    m_listeningForDevice = false;
    m_listeningDevicePath.clear();

    if (m_devicePollTimer) {
        m_devicePollTimer->stop();
    }

    m_logWidget->logInfo(i18n("Stopped listening for HotSync"));
    statusBar()->showMessage(i18n("Ready"));

    m_actionManager->cancelConnectionAction()->setEnabled(false);
    updateMenuState(false);
}

void KF6MainWindow::onDevicePoll()
{
    if (!m_listeningForDevice || m_listeningDevicePath.isEmpty()) {
        return;
    }

    if (QFile::exists(m_listeningDevicePath)) {
        m_logWidget->logInfo(i18n("Device %1 detected!", m_listeningDevicePath));

        m_devicePollTimer->stop();
        m_listeningForDevice = false;

        QString devicePath = m_listeningDevicePath;
        m_listeningDevicePath.clear();

        startConnection(devicePath);
    }
}

void KF6MainWindow::startConnectionMultiPort(const QStringList &devicePaths)
{
    if (devicePaths.isEmpty()) {
        m_logWidget->logError(i18n("No device paths provided"));
        return;
    }

    if (!m_palmRuntime) {
        m_logWidget->logError(i18n("Cannot connect: PalmRuntime not initialized"));
        return;
    }

    // Wire signals (Qt::UniqueConnection prevents duplicate wiring on
    // repeated connect attempts to the same PalmRuntime instance).
    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::connectionStarted,
            this, [this]() {
                statusBar()->showMessage(i18n("Connecting…"));
            }, Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::connectionComplete,
            this, &KF6MainWindow::onConnectionComplete,
            Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::readyForSync,
            this, &KF6MainWindow::onReadyForSync, Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::deviceDisconnected,
            this, [this]() {
                updateMenuState(false);
                statusBar()->showMessage(i18n("Disconnected"));

                KNotification *notification = new KNotification(
                    QStringLiteral("deviceDisconnected"), KNotification::CloseOnTimeout, this);
                notification->setTitle(i18n("Device Disconnected"));
                notification->setText(i18n("Palm device has been disconnected"));
                notification->setIconName(QStringLiteral("network-disconnect"));
                notification->sendEvent();
            }, Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::logMessage,
            m_logWidget, &LogWidget::logInfo, Qt::UniqueConnection);

    m_logWidget->logInfo(i18n("Connecting to %1...", devicePaths.join(QStringLiteral(", "))));
    m_palmRuntime->connectDevice(devicePaths);

    updateMenuState(false);
    if (m_actionManager) {
        m_actionManager->cancelConnectionAction()->setEnabled(true);
    }
}

void KF6MainWindow::startConnection(const QString &devicePath)
{
    startConnectionMultiPort(QStringList{devicePath});
}

void KF6MainWindow::onConnectionComplete(bool success, const QString &error)
{
    m_actionManager->cancelConnectionAction()->setEnabled(false);

    if (!success) {
        m_logWidget->logError(i18n("Connection failed: %1", error));
        statusBar()->showMessage(i18n("Connection failed"));
        updateMenuState(false);
        return;
    }

    m_logWidget->logInfo(i18n("Connection established!"));
    statusBar()->showMessage(i18n("Connected"));

    // Send notification
    KNotification *notification = new KNotification(QStringLiteral("deviceConnected"), KNotification::CloseOnTimeout, this);
    notification->setTitle(i18n("Device Connected"));
    notification->setText(i18n("Palm device connected successfully"));
    notification->setIconName(QStringLiteral("network-connect"));
    notification->sendEvent();

    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("Connected but no device link available"));
        updateMenuState(false);
        return;
    }

    // Use handshake data (captured during connection on the worker thread —
    // no DLP calls on the main thread while tickle may be running)
    if (deviceLink->handshakeUserInfoValid()) {
        QString userName = deviceLink->handshakeUserName();
        quint32 userId = deviceLink->handshakeUserId();

        m_logWidget->logInfo(i18n("User: %1 (ID: %2)", userName, userId));

        DeviceFingerprint connectedDevice;
        connectedDevice.userId = userId;
        connectedDevice.userName = userName;
        if (deviceLink->handshakeSysInfoValid()) {
            connectedDevice.romVersion = deviceLink->handshakeRomVersion();
            connectedDevice.productId = deviceLink->handshakeProductId();
        }
        if (deviceLink->handshakeStorageInfoValid()) {
            connectedDevice.modelName = deviceLink->handshakeCardName();
            connectedDevice.manufacturer = deviceLink->handshakeManufacturer();
            connectedDevice.romSize = deviceLink->handshakeRomSize();
            connectedDevice.ramSize = deviceLink->handshakeRamSize();
            connectedDevice.ramFree = deviceLink->handshakeRamFree();
        }

        if (m_currentProfile) {
            if (!handleDeviceFingerprint(connectedDevice)) {
                deviceLink->closeConnection();
                updateMenuState(false);
                return;
            }
        } else {
            QString knownProfile = KF6Settings::instance().findProfileForDevice(connectedDevice);
            if (!knownProfile.isEmpty()) {
                int ret = QMessageBox::question(this, i18n("Known Device"),
                    i18n("This device (%1) is registered with profile:\n%2\n\nLoad that profile?",
                         connectedDevice.displayString(),
                         QFileInfo(knownProfile).fileName()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

                if (ret == QMessageBox::Yes) {
                    loadProfile(knownProfile);
                }
            } else {
                m_logWidget->logWarning(i18n("No profile loaded. Device data won't be saved. Use File → New Profile."));
            }
        }

        if (userName.isEmpty() && userId == 0) {
            m_logWidget->logWarning(i18n("This appears to be an uninitialized device (no user info)"));

            QString newUserName = QInputDialog::getText(this, i18n("Initialize Device"),
                i18n("This Palm has no user info. Enter a username:"),
                QLineEdit::Normal, QStringLiteral("PalmUser"));

            if (!newUserName.isEmpty()) {
                struct PilotUser user;
                memset(&user, 0, sizeof(user));
                strncpy(user.username, newUserName.toLatin1().constData(), sizeof(user.username) - 1);
                user.userID = static_cast<unsigned long>(QDateTime::currentSecsSinceEpoch());

                if (deviceLink->writeUserInfo(user)) {
                    m_logWidget->logInfo(i18n("Device initialized: %1 (ID: %2)",
                                              newUserName, user.userID));

                    if (m_currentProfile) {
                        DeviceFingerprint newFp;
                        newFp.userId = user.userID;
                        newFp.userName = newUserName;
                        if (deviceLink->handshakeSysInfoValid()) {
                            newFp.romVersion = deviceLink->handshakeRomVersion();
                            newFp.productId = deviceLink->handshakeProductId();
                        }
                        if (deviceLink->handshakeStorageInfoValid()) {
                            newFp.modelName = deviceLink->handshakeCardName();
                            newFp.manufacturer = deviceLink->handshakeManufacturer();
                            newFp.romSize = deviceLink->handshakeRomSize();
                            newFp.ramSize = deviceLink->handshakeRamSize();
                            newFp.ramFree = deviceLink->handshakeRamFree();
                        }
                        registerDeviceWithCurrentProfile(newFp);
                    }
                } else {
                    m_logWidget->logError(i18n("Failed to write user info to device"));
                }
            }
        }
    } else {
        m_logWidget->logWarning(i18n("Could not read user info from device"));
    }

    // System info display (from handshake)
    if (deviceLink->handshakeSysInfoValid()) {
        m_logWidget->logInfo(i18n("Palm OS: %1.%2, Product ID: %3",
                                  deviceLink->handshakeRomVersion() >> 16,
                                  (deviceLink->handshakeRomVersion() >> 8) & 0xFF,
                                  deviceLink->handshakeProductId()));
    }

    // Storage info display (from handshake)
    if (deviceLink->handshakeStorageInfoValid()) {
        QString ramTotal = DeviceFingerprint::formatMemorySize(deviceLink->handshakeRamSize());
        QString ramFree = DeviceFingerprint::formatMemorySize(deviceLink->handshakeRamFree());
        m_logWidget->logInfo(i18n("Device: %1 by %2, RAM: %3/%4",
                                  deviceLink->handshakeCardName(),
                                  deviceLink->handshakeManufacturer(),
                                  ramFree, ramTotal));
    }

    m_syncEngine->setDeviceLink(deviceLink);
    if (deviceLink->handshakeUserInfoValid()) {
        m_syncEngine->setPalmUserName(deviceLink->handshakeUserName());
    }

    updateMenuState(true);
    m_dashboardWidget->updateStatus(m_currentProfile, true);
}

bool KF6MainWindow::handleDeviceFingerprint(const DeviceFingerprint &connectedDevice)
{
    if (!m_currentProfile) return true;

    DeviceFingerprint expectedDevice = m_currentProfile->deviceFingerprint();

    if (!expectedDevice.isValid()) {
        registerDeviceWithCurrentProfile(connectedDevice);
        return true;
    }

    if (expectedDevice.matches(connectedDevice)) {
        m_logWidget->logInfo(i18n("Device fingerprint verified"));

        // Merge extended info from connected device into stored fingerprint
        // so ramFree updates and initially-missing fields get filled
        bool changed = false;
        if (connectedDevice.hasExtendedInfo()) {
            if (expectedDevice.modelName.isEmpty() && !connectedDevice.modelName.isEmpty()) {
                expectedDevice.modelName = connectedDevice.modelName;
                changed = true;
            }
            if (expectedDevice.manufacturer.isEmpty() && !connectedDevice.manufacturer.isEmpty()) {
                expectedDevice.manufacturer = connectedDevice.manufacturer;
                changed = true;
            }
            if (expectedDevice.romVersion == 0 && connectedDevice.romVersion != 0) {
                expectedDevice.romVersion = connectedDevice.romVersion;
                changed = true;
            }
            if (expectedDevice.productId.isEmpty() && !connectedDevice.productId.isEmpty()) {
                expectedDevice.productId = connectedDevice.productId;
                changed = true;
            }
            if (expectedDevice.romSize == 0 && connectedDevice.romSize != 0) {
                expectedDevice.romSize = connectedDevice.romSize;
                changed = true;
            }
            if (expectedDevice.ramSize == 0 && connectedDevice.ramSize != 0) {
                expectedDevice.ramSize = connectedDevice.ramSize;
                changed = true;
            }
            // Always update ramFree (it's a snapshot)
            if (connectedDevice.ramFree != 0 && connectedDevice.ramFree != expectedDevice.ramFree) {
                expectedDevice.ramFree = connectedDevice.ramFree;
                changed = true;
            }
        }
        if (changed) {
            m_currentProfile->setDeviceFingerprint(expectedDevice);
            m_currentProfile->save();
        }

        return true;
    }

    QString message = i18n(
        "Device Mismatch!\n\n"
        "Expected: %1\n"
        "Connected: %2\n\n"
        "This profile is configured for a different Palm device.\n"
        "What would you like to do?",
        expectedDevice.displayString(),
        connectedDevice.displayString());

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(i18n("Wrong Device"));
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *continueBtn = msgBox.addButton(i18n("Continue Anyway"), QMessageBox::AcceptRole);
    QPushButton *switchBtn = msgBox.addButton(i18n("Switch Profile"), QMessageBox::ActionRole);
    QPushButton *abortBtn = msgBox.addButton(i18n("Disconnect"), QMessageBox::RejectRole);
    Q_UNUSED(abortBtn)

    msgBox.exec();

    if (msgBox.clickedButton() == continueBtn) {
        m_logWidget->logWarning(i18n("User chose to continue with mismatched device"));
        return true;
    } else if (msgBox.clickedButton() == switchBtn) {
        QString profilePath = KF6Settings::instance().findProfileForDevice(connectedDevice);
        if (!profilePath.isEmpty()) {
            m_logWidget->logInfo(i18n("Switching to profile: %1", profilePath));
            loadProfile(profilePath);
            return true;
        } else {
            QMessageBox::information(this, i18n("No Profile Found"),
                i18n("No profile is registered for this device.\n"
                     "You can create a new profile using File → New Profile."));
            return false;
        }
    } else {
        m_logWidget->logInfo(i18n("User chose to disconnect"));
        return false;
    }
}

void KF6MainWindow::registerDeviceWithCurrentProfile(const DeviceFingerprint &fingerprint)
{
    if (!m_currentProfile) return;

    m_currentProfile->setDeviceFingerprint(fingerprint);
    m_currentProfile->save();

    KF6Settings::instance().registerDevice(fingerprint, m_currentProfile->syncFolderPath());
    KF6Settings::instance().sync();

    m_logWidget->logInfo(i18n("Device registered: %1", fingerprint.displayString()));
}

void KF6MainWindow::onDisconnectDevice()
{
    if (m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logInfo(i18n("Disconnecting..."));

        KPilotDeviceLink *deviceLink = m_palmRuntime->deviceLink();
        if (deviceLink && deviceLink->isConnected()) {
            m_logWidget->logInfo(i18n("Ending sync session..."));
            deviceLink->endSync();
        }

        m_syncEngine->setDeviceLink(nullptr);

        // Tear down PalmRuntime's device.
        m_palmRuntime->disconnectDevice();

        statusBar()->showMessage(i18n("Disconnected"));
        m_logWidget->logInfo(i18n("Disconnected from device"));
    }
    updateMenuState(false);
}

void KF6MainWindow::onCancelConnection()
{
    if (m_listeningForDevice) {
        stopListening();
        return;
    }

    if (m_palmRuntime) {
        m_palmRuntime->cancelConnect();
        m_logWidget->logInfo(i18n("Connection cancelled by user"));
    }
    m_actionManager->cancelConnectionAction()->setEnabled(false);
}

void KF6MainWindow::onDeviceStatusChanged(int status)
{
    KPilotLink::LinkStatus linkStatus = static_cast<KPilotLink::LinkStatus>(status);
    switch (linkStatus) {
        case KPilotLink::LinkStatus::Init:
            statusBar()->showMessage(i18n("Initializing..."));
            break;
        case KPilotLink::LinkStatus::WaitingForDevice:
            statusBar()->showMessage(i18n("Waiting for device..."));
            break;
        case KPilotLink::LinkStatus::FoundDevice:
            statusBar()->showMessage(i18n("Device found"));
            break;
        case KPilotLink::LinkStatus::CreatedSocket:
            statusBar()->showMessage(i18n("Creating connection..."));
            break;
        case KPilotLink::LinkStatus::DeviceOpen:
            statusBar()->showMessage(i18n("Device open"));
            break;
        case KPilotLink::LinkStatus::AcceptedDevice:
            statusBar()->showMessage(i18n("Connected"));
            break;
        case KPilotLink::LinkStatus::SyncDone:
            statusBar()->showMessage(i18n("Sync complete"));
            break;
        case KPilotLink::LinkStatus::PilotLinkError:
            statusBar()->showMessage(i18n("Error"));
            break;
    }
}

void KF6MainWindow::onAutoDeviceDetected(Profile *profile, const QStringList &ports)
{
    // Guard: if we're already busy with a sync, ignore
    if (m_palmRuntime && m_palmRuntime->isRunning()) {
        m_logWidget->logWarning(i18n("Device detected but sync already in progress — ignoring"));
        delete profile;
        return;
    }

    // If an idle connection exists, disconnect it first
    if (m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logInfo(i18n("Disconnecting previous session for new device"));
        m_palmRuntime->disconnectDevice();
    }

    if (profile) {
        // Known device — load profile and connect
        QString profilePath = profile->syncFolderPath();
        delete profile;  // loadProfile creates its own copy
        loadProfile(profilePath);
        startConnectionMultiPort(ports);
    } else {
        // Unknown device — connect first, then we'll read identity and create profile
        m_logWidget->logInfo(i18n("New Palm device detected — connecting to identify..."));
        startConnectionMultiPort(ports);
    }
}

void KF6MainWindow::onDeviceReady(const QString &userName, const QString &deviceName)
{
    m_logWidget->logInfo(i18n("Device ready: %1 (%2)", userName, deviceName));

    // If no profile is loaded and auto-sync is available, try to create one
    if (!m_currentProfile && m_autoSync) {
        Profile *newProfile = m_autoSync->findOrCreateProfile(
            QString(), userName, 0);
        if (newProfile) {
            QString profilePath = newProfile->syncFolderPath();
            delete newProfile;
            loadProfile(profilePath);
        }
    }
}

void KF6MainWindow::onReadyForSync()
{
    // Auto-sync if profile is configured for it
    if (m_currentProfile && m_currentProfile->autoSyncOnConnect()) {
        m_logWidget->logInfo(i18n("Auto-sync enabled — starting HotSync"));
        QTimer::singleShot(0, this, &KF6MainWindow::onHotSync);
    }
}

void KF6MainWindow::onListDatabases()
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    m_logWidget->logInfo(i18n("=== Database List ==="));

    QStringList databases = deviceLink->listDatabases();

    if (databases.isEmpty()) {
        m_logWidget->logWarning(i18n("No databases found (or device disconnected)"));
        return;
    }

    QSet<QString> pimDatabases = {QStringLiteral("MemoDB"), QStringLiteral("AddressDB"),
                                  QStringLiteral("DatebookDB"), QStringLiteral("ToDoDB")};
    QStringList pimList, otherList;

    for (const QString &db : databases) {
        if (pimDatabases.contains(db)) {
            int count = countDatabaseRecords(db);
            pimList << QStringLiteral("%1 (%2 records)").arg(db).arg(count);
        } else {
            otherList << db;
        }
    }

    m_logWidget->logInfo(i18n("PIM Databases:"));
    for (const QString &db : pimList) {
        m_logWidget->logInfo(QStringLiteral("  - %1").arg(db));
    }

    m_logWidget->logInfo(i18n("Other Databases: %1 total", otherList.size()));
    for (const QString &db : otherList) {
        m_logWidget->logInfo(QStringLiteral("  - %1").arg(db));
    }

    m_logWidget->logInfo(i18n("=== Total: %1 databases ===", databases.size()));
}

int KF6MainWindow::countDatabaseRecords(const QString &dbName)
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) return 0;

    int dbHandle = deviceLink->openDatabase(dbName);
    if (dbHandle < 0) return 0;

    QList<PilotRecord*> records = deviceLink->readAllRecords(dbHandle);
    int count = records.size();

    for (PilotRecord *r : records) {
        delete r;
    }

    deviceLink->closeDatabase(dbHandle);
    return count;
}

void KF6MainWindow::onSetUserInfo()
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    if (!deviceLink->readUserInfo(user)) {
        m_logWidget->logError(i18n("Failed to read current user info"));
        return;
    }

    QString currentName = QString::fromLatin1(user.username);

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Set User Info"));
    dialog.setMinimumWidth(300);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QFormLayout *formLayout = new QFormLayout();

    QLineEdit *nameEdit = new QLineEdit(currentName);
    formLayout->addRow(i18n("Username:"), nameEdit);

    QLabel *idLabel = new QLabel(QString::number(user.userID));
    formLayout->addRow(i18n("User ID:"), idLabel);

    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) return;

    QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, i18n("Error"), i18n("Username cannot be empty"));
        return;
    }

    strncpy(user.username, newName.toLatin1().constData(), sizeof(user.username) - 1);

    if (deviceLink->writeUserInfo(user)) {
        m_logWidget->logInfo(i18n("User info updated: %1", newName));
        QMessageBox::information(this, i18n("Success"), i18n("User info updated successfully!"));

        if (m_currentProfile) {
            DeviceFingerprint fp;
            fp.userId = user.userID;
            fp.userName = newName;
            registerDeviceWithCurrentProfile(fp);
        }
    } else {
        m_logWidget->logError(i18n("Failed to write user info"));
        QMessageBox::warning(this, i18n("Error"), i18n("Failed to update user info on device"));
    }
}

void KF6MainWindow::onDeviceInfo()
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    struct PilotUser user;
    memset(&user, 0, sizeof(user));
    deviceLink->readUserInfo(user);

    struct SysInfo sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));
    deviceLink->readSysInfo(sysInfo);

    QString osVersion = QStringLiteral("%1.%2.%3")
        .arg(sysInfo.romVersion >> 16)
        .arg((sysInfo.romVersion >> 8) & 0xFF)
        .arg(sysInfo.romVersion & 0xFF);

    // Read storage info for live device details
    struct CardInfo cardInfo;
    memset(&cardInfo, 0, sizeof(cardInfo));
    bool hasStorageInfo = deviceLink->readStorageInfo(0, cardInfo);

    QStringList databases = deviceLink->listDatabases();

    QString info = QStringLiteral(
        "<h3>Device Information</h3>"
        "<table>"
        "<tr><td><b>Username:</b></td><td>%1</td></tr>"
        "<tr><td><b>User ID:</b></td><td>%2</td></tr>"
        "<tr><td><b>Palm OS:</b></td><td>%3</td></tr>"
        "<tr><td><b>Product ID:</b></td><td>%4</td></tr>"
        "<tr><td><b>Databases:</b></td><td>%5</td></tr>"
        "</table>")
        .arg(QString::fromLatin1(user.username))
        .arg(user.userID)
        .arg(osVersion)
        .arg(QString::fromLatin1(sysInfo.prodID))
        .arg(databases.size());

    if (hasStorageInfo) {
        info += QStringLiteral(
            "<h4>Storage</h4>"
            "<table>"
            "<tr><td><b>Device Model:</b></td><td>%1</td></tr>"
            "<tr><td><b>Manufacturer:</b></td><td>%2</td></tr>"
            "<tr><td><b>ROM Size:</b></td><td>%3</td></tr>"
            "<tr><td><b>RAM Size:</b></td><td>%4</td></tr>"
            "<tr><td><b>Free RAM:</b></td><td>%5</td></tr>"
            "</table>")
            .arg(QString::fromLatin1(cardInfo.name))
            .arg(QString::fromLatin1(cardInfo.manufacturer))
            .arg(DeviceFingerprint::formatMemorySize(cardInfo.romSize))
            .arg(DeviceFingerprint::formatMemorySize(cardInfo.ramSize))
            .arg(DeviceFingerprint::formatMemorySize(cardInfo.ramFree));
    }

    info += i18n("<h4>PIM Databases</h4><ul>");
    QStringList pimDbs = {QStringLiteral("MemoDB"), QStringLiteral("AddressDB"),
                          QStringLiteral("DatebookDB"), QStringLiteral("ToDoDB")};
    for (const QString &db : pimDbs) {
        int count = countDatabaseRecords(db);
        info += QStringLiteral("<li>%1: %2 records</li>").arg(db).arg(count);
    }
    info += QStringLiteral("</ul>");

    QMessageBox::information(this, i18n("Device Information"), info);
}

// ========== Profile Slots ==========

void KF6MainWindow::onNewProfile()
{
    QString path = QFileDialog::getExistingDirectory(this,
        i18n("Select Folder for New Profile"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly);

    if (path.isEmpty()) return;

    loadProfile(path);
}

void KF6MainWindow::onOpenProfile()
{
    QString path = QFileDialog::getExistingDirectory(this,
        i18n("Open Profile Folder"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly);

    if (path.isEmpty()) return;

    loadProfile(path);
}

void KF6MainWindow::onCloseProfile()
{
    closeProfile();
}

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
            if (m_conduitManager && m_conduitManager->hasDatabaseClaims(conduitId)) {
                // Sync conduit: enabled if it's the active handler for any of its claimed databases
                QStringList activeDBs = m_conduitManager->activeDatabasesForConduit(conduitId, m_currentProfile);
                m_syncEngine->setConduitEnabled(conduitId, !activeDBs.isEmpty());
            } else {
                // Standalone conduit: use simple enable/disable toggle
                m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));
            }

            QJsonObject conduitSettings = m_currentProfile->conduitSettings(conduitId);
            if (!conduitSettings.isEmpty()) {
                auto *conduit = dynamic_cast<Sync::SyncConduitBase*>(m_syncEngine->conduit(conduitId));
                if (conduit) {
                    conduit->loadSettings(conduitSettings);
                }
            }
        }

        // Connection mode now encoded in devicePaths passed to PalmRuntime.

        updateWindowTitle();
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// ========== Sync Operations ==========

void KF6MainWindow::onHotSync()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("HotSync: no Palm device connected"));
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->hotSync());
}

void KF6MainWindow::onFullSync()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("FullSync: no Palm device connected"));
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->fullSync());
}

void KF6MainWindow::onCopyPalmToPC()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("CopyPalmToPC: no Palm device connected"));
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->copyPalmToPC());
}

void KF6MainWindow::onCopyPCToPalm()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("CopyPCToPalm: no Palm device connected"));
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->copyPCToPalm());
}

void KF6MainWindow::onBackup()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("Backup: no Palm device connected"));
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->backup());
}

void KF6MainWindow::onRestore()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("Restore: no Palm device connected"));
        return;
    }
    if (QMessageBox::question(this, i18n("Restore"),
            i18n("Restore is destructive. All Palm records not in the backup WILL BE DELETED. Continue?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->restore());
}

void KF6MainWindow::onChangeSyncFolder()
{
    onOpenProfile();
}

void KF6MainWindow::onOpenSyncFolder()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_syncPath));
}

void KF6MainWindow::onInstallFiles()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        i18n("Select Palm Files to Install"),
        QString(),
        i18n("Palm Files (*.prc *.pdb *.PRC *.PDB);;All Files (*)"));

    if (files.isEmpty()) {
        return;
    }

    QString installFolder = m_currentProfile->installFolderPath();
    QDir installDir(installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    int copiedCount = 0;
    int failCount = 0;

    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }

        if (QFile::copy(filePath, destPath)) {
            m_logWidget->logInfo(i18n("Queued for install: %1", fileInfo.fileName()));
            copiedCount++;
        } else {
            m_logWidget->logError(i18n("Failed to copy %1 to install folder", fileInfo.fileName()));
            failCount++;
        }
    }

    if (failCount == 0) {
        QMessageBox::information(this, i18n("Files Queued"),
            i18n("%1 file(s) queued for installation.\n\n"
                 "They will be installed on the next sync.", copiedCount));
    } else {
        QMessageBox::warning(this, i18n("Files Queued"),
            i18n("%1 file(s) queued, %2 failed to copy.\nCheck the log for details.",
                 copiedCount, failCount));
    }
}

void KF6MainWindow::onSyncStarted()
{
    statusBar()->showMessage(i18n("Syncing..."));

    if (m_interactiveConflictHandler) {
        m_interactiveConflictHandler->onSyncStart();
    }

    KNotification *notification = new KNotification(QStringLiteral("syncStarted"), KNotification::CloseOnTimeout, this);
    notification->setTitle(i18n("Sync Started"));
    notification->setText(i18n("Synchronization has started"));
    notification->setIconName(QStringLiteral("view-refresh"));
    notification->sendEvent();
}

void KF6MainWindow::onPalmRunStarted(const QString &label)
{
    m_currentPalmRunLabel = label;
    m_logWidget->logInfo(i18n("=== Starting %1 via PalmRuntime ===", label));
}

void KF6MainWindow::onPalmConflictHandlerKeepAlive()
{
    // Refresh the tickle when the conflict dialog is open for a long time.
    // The legacy code called m_session->resumeTickle(); now go through
    // the device link which emits to PalmTickle's start() (idempotent).
    if (m_palmRuntime && m_palmRuntime->deviceLink()) {
        m_palmRuntime->deviceLink()->resumeTickle();
    }
}

void KF6MainWindow::onConfigureMappings()
{
    if (!m_currentProfile) {
        QMessageBox::information(this, tr("Configure Mappings"),
            tr("No profile loaded."));
        return;
    }
    if (m_palmRuntime && m_palmRuntime->isRunning()) {
        QMessageBox::information(this, tr("Configure Mappings"),
            tr("A sync is currently in progress. Wait for it to finish "
               "before editing mappings."));
        return;
    }

    MappingEditorDialog dlg(this);
    dlg.setMappings(m_currentProfile->syncMappingsJson());

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QJsonArray updated = dlg.mappings();
    m_currentProfile->setSyncMappingsJson(updated);
    m_currentProfile->save();

    if (m_palmRuntime)
        m_palmRuntime->reloadMappings(updated);
}

void KF6MainWindow::onPalmRunFinished(WildPalms::Runtime::PalmRunResult result)
{
    const QString op = m_currentPalmRunLabel.isEmpty()
                       ? i18n("Palm operation") : m_currentPalmRunLabel;
    if (result.success) {
        m_logWidget->logInfo(i18n("%1 completed successfully", op));
        statusBar()->showMessage(i18n("%1 complete", op));
    } else {
        m_logWidget->logError(i18n("%1 finished with errors: %2",
                                   op, result.errorMessage));
        statusBar()->showMessage(i18n("%1 finished with errors", op));
    }

    // M6b Task 5 fix: honor ConnectionMode::DisconnectAfterSync.
    // Pre-M6b this was driven by DeviceSession::setConnectionMode (since
    // deleted in M6b Task 6). PalmRuntime doesn't model post-sync policy,
    // so KF6MainWindow checks the profile and disconnects here.
    if (m_currentProfile
        && m_currentProfile->connectionMode() == ConnectionMode::DisconnectAfterSync
        && m_palmRuntime
        && m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logInfo(i18n("Auto-disconnecting per profile policy (DisconnectAfterSync)"));
        m_palmRuntime->disconnectDevice();
    }
}

void KF6MainWindow::onSyncFinished(const Sync::SyncResult &result)
{
    if (m_interactiveConflictHandler) {
        bool hadConflicts = (result.palmStats.conflicts + result.pcStats.conflicts) > 0;
        bool allResolved = m_interactiveConflictHandler->conflictsDeferred() == 0;
        m_interactiveConflictHandler->onSyncEnd(hadConflicts, allResolved);
    }
    statusBar()->showMessage(i18n("Sync complete"));
}

void KF6MainWindow::onSyncProgress(int current, int total, const QString &message)
{
    statusBar()->showMessage(QStringLiteral("[%1/%2] %3").arg(current).arg(total).arg(message));
}

void KF6MainWindow::onSessionPalmScreen(const QString &message)
{
    m_logWidget->logInfo(i18n("[Palm Screen] %1", message));
}

void KF6MainWindow::onAsyncSyncResult(const Sync::SyncResult &result)
{
    QString operationName = m_pendingSyncOperationName;
    m_pendingSyncOperationName.clear();

    if (operationName.isEmpty()) {
        operationName = i18n("Sync");
    }

    showSyncResult(result, operationName);
}

// ========== Misc ==========

void KF6MainWindow::onAbout()
{
    QMessageBox::about(this, i18n("About Wild Palms"),
        i18n("<h3>Wild Palms %1</h3>"
             "<p>Modern Palm OS synchronization for Linux.</p>"
             "<p>Copyright &copy; 2024–2026 Clinton Ignatov</p>"
             "<p>Licensed under the "
             "<a href=\"https://www.gnu.org/licenses/gpl-3.0.txt\">GNU General Public License v3.0</a> "
             "or later.</p>"
             "<hr>"
             "<p style='font-size:small;'>Built with "
             "<a href=\"https://www.qt.io\">Qt %2</a>, "
             "<a href=\"https://develop.kde.org/frameworks/\">KDE Frameworks 6</a>, and "
             "<a href=\"https://github.com/desrod/pilot-link\">pilot-link</a>.</p>",
             QString::fromLatin1(WILDPALMS_VERSION_STRING),
             QString::fromLatin1(QT_VERSION_STR)));
}

void KF6MainWindow::onSettings()
{
    SettingsDialog dialog(this, m_currentProfile);
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_minimizeToTray = KF6Settings::instance().minimizeToTray();
    });
    dialog.exec();
}

void KF6MainWindow::onClearLog()
{
    if (m_logWidget) {
        m_logWidget->clear();
        m_logWidget->logInfo(i18n("Log cleared"));
    }
}

void KF6MainWindow::onShowConflicts()
{
    if (!m_currentProfile) {
        QMessageBox::information(this, i18n("No Profile"),
            i18n("Please load a profile first to review conflicts."));
        return;
    }

    // Create the consolidated conflict store if needed
    if (!m_conflictStore) {
        m_conflictStore = new QSyncCore::ConflictStore(this);
    }
    m_conflictStore->clear();

    // Collect conflicts from all conduit sync states
    QString userName = m_currentProfile->deviceFingerprint().userName;
    QString statePath = m_currentProfile->stateDirectoryPath();
    int totalConflicts = 0;

    QStringList conduits = {QStringLiteral("memos"), QStringLiteral("contacts"),
                            QStringLiteral("calendar"), QStringLiteral("todos")};
    for (const QString &conduitId : conduits) {
        Sync::SyncState state(userName, conduitId);
        state.setStateDirectory(statePath);
        state.load();

        QList<QSyncCore::ConflictRecord> conduitConflicts = state.pendingConflicts();
        for (const QSyncCore::ConflictRecord &conflict : conduitConflicts) {
            m_conflictStore->addConflict(conflict);
        }
        totalConflicts += conduitConflicts.size();
    }

    if (totalConflicts == 0) {
        m_logWidget->logInfo(i18n("No pending conflicts to review"));
        QMessageBox::information(this, i18n("No Conflicts"),
            i18n("There are no pending conflicts to review."));
    } else {
        m_logWidget->logInfo(i18n("Found %1 pending conflicts to review", totalConflicts));

        auto conduitLookup = [this](const QString &conduitId) -> const ISyncConduit* {
            if (!m_conduitManager) return nullptr;
            IConduit *c = m_conduitManager->conduit(conduitId);
            return dynamic_cast<const ISyncConduit*>(c);
        };

        ConflictReviewDialog dialog(m_conflictStore, conduitLookup, this);
        connect(&dialog, &ConflictReviewDialog::applyResolutionsRequested,
                this, &KF6MainWindow::onApplyConflictResolutions);
        dialog.exec();
    }
}

void KF6MainWindow::onApplyConflictResolutions()
{
    if (!m_conflictStore) return;

    QList<QSyncCore::ConflictRecord> resolved = m_conflictStore->resolvedUnappliedConflicts();
    if (resolved.isEmpty()) {
        m_logWidget->logInfo(i18n("No resolved conflicts to apply"));
        return;
    }

    // Write resolved conflicts back to each conduit's SyncState conflict store
    QString userName = m_currentProfile ? m_currentProfile->deviceFingerprint().userName : QString();
    QString statePath = m_currentProfile ? m_currentProfile->stateDirectoryPath() : QString();

    int applied = 0;
    for (const QSyncCore::ConflictRecord &conflict : resolved) {
        Sync::SyncState state(userName, conflict.conduitId);
        state.setStateDirectory(statePath);
        state.load();

        state.conflictStore()->resolveConflict(
            conflict.conflictId, conflict.decision, conflict.resolvedBy);
        state.save();
        m_conflictStore->markApplied(conflict.conflictId);
        applied++;
    }

    m_logWidget->logInfo(i18n("Applied %1 conflict resolution(s). "
                              "Resolutions will take effect on next sync.", applied));
}

void KF6MainWindow::updateTrayState(const QString &status)
{
    if (!m_trayIcon) return;
    m_trayIcon->setToolTipSubTitle(status);
}
