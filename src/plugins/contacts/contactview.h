#ifndef CONTACTVIEW_H
#define CONTACTVIEW_H

#include <QWidget>
#include <QHash>

class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QSplitter;
class QLineEdit;
class QToolBar;
class CategoryManager;
class CategoryModel;
class CategoryFilterWidget;

/**
 * @brief Contact data browser view
 *
 * Displays contacts from individual .vcf files in the sync folder
 * with category filtering and search.
 */
class ContactView : public QWidget
{
    Q_OBJECT

public:
    explicit ContactView(QWidget *parent = nullptr);
    ~ContactView() override = default;

    void loadFromPath(const QString &syncPath);
    void refresh();

private Q_SLOTS:
    void onContactSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void onSearchTextChanged(const QString &text);
    void onCategoryFilterChanged(const QString &category);
    void onManageCategories();

private:
    struct ContactItem {
        QString filePath;
        int recordId;
        QString firstName;
        QString lastName;
        QString displayName;  // FN field
        QString company;
        QString title;
        QString phone1, phone2, phone3, phone4, phone5;
        QStringList phoneLabels;  // "Work", "Home", "Mobile", etc.
        QString email;
        QString address, city, state, zip, country;
        QString custom1, custom2, custom3, custom4;
        QString note;
        QString category;
        bool isPrivate;
    };

    void setupUI();
    void loadContacts();
    void applyFilter();
    ContactItem parseVCard(const QString &filePath) const;
    QString unfoldVCardContent(const QString &content) const;
    QString phoneLabelForType(const QString &typeParam) const;
    QString buildDetailHtml(const ContactItem &contact) const;
    bool contactMatchesSearch(const ContactItem &contact, const QString &text) const;

    QToolBar *m_toolbar;
    CategoryFilterWidget *m_categoryFilter;
    CategoryManager *m_categoryManager;
    CategoryModel *m_categoryModel;

    QSplitter *m_splitter;
    QLineEdit *m_searchEdit;
    QListWidget *m_contactList;
    QTextEdit *m_detailsView;

    QString m_syncPath;
    QList<ContactItem> m_contacts;
    QHash<QListWidgetItem*, int> m_itemToIndex;
};

#endif // CONTACTVIEW_H
