#include "categoryeditordialog.h"
#include "../common/categorymanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialogButtonBox>

#include <KLocalizedString>

CategoryEditorDialog::CategoryEditorDialog(CategoryManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_categoryList(nullptr)
    , m_addButton(nullptr)
    , m_renameButton(nullptr)
    , m_deleteButton(nullptr)
    , m_manager(manager)
{
    setWindowTitle(i18n("Manage Categories"));
    setMinimumSize(350, 400);
    setupUI();
    refreshList();
}

void CategoryEditorDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Category list
    m_categoryList = new QListWidget(this);
    m_categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_categoryList, &QListWidget::itemSelectionChanged,
            this, &CategoryEditorDialog::onSelectionChanged);
    mainLayout->addWidget(m_categoryList, 1);

    // Buttons row
    QHBoxLayout *buttonLayout = new QHBoxLayout;

    m_addButton = new QPushButton(i18n("Add..."), this);
    connect(m_addButton, &QPushButton::clicked,
            this, &CategoryEditorDialog::onAddCategory);
    buttonLayout->addWidget(m_addButton);

    m_renameButton = new QPushButton(i18n("Rename..."), this);
    m_renameButton->setEnabled(false);
    connect(m_renameButton, &QPushButton::clicked,
            this, &CategoryEditorDialog::onRenameCategory);
    buttonLayout->addWidget(m_renameButton);

    m_deleteButton = new QPushButton(i18n("Delete"), this);
    m_deleteButton->setEnabled(false);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &CategoryEditorDialog::onDeleteCategory);
    buttonLayout->addWidget(m_deleteButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // Dialog buttons
    QDialogButtonBox *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(dialogButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(dialogButtons);
}

void CategoryEditorDialog::refreshList()
{
    m_categoryList->clear();

    if (!m_manager) {
        return;
    }

    QStringList categories = m_manager->categories();
    for (const QString &category : categories) {
        QListWidgetItem *item = new QListWidgetItem(category, m_categoryList);

        // Mark "Unfiled" as non-editable visually
        if (category == CategoryManager::unfiledCategoryName()) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
        }
    }

    onSelectionChanged();
}

void CategoryEditorDialog::onAddCategory()
{
    bool ok;
    QString name = QInputDialog::getText(this, i18n("Add Category"),
                                         i18n("Category name:"),
                                         QLineEdit::Normal, QString(), &ok);

    if (ok && !name.trimmed().isEmpty()) {
        if (m_manager->addCategory(name.trimmed())) {
            refreshList();
        } else {
            QMessageBox::warning(this, i18n("Error"),
                                 i18n("A category with this name already exists."));
        }
    }
}

void CategoryEditorDialog::onRenameCategory()
{
    QListWidgetItem *item = m_categoryList->currentItem();
    if (!item) {
        return;
    }

    QString oldName = item->text();
    if (oldName == CategoryManager::unfiledCategoryName()) {
        QMessageBox::information(this, i18n("Cannot Rename"),
                                 i18n("The 'Unfiled' category cannot be renamed."));
        return;
    }

    bool ok;
    QString newName = QInputDialog::getText(this, i18n("Rename Category"),
                                            i18n("New name:"),
                                            QLineEdit::Normal, oldName, &ok);

    if (ok && !newName.trimmed().isEmpty() && newName.trimmed() != oldName) {
        if (m_manager->renameCategory(oldName, newName.trimmed())) {
            refreshList();
        } else {
            QMessageBox::warning(this, i18n("Error"),
                                 i18n("A category with this name already exists."));
        }
    }
}

void CategoryEditorDialog::onDeleteCategory()
{
    QListWidgetItem *item = m_categoryList->currentItem();
    if (!item) {
        return;
    }

    QString name = item->text();
    if (name == CategoryManager::unfiledCategoryName()) {
        QMessageBox::information(this, i18n("Cannot Delete"),
                                 i18n("The 'Unfiled' category cannot be deleted."));
        return;
    }

    int result = QMessageBox::question(this, i18n("Delete Category"),
                                       i18n("Delete category '%1'?\n\nItems in this category will be moved to 'Unfiled'.", name),
                                       QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        m_manager->deleteCategory(name);
        refreshList();
    }
}

void CategoryEditorDialog::onSelectionChanged()
{
    QListWidgetItem *item = m_categoryList->currentItem();
    bool hasSelection = (item != nullptr);
    bool isUnfiled = hasSelection && (item->text() == CategoryManager::unfiledCategoryName());

    m_renameButton->setEnabled(hasSelection && !isUnfiled);
    m_deleteButton->setEnabled(hasSelection && !isUnfiled);
}
