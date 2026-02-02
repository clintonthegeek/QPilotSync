#include "taskeditdialog.h"
#include "../common/categorymodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDateEdit>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>

#include <KLocalizedString>

TaskEditDialog::TaskEditDialog(QWidget *parent)
    : QDialog(parent)
    , m_summaryEdit(nullptr)
    , m_notesEdit(nullptr)
    , m_categoryCombo(nullptr)
    , m_prioritySpinBox(nullptr)
    , m_hasDueDateCheck(nullptr)
    , m_dueDateEdit(nullptr)
    , m_completeCheck(nullptr)
{
    setWindowTitle(i18n("New Task"));
    setupUI();
}

TaskEditDialog::TaskEditDialog(const TaskData &task, QWidget *parent)
    : QDialog(parent)
    , m_summaryEdit(nullptr)
    , m_notesEdit(nullptr)
    , m_categoryCombo(nullptr)
    , m_prioritySpinBox(nullptr)
    , m_hasDueDateCheck(nullptr)
    , m_dueDateEdit(nullptr)
    , m_completeCheck(nullptr)
    , m_originalData(task)
{
    setWindowTitle(i18n("Edit Task"));
    setupUI();
    populateFromData(task);
}

void TaskEditDialog::setupUI()
{
    setMinimumWidth(400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Form layout for main fields
    QFormLayout *formLayout = new QFormLayout;

    // Summary (required)
    m_summaryEdit = new QLineEdit(this);
    m_summaryEdit->setPlaceholderText(i18n("Enter task summary..."));
    formLayout->addRow(i18n("Summary:"), m_summaryEdit);

    // Category
    m_categoryCombo = new QComboBox(this);
    formLayout->addRow(i18n("Category:"), m_categoryCombo);

    // Priority (1-5)
    m_prioritySpinBox = new QSpinBox(this);
    m_prioritySpinBox->setRange(1, 5);
    m_prioritySpinBox->setValue(3);
    m_prioritySpinBox->setToolTip(i18n("1 = Highest priority, 5 = Lowest priority"));
    formLayout->addRow(i18n("Priority:"), m_prioritySpinBox);

    // Due date with checkbox
    QHBoxLayout *dueDateLayout = new QHBoxLayout;
    m_hasDueDateCheck = new QCheckBox(this);
    m_hasDueDateCheck->setToolTip(i18n("Check to set a due date"));
    connect(m_hasDueDateCheck, &QCheckBox::toggled,
            this, &TaskEditDialog::onDueDateToggled);
    dueDateLayout->addWidget(m_hasDueDateCheck);

    m_dueDateEdit = new QDateEdit(this);
    m_dueDateEdit->setCalendarPopup(true);
    m_dueDateEdit->setDate(QDate::currentDate());
    m_dueDateEdit->setEnabled(false);
    dueDateLayout->addWidget(m_dueDateEdit);
    dueDateLayout->addStretch();

    formLayout->addRow(i18n("Due Date:"), dueDateLayout);

    // Complete checkbox
    m_completeCheck = new QCheckBox(i18n("Task is complete"), this);
    formLayout->addRow(QString(), m_completeCheck);

    mainLayout->addLayout(formLayout);

    // Notes (multi-line)
    QGroupBox *notesGroup = new QGroupBox(i18n("Notes"), this);
    QVBoxLayout *notesLayout = new QVBoxLayout(notesGroup);
    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setPlaceholderText(i18n("Enter additional notes..."));
    m_notesEdit->setMinimumHeight(100);
    notesLayout->addWidget(m_notesEdit);
    mainLayout->addWidget(notesGroup, 1);

    // Dialog buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void TaskEditDialog::setCategoryModel(CategoryModel *model)
{
    m_categoryCombo->setModel(model);

    // Skip "All" - start from index 1 if available
    if (m_categoryCombo->count() > 1) {
        m_categoryCombo->setCurrentIndex(1);  // First actual category (Unfiled)
    }
}

TaskEditDialog::TaskData TaskEditDialog::taskData() const
{
    TaskData data;
    data.uid = m_originalData.uid;  // Preserve original UID
    data.summary = m_summaryEdit->text().trimmed();
    data.notes = m_notesEdit->toPlainText();

    // Get category, but skip "All" if selected
    QString category = m_categoryCombo->currentText();
    if (category == CategoryModel::allCategoriesText()) {
        category = CategoryModel::unfiledCategoryText();
    }
    data.category = category;

    data.priority = m_prioritySpinBox->value();
    data.hasDueDate = m_hasDueDateCheck->isChecked();
    data.dueDate = m_dueDateEdit->date();
    data.isComplete = m_completeCheck->isChecked();

    return data;
}

void TaskEditDialog::setTaskData(const TaskData &task)
{
    m_originalData = task;
    populateFromData(task);
}

void TaskEditDialog::populateFromData(const TaskData &task)
{
    m_summaryEdit->setText(task.summary);
    m_notesEdit->setPlainText(task.notes);

    // Find and select category
    int index = m_categoryCombo->findText(task.category);
    if (index >= 0) {
        m_categoryCombo->setCurrentIndex(index);
    }

    m_prioritySpinBox->setValue(task.priority);

    m_hasDueDateCheck->setChecked(task.hasDueDate);
    m_dueDateEdit->setEnabled(task.hasDueDate);
    if (task.hasDueDate && task.dueDate.isValid()) {
        m_dueDateEdit->setDate(task.dueDate);
    }

    m_completeCheck->setChecked(task.isComplete);
}

void TaskEditDialog::onDueDateToggled(bool checked)
{
    m_dueDateEdit->setEnabled(checked);
}
