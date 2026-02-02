#ifndef TASKVIEW_H
#define TASKVIEW_H

#include <QWidget>
#include <QDate>

class QTableView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QToolBar;
class QAction;
class CategoryManager;
class CategoryModel;
class CategoryFilterWidget;

/**
 * @brief Task/Todo data browser view with editing capabilities
 *
 * Displays tasks from todos.ics with category filtering,
 * and allows creating, editing, and deleting tasks.
 */
class TaskView : public QWidget
{
    Q_OBJECT

public:
    explicit TaskView(QWidget *parent = nullptr);
    ~TaskView() override = default;

    void loadFromPath(const QString &syncPath);
    void refresh();

    /**
     * @brief Check if there are unsaved changes
     */
    bool hasUnsavedChanges() const;

    /**
     * @brief Save all unsaved changes
     * @return true if save succeeded
     */
    bool saveAll();

Q_SIGNALS:
    /**
     * @brief Emitted when tasks are modified
     */
    void tasksModified();

    /**
     * @brief Emitted when tasks are saved
     */
    void tasksSaved();

private Q_SLOTS:
    void onCategoryFilterChanged(const QString &category);
    void onManageCategories();
    void onItemDoubleClicked(const QModelIndex &index);
    void onSelectionChanged();

    void onNewTask();
    void onDeleteTask();
    void onToggleComplete();

private:
    struct TaskItem {
        QString filePath;   // Path to .ics file
        QString uid;
        int recordId;
        QString summary;
        QString notes;
        QString category;
        int priority;       // 1-5
        bool hasDueDate;
        QDate dueDate;
        bool isComplete;
        bool isDirty;
        bool isDeleted;     // Marked for deletion
    };

    // Column indices
    enum Column {
        ColComplete = 0,
        ColPriority,
        ColSummary,
        ColCategory,
        ColDueDate,
        ColCount
    };

    void setupUI();
    void loadTasks();
    void applyFilter();
    void populateModel();
    bool saveToFile();

    void updateRowFromTask(int row, const TaskItem &task);
    int findTaskByUid(const QString &uid) const;
    QString newUid() const;
    int newRecordId() const;

    QToolBar *m_toolbar;
    QAction *m_newAction;
    QAction *m_deleteAction;
    QAction *m_toggleCompleteAction;

    CategoryFilterWidget *m_categoryFilter;
    CategoryManager *m_categoryManager;
    CategoryModel *m_categoryModel;

    QTableView *m_tableView;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxyModel;

    QString m_syncPath;
    QList<TaskItem> m_tasks;
    bool m_isDirty;
};

#endif // TASKVIEW_H
