#ifndef MEMOVIEW_H
#define MEMOVIEW_H

#include <QWidget>
#include <QHash>

class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QSplitter;
class QToolBar;
class QComboBox;
class QAction;
class CategoryManager;
class CategoryModel;
class CategoryFilterWidget;

/**
 * @brief Memo data browser view with editing capabilities
 *
 * Displays memos from the sync folder with category filtering,
 * and allows creating, editing, and deleting memos.
 */
class MemoView : public QWidget
{
    Q_OBJECT

public:
    explicit MemoView(QWidget *parent = nullptr);
    ~MemoView() override = default;

    void loadFromPath(const QString &syncPath);
    void refresh();

    /**
     * @brief Check if there are unsaved changes
     */
    bool hasUnsavedChanges() const;

    /**
     * @brief Save all unsaved changes
     * @return true if all saves succeeded
     */
    bool saveAll();

Q_SIGNALS:
    /**
     * @brief Emitted when a memo is modified
     */
    void memoModified();

    /**
     * @brief Emitted when memos are saved
     */
    void memosSaved();

private Q_SLOTS:
    void onMemoSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void onContentChanged();
    void onCategoryFilterChanged(const QString &category);
    void onManageCategories();

    void onNewMemo();
    void onDeleteMemo();
    void onSaveMemo();

private:
    struct MemoItem {
        QString filePath;
        int recordId;
        QString title;
        QString category;
        QString content;
        bool isPrivate;
        bool isDirty;
    };

    void setupUI();
    void loadMemos();
    void applyFilter();
    bool saveMemo(int index);
    void updateListItemText(int index);

    QString extractTitle(const QString &content) const;
    QString buildFileContent(const MemoItem &memo) const;
    int newRecordId() const;

    QToolBar *m_toolbar;
    QAction *m_newAction;
    QAction *m_deleteAction;
    QAction *m_saveAction;

    CategoryFilterWidget *m_categoryFilter;
    CategoryManager *m_categoryManager;
    CategoryModel *m_categoryModel;
    QComboBox *m_memoCategoryCombo;

    QSplitter *m_splitter;
    QListWidget *m_memoList;
    QTextEdit *m_contentView;

    QString m_syncPath;
    QList<MemoItem> m_memos;
    QHash<QListWidgetItem*, int> m_itemToIndex;  // Map list items to memo indices

    int m_currentMemoIndex;
    bool m_ignoreContentChanges;
};

#endif // MEMOVIEW_H
