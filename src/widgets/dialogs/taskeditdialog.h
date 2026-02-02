#ifndef TASKEDITDIALOG_H
#define TASKEDITDIALOG_H

#include <QDialog>
#include <QDate>

class QLineEdit;
class QTextEdit;
class QComboBox;
class QSpinBox;
class QDateEdit;
class QCheckBox;
class CategoryModel;

/**
 * @brief Dialog for creating and editing tasks
 *
 * Provides form fields for all task properties: summary, notes,
 * category, priority (1-5), due date, and completion status.
 */
class TaskEditDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Task data structure for dialog
     */
    struct TaskData {
        QString uid;           // Unique ID (empty for new tasks)
        QString summary;       // Task title
        QString notes;         // Detailed notes
        QString category;      // Category name
        int priority = 3;      // 1 (highest) to 5 (lowest)
        bool hasDueDate = false;
        QDate dueDate;
        bool isComplete = false;
    };

    /**
     * @brief Create dialog for a new task
     */
    explicit TaskEditDialog(QWidget *parent = nullptr);

    /**
     * @brief Create dialog for editing an existing task
     */
    explicit TaskEditDialog(const TaskData &task, QWidget *parent = nullptr);

    ~TaskEditDialog() override = default;

    /**
     * @brief Set the category model for the category combo box
     * @param model Category model (ownership not transferred)
     */
    void setCategoryModel(CategoryModel *model);

    /**
     * @brief Get the task data from the dialog
     * @return TaskData with current field values
     */
    TaskData taskData() const;

    /**
     * @brief Set task data in the dialog
     */
    void setTaskData(const TaskData &task);

private Q_SLOTS:
    void onDueDateToggled(bool checked);

private:
    void setupUI();
    void populateFromData(const TaskData &task);

    QLineEdit *m_summaryEdit;
    QTextEdit *m_notesEdit;
    QComboBox *m_categoryCombo;
    QSpinBox *m_prioritySpinBox;
    QCheckBox *m_hasDueDateCheck;
    QDateEdit *m_dueDateEdit;
    QCheckBox *m_completeCheck;

    TaskData m_originalData;
};

#endif // TASKEDITDIALOG_H
