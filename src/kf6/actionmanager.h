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
    QAction* openProfileAction() const { return action(QStringLiteral("file_open_profile")); }
    QAction* closeProfileAction() const { return action(QStringLiteral("file_close_profile")); }
    QAction* profileSettingsAction() const { return action(QStringLiteral("file_profile_settings")); }

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
    QAction* copyPCToPalmAction() const { return action(QStringLiteral("sync_copy_pc_to_palm")); }
    QAction* backupAction() const { return action(QStringLiteral("sync_backup")); }
    QAction* restoreAction() const { return action(QStringLiteral("sync_restore")); }
    QAction* changeSyncFolderAction() const { return action(QStringLiteral("sync_change_folder")); }
    QAction* openSyncFolderAction() const { return action(QStringLiteral("sync_open_folder")); }
    QAction* installFilesAction() const { return action(QStringLiteral("sync_install_files")); }

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

    // ========== View Actions ==========
    QAction* tabbedViewAction() const { return action(QStringLiteral("view_tabbed")); }
    QAction* showLogAction() const { return action(QStringLiteral("view_show_log")); }
    QAction* clearLogAction() const { return action(QStringLiteral("view_clear_log")); }
    QAction* showConflictsAction() const { return action(QStringLiteral("view_show_conflicts")); }
    QAction* toggleSidebarAction() const { return action(QStringLiteral("view_toggle_sidebar")); }
    QAction* toggleLogPanelAction() const { return action(QStringLiteral("view_toggle_log_panel")); }

    // Update action states based on application state
    void updateConnectionState(bool connected, bool hasProfile);
    void updateProfileState(bool hasProfile);
    void updateConflictCount(int count);

Q_SIGNALS:
    // File operations
    void newProfileRequested();
    void openProfileRequested();
    void closeProfileRequested();
    void profileSettingsRequested();

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
    void copyPCToPalmRequested();
    void backupRequested();
    void restoreRequested();
    void changeSyncFolderRequested();
    void openSyncFolderRequested();
    void installFilesRequested();

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

    // View operations
    void clearLogRequested();
    void showConflictsRequested();

private:
    void setupActions();
    void setupFileActions();
    void setupDeviceActions();
    void setupSyncActions();
    void setupDataActions();
    void setupViewActions();

    KActionCollection *m_actionCollection;
    KXmlGuiWindow *m_window;
};

#endif // ACTIONMANAGER_H
