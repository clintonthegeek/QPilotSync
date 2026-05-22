#ifndef KF6MAINWINDOW_H
#define KF6MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QMap>
#include <memory>
#include "runtime/palmrunresult.h"
#include "runtime/profileregistry.h"

// Forward declarations
class QTimer;
class QDockWidget;
class KPageWidget;
class KPageWidgetItem;
class ActionManager;
class LogWidget;
class KPilotDeviceLink;
class Profile;
class DashboardWidget;
class KStatusNotifierItem;
class PalmDeviceMonitor;
class AutoSyncOrchestrator;

namespace WildPalms::Runtime {
    class PalmRuntime;
    class AccountController;
    // ProfileRegistry is fully included above; no forward declaration needed.
}


/**
 * @brief KDE Frameworks 6 native main window for Wild Palms
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

    // Test seam — read-only view of the per-Palm-plugin KPageWidget map.
    const QMap<QString, KPageWidgetItem *> &palmPluginPagesForTest() const
        { return m_palmPluginPages; }

    // F.1a test seams (used by T15)
    void setProfileRegistryForTest(
        std::unique_ptr<WildPalms::Runtime::ProfileRegistry> reg);
    QString runStartupForTest();

protected:
    void closeEvent(QCloseEvent *event) override;

    /// Test seam: F.1a stopgap profile-picker UI. Production override
    /// shows a QMessageBox / QInputDialog; tests stub it to return a
    /// pre-set path (or empty for cancel).
    virtual QString showProfilePickerStopgap();

private Q_SLOTS:
    // Device connection
    void onConnectDevice();
    void onConnectionStarted();
    void onConnectionComplete(bool success, const QString &error);
    void onDeviceDisconnected();
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

    // PalmRuntime callbacks
    void onSessionPalmScreen(const QString &message);

    // Misc
    void onAbout();
    void onSettings();
    void onClearLog();

    // View management
    void onToggleLogPanel(bool visible);
    void onPageChanged(KPageWidgetItem *current, KPageWidgetItem *previous);
    void onFocusLog();

    // Auto-sync detection
    void onAutoDeviceDetected(Profile *profile, const QStringList &ports);
    void onUnregisteredDeviceDetected(const QString &usbSerial,
                                       const QString &userName,
                                       quint32 userId);

    // M2/M3 — PalmRuntime callbacks
    void onPalmRunStarted(const QString &label);
    void onPalmRunFinished(WildPalms::Runtime::PalmRunResult result);

    // M5a — keepAlive from KalburatorInteractiveConflictHandler
    void onPalmConflictHandlerKeepAlive();

    // M5b — open MappingEditorDialog and reload PalmRuntime
    void onConfigureMappings();

private:
    // UI setup
    void setupUI();
    void setupActions();
    void setupConnections();
    void createCentralLayout();
    void updateMenuState(bool connected);
    void updateWindowTitle();
    void updateProfileMenuState();

    // Profile management
    void loadProfile(const QString &path);
    void closeProfile();
    QString resolveStartupProfile();

    // Device handling
    void startConnectionMultiPort(const QStringList &devicePaths);
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

    // Status header strip (above plugin pages)
    DashboardWidget *m_dashboardWidget;

    // Dynamic plugin pages (keyed by plugin id), populated synchronously in loadProfile()
    QMap<QString, KPageWidgetItem *> m_palmPluginPages;

    // PalmRuntime owns the hotSync path.
    std::unique_ptr<WildPalms::Runtime::PalmRuntime> m_palmRuntime;

    // AccountController is profile-scoped, recreated alongside
    // m_palmRuntime in loadProfile(). Borrows m_palmRuntime->backendRegistry(),
    // m_currentProfile, and m_palmRuntime — torn down BEFORE m_palmRuntime
    // and m_currentProfile in closeProfile()/loadProfile() to avoid dangling
    // borrowed pointers.
    std::unique_ptr<WildPalms::Runtime::AccountController> m_accountController;

    // Action manager
    ActionManager *m_actionManager;

    // F.1a: App-level profile registry
    std::unique_ptr<WildPalms::Runtime::ProfileRegistry> m_profileRegistry;

    QString m_syncPath;

    // Last used connection settings
    QString m_lastUsedDevicePath;
    QString m_lastUsedBaudRate;

    // Device listening mode
    QTimer *m_devicePollTimer = nullptr;
    bool m_listeningForDevice = false;
    QString m_listeningDevicePath;

    // Current async operation
    QString m_currentPalmRunLabel;

    // Profile
    std::unique_ptr<Profile> m_currentProfile;

    // M5a: stored as QObject* to avoid including libkalburator headers in this
    // header (include-guard collision with WP-local QSyncCore headers).
    // Actual type: KalburatorInteractiveConflictHandler (QObject subclass).
    QObject *m_palmConflictHandler = nullptr;

    // Auto-detection and auto-sync
    PalmDeviceMonitor *m_deviceMonitor = nullptr;
    AutoSyncOrchestrator *m_autoSync = nullptr;

    // System tray
    KStatusNotifierItem *m_trayIcon = nullptr;
    bool m_minimizeToTray = true;
    void updateTrayState(const QString &status);
};

#endif // KF6MAINWINDOW_H
