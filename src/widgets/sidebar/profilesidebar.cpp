#include "profilesidebar.h"

#include <QVBoxLayout>
#include <QTreeView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QIcon>
#include <QFileInfo>

#include <KLocalizedString>

#include "../../profile.h"
#include "../../kf6/kf6settings.h"

ProfileSidebar::ProfileSidebar(QWidget *parent)
    : QWidget(parent)
    , m_treeView(nullptr)
    , m_model(nullptr)
    , m_profileRootItem(nullptr)
    , m_dataRootItem(nullptr)
    , m_recentRootItem(nullptr)
    , m_currentProfile(nullptr)
{
    setupUI();
}

void ProfileSidebar::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create tree view
    m_treeView = new QTreeView(this);
    m_treeView->setHeaderHidden(true);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setAnimated(true);
    m_treeView->setExpandsOnDoubleClick(false);

    // Create model
    m_model = new QStandardItemModel(this);

    // Create root items
    m_profileRootItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("user-identity")),
                                           i18n("Profile"));
    m_profileRootItem->setEditable(false);
    m_profileRootItem->setData(QStringLiteral("profile_root"), Qt::UserRole);
    m_model->appendRow(m_profileRootItem);

    m_dataRootItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("folder-documents")),
                                        i18n("Data"));
    m_dataRootItem->setEditable(false);
    m_dataRootItem->setData(QStringLiteral("data_root"), Qt::UserRole);
    m_model->appendRow(m_dataRootItem);

    // Add data type items
    QStandardItem *calendarItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("view-calendar")),
                                                     i18n("Calendar"));
    calendarItem->setEditable(false);
    calendarItem->setData(QStringLiteral("calendar"), Qt::UserRole);
    m_dataRootItem->appendRow(calendarItem);

    QStandardItem *tasksItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("view-task")),
                                                  i18n("Tasks"));
    tasksItem->setEditable(false);
    tasksItem->setData(QStringLiteral("tasks"), Qt::UserRole);
    m_dataRootItem->appendRow(tasksItem);

    QStandardItem *contactsItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("view-pim-contacts")),
                                                     i18n("Contacts"));
    contactsItem->setEditable(false);
    contactsItem->setData(QStringLiteral("contacts"), Qt::UserRole);
    m_dataRootItem->appendRow(contactsItem);

    QStandardItem *memosItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("view-pim-notes")),
                                                  i18n("Memos"));
    memosItem->setEditable(false);
    memosItem->setData(QStringLiteral("memos"), Qt::UserRole);
    m_dataRootItem->appendRow(memosItem);

    m_recentRootItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("document-open-recent")),
                                          i18n("Recent Profiles"));
    m_recentRootItem->setEditable(false);
    m_recentRootItem->setData(QStringLiteral("recent_root"), Qt::UserRole);
    m_model->appendRow(m_recentRootItem);

    m_treeView->setModel(m_model);

    // Expand roots by default
    m_treeView->expand(m_model->indexFromItem(m_profileRootItem));
    m_treeView->expand(m_model->indexFromItem(m_dataRootItem));
    m_treeView->expand(m_model->indexFromItem(m_recentRootItem));

    // Connect signals
    connect(m_treeView, &QTreeView::clicked, this, &ProfileSidebar::onItemClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &ProfileSidebar::onItemDoubleClicked);

    layout->addWidget(m_treeView);

    // Load recent profiles
    updateRecentProfiles();
}

void ProfileSidebar::setCurrentProfile(Profile *profile)
{
    m_currentProfile = profile;
    updateProfileInfo();
    updateDataItems();
}

void ProfileSidebar::updateProfileInfo()
{
    // Clear existing profile children
    m_profileRootItem->removeRows(0, m_profileRootItem->rowCount());

    if (!m_currentProfile) {
        QStandardItem *noProfile = new QStandardItem(i18n("No profile loaded"));
        noProfile->setEditable(false);
        noProfile->setEnabled(false);
        m_profileRootItem->appendRow(noProfile);
        return;
    }

    // Add profile name
    QStandardItem *nameItem = new QStandardItem(QIcon::fromTheme(QStringLiteral("folder")),
                                                 m_currentProfile->name());
    nameItem->setEditable(false);
    nameItem->setData(m_currentProfile->syncFolderPath(), Qt::UserRole + 1);
    m_profileRootItem->appendRow(nameItem);

    // Add device info if registered
    DeviceFingerprint fp = m_currentProfile->deviceFingerprint();
    if (fp.isValid()) {
        QStandardItem *deviceItem = new QStandardItem(
            QIcon::fromTheme(QStringLiteral("phone")),
            fp.displayString());
        deviceItem->setEditable(false);
        nameItem->appendRow(deviceItem);
    }

    // Add sync folder path
    QStandardItem *pathItem = new QStandardItem(
        QIcon::fromTheme(QStringLiteral("folder-sync")),
        m_currentProfile->syncFolderPath());
    pathItem->setEditable(false);
    pathItem->setToolTip(m_currentProfile->syncFolderPath());
    nameItem->appendRow(pathItem);

    m_treeView->expand(m_model->indexFromItem(m_profileRootItem));
    m_treeView->expand(m_model->indexFromItem(nameItem));
}

void ProfileSidebar::updateDataItems()
{
    // Enable/disable data items based on profile state
    bool hasProfile = m_currentProfile != nullptr;

    for (int i = 0; i < m_dataRootItem->rowCount(); ++i) {
        QStandardItem *item = m_dataRootItem->child(i);
        item->setEnabled(hasProfile);
    }
}

void ProfileSidebar::updateRecentProfiles()
{
    // Clear existing recent items
    m_recentRootItem->removeRows(0, m_recentRootItem->rowCount());

    QStringList recent = KF6Settings::instance().recentProfiles();

    if (recent.isEmpty()) {
        QStandardItem *noRecent = new QStandardItem(i18n("No recent profiles"));
        noRecent->setEditable(false);
        noRecent->setEnabled(false);
        m_recentRootItem->appendRow(noRecent);
        return;
    }

    for (const QString &path : recent) {
        QFileInfo info(path);
        QStandardItem *item = new QStandardItem(QIcon::fromTheme(QStringLiteral("folder")),
                                                 info.fileName());
        item->setEditable(false);
        item->setData(path, Qt::UserRole + 1);
        item->setData(QStringLiteral("recent_profile"), Qt::UserRole);
        item->setToolTip(path);
        m_recentRootItem->appendRow(item);
    }

    m_treeView->expand(m_model->indexFromItem(m_recentRootItem));
}

void ProfileSidebar::onItemClicked(const QModelIndex &index)
{
    QString itemType = index.data(Qt::UserRole).toString();

    if (itemType == QStringLiteral("calendar") ||
        itemType == QStringLiteral("tasks") ||
        itemType == QStringLiteral("contacts") ||
        itemType == QStringLiteral("memos")) {
        emit dataTypeSelected(itemType);
    }
}

void ProfileSidebar::onItemDoubleClicked(const QModelIndex &index)
{
    QString itemType = index.data(Qt::UserRole).toString();
    QString path = index.data(Qt::UserRole + 1).toString();

    if (itemType == QStringLiteral("recent_profile") && !path.isEmpty()) {
        emit profileSelected(path);
    }
}
