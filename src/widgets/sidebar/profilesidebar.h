#ifndef PROFILESIDEBAR_H
#define PROFILESIDEBAR_H

#include <QWidget>

class QTreeView;
class QStandardItemModel;
class QStandardItem;
class Profile;

/**
 * @brief Sidebar widget for profile and data navigation
 *
 * Displays a tree view with:
 * - Current profile info
 * - Data type shortcuts (Calendar, Tasks, Contacts, Memos)
 * - Recent profiles list
 */
class ProfileSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit ProfileSidebar(QWidget *parent = nullptr);
    ~ProfileSidebar() override = default;

    void setCurrentProfile(Profile *profile);
    void updateRecentProfiles();

Q_SIGNALS:
    void profileSelected(const QString &path);
    void dataTypeSelected(const QString &dataType);

private Q_SLOTS:
    void onItemClicked(const QModelIndex &index);
    void onItemDoubleClicked(const QModelIndex &index);

private:
    void setupUI();
    void updateProfileInfo();
    void updateDataItems();

    QTreeView *m_treeView;
    QStandardItemModel *m_model;

    // Model items
    QStandardItem *m_profileRootItem;
    QStandardItem *m_dataRootItem;
    QStandardItem *m_recentRootItem;

    Profile *m_currentProfile;
};

#endif // PROFILESIDEBAR_H
