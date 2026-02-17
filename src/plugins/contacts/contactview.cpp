#include "contactview.h"
#include "widgets/common/categorymanager.h"
#include "widgets/common/categorymodel.h"
#include "widgets/common/categoryfilterwidget.h"
#include "widgets/dialogs/categoryeditordialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QToolBar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>

#include <KLocalizedString>

#include <algorithm>

ContactView::ContactView(QWidget *parent)
    : QWidget(parent)
    , m_toolbar(nullptr)
    , m_categoryFilter(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryModel(nullptr)
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
    layout->setContentsMargins(0, 0, 0, 0);

    // Toolbar
    m_toolbar = new QToolBar(this);

    m_toolbar->addSeparator();

    // Category filter
    m_categoryManager = new CategoryManager(QStringLiteral("contacts"), this);
    m_categoryModel = new CategoryModel(this);
    m_categoryModel->setManager(m_categoryManager);

    m_categoryFilter = new CategoryFilterWidget(this);
    m_categoryFilter->setModel(m_categoryModel);
    connect(m_categoryFilter, &CategoryFilterWidget::categoryChanged,
            this, &ContactView::onCategoryFilterChanged);
    connect(m_categoryFilter, &CategoryFilterWidget::manageRequested,
            this, &ContactView::onManageCategories);
    m_toolbar->addWidget(m_categoryFilter);

    layout->addWidget(m_toolbar);

    // Search bar
    QHBoxLayout *searchLayout = new QHBoxLayout();
    QLabel *searchLabel = new QLabel(i18n("Search:"));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(i18n("Filter by name, company, phone, email..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ContactView::onSearchTextChanged);
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchEdit);
    layout->addLayout(searchLayout);

    // Main splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_contactList = new QListWidget(this);
    connect(m_contactList, &QListWidget::currentItemChanged,
            this, &ContactView::onContactSelected);
    m_splitter->addWidget(m_contactList);

    m_detailsView = new QTextEdit(this);
    m_detailsView->setReadOnly(true);
    m_detailsView->setPlaceholderText(i18n("Select a contact to view details..."));
    m_splitter->addWidget(m_detailsView);

    m_splitter->setSizes({250, 550});
    layout->addWidget(m_splitter, 1);
}

void ContactView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;

    // Initialize category manager
    m_categoryManager->setBasePath(syncPath);
    m_categoryManager->load();
    m_categoryModel->reload();

    loadContacts();
}

void ContactView::refresh()
{
    loadContacts();
}

void ContactView::loadContacts()
{
    m_contactList->clear();
    m_contacts.clear();
    m_itemToIndex.clear();
    m_detailsView->clear();

    if (m_syncPath.isEmpty()) {
        m_contactList->addItem(i18n("No sync folder selected"));
        return;
    }

    QString contactsPath = m_syncPath + QStringLiteral("/contacts");
    QDir contactsDir(contactsPath);

    if (!contactsDir.exists()) {
        m_contactList->addItem(i18n("No contacts found"));
        return;
    }

    // Load all .vcf files from contacts directory
    QStringList filters;
    filters << QStringLiteral("*.vcf");
    QFileInfoList files = contactsDir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const QFileInfo &fileInfo : files) {
        ContactItem contact = parseVCard(fileInfo.filePath());
        if (!contact.displayName.isEmpty()) {
            m_contacts.append(contact);
        }
    }

    // Sort by display name
    std::sort(m_contacts.begin(), m_contacts.end(),
              [](const ContactItem &a, const ContactItem &b) {
                  return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
              });

    applyFilter();
}

void ContactView::applyFilter()
{
    m_contactList->clear();
    m_itemToIndex.clear();

    QString filter = m_categoryFilter->selectedCategory();
    bool showAll = m_categoryFilter->isAllSelected();
    QString searchText = m_searchEdit->text();

    for (int i = 0; i < m_contacts.size(); ++i) {
        const ContactItem &contact = m_contacts[i];

        // Category filter
        if (!showAll && contact.category != filter) {
            continue;
        }

        // Search filter
        if (!searchText.isEmpty() && !contactMatchesSearch(contact, searchText)) {
            continue;
        }

        QListWidgetItem *item = new QListWidgetItem(contact.displayName, m_contactList);
        m_itemToIndex[item] = i;
    }

    if (m_contactList->count() == 0) {
        m_contactList->addItem(i18n("No contacts found"));
    }
}

ContactView::ContactItem ContactView::parseVCard(const QString &filePath) const
{
    ContactItem contact;
    contact.filePath = filePath;
    contact.recordId = 0;
    contact.category = CategoryManager::unfiledCategoryName();
    contact.isPrivate = false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return contact;
    }

    QTextStream stream(&file);
    QString rawContent = stream.readAll();
    file.close();

    // Unfold continuation lines
    QString content = unfoldVCardContent(rawContent);

    // Split into lines
    QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);

    int phoneIndex = 0;

    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("BEGIN:")) ||
            line.startsWith(QStringLiteral("END:")) ||
            line.startsWith(QStringLiteral("VERSION:"))) {
            continue;
        }

        int colonPos = line.indexOf(QLatin1Char(':'));
        if (colonPos == -1) continue;

        QString propertyPart = line.left(colonPos);
        QString value = line.mid(colonPos + 1);

        // Split property into name and parameters
        QStringList propertyParts = propertyPart.split(QLatin1Char(';'));
        QString propertyName = propertyParts.first().toUpper();

        // Parse TYPE parameter
        QString typeParam;
        for (int i = 1; i < propertyParts.size(); i++) {
            if (propertyParts[i].toUpper().startsWith(QStringLiteral("TYPE="))) {
                typeParam = propertyParts[i].mid(5);
            }
        }

        if (propertyName == QStringLiteral("FN")) {
            contact.displayName = value;
        } else if (propertyName == QStringLiteral("N")) {
            // Structured name: Family;Given;Middle;Prefix;Suffix
            QStringList nameParts = value.split(QLatin1Char(';'));
            if (nameParts.size() > 0) contact.lastName = nameParts[0];
            if (nameParts.size() > 1) contact.firstName = nameParts[1];
        } else if (propertyName == QStringLiteral("ORG")) {
            contact.company = value;
        } else if (propertyName == QStringLiteral("TITLE")) {
            contact.title = value;
        } else if (propertyName == QStringLiteral("TEL")) {
            if (phoneIndex < 5) {
                QString label = phoneLabelForType(typeParam);
                switch (phoneIndex) {
                    case 0: contact.phone1 = value; break;
                    case 1: contact.phone2 = value; break;
                    case 2: contact.phone3 = value; break;
                    case 3: contact.phone4 = value; break;
                    case 4: contact.phone5 = value; break;
                }
                contact.phoneLabels.append(label);
                phoneIndex++;
            }
        } else if (propertyName == QStringLiteral("EMAIL")) {
            if (contact.email.isEmpty()) {
                contact.email = value;
            }
            // Also store as phone slot if there's room
            if (phoneIndex < 5) {
                switch (phoneIndex) {
                    case 0: contact.phone1 = value; break;
                    case 1: contact.phone2 = value; break;
                    case 2: contact.phone3 = value; break;
                    case 3: contact.phone4 = value; break;
                    case 4: contact.phone5 = value; break;
                }
                contact.phoneLabels.append(QStringLiteral("Email"));
                phoneIndex++;
            }
        } else if (propertyName == QStringLiteral("ADR")) {
            // ADR: PO;Ext;Street;City;State;ZIP;Country
            QStringList addrParts = value.split(QLatin1Char(';'));
            if (addrParts.size() > 2) contact.address = addrParts[2];
            if (addrParts.size() > 3) contact.city = addrParts[3];
            if (addrParts.size() > 4) contact.state = addrParts[4];
            if (addrParts.size() > 5) contact.zip = addrParts[5];
            if (addrParts.size() > 6) contact.country = addrParts[6];
        } else if (propertyName == QStringLiteral("NOTE")) {
            contact.note = value;
        } else if (propertyName == QStringLiteral("CATEGORIES")) {
            QStringList cats = value.split(QLatin1Char(','));
            if (!cats.isEmpty()) {
                contact.category = cats.first().trimmed();
            }
        } else if (propertyName == QStringLiteral("X-PALM-CUSTOM1")) {
            contact.custom1 = value;
        } else if (propertyName == QStringLiteral("X-PALM-CUSTOM2")) {
            contact.custom2 = value;
        } else if (propertyName == QStringLiteral("X-PALM-CUSTOM3")) {
            contact.custom3 = value;
        } else if (propertyName == QStringLiteral("X-PALM-CUSTOM4")) {
            contact.custom4 = value;
        } else if (propertyName == QStringLiteral("UID")) {
            if (value.startsWith(QStringLiteral("palm-"))) {
                bool ok;
                int id = value.mid(5).toInt(&ok);
                if (ok) contact.recordId = id;
            }
        }
    }

    // Fallback: if FN was empty, build from N fields
    if (contact.displayName.isEmpty()) {
        if (!contact.firstName.isEmpty() && !contact.lastName.isEmpty()) {
            contact.displayName = QStringLiteral("%1 %2").arg(contact.firstName, contact.lastName);
        } else if (!contact.firstName.isEmpty()) {
            contact.displayName = contact.firstName;
        } else if (!contact.lastName.isEmpty()) {
            contact.displayName = contact.lastName;
        } else if (!contact.company.isEmpty()) {
            contact.displayName = contact.company;
        }
    }

    return contact;
}

QString ContactView::unfoldVCardContent(const QString &content) const
{
    QString result = content;
    result.replace(QStringLiteral("\r\n "), QString());
    result.replace(QStringLiteral("\r\n\t"), QString());
    result.replace(QStringLiteral("\n "), QString());
    result.replace(QStringLiteral("\n\t"), QString());
    return result;
}

QString ContactView::phoneLabelForType(const QString &typeParam) const
{
    QString t = typeParam.toLower();
    if (t.contains(QStringLiteral("cell")) || t.contains(QStringLiteral("mobile"))) {
        return QStringLiteral("Mobile");
    }
    if (t.contains(QStringLiteral("fax"))) {
        return QStringLiteral("Fax");
    }
    if (t.contains(QStringLiteral("pager"))) {
        return QStringLiteral("Pager");
    }
    if (t.contains(QStringLiteral("home"))) {
        return QStringLiteral("Home");
    }
    if (t.contains(QStringLiteral("work"))) {
        return QStringLiteral("Work");
    }
    if (t.contains(QStringLiteral("pref")) || t.contains(QStringLiteral("main"))) {
        return QStringLiteral("Main");
    }
    return QStringLiteral("Other");
}

QString ContactView::buildDetailHtml(const ContactItem &contact) const
{
    QString html = QStringLiteral("<html><body style='font-family: sans-serif;'>");

    // Name header
    html += QStringLiteral("<h2>%1</h2>").arg(contact.displayName.toHtmlEscaped());

    // Company and title
    if (!contact.company.isEmpty() || !contact.title.isEmpty()) {
        html += QStringLiteral("<p style='color: #555;'>");
        if (!contact.title.isEmpty()) {
            html += contact.title.toHtmlEscaped();
            if (!contact.company.isEmpty()) {
                html += QStringLiteral(", ");
            }
        }
        if (!contact.company.isEmpty()) {
            html += contact.company.toHtmlEscaped();
        }
        html += QStringLiteral("</p>");
    }

    html += QStringLiteral("<hr>");

    // Phone numbers
    QStringList phones = {contact.phone1, contact.phone2, contact.phone3, contact.phone4, contact.phone5};
    bool hasPhone = false;
    for (int i = 0; i < phones.size(); i++) {
        if (!phones[i].isEmpty()) {
            if (!hasPhone) {
                html += QStringLiteral("<h3>Phone</h3>");
                hasPhone = true;
            }
            QString label = (i < contact.phoneLabels.size()) ? contact.phoneLabels[i] : QStringLiteral("Other");
            html += QStringLiteral("<p><b>%1:</b> %2</p>")
                         .arg(label.toHtmlEscaped(), phones[i].toHtmlEscaped());
        }
    }

    // Email (show separately if not already shown in phone slots)
    if (!contact.email.isEmpty()) {
        bool emailInPhones = false;
        for (int i = 0; i < contact.phoneLabels.size(); i++) {
            if (contact.phoneLabels[i] == QStringLiteral("Email")) {
                emailInPhones = true;
                break;
            }
        }
        if (!emailInPhones) {
            html += QStringLiteral("<h3>Email</h3>");
            html += QStringLiteral("<p>%1</p>").arg(contact.email.toHtmlEscaped());
        }
    }

    // Address
    if (!contact.address.isEmpty() || !contact.city.isEmpty() ||
        !contact.state.isEmpty() || !contact.zip.isEmpty() || !contact.country.isEmpty()) {
        html += QStringLiteral("<h3>Address</h3><p>");
        if (!contact.address.isEmpty()) {
            html += contact.address.toHtmlEscaped() + QStringLiteral("<br>");
        }
        QStringList cityLine;
        if (!contact.city.isEmpty()) cityLine << contact.city.toHtmlEscaped();
        if (!contact.state.isEmpty()) cityLine << contact.state.toHtmlEscaped();
        if (!contact.zip.isEmpty()) cityLine << contact.zip.toHtmlEscaped();
        if (!cityLine.isEmpty()) {
            html += cityLine.join(QStringLiteral(", ")) + QStringLiteral("<br>");
        }
        if (!contact.country.isEmpty()) {
            html += contact.country.toHtmlEscaped();
        }
        html += QStringLiteral("</p>");
    }

    // Custom fields
    QStringList customs = {contact.custom1, contact.custom2, contact.custom3, contact.custom4};
    bool hasCustom = false;
    for (int i = 0; i < customs.size(); i++) {
        if (!customs[i].isEmpty()) {
            if (!hasCustom) {
                html += QStringLiteral("<h3>Custom Fields</h3>");
                hasCustom = true;
            }
            html += QStringLiteral("<p><b>Custom %1:</b> %2</p>")
                         .arg(i + 1)
                         .arg(customs[i].toHtmlEscaped());
        }
    }

    // Notes
    if (!contact.note.isEmpty()) {
        html += QStringLiteral("<h3>Notes</h3>");
        html += QStringLiteral("<p>%1</p>").arg(contact.note.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    }

    // Category
    if (!contact.category.isEmpty()) {
        html += QStringLiteral("<hr><p style='color: #888;'><i>Category: %1</i></p>")
                     .arg(contact.category.toHtmlEscaped());
    }

    html += QStringLiteral("</body></html>");
    return html;
}

bool ContactView::contactMatchesSearch(const ContactItem &contact, const QString &text) const
{
    if (contact.displayName.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.firstName.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.lastName.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.company.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.email.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.phone1.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.phone2.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.phone3.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.phone4.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.phone5.contains(text, Qt::CaseInsensitive)) return true;
    if (contact.note.contains(text, Qt::CaseInsensitive)) return true;
    return false;
}

void ContactView::onContactSelected(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)

    if (!current || !m_itemToIndex.contains(current)) {
        m_detailsView->clear();
        return;
    }

    int index = m_itemToIndex[current];
    const ContactItem &contact = m_contacts[index];
    m_detailsView->setHtml(buildDetailHtml(contact));
}

void ContactView::onSearchTextChanged(const QString &text)
{
    Q_UNUSED(text)
    applyFilter();
}

void ContactView::onCategoryFilterChanged(const QString &category)
{
    Q_UNUSED(category)
    applyFilter();
}

void ContactView::onManageCategories()
{
    CategoryEditorDialog dialog(m_categoryManager, this);
    dialog.exec();

    m_categoryModel->reload();
}
