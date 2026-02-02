#ifndef CONTACTVIEW_H
#define CONTACTVIEW_H

#include <QWidget>

class QListWidget;
class QTextEdit;
class QSplitter;
class QLineEdit;

/**
 * @brief Contact data browser view
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
    void onContactSelected(int index);
    void onSearchTextChanged(const QString &text);

private:
    void setupUI();
    void loadContacts();

    QSplitter *m_splitter;
    QLineEdit *m_searchEdit;
    QListWidget *m_contactList;
    QTextEdit *m_detailsView;

    QString m_syncPath;
    QStringList m_vcardData;  // Store raw vCard data
};

#endif // CONTACTVIEW_H
