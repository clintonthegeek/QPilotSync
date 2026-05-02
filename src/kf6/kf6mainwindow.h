#ifndef KF6MAINWINDOW_H
#define KF6MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QMap>
#include <memory>
#include "runtime/palmrunresult.h"

// Forward declarations
class QTimer;
class QDockWidget;
class KPageWidget;
class KPageWidgetItem;
class KXMLGUIClient;
class ActionManager;
class LogWidget;
class KPilotDeviceLink;
class DeviceSession;
class Profile;
class DashboardWidget;
class ConduitManager;
class IConduit;
class KStatusNotifierItem;
class PalmDeviceMonitor;
class AutoSyncOrchestrator;

// Phase E.9 — new-ABI plugin manager. Coexists with ConduitManager
// until E.16 retires the old surface.
namespace WildPalms { class BackendPluginManager; class IBackendPlugin; }
#ifndef WILDPALMS_CALENDAR_MVP_ONLY
namespace WildPalms::Runtime { class SyncRunner; }
#endif
namespace WildPalms::Runtime { class PalmRuntime; }

namespace Sync {
class SyncEngine;
class SyncResult;
}

class InteractiveConflictHandler;

namespace QSyncCore {
class ConflictStore;
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

    // Conduit lifecycle
    void onConduitLoaded(IConduit *conduit);
    void onConduitUnloading(IConduit *conduit);

    // Phase E.9 — new-ABI plugin lifecycle. Runs alongside the conduit
    // loop until E.16 retires ConduitManager.
    void onBackendPluginLoaded(WildPalms::IBackendPlugin *plugin);
    void onBackendPluginUnloading(WildPalms::IBackendPlugin *plugin);

    // Auto-sync detection
    void onAutoDeviceDetected(Profile *profile, const QStringList &ports);

    // M2 — PalmRuntime callback
    void onPalmRunFinished(WildPalms::Runtime::PalmRunResult result);

private:
    // UI setup
    void setupUI();
    void setupActions();
    void setupConnections();
    void createCentralLayout();
    void updateMenuState(bool connected);
    void updateWindowTitle();
    void updateProfileMenuState();

    // Conduit management
    void initializeConduits();

    // Sync engine
    void initializeSyncEngine();
    void showSyncResult(const Sync::SyncResult &result, const QString &operationName);

    // Profile management
    void loadProfile(const QString &path);
    void closeProfile();

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

    // Status header strip (above conduit pages)
    DashboardWidget *m_dashboardWidget;

    // Dynamic conduit pages (keyed by conduit ID)
    QMap<QString, KPageWidgetItem*> m_conduitPages;
    QMap<QString, KXMLGUIClient*> m_conduitGUIClients;

    // Conduit manager
    ConduitManager *m_conduitManager = nullptr;

    // Phase E.9 — new-ABI plugin manager. Coexists with ConduitManager
    // until E.16 retires the old surface.
    WildPalms::BackendPluginManager   *m_backendPluginManager = nullptr;
    QMap<QString, KPageWidgetItem *>   m_backendPluginPages;

#ifndef WILDPALMS_CALENDAR_MVP_ONLY
    // Phase E.16 — new-ABI sync orchestrator. Replaces Sync::SyncEngine
    // for the Tools-menu sync actions. Coexists with m_syncEngine
    // until the legacy IConduit deletion lands; the legacy engine is
    // constructed but unused from the menu wiring.
    WildPalms::Runtime::SyncRunner    *m_syncRunner = nullptr;
#endif

    // M2 — PalmRuntime owns the new hotSync path.
    std::unique_ptr<WildPalms::Runtime::PalmRuntime> m_palmRuntime;

    // (Phase E.16 — the PalmDeviceConnection wrapping the live
    // KPilotDeviceLink is owned inside m_syncRunner via
    // SyncRunner::setKPilotLink, set on connect and torn down on
    // disconnect. Keeps the device-side headers off this one's
    // include surface.)

    // Action manager
    ActionManager *m_actionManager;

    // Device connection
    DeviceSession *m_session;
    KPilotDeviceLink *m_deviceLink;

    // Sync engine and conduits
    Sync::SyncEngine *m_syncEngine;
    QString m_syncPath;

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
    InteractiveConflictHandler *m_interactiveConflictHandler = nullptr;

    // Auto-detection and auto-sync
    PalmDeviceMonitor *m_deviceMonitor = nullptr;
    AutoSyncOrchestrator *m_autoSync = nullptr;

    // System tray
    KStatusNotifierItem *m_trayIcon = nullptr;
    bool m_minimizeToTray = true;
    void updateTrayState(const QString &status);
};

#endif // KF6MAINWINDOW_H
