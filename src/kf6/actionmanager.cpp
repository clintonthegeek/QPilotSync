#include "actionmanager.h"

#include <QAction>
#include <QKeySequence>
#include <KActionCollection>
#include <KStandardAction>
#include <KLocalizedString>
#include <KXmlGuiWindow>
#include <KXMLGUIFactory>

ActionManager::ActionManager(KXmlGuiWindow *window, QObject *parent)
    : QObject(parent)
    , m_actionCollection(window->actionCollection())
    , m_window(window)
{
    setupActions();
}

QAction* ActionManager::action(const QString &name) const
{
    return m_actionCollection->action(name);
}

void ActionManager::setupActions()
{
    setupFileActions();
    setupDeviceActions();
    setupSyncActions();
    setupDataActions();
    setupViewActions();
    setupNavigationActions();

    // Add standard KDE actions
    KStandardAction::quit(m_window, &QWidget::close, m_actionCollection);
    KStandardAction::preferences(this, [this]() {
        // Will be connected to settings dialog
    }, m_actionCollection);
    KStandardAction::configureToolbars(m_window, [this]() {
        m_window->configureToolbars();
    }, m_actionCollection);
    KStandardAction::keyBindings(m_window, [this]() {
        m_window->guiFactory()->showConfigureShortcutsDialog();
    }, m_actionCollection);
}

void ActionManager::setupFileActions()
{
    // New Profile
    QAction *newProfile = new QAction(QIcon::fromTheme(QStringLiteral("folder-new")),
                                       i18n("&New Profile..."), this);
    m_actionCollection->addAction(QStringLiteral("file_new_profile"), newProfile);
    m_actionCollection->setDefaultShortcut(newProfile, QKeySequence::New);
    connect(newProfile, &QAction::triggered, this, &ActionManager::newProfileRequested);

    // Open Profile
    QAction *openProfile = new QAction(QIcon::fromTheme(QStringLiteral("document-open-folder")),
                                        i18n("&Open Profile..."), this);
    m_actionCollection->addAction(QStringLiteral("file_open_profile"), openProfile);
    m_actionCollection->setDefaultShortcut(openProfile, QKeySequence::Open);
    connect(openProfile, &QAction::triggered, this, &ActionManager::openProfileRequested);

    // Close Profile
    QAction *closeProfile = new QAction(i18n("Close Profile"), this);
    connect(closeProfile, &QAction::triggered, this, &ActionManager::closeProfileRequested);
    m_actionCollection->addAction(QStringLiteral("file_close_profile"), closeProfile);

    // Profile Settings
    QAction *profileSettings = new QAction(i18n("Profile Settings..."), this);
    connect(profileSettings, &QAction::triggered, this, &ActionManager::profileSettingsRequested);
    m_actionCollection->addAction(QStringLiteral("file_profile_settings"), profileSettings);
}

void ActionManager::setupDeviceActions()
{
    // Connect
    QAction *connect = new QAction(QIcon::fromTheme(QStringLiteral("network-connect")),
                                    i18n("&Connect..."), this);
    QObject::connect(connect, &QAction::triggered, this, &ActionManager::connectRequested);
    m_actionCollection->addAction(QStringLiteral("device_connect"), connect);

    // Disconnect
    QAction *disconnect = new QAction(QIcon::fromTheme(QStringLiteral("network-disconnect")),
                                       i18n("&Disconnect"), this);
    QObject::connect(disconnect, &QAction::triggered, this, &ActionManager::disconnectRequested);
    m_actionCollection->addAction(QStringLiteral("device_disconnect"), disconnect);

    // Cancel Connection
    QAction *cancelConnection = new QAction(i18n("Cancel Connection"), this);
    QObject::connect(cancelConnection, &QAction::triggered, this, &ActionManager::cancelConnectionRequested);
    m_actionCollection->addAction(QStringLiteral("device_cancel_connection"), cancelConnection);

    // List Databases
    QAction *listDatabases = new QAction(i18n("List &Databases"), this);
    QObject::connect(listDatabases, &QAction::triggered, this, &ActionManager::listDatabasesRequested);
    m_actionCollection->addAction(QStringLiteral("device_list_databases"), listDatabases);

    // Set User Info
    QAction *setUserInfo = new QAction(i18n("Set &User Info..."), this);
    QObject::connect(setUserInfo, &QAction::triggered, this, &ActionManager::setUserInfoRequested);
    m_actionCollection->addAction(QStringLiteral("device_set_user_info"), setUserInfo);

    // Device Info
    QAction *deviceInfo = new QAction(i18n("Device &Info"), this);
    QObject::connect(deviceInfo, &QAction::triggered, this, &ActionManager::deviceInfoRequested);
    m_actionCollection->addAction(QStringLiteral("device_info"), deviceInfo);
}

void ActionManager::setupSyncActions()
{
    // HotSync
    QAction *hotSync = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                    i18n("&HotSync"), this);
    m_actionCollection->addAction(QStringLiteral("sync_hotsync"), hotSync);
    m_actionCollection->setDefaultShortcut(hotSync, QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(hotSync, &QAction::triggered, this, &ActionManager::hotSyncRequested);

    // Full Sync
    QAction *fullSync = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                     i18n("&Full Sync"), this);
    m_actionCollection->addAction(QStringLiteral("sync_fullsync"), fullSync);
    m_actionCollection->setDefaultShortcut(fullSync, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    connect(fullSync, &QAction::triggered, this, &ActionManager::fullSyncRequested);

    // Copy Palm to PC
    QAction *copyPalmToPC = new QAction(i18n("Copy Palm → PC"), this);
    connect(copyPalmToPC, &QAction::triggered, this, &ActionManager::copyPalmToPCRequested);
    m_actionCollection->addAction(QStringLiteral("sync_copy_palm_to_pc"), copyPalmToPC);

    // Copy PC to Palm
    QAction *copyPCToPalm = new QAction(i18n("Copy PC → Palm"), this);
    connect(copyPCToPalm, &QAction::triggered, this, &ActionManager::copyPCToPalmRequested);
    m_actionCollection->addAction(QStringLiteral("sync_copy_pc_to_palm"), copyPCToPalm);

    // Backup
    QAction *backup = new QAction(QIcon::fromTheme(QStringLiteral("document-save")),
                                   i18n("&Backup (Palm → PC)"), this);
    connect(backup, &QAction::triggered, this, &ActionManager::backupRequested);
    m_actionCollection->addAction(QStringLiteral("sync_backup"), backup);

    // Restore
    QAction *restore = new QAction(QIcon::fromTheme(QStringLiteral("document-open")),
                                    i18n("&Restore (PC → Palm)"), this);
    connect(restore, &QAction::triggered, this, &ActionManager::restoreRequested);
    m_actionCollection->addAction(QStringLiteral("sync_restore"), restore);

    // Change Sync Folder
    QAction *changeFolder = new QAction(i18n("Change Sync &Folder..."), this);
    connect(changeFolder, &QAction::triggered, this, &ActionManager::changeSyncFolderRequested);
    m_actionCollection->addAction(QStringLiteral("sync_change_folder"), changeFolder);

    // Open Sync Folder
    QAction *openFolder = new QAction(QIcon::fromTheme(QStringLiteral("folder-open")),
                                       i18n("Open Sync Folder"), this);
    connect(openFolder, &QAction::triggered, this, &ActionManager::openSyncFolderRequested);
    m_actionCollection->addAction(QStringLiteral("sync_open_folder"), openFolder);

    // Install Files
    QAction *installFiles = new QAction(QIcon::fromTheme(QStringLiteral("document-import")),
                                         i18n("Install Files..."), this);
    connect(installFiles, &QAction::triggered, this, &ActionManager::installFilesRequested);
    m_actionCollection->addAction(QStringLiteral("sync_install_files"), installFiles);
}

void ActionManager::setupDataActions()
{
    // Export Memos
    QAction *exportMemos = new QAction(i18n("Memos to Markdown..."), this);
    connect(exportMemos, &QAction::triggered, this, &ActionManager::exportMemosRequested);
    m_actionCollection->addAction(QStringLiteral("export_memos"), exportMemos);

    // Export Contacts
    QAction *exportContacts = new QAction(i18n("Contacts to vCard..."), this);
    connect(exportContacts, &QAction::triggered, this, &ActionManager::exportContactsRequested);
    m_actionCollection->addAction(QStringLiteral("export_contacts"), exportContacts);

    // Export Calendar
    QAction *exportCalendar = new QAction(i18n("Calendar to iCalendar..."), this);
    connect(exportCalendar, &QAction::triggered, this, &ActionManager::exportCalendarRequested);
    m_actionCollection->addAction(QStringLiteral("export_calendar"), exportCalendar);

    // Export Todos
    QAction *exportTodos = new QAction(i18n("Todos to iCalendar..."), this);
    connect(exportTodos, &QAction::triggered, this, &ActionManager::exportTodosRequested);
    m_actionCollection->addAction(QStringLiteral("export_todos"), exportTodos);

    // Export All
    QAction *exportAll = new QAction(i18n("Export All..."), this);
    connect(exportAll, &QAction::triggered, this, &ActionManager::exportAllRequested);
    m_actionCollection->addAction(QStringLiteral("export_all"), exportAll);

    // Import Memo
    QAction *importMemo = new QAction(i18n("Memo from Markdown..."), this);
    connect(importMemo, &QAction::triggered, this, &ActionManager::importMemoRequested);
    m_actionCollection->addAction(QStringLiteral("import_memo"), importMemo);

    // Import Contact
    QAction *importContact = new QAction(i18n("Contact from vCard..."), this);
    connect(importContact, &QAction::triggered, this, &ActionManager::importContactRequested);
    m_actionCollection->addAction(QStringLiteral("import_contact"), importContact);

    // Import Event
    QAction *importEvent = new QAction(i18n("Event from iCalendar..."), this);
    connect(importEvent, &QAction::triggered, this, &ActionManager::importEventRequested);
    m_actionCollection->addAction(QStringLiteral("import_event"), importEvent);

    // Import Todo
    QAction *importTodo = new QAction(i18n("Todo from iCalendar..."), this);
    connect(importTodo, &QAction::triggered, this, &ActionManager::importTodoRequested);
    m_actionCollection->addAction(QStringLiteral("import_todo"), importTodo);
}

void ActionManager::setupViewActions()
{
    // Toggle Log Panel
    QAction *toggleLogPanel = new QAction(i18n("Show Lo&g Panel"), this);
    toggleLogPanel->setCheckable(true);
    toggleLogPanel->setChecked(true);
    m_actionCollection->addAction(QStringLiteral("view_toggle_log_panel"), toggleLogPanel);
    m_actionCollection->setDefaultShortcut(toggleLogPanel, QKeySequence(Qt::Key_F10));

    // Focus Log
    QAction *focusLog = new QAction(i18n("Focus &Log"), this);
    m_actionCollection->addAction(QStringLiteral("view_focus_log"), focusLog);
    m_actionCollection->setDefaultShortcut(focusLog, QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(focusLog, &QAction::triggered, this, &ActionManager::focusLogRequested);

    // Clear Log
    QAction *clearLog = new QAction(i18n("&Clear Log"), this);
    connect(clearLog, &QAction::triggered, this, &ActionManager::clearLogRequested);
    m_actionCollection->addAction(QStringLiteral("view_clear_log"), clearLog);

    // Show Conflicts
    QAction *showConflicts = new QAction(QIcon::fromTheme(QStringLiteral("dialog-warning")),
                                          i18n("Review &Conflicts..."), this);
    connect(showConflicts, &QAction::triggered, this, &ActionManager::showConflictsRequested);
    m_actionCollection->addAction(QStringLiteral("view_show_conflicts"), showConflicts);
}

void ActionManager::setupNavigationActions()
{
    // Page navigation: Ctrl+1 through Ctrl+5
    QAction *viewDashboard = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                          i18n("&Sync / Dashboard"), this);
    m_actionCollection->addAction(QStringLiteral("view_page_dashboard"), viewDashboard);
    m_actionCollection->setDefaultShortcut(viewDashboard, QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(viewDashboard, &QAction::triggered, this, &ActionManager::viewDashboardRequested);

    QAction *viewMemos = new QAction(QIcon::fromTheme(QStringLiteral("view-pim-notes")),
                                      i18n("&Memos"), this);
    m_actionCollection->addAction(QStringLiteral("view_page_memos"), viewMemos);
    m_actionCollection->setDefaultShortcut(viewMemos, QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(viewMemos, &QAction::triggered, this, &ActionManager::viewMemosRequested);

    QAction *viewContacts = new QAction(QIcon::fromTheme(QStringLiteral("view-pim-contacts")),
                                         i18n("C&ontacts"), this);
    m_actionCollection->addAction(QStringLiteral("view_page_contacts"), viewContacts);
    m_actionCollection->setDefaultShortcut(viewContacts, QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(viewContacts, &QAction::triggered, this, &ActionManager::viewContactsRequested);

    QAction *viewCalendar = new QAction(QIcon::fromTheme(QStringLiteral("view-calendar")),
                                         i18n("Ca&lendar"), this);
    m_actionCollection->addAction(QStringLiteral("view_page_calendar"), viewCalendar);
    m_actionCollection->setDefaultShortcut(viewCalendar, QKeySequence(Qt::CTRL | Qt::Key_4));
    connect(viewCalendar, &QAction::triggered, this, &ActionManager::viewCalendarRequested);

    QAction *viewTasks = new QAction(QIcon::fromTheme(QStringLiteral("view-task")),
                                      i18n("&Tasks"), this);
    m_actionCollection->addAction(QStringLiteral("view_page_tasks"), viewTasks);
    m_actionCollection->setDefaultShortcut(viewTasks, QKeySequence(Qt::CTRL | Qt::Key_5));
    connect(viewTasks, &QAction::triggered, this, &ActionManager::viewTasksRequested);
}

void ActionManager::updateConnectionState(bool connected, bool hasProfile)
{
    // Device actions
    disconnectAction()->setEnabled(connected);
    cancelConnectionAction()->setEnabled(false); // Only enabled during connection attempt
    listDatabasesAction()->setEnabled(connected);
    setUserInfoAction()->setEnabled(connected);
    deviceInfoAction()->setEnabled(connected);

    // Sync actions require both connection and profile
    bool canSync = connected && hasProfile;
    hotSyncAction()->setEnabled(canSync);
    fullSyncAction()->setEnabled(canSync);
    copyPalmToPCAction()->setEnabled(canSync);
    copyPCToPalmAction()->setEnabled(canSync);
    backupAction()->setEnabled(canSync);
    restoreAction()->setEnabled(canSync);

    // Install files label changes based on connection
    if (connected) {
        installFilesAction()->setText(i18n("Install Files Now..."));
    } else {
        installFilesAction()->setText(i18n("Install Files on Next Sync..."));
    }

    // Export/Import require connection
    exportMemosAction()->setEnabled(connected);
    exportContactsAction()->setEnabled(connected);
    exportCalendarAction()->setEnabled(connected);
    exportTodosAction()->setEnabled(connected);
    exportAllAction()->setEnabled(connected);
    importMemoAction()->setEnabled(connected);
    importContactAction()->setEnabled(connected);
    importEventAction()->setEnabled(connected);
    importTodoAction()->setEnabled(connected);
}

void ActionManager::updateProfileState(bool hasProfile)
{
    closeProfileAction()->setEnabled(hasProfile);
    profileSettingsAction()->setEnabled(hasProfile);
    installFilesAction()->setEnabled(hasProfile);
    showConflictsAction()->setEnabled(hasProfile);
}

void ActionManager::updateConflictCount(int count)
{
    if (count > 0) {
        showConflictsAction()->setText(i18n("Review &Conflicts... (%1)", count));
    } else {
        showConflictsAction()->setText(i18n("Review &Conflicts..."));
    }
}
