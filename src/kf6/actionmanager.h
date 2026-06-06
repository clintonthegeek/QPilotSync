#ifndef ACTIONMANAGER_H
#define ACTIONMANAGER_H

#include <QObject>

class QAction;
class KActionCollection;
class KXmlGuiWindow;

/**
 * @brief Manages all application actions using KActionCollection
 *
 * This class centralizes action creation and management, making it easy
 * to access actions from different parts of the application and ensuring
 * proper XMLGUI integration.
 */
class ActionManager : public QObject
{
    Q_OBJECT

public:
    explicit ActionManager(KXmlGuiWindow *window, QObject *parent = nullptr);
    ~ActionManager() override = default;

    // Access the action collection
    KActionCollection* actionCollection() const { return m_actionCollection; }

    // Get specific actions by name
    QAction* action(const QString &name) const;

    // ========== File Actions ==========
    QAction* newProfileAction() const { return action(QStringLiteral("file_new_profile")); }
    QAction* importProfileAction() const { return action(QStringLiteral("file_import_profile")); }
    QAction* closeProfileAction() const { return action(QStringLiteral("file_close_profile")); }
    QAction* profileSettingsAction() const { return action(QStringLiteral("file_profile_settings")); }
    QAction* configureMappingsAction() const { return action(QStringLiteral("file_configure_mappings")); }

    // ========== Device Actions ==========
    QAction* connectAction() const { return action(QStringLiteral("device_connect")); }
    QAction* disconnectAction() const { return action(QStringLiteral("device_disconnect")); }
    QAction* cancelConnectionAction() const { return action(QStringLiteral("device_cancel_connection")); }
    QAction* listDatabasesAction() const { return action(QStringLiteral("device_list_databases")); }
    QAction* setUserInfoAction() const { return action(QStringLiteral("device_set_user_info")); }
    QAction* deviceInfoAction() const { return action(QStringLiteral("device_info")); }

    // ========== Sync Actions ==========
    QAction* hotSyncAction() const { return action(QStringLiteral("sync_hotsync")); }
    QAction* fullSyncAction() const { return action(QStringLiteral("sync_fullsync")); }
    QAction* copyPalmToPCAction() const { return action(QStringLiteral("sync_copy_palm_to_pc")); }
    QAction* clobberPalmFromPCAction() const { return action(QStringLiteral("sync_clobber_palm_from_pc")); }
    QAction* backupAction() const { return action(QStringLiteral("sync_backup")); }
    QAction* restoreAction() const { return action(QStringLiteral("sync_restore")); }
    QAction* changeSyncFolderAction() const { return action(QStringLiteral("sync_change_folder")); }
    QAction* openSyncFolderAction() const { return action(QStringLiteral("sync_open_folder")); }
    QAction* installFilesAction() const { return action(QStringLiteral("sync_install_files")); }

    // ========== View Actions ==========
    QAction* clearLogAction() const { return action(QStringLiteral("view_clear_log")); }
    QAction* showConflictsAction() const { return action(QStringLiteral("view_show_conflicts")); }
    QAction* toggleLogPanelAction() const { return action(QStringLiteral("view_toggle_log_panel")); }
    QAction* focusLogAction() const { return action(QStringLiteral("view_focus_log")); }

    // ========== Page Navigation Actions ==========
    QAction* viewDashboardAction() const { return action(QStringLiteral("view_page_dashboard")); }
    QAction* viewMemosAction() const { return action(QStringLiteral("view_page_memos")); }
    QAction* viewContactsAction() const { return action(QStringLiteral("view_page_contacts")); }
    QAction* viewCalendarAction() const { return action(QStringLiteral("view_page_calendar")); }
    QAction* viewTasksAction() const { return action(QStringLiteral("view_page_tasks")); }

    // Update action states based on application state
    void updateConnectionState(bool connected, bool hasProfile);
    void updateProfileState(bool hasProfile);
    void updateConflictCount(int count);

Q_SIGNALS:
    // File operations
    void newProfileRequested();
    void importProfileRequested();
    void closeProfileRequested();
    void profileSettingsRequested();
    void configureMappingsRequested();
    void settingsRequested();

    // Device operations
    void connectRequested();
    void disconnectRequested();
    void cancelConnectionRequested();
    void listDatabasesRequested();
    void setUserInfoRequested();
    void deviceInfoRequested();

    // Sync operations
    void hotSyncRequested();
    void fullSyncRequested();
    void copyPalmToPCRequested();
    void clobberPalmFromPCRequested();
    void backupRequested();
    void restoreRequested();
    void changeSyncFolderRequested();
    void openSyncFolderRequested();
    void installFilesRequested();

    // View operations
    void clearLogRequested();
    void showConflictsRequested();

    // Page navigation
    void viewDashboardRequested();
    void viewMemosRequested();
    void viewContactsRequested();
    void viewCalendarRequested();
    void viewTasksRequested();
    void focusLogRequested();

private:
    void setupActions();
    void setupFileActions();
    void setupDeviceActions();
    void setupSyncActions();
    void setupViewActions();
    void setupNavigationActions();

    KActionCollection *m_actionCollection;
    KXmlGuiWindow *m_window;
};

#endif // ACTIONMANAGER_H
