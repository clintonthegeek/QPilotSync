#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

// Forward declarations
class QTimer;
class QMdiArea;
class QMdiSubWindow;
class QMenu;
class QAction;
class LogWidget;
class KPilotDeviceLink;
class DeviceSession;
class ExportHandler;
class ImportHandler;
class Profile;

namespace Sync {
class SyncEngine;
class SyncResult;
class InstallConduit;
}

/**
 * @brief Main application window for QPilotSync
 *
 * Manages the application UI, device connection, sync operations,
 * and profile management. Uses an MDI interface for multiple views.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Device connection
    void onConnectDevice();
    void onConnectionComplete(bool success);
    void onDisconnectDevice();
    void onDevicePoll();  // Check if device appeared while listening
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
    void onQuit();
    void onAbout();
    void onSettings();
    void onClearLog();

private:
    // UI setup
    void createMenus();
    void createToolBar();
    void createLogWindow();
    void showLogWindow();
    void updateMenuState(bool connected);
    void updateWindowTitle();
    void updateProfileMenuState();
    void updateRecentProfilesMenu();

    // Sync engine
    void initializeSyncEngine();
    void runInstallConduit();
    void showSyncResult(const Sync::SyncResult &result, const QString &operationName);
    void showWebCalendarSettings(QWidget *parent);

    // Profile management
    void loadProfile(const QString &path);
    void closeProfile();

    // Device handling
    bool handleDeviceFingerprint(const struct DeviceFingerprint &connectedDevice);
    void registerDeviceWithCurrentProfile(const struct DeviceFingerprint &fingerprint);
    int countDatabaseRecords(const QString &dbName);

    // MDI area and log window
    QMdiArea *m_mdiArea;
    QMdiSubWindow *m_logSubWindow;
    LogWidget *m_logWidget;

    // Device connection
    DeviceSession *m_session;
    KPilotDeviceLink *m_deviceLink;  // Kept for export/import handlers compatibility

    // Sync engine and conduits
    Sync::SyncEngine *m_syncEngine;
    Sync::InstallConduit *m_installConduit;
    QString m_syncPath;

    // Export/Import handlers
    ExportHandler *m_exportHandler;
    ImportHandler *m_importHandler;

    // Last used connection settings (for passing to new profiles)
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
    QMenu *m_recentProfilesMenu;
    QAction *m_closeProfileAction;
    QAction *m_profileSettingsAction;

    // Menu actions that need to be enabled/disabled
    QAction *m_disconnectAction;
    QAction *m_cancelConnectionAction;
    QAction *m_listDatabasesAction;
    QAction *m_setUserInfoAction;
    QAction *m_deviceInfoAction;

    // Sync menu actions
    QAction *m_hotSyncAction;
    QAction *m_fullSyncAction;
    QAction *m_copyPalmToPCAction;
    QAction *m_copyPCToPalmAction;
    QAction *m_backupAction;
    QAction *m_restoreAction;
    QAction *m_changeSyncFolderAction;
    QAction *m_openSyncFolderAction;
    QAction *m_installFilesAction;

    // Toolbar actions (some duplicated for independent enable/disable)
    QAction *m_toolbarDisconnectAction;
    QAction *m_toolbarHotSyncAction;
    QAction *m_toolbarFullSyncAction;
    QAction *m_toolbarBackupAction;
    QAction *m_toolbarRestoreAction;

    // Export/Import actions
    QAction *m_exportMemosAction;
    QAction *m_exportContactsAction;
    QAction *m_exportCalendarAction;
    QAction *m_exportTodosAction;
    QAction *m_exportAllAction;
    QAction *m_importMemoAction;
    QAction *m_importContactAction;
    QAction *m_importEventAction;
    QAction *m_importTodoAction;
};

#endif // MAINWINDOW_H
