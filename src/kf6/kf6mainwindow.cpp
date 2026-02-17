#include "kf6mainwindow.h"
#include "kf6settings.h"
#include "actionmanager.h"
#include "conduitmanager.h"

#include "../app/logwidget.h"
#include "../app/exporthandler.h"
#include "../app/importhandler.h"

#include "../qpilotsync_version.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/devicesession.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"
#include "../profile.h"

#include "../core/iconduit.h"
#include "../sync/syncengine.h"
#include "../sync/synctypes.h"
#include "../sync/conduit.h"
#include "../sync/localfilebackend.h"
#include "../sync/conduits/installconduit.h"
#include "../sync/qsynccore/conflictstore.h"
#include "../sync/syncstate.h"

// Widget includes
#include "../widgets/dashboard/dashboardwidget.h"

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
#include <cstring>

#include <KAboutData>
#include <KLocalizedString>
#include <KNotification>
#include <KPageWidget>
#include <KPageWidgetItem>

#include <pi-dlp.h>

KF6MainWindow::KF6MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    // KPageWidget layout
    , m_pageWidget(nullptr)
    , m_logDock(nullptr)
    , m_logWidget(nullptr)
    // Built-in page items
    , m_dashboardPage(nullptr)
    // Built-in data views
    , m_dashboardWidget(nullptr)
    // Action manager
    , m_actionManager(nullptr)
    // Device connection
    , m_session(nullptr)
    , m_deviceLink(nullptr)
    // Sync engine and conduits
    , m_syncEngine(nullptr)
    , m_installConduit(nullptr)
    , m_syncPath()
    // Export/Import handlers
    , m_exportHandler(nullptr)
    , m_importHandler(nullptr)
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

    // Create export/import handlers (must be before setupConnections)
    m_exportHandler = new ExportHandler(this);
    m_exportHandler->setLogWidget(m_logWidget);

    m_importHandler = new ImportHandler(this);
    m_importHandler->setLogWidget(m_logWidget);

    // Now setup connections after all objects are created
    setupConnections();

    // Setup XMLGUI
    setupGUI(Default, QStringLiteral("qpilotsyncui.rc"));

    // Status bar
    statusBar()->showMessage(i18n("Ready - No device connected"));

    // Log initial message
    m_logWidget->logInfo(QStringLiteral("QPilotSync %1 initialized").arg(QPILOTSYNC_VERSION_STRING));

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
}

KF6MainWindow::~KF6MainWindow()
{
    // Stop any pending timers first
    if (m_devicePollTimer) {
        m_devicePollTimer->stop();
    }

    // Clear device link references from handlers before anything else
    // This prevents use-after-free if the link gets deleted
    if (m_exportHandler) {
        m_exportHandler->setDeviceLink(nullptr);
    }
    if (m_importHandler) {
        m_importHandler->setDeviceLink(nullptr);
    }
    m_deviceLink = nullptr;

    // Disconnect device if connected (don't delete - let Qt handle it)
    if (m_session && m_session->isConnected()) {
        m_session->disconnectDevice();
    }

    // Clear sync engine's device link reference
    if (m_syncEngine) {
        m_syncEngine->setDeviceLink(nullptr);
    }

    // Delete non-parented objects only
    // m_session and m_syncEngine are QObject children - Qt will delete them

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
    // Save window state
    saveWindowState();

    // Close profile
    if (m_currentProfile) {
        m_currentProfile->save();
    }

    event->accept();
}

void KF6MainWindow::setupUI()
{
    setWindowTitle(i18n("QPilotSync - Palm Pilot Synchronization"));
    setMinimumSize(1000, 700);

    createCentralLayout();
}

void KF6MainWindow::createCentralLayout()
{
    // Create KPageWidget with List (icon sidebar) face type
    m_pageWidget = new KPageWidget(this);
    m_pageWidget->setFaceType(KPageWidget::List);

    // Dashboard is the only built-in page.
    // All other pages (Memos, Contacts, Calendar, Tasks) are created
    // dynamically by conduit plugins via onConduitLoaded().
    m_dashboardWidget = new DashboardWidget(this);

    m_dashboardPage = new KPageWidgetItem(m_dashboardWidget, i18n("Sync"));
    m_dashboardPage->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_dashboardPage->setHeaderVisible(false);
    m_pageWidget->addPage(m_dashboardPage);

    setCentralWidget(m_pageWidget);

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

    // Page navigation from ActionManager
    // Only Dashboard has a fixed shortcut (Ctrl+1). Conduit pages
    // are added dynamically and don't have static navigation bindings.
    connect(m_actionManager, &ActionManager::viewDashboardRequested, this, [this]() {
        m_pageWidget->setCurrentPage(m_dashboardPage);
    });

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

    // Save current page — use 0 for Dashboard, or find the conduit page index
    int pageIndex = 0;
    KPageWidgetItem *current = m_pageWidget->currentPage();
    if (current != m_dashboardPage) {
        // Conduit pages start at index 1
        int idx = 1;
        for (auto it = m_conduitPages.constBegin(); it != m_conduitPages.constEnd(); ++it) {
            if (it.value() == current) {
                pageIndex = idx;
                break;
            }
            ++idx;
        }
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

    // Restore current page (0 = Dashboard, 1+ = conduit pages in insertion order)
    int pageIndex = settings.currentTabIndex();
    if (pageIndex == 0 || m_conduitPages.isEmpty()) {
        m_pageWidget->setCurrentPage(m_dashboardPage);
    } else {
        // Conduit pages in map iteration order
        int idx = 1;
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

    if (!m_currentProfile) {
        return;
    }

    // Update the dashboard when it becomes visible
    if (current == m_dashboardPage) {
        m_dashboardWidget->updateStatus(m_currentProfile,
                                        m_session && m_session->isConnected());
    }
    // Conduit views handle their own data loading via loadFromPath()
    // when a profile is loaded. Lazy per-tab refresh can be added later
    // as an optimization.
}

void KF6MainWindow::updateMenuState(bool connected)
{
    bool hasProfile = m_currentProfile != nullptr;
    m_actionManager->updateConnectionState(connected, hasProfile);
    m_actionManager->updateProfileState(hasProfile);
}

void KF6MainWindow::updateWindowTitle()
{
    QString title = i18n("QPilotSync");
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

    // Create install conduit
    m_installConduit = new Sync::InstallConduit(this);
    connect(m_installConduit, &Sync::InstallConduit::logMessage,
            m_logWidget, &LogWidget::logInfo);
    connect(m_installConduit, &Sync::InstallConduit::errorOccurred,
            m_logWidget, &LogWidget::logError);
    connect(m_installConduit, &Sync::InstallConduit::progressUpdated,
            this, [this](int current, int total, const QString &fileName) {
                statusBar()->showMessage(i18n("Installing %1 (%2/%3)", fileName, current, total));
            });

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
    m_conduitManager->loadConfig();

    connect(m_conduitManager, &ConduitManager::conduitLoaded,
            this, &KF6MainWindow::onConduitLoaded);
    connect(m_conduitManager, &ConduitManager::conduitUnloading,
            this, &KF6MainWindow::onConduitUnloading);

    // Load all enabled conduits (creates views & registers with SyncEngine)
    for (const auto &info : m_conduitManager->conduitList()) {
        if (info.enabled) {
            QString conduitId = info.metaData.value(QStringLiteral("X-QPilotSync-ConduitId"));
            if (conduitId.isEmpty()) {
                conduitId = info.metaData.pluginId();
            }
            m_conduitManager->loadConduit(conduitId);
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

void KF6MainWindow::runInstallConduit()
{
    if (!m_installConduit || !m_session || !m_session->isConnected()) {
        return;
    }

    if (!m_installConduit->hasPendingFiles()) {
        return;
    }

    m_logWidget->logInfo(i18n("--- Installing pending files ---"));

    int socket = m_deviceLink->socketDescriptor();
    QList<Sync::InstallResult> results = m_installConduit->installAll(socket);

    int successCount = 0;
    int failCount = 0;
    for (const Sync::InstallResult &r : results) {
        if (r.success) {
            successCount++;
        } else {
            failCount++;
        }
    }

    if (successCount > 0 || failCount > 0) {
        m_logWidget->logInfo(i18n("Install complete: %1 succeeded, %2 failed",
                                  successCount, failCount));
    }
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

    // Update dashboard after sync
    m_dashboardWidget->updateStatus(m_currentProfile,
                                    m_session && m_session->isConnected());
}

// ========== Profile Management ==========

void KF6MainWindow::loadProfile(const QString &path)
{
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
    m_syncEngine->setConflictAutoResolve(m_currentProfile->conflictAutoResolve());
    m_syncEngine->setConflictFallback(m_currentProfile->conflictFallback());

    Sync::LocalFileBackend *backend = new Sync::LocalFileBackend(m_syncPath);
    m_syncEngine->setBackend(backend);

    // Apply profile's conduit enabled settings
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

    // Apply connection mode to session
    if (m_session) {
        m_session->setConnectionMode(m_currentProfile->connectionMode());
    }

    // Configure install conduit
    m_installConduit->setInstallFolder(m_currentProfile->installFolderPath());

    // Add to recent profiles
    KF6Settings::instance().addRecentProfile(path);

    // Auto-set as default if none exists
    if (KF6Settings::instance().defaultProfilePath().isEmpty()) {
        KF6Settings::instance().setDefaultProfilePath(path);
        m_logWidget->logInfo(i18n("Set as default profile"));
    }

    KF6Settings::instance().sync();

    // Update UI
    updateWindowTitle();
    updateProfileMenuState();
    m_dashboardWidget->updateStatus(m_currentProfile, m_session && m_session->isConnected());

    m_logWidget->logInfo(i18n("Loaded profile: %1", m_currentProfile->name()));
    m_logWidget->logInfo(i18n("Sync folder: %1", m_syncPath));
}

void KF6MainWindow::closeProfile()
{
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
             "Press the HotSync button on your Palm - QPilotSync will wait for it."));
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

void KF6MainWindow::startConnection(const QString &devicePath)
{
    if (m_session) {
        delete m_session;
        m_session = nullptr;
        m_deviceLink = nullptr;
    }

    m_session = new DeviceSession(this);

    if (m_currentProfile) {
        m_session->setConnectionMode(m_currentProfile->connectionMode());
    }

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
                Q_UNUSED(summary);
                statusBar()->showMessage(success ? i18n("Sync complete") : i18n("Sync failed"));
            });
    connect(m_session, &DeviceSession::syncResultReady,
            this, &KF6MainWindow::onAsyncSyncResult);
    connect(m_session, &DeviceSession::disconnected,
            this, [this]() {
                m_deviceLink = nullptr;
                m_exportHandler->setDeviceLink(nullptr);
                m_importHandler->setDeviceLink(nullptr);
                updateMenuState(false);
                statusBar()->showMessage(i18n("Disconnected"));

                // Send notification
                KNotification *notification = new KNotification(
                    QStringLiteral("deviceDisconnected"), KNotification::CloseOnTimeout, this);
                notification->setTitle(i18n("Device Disconnected"));
                notification->setText(i18n("Palm device has been disconnected"));
                notification->setIconName(QStringLiteral("network-disconnect"));
                notification->sendEvent();
            });
    connect(m_session, &DeviceSession::readyForSync,
            this, &KF6MainWindow::onReadyForSync);

    m_logWidget->logInfo(i18n("Connecting to %1...", devicePath));
    statusBar()->showMessage(i18n("Connecting to %1...", devicePath));

    m_session->connectDevice(devicePath);

    updateMenuState(false);
    m_actionManager->cancelConnectionAction()->setEnabled(true);
}

void KF6MainWindow::onConnectionComplete(bool success)
{
    m_actionManager->cancelConnectionAction()->setEnabled(false);

    if (!success) {
        m_logWidget->logError(i18n("Connection failed or was cancelled"));
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

    m_deviceLink = m_session->deviceLink();

    m_exportHandler->setDeviceLink(m_deviceLink);
    m_importHandler->setDeviceLink(m_deviceLink);

    // Read user info
    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    if (m_deviceLink->readUserInfo(user)) {
        QString userName = QString::fromLatin1(user.username);
        quint32 userId = user.userID;

        m_logWidget->logInfo(i18n("User: %1 (ID: %2)", userName, userId));

        DeviceFingerprint connectedDevice;
        connectedDevice.userId = userId;
        connectedDevice.userName = userName;

        if (m_currentProfile) {
            if (!handleDeviceFingerprint(connectedDevice)) {
                m_deviceLink->closeConnection();
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
                strncpy(user.username, newUserName.toLatin1().constData(), sizeof(user.username) - 1);
                user.userID = static_cast<unsigned long>(QDateTime::currentSecsSinceEpoch());

                if (m_deviceLink->writeUserInfo(user)) {
                    m_logWidget->logInfo(i18n("Device initialized: %1 (ID: %2)",
                                              newUserName, user.userID));

                    if (m_currentProfile) {
                        DeviceFingerprint newFp;
                        newFp.userId = user.userID;
                        newFp.userName = newUserName;
                        registerDeviceWithCurrentProfile(newFp);
                    }
                } else {
                    m_logWidget->logError(i18n("Failed to write user info to device"));
                }
            }
        }
    }

    // Read system info
    struct SysInfo sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));

    if (m_deviceLink->readSysInfo(sysInfo)) {
        m_logWidget->logInfo(i18n("Palm OS: %1.%2, Product ID: %3",
                                  sysInfo.romVersion >> 16,
                                  (sysInfo.romVersion >> 8) & 0xFF,
                                  QString::fromLatin1(sysInfo.prodID)));
    }

    m_syncEngine->setDeviceLink(m_deviceLink);

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
    if (m_session && m_session->isConnected()) {
        m_logWidget->logInfo(i18n("Disconnecting..."));

        if (m_deviceLink && m_deviceLink->isConnected()) {
            m_logWidget->logInfo(i18n("Ending sync session..."));
            m_deviceLink->endSync();
        }

        m_session->disconnectDevice();
        m_deviceLink = nullptr;

        m_exportHandler->setDeviceLink(nullptr);
        m_importHandler->setDeviceLink(nullptr);

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

    if (m_session) {
        m_session->requestCancel();
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

void KF6MainWindow::onDeviceReady(const QString &userName, const QString &deviceName)
{
    m_logWidget->logInfo(i18n("Device ready: %1 (%2)", userName, deviceName));
}

void KF6MainWindow::onReadyForSync()
{
    if (!m_currentProfile || !m_currentProfile->autoSyncOnConnect()) {
        return;
    }

    m_logWidget->logInfo(i18n("Auto-sync enabled - starting sync..."));

    QString syncType = m_currentProfile->defaultSyncType();
    if (syncType == QStringLiteral("fullsync")) {
        onFullSync();
    } else {
        onHotSync();
    }
}

void KF6MainWindow::onListDatabases()
{
    if (!m_deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    m_logWidget->logInfo(i18n("=== Database List ==="));

    QStringList databases = m_deviceLink->listDatabases();

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
    if (!m_deviceLink) return 0;

    int dbHandle = m_deviceLink->openDatabase(dbName);
    if (dbHandle < 0) return 0;

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
    int count = records.size();

    for (PilotRecord *r : records) {
        delete r;
    }

    m_deviceLink->closeDatabase(dbHandle);
    return count;
}

void KF6MainWindow::onSetUserInfo()
{
    if (!m_deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    if (!m_deviceLink->readUserInfo(user)) {
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

    if (m_deviceLink->writeUserInfo(user)) {
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
    if (!m_deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    struct PilotUser user;
    memset(&user, 0, sizeof(user));
    m_deviceLink->readUserInfo(user);

    struct SysInfo sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));
    m_deviceLink->readSysInfo(sysInfo);

    QString osVersion = QStringLiteral("%1.%2.%3")
        .arg(sysInfo.romVersion >> 16)
        .arg((sysInfo.romVersion >> 8) & 0xFF)
        .arg(sysInfo.romVersion & 0xFF);

    QStringList databases = m_deviceLink->listDatabases();

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
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }

    // Use a simplified settings dialog for now
    // Full implementation would use KConfigDialog
    QMessageBox::information(this, i18n("Profile Settings"),
        i18n("Profile settings dialog will be implemented with KConfigDialog."));
}

// ========== Sync Operations ==========

void KF6MainWindow::onHotSync()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded. Use File → Open Profile first."));
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    m_logWidget->logInfo(i18n("=== Starting HotSync ==="));
    runInstallConduit();

    m_pendingSyncOperationName = i18n("HotSync");
    m_session->requestSync(Sync::SyncMode::HotSync, m_syncEngine);
}

void KF6MainWindow::onFullSync()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded. Use File → Open Profile first."));
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    int ret = QMessageBox::question(this, i18n("Full Sync"),
        i18n("Full Sync will compare all records on both sides.\n"
             "This may take longer than HotSync.\n\nProceed?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo(i18n("=== Starting Full Sync ==="));
    runInstallConduit();

    m_pendingSyncOperationName = i18n("Full Sync");
    m_session->requestSync(Sync::SyncMode::FullSync, m_syncEngine);
}

void KF6MainWindow::onCopyPalmToPC()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded. Use File → Open Profile first."));
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    int ret = QMessageBox::warning(this, i18n("Copy Palm → PC"),
        i18n("This will overwrite PC data with Palm data.\n"
             "Any changes on the PC will be lost.\n\nProceed?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo(i18n("=== Copying Palm → PC ==="));
    m_pendingSyncOperationName = i18n("Copy Palm → PC");
    m_session->requestSync(Sync::SyncMode::CopyPalmToPC, m_syncEngine);
}

void KF6MainWindow::onCopyPCToPalm()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded. Use File → Open Profile first."));
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    int ret = QMessageBox::warning(this, i18n("Copy PC → Palm"),
        i18n("This will overwrite Palm data with PC data.\n"
             "Any changes on the Palm will be lost.\n\nProceed?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo(i18n("=== Copying PC → Palm ==="));
    runInstallConduit();

    m_pendingSyncOperationName = i18n("Copy PC → Palm");
    m_session->requestSync(Sync::SyncMode::CopyPCToPalm, m_syncEngine);
}

void KF6MainWindow::onBackup()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded. Use File → Open Profile first."));
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    int ret = QMessageBox::question(this, i18n("Backup Palm → PC"),
        i18n("This will backup all Palm data to your PC.\n"
             "Existing backup files will be updated.\n"
             "Old files not on Palm will be preserved.\n\nProceed?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo(i18n("=== Backing up Palm → PC ==="));
    m_pendingSyncOperationName = i18n("Backup");
    m_session->requestSync(Sync::SyncMode::Backup, m_syncEngine);
}

void KF6MainWindow::onRestore()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded. Use File → Open Profile first."));
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    int ret = QMessageBox::warning(this, i18n("Restore PC → Palm"),
        i18n("FULL RESTORE\n\n"
             "This will completely overwrite your Palm with PC backup data.\n"
             "Palm records not in the backup WILL BE DELETED.\n\n"
             "Are you sure you want to restore?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo(i18n("=== Restoring PC → Palm ==="));
    runInstallConduit();

    m_pendingSyncOperationName = i18n("Restore");
    m_session->requestSync(Sync::SyncMode::Restore, m_syncEngine);
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

    bool connected = m_session && m_session->isConnected();

    if (connected) {
        m_logWidget->logInfo(i18n("--- Installing files to Palm ---"));
        statusBar()->showMessage(i18n("Installing files..."));
        m_session->requestInstall(files);
    } else {
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
                     "They will be installed on the next HotSync.", copiedCount));
        } else {
            QMessageBox::warning(this, i18n("Files Queued"),
                i18n("%1 file(s) queued, %2 failed to copy.\nCheck the log for details.",
                     copiedCount, failCount));
        }
    }
}

void KF6MainWindow::onSyncStarted()
{
    statusBar()->showMessage(i18n("Syncing..."));

    KNotification *notification = new KNotification(QStringLiteral("syncStarted"), KNotification::CloseOnTimeout, this);
    notification->setTitle(i18n("Sync Started"));
    notification->setText(i18n("Synchronization has started"));
    notification->setIconName(QStringLiteral("view-refresh"));
    notification->sendEvent();
}

void KF6MainWindow::onSyncFinished(const Sync::SyncResult &result)
{
    Q_UNUSED(result)
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

void KF6MainWindow::onInstallFinished(bool success, int successCount, int failCount)
{
    statusBar()->showMessage(i18n("Install complete"));

    m_logWidget->logInfo(i18n("Installation complete: %1 succeeded, %2 failed",
                              successCount, failCount));

    KNotification *notification = new KNotification(QStringLiteral("installComplete"), KNotification::CloseOnTimeout, this);
    notification->setTitle(i18n("Install Complete"));
    notification->setText(i18n("Successfully installed %1 file(s)", successCount));
    notification->setIconName(QStringLiteral("document-import"));
    notification->sendEvent();

    if (success && failCount == 0) {
        QMessageBox::information(this, i18n("Install Complete"),
            i18n("Successfully installed %1 file(s) to Palm.", successCount));
    } else {
        QMessageBox::warning(this, i18n("Install Complete"),
            i18n("Installed %1 file(s), %2 failed.\nCheck the log for details.",
                 successCount, failCount));
    }
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
    // KAboutApplicationDialog will be used via standard KDE mechanisms
    // For now, show a simple about box
    QMessageBox::about(this, i18n("About QPilotSync"),
        i18n("<h3>QPilotSync %1</h3>"
             "<p>Modern Palm Pilot synchronization for Linux</p>"
             "<p>Built with:</p>"
             "<ul>"
             "<li>Qt %2</li>"
             "<li>KDE Frameworks 6</li>"
             "<li>pilot-link</li>"
             "</ul>"
             "<p>Bringing classic Palm Pilots into the modern era!</p>"
             "<p>Licensed with the <a href=https://www.gnu.org/licenses/gpl-3.0.txt>GPL version 3.0</a> or later.</p>",
             QString::fromLatin1(QPILOTSYNC_VERSION_STRING),
             QString::fromLatin1(QT_VERSION_STR)));
}

void KF6MainWindow::onSettings()
{
    // Will be replaced with KConfigDialog
    QMessageBox::information(this, i18n("Settings"),
        i18n("Settings dialog will be implemented with KConfigDialog."));
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
        // Full conflict review dialog will be implemented
        QMessageBox::information(this, i18n("Conflicts Found"),
            i18n("Found %1 conflict(s). Conflict review UI will be implemented.", totalConflicts));
    }
}

void KF6MainWindow::onApplyConflictResolutions()
{
    // Will be implemented with conflict review widget
}
