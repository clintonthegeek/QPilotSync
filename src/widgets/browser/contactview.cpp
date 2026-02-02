#include "contactview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <KLocalizedString>

ContactView::ContactView(QWidget *parent)
    : QWidget(parent)
    , m_splitter(nullptr)
    , m_searchEdit(nullptr)
    , m_contactList(nullptr)
    , m_detailsView(nullptr)
{
    setupUI();
}

void ContactView::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *titleLabel = new QLabel(i18n("<h3>Contacts</h3>"));
    layout->addWidget(titleLabel);

    // Search bar
    QHBoxLayout *searchLayout = new QHBoxLayout();
    QLabel *searchLabel = new QLabel(i18n("Search:"));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(i18n("Type to filter contacts..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ContactView::onSearchTextChanged);
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchEdit);
    layout->addLayout(searchLayout);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_contactList = new QListWidget(this);
    connect(m_contactList, &QListWidget::currentRowChanged, this, &ContactView::onContactSelected);
    m_splitter->addWidget(m_contactList);

    m_detailsView = new QTextEdit(this);
    m_detailsView->setReadOnly(true);
    m_splitter->addWidget(m_detailsView);

    m_splitter->setSizes({300, 500});

    layout->addWidget(m_splitter);
}

void ContactView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;
    loadContacts();
}

void ContactView::refresh()
{
    loadContacts();
}

void ContactView::loadContacts()
{
    m_contactList->clear();
    m_vcardData.clear();
    m_detailsView->clear();

    if (m_syncPath.isEmpty()) {
        m_contactList->addItem(i18n("No sync folder selected"));
        return;
    }

    QString contactsPath = m_syncPath + QStringLiteral("/contacts.vcf");
    QFile file(contactsPath);

    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_contactList->addItem(i18n("No contacts found"));
        return;
    }

    QTextStream stream(&file);
    QString currentCard;
    QString currentName;

    while (!stream.atEnd()) {
        QString line = stream.readLine();
        currentCard += line + QStringLiteral("\n");

        if (line.startsWith(QStringLiteral("FN:"))) {
            currentName = line.mid(3);
        }

        if (line == QStringLiteral("END:VCARD")) {
            if (!currentName.isEmpty()) {
                m_contactList->addItem(currentName);
                m_vcardData.append(currentCard);
            }
            currentCard.clear();
            currentName.clear();
        }
    }

    file.close();
}

void ContactView::onContactSelected(int index)
{
    if (index < 0 || index >= m_vcardData.size()) {
        m_detailsView->clear();
        return;
    }

    // Parse and display vCard details
    QString vcard = m_vcardData.at(index);
    QString html = QStringLiteral("<html><body>");

    QStringList lines = vcard.split(QStringLiteral("\n"));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("FN:"))) {
            html += QStringLiteral("<h3>%1</h3>").arg(line.mid(3));
        } else if (line.startsWith(QStringLiteral("TEL"))) {
            int colonPos = line.indexOf(QStringLiteral(":"));
            if (colonPos > 0) {
                html += QStringLiteral("<p><b>Phone:</b> %1</p>").arg(line.mid(colonPos + 1));
            }
        } else if (line.startsWith(QStringLiteral("EMAIL"))) {
            int colonPos = line.indexOf(QStringLiteral(":"));
            if (colonPos > 0) {
                html += QStringLiteral("<p><b>Email:</b> %1</p>").arg(line.mid(colonPos + 1));
            }
        } else if (line.startsWith(QStringLiteral("ADR"))) {
            int colonPos = line.indexOf(QStringLiteral(":"));
            if (colonPos > 0) {
                QString addr = line.mid(colonPos + 1).replace(QStringLiteral(";"), QStringLiteral(", "));
                html += QStringLiteral("<p><b>Address:</b> %1</p>").arg(addr);
            }
        } else if (line.startsWith(QStringLiteral("ORG:"))) {
            html += QStringLiteral("<p><b>Organization:</b> %1</p>").arg(line.mid(4));
        } else if (line.startsWith(QStringLiteral("TITLE:"))) {
            html += QStringLiteral("<p><b>Title:</b> %1</p>").arg(line.mid(6));
        }
    }

    html += QStringLiteral("</body></html>");
    m_detailsView->setHtml(html);
}

void ContactView::onSearchTextChanged(const QString &text)
{
    for (int i = 0; i < m_contactList->count(); ++i) {
        QListWidgetItem *item = m_contactList->item(i);
        bool matches = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}
