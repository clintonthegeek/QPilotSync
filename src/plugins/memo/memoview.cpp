#include "memoview.h"
#include "widgets/common/categorymanager.h"
#include "widgets/common/categorymodel.h"
#include "widgets/common/categoryfilterwidget.h"
#include "widgets/dialogs/categoryeditordialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDateTime>

#include <KLocalizedString>

MemoView::MemoView(QWidget *parent)
    : QWidget(parent)
    , m_toolbar(nullptr)
    , m_newAction(nullptr)
    , m_deleteAction(nullptr)
    , m_saveAction(nullptr)
    , m_categoryFilter(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryModel(nullptr)
    , m_memoCategoryCombo(nullptr)
    , m_splitter(nullptr)
    , m_memoList(nullptr)
    , m_contentView(nullptr)
    , m_currentMemoIndex(-1)
    , m_ignoreContentChanges(false)
{
    setupUI();
}

void MemoView::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Toolbar
    m_toolbar = new QToolBar(this);

    m_newAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-new")),
                                       i18n("New Memo"));
    connect(m_newAction, &QAction::triggered, this, &MemoView::onNewMemo);

    m_deleteAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                          i18n("Delete"));
    m_deleteAction->setEnabled(false);
    connect(m_deleteAction, &QAction::triggered, this, &MemoView::onDeleteMemo);

    m_saveAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-save")),
                                        i18n("Save"));
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, &MemoView::onSaveMemo);

    m_toolbar->addSeparator();

    // Category filter
    m_categoryManager = new CategoryManager(QStringLiteral("memos"), this);
    m_categoryModel = new CategoryModel(this);
    m_categoryModel->setManager(m_categoryManager);

    m_categoryFilter = new CategoryFilterWidget(this);
    m_categoryFilter->setModel(m_categoryModel);
    connect(m_categoryFilter, &CategoryFilterWidget::categoryChanged,
            this, &MemoView::onCategoryFilterChanged);
    connect(m_categoryFilter, &CategoryFilterWidget::manageRequested,
            this, &MemoView::onManageCategories);
    m_toolbar->addWidget(m_categoryFilter);

    layout->addWidget(m_toolbar);

    // Header with memo category selector
    QHBoxLayout *headerLayout = new QHBoxLayout;
    headerLayout->addWidget(new QLabel(i18n("Memo Category:"), this));
    m_memoCategoryCombo = new QComboBox(this);
    m_memoCategoryCombo->setEnabled(false);
    connect(m_memoCategoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_currentMemoIndex >= 0 && !m_ignoreContentChanges) {
                    QString category = m_memoCategoryCombo->currentText();
                    if (category != CategoryModel::allCategoriesText()) {
                        m_memos[m_currentMemoIndex].category = category;
                        m_memos[m_currentMemoIndex].isDirty = true;
                        updateListItemText(m_currentMemoIndex);
                        m_saveAction->setEnabled(true);
                        Q_EMIT memoModified();
                    }
                }
            });
    headerLayout->addWidget(m_memoCategoryCombo);
    headerLayout->addStretch();
    layout->addLayout(headerLayout);

    // Main splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_memoList = new QListWidget(this);
    connect(m_memoList, &QListWidget::currentItemChanged,
            this, &MemoView::onMemoSelected);
    m_splitter->addWidget(m_memoList);

    m_contentView = new QTextEdit(this);
    m_contentView->setFont(QFont(QStringLiteral("monospace")));
    m_contentView->setPlaceholderText(i18n("Select a memo or create a new one..."));
    connect(m_contentView, &QTextEdit::textChanged,
            this, &MemoView::onContentChanged);
    m_splitter->addWidget(m_contentView);

    m_splitter->setSizes({250, 550});
    layout->addWidget(m_splitter, 1);
}

void MemoView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;

    // Initialize category manager
    m_categoryManager->setBasePath(syncPath);
    m_categoryManager->load();
    m_categoryModel->reload();

    // Set category combo model (skip "All" for editing)
    m_memoCategoryCombo->setModel(m_categoryModel);

    loadMemos();
}

void MemoView::refresh()
{
    loadMemos();
}

void MemoView::loadMemos()
{
    m_ignoreContentChanges = true;
    m_memoList->clear();
    m_memos.clear();
    m_itemToIndex.clear();
    m_contentView->clear();
    m_currentMemoIndex = -1;
    m_deleteAction->setEnabled(false);
    m_memoCategoryCombo->setEnabled(false);

    if (m_syncPath.isEmpty()) {
        m_memoList->addItem(i18n("No sync folder selected"));
        m_ignoreContentChanges = false;
        return;
    }

    QString memosPath = m_syncPath + QStringLiteral("/memos");
    QDir memosDir(memosPath);

    if (!memosDir.exists()) {
        // Create memos directory if it doesn't exist
        QDir().mkpath(memosPath);
    }

    // Load all .md and .txt files from memos directory
    QStringList filters;
    filters << QStringLiteral("*.md") << QStringLiteral("*.txt");
    QFileInfoList files = memosDir.entryInfoList(filters, QDir::Files, QDir::Name);

    // Regex for YAML frontmatter parsing
    static QRegularExpression frontmatterRe(
        QStringLiteral("^---\\n(.+?)\\n---\\n"),
        QRegularExpression::DotMatchesEverythingOption);

    static QRegularExpression idRe(QStringLiteral("^id:\\s*(\\d+)$"),
                                    QRegularExpression::MultilineOption);
    static QRegularExpression categoryRe(QStringLiteral("^category:\\s*(.+)$"),
                                          QRegularExpression::MultilineOption);
    static QRegularExpression privateRe(QStringLiteral("^private:\\s*true$"),
                                         QRegularExpression::MultilineOption);

    for (const QFileInfo &fileInfo : files) {
        QFile file(fileInfo.filePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream stream(&file);
        QString fullContent = stream.readAll();
        file.close();

        MemoItem memo;
        memo.filePath = fileInfo.filePath();
        memo.recordId = 0;
        memo.category = CategoryManager::unfiledCategoryName();
        memo.isPrivate = false;
        memo.isDirty = false;

        // Extract content after frontmatter
        QString content = fullContent;
        QRegularExpressionMatch fmMatch = frontmatterRe.match(fullContent);
        if (fmMatch.hasMatch()) {
            QString frontmatter = fmMatch.captured(1);
            content = fullContent.mid(fmMatch.capturedEnd()).trimmed();

            // Parse frontmatter
            QRegularExpressionMatch idMatch = idRe.match(frontmatter);
            if (idMatch.hasMatch()) {
                memo.recordId = idMatch.captured(1).toInt();
            }

            QRegularExpressionMatch catMatch = categoryRe.match(frontmatter);
            if (catMatch.hasMatch()) {
                memo.category = catMatch.captured(1).trimmed();
            }

            if (privateRe.match(frontmatter).hasMatch()) {
                memo.isPrivate = true;
            }
        }

        memo.content = content;
        memo.title = extractTitle(content);

        m_memos.append(memo);
    }

    applyFilter();
    m_ignoreContentChanges = false;
}

void MemoView::applyFilter()
{
    m_memoList->clear();
    m_itemToIndex.clear();

    QString filter = m_categoryFilter->selectedCategory();
    bool showAll = m_categoryFilter->isAllSelected();

    for (int i = 0; i < m_memos.size(); ++i) {
        const MemoItem &memo = m_memos[i];

        if (!showAll && memo.category != filter) {
            continue;
        }

        QString displayText = memo.title;
        if (memo.isDirty) {
            displayText += QStringLiteral(" *");
        }

        QListWidgetItem *item = new QListWidgetItem(displayText, m_memoList);
        m_itemToIndex[item] = i;
    }

    if (m_memoList->count() == 0) {
        m_memoList->addItem(i18n("No memos found"));
    }
}

void MemoView::onMemoSelected(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)

    if (!current || !m_itemToIndex.contains(current)) {
        m_currentMemoIndex = -1;
        m_contentView->clear();
        m_deleteAction->setEnabled(false);
        m_memoCategoryCombo->setEnabled(false);
        return;
    }

    m_ignoreContentChanges = true;

    m_currentMemoIndex = m_itemToIndex[current];
    const MemoItem &memo = m_memos[m_currentMemoIndex];

    m_contentView->setPlainText(memo.content);
    m_deleteAction->setEnabled(true);
    m_memoCategoryCombo->setEnabled(true);

    // Select current category in combo
    int catIndex = m_memoCategoryCombo->findText(memo.category);
    if (catIndex >= 0) {
        m_memoCategoryCombo->setCurrentIndex(catIndex);
    }

    m_saveAction->setEnabled(memo.isDirty);
    m_ignoreContentChanges = false;
}

void MemoView::onContentChanged()
{
    if (m_ignoreContentChanges || m_currentMemoIndex < 0) {
        return;
    }

    MemoItem &memo = m_memos[m_currentMemoIndex];
    QString newContent = m_contentView->toPlainText();

    if (newContent != memo.content) {
        memo.content = newContent;
        memo.title = extractTitle(newContent);
        memo.isDirty = true;
        updateListItemText(m_currentMemoIndex);
        m_saveAction->setEnabled(true);
        Q_EMIT memoModified();
    }
}

void MemoView::onCategoryFilterChanged(const QString &category)
{
    Q_UNUSED(category)
    applyFilter();
}

void MemoView::onManageCategories()
{
    CategoryEditorDialog dialog(m_categoryManager, this);
    dialog.exec();

    // Reload categories
    m_categoryModel->reload();
}

void MemoView::onNewMemo()
{
    MemoItem memo;
    memo.recordId = newRecordId();
    memo.category = m_categoryFilter->isAllSelected()
                        ? CategoryManager::unfiledCategoryName()
                        : m_categoryFilter->selectedCategory();
    memo.content = i18n("New Memo");
    memo.title = memo.content;
    memo.isPrivate = false;
    memo.isDirty = true;

    // Generate filename
    QString memosPath = m_syncPath + QStringLiteral("/memos");
    QDir().mkpath(memosPath);

    QString baseFilename = QStringLiteral("memo_%1.md").arg(memo.recordId);
    memo.filePath = memosPath + QStringLiteral("/") + baseFilename;

    m_memos.append(memo);

    // Save immediately to create file
    int newIndex = m_memos.size() - 1;
    saveMemo(newIndex);

    // Refresh and select new memo
    applyFilter();

    // Find and select the new item
    for (auto it = m_itemToIndex.begin(); it != m_itemToIndex.end(); ++it) {
        if (it.value() == newIndex) {
            m_memoList->setCurrentItem(it.key());
            m_contentView->setFocus();
            m_contentView->selectAll();
            break;
        }
    }

    Q_EMIT memoModified();
}

void MemoView::onDeleteMemo()
{
    if (m_currentMemoIndex < 0) {
        return;
    }

    const MemoItem &memo = m_memos[m_currentMemoIndex];

    int result = QMessageBox::question(this, i18n("Delete Memo"),
                                       i18n("Delete memo '%1'?", memo.title),
                                       QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    // Delete the file
    if (!memo.filePath.isEmpty()) {
        QFile::remove(memo.filePath);
    }

    // Remove from list
    m_memos.removeAt(m_currentMemoIndex);
    m_currentMemoIndex = -1;

    applyFilter();
    Q_EMIT memoModified();
}

void MemoView::onSaveMemo()
{
    if (m_currentMemoIndex >= 0 && m_memos[m_currentMemoIndex].isDirty) {
        saveMemo(m_currentMemoIndex);
        Q_EMIT memosSaved();
    }
}

bool MemoView::saveMemo(int index)
{
    if (index < 0 || index >= m_memos.size()) {
        return false;
    }

    MemoItem &memo = m_memos[index];

    // Ensure memos directory exists
    QString memosPath = m_syncPath + QStringLiteral("/memos");
    QDir().mkpath(memosPath);

    // Generate filename if needed
    if (memo.filePath.isEmpty()) {
        QString safeName = memo.title.left(50);
        static QRegularExpression invalidChars(QStringLiteral("[^a-zA-Z0-9_\\-. ]"));
        safeName.replace(invalidChars, QStringLiteral("_"));
        safeName = safeName.trimmed();
        if (safeName.isEmpty()) {
            safeName = QStringLiteral("memo_%1").arg(memo.recordId);
        }
        memo.filePath = memosPath + QStringLiteral("/") + safeName + QStringLiteral(".md");
    }

    // Build file content with YAML frontmatter
    QString fileContent = buildFileContent(memo);

    QFile file(memo.filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n("Error"),
                             i18n("Could not save memo: %1", file.errorString()));
        return false;
    }

    QTextStream stream(&file);
    stream << fileContent;
    file.close();

    memo.isDirty = false;
    updateListItemText(index);
    m_saveAction->setEnabled(false);

    return true;
}

void MemoView::updateListItemText(int index)
{
    // Find the list item for this memo index
    for (auto it = m_itemToIndex.begin(); it != m_itemToIndex.end(); ++it) {
        if (it.value() == index) {
            const MemoItem &memo = m_memos[index];
            QString displayText = memo.title;
            if (memo.isDirty) {
                displayText += QStringLiteral(" *");
            }
            it.key()->setText(displayText);
            break;
        }
    }
}

QString MemoView::extractTitle(const QString &content) const
{
    QString firstLine = content.split(QStringLiteral("\n")).first().trimmed();
    if (firstLine.isEmpty()) {
        return i18n("(Untitled)");
    }
    if (firstLine.length() > 50) {
        return firstLine.left(47) + QStringLiteral("...");
    }
    return firstLine;
}

QString MemoView::buildFileContent(const MemoItem &memo) const
{
    QString content;

    // YAML frontmatter
    content += QStringLiteral("---\n");
    content += QStringLiteral("id: %1\n").arg(memo.recordId);
    content += QStringLiteral("category: %1\n").arg(memo.category);
    content += QStringLiteral("modified: %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));

    if (memo.isPrivate) {
        content += QStringLiteral("private: true\n");
    }

    content += QStringLiteral("---\n\n");

    // Memo content
    content += memo.content;

    if (!content.endsWith(QStringLiteral("\n"))) {
        content += QStringLiteral("\n");
    }

    return content;
}

int MemoView::newRecordId() const
{
    int maxId = 0;
    for (const MemoItem &memo : m_memos) {
        if (memo.recordId > maxId) {
            maxId = memo.recordId;
        }
    }
    return maxId + 1;
}

bool MemoView::hasUnsavedChanges() const
{
    for (const MemoItem &memo : m_memos) {
        if (memo.isDirty) {
            return true;
        }
    }
    return false;
}

bool MemoView::saveAll()
{
    bool allSaved = true;
    for (int i = 0; i < m_memos.size(); ++i) {
        if (m_memos[i].isDirty) {
            if (!saveMemo(i)) {
                allSaved = false;
            }
        }
    }
    if (allSaved) {
        Q_EMIT memosSaved();
    }
    return allSaved;
}
