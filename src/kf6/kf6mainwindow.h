#ifndef KF6MAINWINDOW_H
#define KF6MAINWINDOW_H

#include <KXmlGuiWindow>

// Forward declarations
class QTimer;
class QDockWidget;
class KPageWidget;
class KPageWidgetItem;
class ActionManager;
class LogWidget;
class KPilotDeviceLink;
class DeviceSession;
class ExportHandler;
class ImportHandler;
class Profile;
class DashboardWidget;
class CalendarView;
class TaskView;
class ContactView;
class MemoView;

namespace Sync {
class SyncEngine;
class SyncResult;
class InstallConduit;
}

namespace QSyncCore {
class ConflictStore;
}

/**
 * @brief KDE Frameworks 6 native main window for QPilotSync
 *
 * Implements a KPageWidget icon-sidebar layout (like KDE System Settings)
 * with pages for Sync/Dashboard, Memos, Contacts, Calendar, and Tasks.
 * A QDockWidget at the bottom holds the log panel.
 *
 * Uses KXmlGuiWindow for proper KDE desktop integration with XMLGUI
 * menus and toolbars.
 */
class KF6MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit KF6MainWindow(QWidget *parent = nullptr);
    ~KF6MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    // Device connection
    void onConnectDevice();
    void onConnectionComplete(bool success);
    void onDisconnectDevice();
    void onDevicePoll();
    void onCancelConnection();
    void startListening(const QString &devicePath);
    void stopListening();
    void startConnection(const QString &devicePath);
    void onDeviceStatusChanged(int status);
    void onDeviceReady(const QString &userName, const QString &deviceName);
    void onReadyForSync();
    void onListDatabases();
    void onSetUserInfo();
    void onDeviceInfo();

    // Profile management
    void onNewProfile();
    void onOpenProfile();
    void onCloseProfile();
    void onProfileSettings();

    // Sync operations
    void onHotSync();
    void onFullSync();
    void onCopyPalmToPC();
    void onCopyPCToPalm();
    void onBackup();
    void onRestore();
    void onChangeSyncFolder();
    void onOpenSyncFolder();
    void onInstallFiles();
    void onSyncStarted();
    void onSyncFinished(const Sync::SyncResult &result);
    void onSyncProgress(int current, int total, const QString &message);

    // DeviceSession callbacks
    void onSessionPalmScreen(const QString &message);
    void onInstallFinished(bool success, int successCount, int failCount);
    void onAsyncSyncResult(const Sync::SyncResult &result);

    // Misc
    void onAbout();
    void onSettings();
    void onClearLog();

    // View management
    void onShowConflicts();
    void onApplyConflictResolutions();
    void onToggleLogPanel(bool visible);
    void onPageChanged(KPageWidgetItem *current, KPageWidgetItem *previous);
    void onFocusLog();

private:
    // UI setup
    void setupUI();
    void setupActions();
    void setupConnections();
    void createCentralLayout();
    void updateMenuState(bool connected);
    void updateWindowTitle();
    void updateProfileMenuState();
    void updateDataViews();

    // Sync engine
    void initializeSyncEngine();
    void runInstallConduit();
    void showSyncResult(const Sync::SyncResult &result, const QString &operationName);

    // Profile management
    void loadProfile(const QString &path);
    void closeProfile();

    // Device handling
    bool handleDeviceFingerprint(const struct DeviceFingerprint &connectedDevice);
    void registerDeviceWithCurrentProfile(const struct DeviceFingerprint &fingerprint);
    int countDatabaseRecords(const QString &dbName);

    // State persistence
    void saveWindowState();
    void restoreWindowState();

    // KPageWidget layout
    KPageWidget *m_pageWidget;
    QDockWidget *m_logDock;
    LogWidget *m_logWidget;

    // Page items
    KPageWidgetItem *m_dashboardPage;
    KPageWidgetItem *m_memosPage;
    KPageWidgetItem *m_contactsPage;
    KPageWidgetItem *m_calendarPage;
    KPageWidgetItem *m_tasksPage;

    // Data views (page content)
    DashboardWidget *m_dashboardWidget;
    CalendarView *m_calendarView;
    TaskView *m_taskView;
    ContactView *m_contactView;
    MemoView *m_memoView;

    // Action manager
    ActionManager *m_actionManager;

    // Device connection
    DeviceSession *m_session;
    KPilotDeviceLink *m_deviceLink;

    // Sync engine and conduits
    Sync::SyncEngine *m_syncEngine;
    Sync::InstallConduit *m_installConduit;
    QString m_syncPath;

    // Export/Import handlers
    ExportHandler *m_exportHandler;
    ImportHandler *m_importHandler;

    // Last used connection settings
    QString m_lastUsedDevicePath;
    QString m_lastUsedBaudRate;

    // Device listening mode
    QTimer *m_devicePollTimer = nullptr;
    bool m_listeningForDevice = false;
    QString m_listeningDevicePath;

    // Current async operation
    QString m_pendingSyncOperationName;

    // Profile
    Profile *m_currentProfile;

    // Conflict review
    QSyncCore::ConflictStore *m_conflictStore;
};

#endif // KF6MAINWINDOW_H
