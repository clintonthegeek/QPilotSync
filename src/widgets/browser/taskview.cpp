#include "taskview.h"
#include "../common/categorymanager.h"
#include "../common/categorymodel.h"
#include "../common/categoryfilterwidget.h"
#include "../dialogs/categoryeditordialog.h"
#include "../dialogs/taskeditdialog.h"

#include <QVBoxLayout>
#include <QTableView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QToolBar>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QUuid>
#include <QRegularExpression>

#include <algorithm>

#include <KLocalizedString>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Todo>

TaskView::TaskView(QWidget *parent)
    : QWidget(parent)
    , m_toolbar(nullptr)
    , m_newAction(nullptr)
    , m_deleteAction(nullptr)
    , m_toggleCompleteAction(nullptr)
    , m_categoryFilter(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryModel(nullptr)
    , m_tableView(nullptr)
    , m_model(nullptr)
    , m_proxyModel(nullptr)
    , m_isDirty(false)
{
    setupUI();
}

void TaskView::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Toolbar
    m_toolbar = new QToolBar(this);

    m_newAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                       i18n("New Task"));
    connect(m_newAction, &QAction::triggered, this, &TaskView::onNewTask);

    m_deleteAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                          i18n("Delete"));
    m_deleteAction->setEnabled(false);
    connect(m_deleteAction, &QAction::triggered, this, &TaskView::onDeleteTask);

    m_toggleCompleteAction = m_toolbar->addAction(
        QIcon::fromTheme(QStringLiteral("checkbox")),
        i18n("Toggle Done"));
    m_toggleCompleteAction->setEnabled(false);
    connect(m_toggleCompleteAction, &QAction::triggered,
            this, &TaskView::onToggleComplete);

    m_toolbar->addSeparator();

    // Category filter
    m_categoryManager = new CategoryManager(QStringLiteral("todos"), this);
    m_categoryModel = new CategoryModel(this);
    m_categoryModel->setManager(m_categoryManager);

    m_categoryFilter = new CategoryFilterWidget(this);
    m_categoryFilter->setModel(m_categoryModel);
    connect(m_categoryFilter, &CategoryFilterWidget::categoryChanged,
            this, &TaskView::onCategoryFilterChanged);
    connect(m_categoryFilter, &CategoryFilterWidget::manageRequested,
            this, &TaskView::onManageCategories);
    m_toolbar->addWidget(m_categoryFilter);

    layout->addWidget(m_toolbar);

    // Model setup
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({
        QString(),  // Checkbox column (no header text)
        i18n("Pri"),
        i18n("Task"),
        i18n("Category"),
        i18n("Due Date")
    });

    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterKeyColumn(ColCategory);

    // Table view
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->verticalHeader()->setVisible(false);

    // Column widths
    m_tableView->horizontalHeader()->setSectionResizeMode(ColComplete, QHeaderView::Fixed);
    m_tableView->horizontalHeader()->setSectionResizeMode(ColPriority, QHeaderView::Fixed);
    m_tableView->horizontalHeader()->setSectionResizeMode(ColSummary, QHeaderView::Stretch);
    m_tableView->horizontalHeader()->setSectionResizeMode(ColCategory, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(ColDueDate, QHeaderView::ResizeToContents);
    m_tableView->setColumnWidth(ColComplete, 30);
    m_tableView->setColumnWidth(ColPriority, 40);

    connect(m_tableView, &QTableView::doubleClicked,
            this, &TaskView::onItemDoubleClicked);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TaskView::onSelectionChanged);

    layout->addWidget(m_tableView, 1);
}

void TaskView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;

    // Initialize category manager
    m_categoryManager->setBasePath(syncPath);
    m_categoryManager->load();
    m_categoryModel->reload();

    loadTasks();
}

void TaskView::refresh()
{
    loadTasks();
}

void TaskView::loadTasks()
{
    m_tasks.clear();
    m_isDirty = false;
    m_deleteAction->setEnabled(false);
    m_toggleCompleteAction->setEnabled(false);

    if (m_syncPath.isEmpty()) {
        populateModel();
        return;
    }

    // Todos are stored as individual .ics files in the todos/ directory
    QString todosPath = m_syncPath + QStringLiteral("/todos");
    QDir todosDir(todosPath);

    if (!todosDir.exists()) {
        // Create todos directory if it doesn't exist
        QDir().mkpath(todosPath);
        populateModel();
        return;
    }

    // Load all .ics files from todos directory
    QStringList filters;
    filters << QStringLiteral("*.ics");
    QFileInfoList files = todosDir.entryInfoList(filters, QDir::Files, QDir::Name);

    KCalendarCore::ICalFormat format;

    for (const QFileInfo &fileInfo : files) {
        KCalendarCore::MemoryCalendar::Ptr calendar(
            new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

        if (!format.load(calendar, fileInfo.filePath())) {
            continue;
        }

        KCalendarCore::Todo::List todos = calendar->todos();
        for (const KCalendarCore::Todo::Ptr &todo : todos) {
            TaskItem task;
            task.filePath = fileInfo.filePath();
            task.uid = todo->uid();
            task.summary = todo->summary();
            task.notes = todo->description();

            // Convert iCal priority (1-9) to Palm priority (1-5)
            // iCal 1-2->Palm 1, 3-4->Palm 2, 5-6->Palm 3, 7-8->Palm 4, 9->Palm 5
            int icalPriority = todo->priority();
            if (icalPriority >= 1 && icalPriority <= 9) {
                task.priority = (icalPriority + 1) / 2;
                if (task.priority > 5) task.priority = 5;
                if (task.priority < 1) task.priority = 1;
            } else {
                task.priority = 3;  // Default middle priority
            }

            task.isComplete = todo->isCompleted();
            task.isDirty = false;
            task.isDeleted = false;
            task.recordId = 0;

            // Parse record ID from custom property if present
            QString recordIdStr = todo->customProperty("QPILOTSYNC", "RECORD_ID");
            if (!recordIdStr.isEmpty()) {
                task.recordId = recordIdStr.toInt();
            }

            // Category
            QStringList categories = todo->categories();
            task.category = categories.isEmpty()
                                ? CategoryManager::unfiledCategoryName()
                                : categories.first();

            // Due date
            task.hasDueDate = todo->hasDueDate();
            if (task.hasDueDate) {
                task.dueDate = todo->dtDue().date();
            }

            m_tasks.append(task);
        }
    }

    populateModel();
}

void TaskView::populateModel()
{
    m_model->removeRows(0, m_model->rowCount());

    QString filter = m_categoryFilter->selectedCategory();
    bool showAll = m_categoryFilter->isAllSelected();

    for (int i = 0; i < m_tasks.size(); ++i) {
        const TaskItem &task = m_tasks[i];

        // Apply category filter
        if (!showAll && task.category != filter) {
            continue;
        }

        QList<QStandardItem*> row;

        // Checkbox column
        QStandardItem *checkItem = new QStandardItem();
        checkItem->setCheckable(true);
        checkItem->setCheckState(task.isComplete ? Qt::Checked : Qt::Unchecked);
        checkItem->setData(task.uid, Qt::UserRole);  // Store UID for lookup
        checkItem->setData(i, Qt::UserRole + 1);     // Store index
        row.append(checkItem);

        // Priority
        QStandardItem *priItem = new QStandardItem(QString::number(task.priority));
        priItem->setTextAlignment(Qt::AlignCenter);
        row.append(priItem);

        // Summary
        QString summary = task.summary;
        if (task.isDirty) {
            summary += QStringLiteral(" *");
        }
        row.append(new QStandardItem(summary));

        // Category
        row.append(new QStandardItem(task.category));

        // Due date
        QString dueStr;
        if (task.hasDueDate) {
            dueStr = task.dueDate.toString(Qt::ISODate);
        }
        row.append(new QStandardItem(dueStr));

        m_model->appendRow(row);
    }
}

void TaskView::applyFilter()
{
    populateModel();
}

void TaskView::onCategoryFilterChanged(const QString &category)
{
    Q_UNUSED(category)
    applyFilter();
}

void TaskView::onManageCategories()
{
    CategoryEditorDialog dialog(m_categoryManager, this);
    dialog.exec();

    // Reload categories
    m_categoryModel->reload();
}

void TaskView::onItemDoubleClicked(const QModelIndex &proxyIndex)
{
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return;
    }

    // Get task index from first column's user data
    QStandardItem *item = m_model->item(sourceIndex.row(), ColComplete);
    if (!item) {
        return;
    }

    int taskIndex = item->data(Qt::UserRole + 1).toInt();
    if (taskIndex < 0 || taskIndex >= m_tasks.size()) {
        return;
    }

    TaskItem &task = m_tasks[taskIndex];

    // Populate dialog data
    TaskEditDialog::TaskData dialogData;
    dialogData.uid = task.uid;
    dialogData.summary = task.summary;
    dialogData.notes = task.notes;
    dialogData.category = task.category;
    dialogData.priority = task.priority;
    dialogData.hasDueDate = task.hasDueDate;
    dialogData.dueDate = task.dueDate;
    dialogData.isComplete = task.isComplete;

    TaskEditDialog dialog(dialogData, this);
    dialog.setCategoryModel(m_categoryModel);

    if (dialog.exec() == QDialog::Accepted) {
        TaskEditDialog::TaskData result = dialog.taskData();

        task.summary = result.summary;
        task.notes = result.notes;
        task.category = result.category;
        task.priority = result.priority;
        task.hasDueDate = result.hasDueDate;
        task.dueDate = result.dueDate;
        task.isComplete = result.isComplete;
        task.isDirty = true;
        m_isDirty = true;

        populateModel();
        Q_EMIT tasksModified();
    }
}

void TaskView::onSelectionChanged()
{
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    bool hasSelection = !selected.isEmpty();

    m_deleteAction->setEnabled(hasSelection);
    m_toggleCompleteAction->setEnabled(hasSelection);
}

void TaskView::onNewTask()
{
    TaskEditDialog dialog(this);
    dialog.setCategoryModel(m_categoryModel);

    if (dialog.exec() == QDialog::Accepted) {
        TaskEditDialog::TaskData result = dialog.taskData();

        TaskItem task;
        task.filePath = QString();  // Will be generated on save
        task.uid = newUid();
        task.recordId = 0;  // 0 = new record, will be assigned by Palm on sync
        task.summary = result.summary;
        task.notes = result.notes;
        task.category = result.category.isEmpty()
                            ? CategoryManager::unfiledCategoryName()
                            : result.category;
        task.priority = result.priority;
        task.hasDueDate = result.hasDueDate;
        task.dueDate = result.dueDate;
        task.isComplete = result.isComplete;
        task.isDirty = true;
        task.isDeleted = false;

        m_tasks.append(task);
        m_isDirty = true;

        // Save immediately to create the file
        saveToFile();

        populateModel();
        Q_EMIT tasksModified();
    }
}

void TaskView::onDeleteTask()
{
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    QModelIndex sourceIndex = m_proxyModel->mapToSource(selected.first());
    QStandardItem *item = m_model->item(sourceIndex.row(), ColComplete);
    if (!item) {
        return;
    }

    int taskIndex = item->data(Qt::UserRole + 1).toInt();
    if (taskIndex < 0 || taskIndex >= m_tasks.size()) {
        return;
    }

    TaskItem &task = m_tasks[taskIndex];

    int result = QMessageBox::question(this, i18n("Delete Task"),
                                       i18n("Delete task '%1'?", task.summary),
                                       QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    // Delete the file immediately if it exists
    if (!task.filePath.isEmpty()) {
        QFile::remove(task.filePath);
    }

    // Remove from list
    m_tasks.removeAt(taskIndex);
    m_isDirty = true;

    populateModel();
    Q_EMIT tasksModified();
}

void TaskView::onToggleComplete()
{
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    QModelIndex sourceIndex = m_proxyModel->mapToSource(selected.first());
    QStandardItem *item = m_model->item(sourceIndex.row(), ColComplete);
    if (!item) {
        return;
    }

    int taskIndex = item->data(Qt::UserRole + 1).toInt();
    if (taskIndex < 0 || taskIndex >= m_tasks.size()) {
        return;
    }

    TaskItem &task = m_tasks[taskIndex];
    task.isComplete = !task.isComplete;
    task.isDirty = true;
    m_isDirty = true;

    // Update the checkbox in the model
    item->setCheckState(task.isComplete ? Qt::Checked : Qt::Unchecked);

    Q_EMIT tasksModified();
}

bool TaskView::saveToFile()
{
    if (m_syncPath.isEmpty()) {
        return false;
    }

    QString todosPath = m_syncPath + QStringLiteral("/todos");
    QDir().mkpath(todosPath);

    KCalendarCore::ICalFormat format;
    bool allSuccess = true;

    for (TaskItem &task : m_tasks) {
        // Handle deleted tasks
        if (task.isDeleted) {
            if (!task.filePath.isEmpty()) {
                QFile::remove(task.filePath);
            }
            continue;
        }

        // Only save dirty tasks
        if (!task.isDirty && !task.filePath.isEmpty()) {
            continue;
        }

        // Create calendar with single todo
        KCalendarCore::MemoryCalendar::Ptr calendar(
            new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

        KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);
        todo->setUid(task.uid);
        todo->setSummary(task.summary);
        todo->setDescription(task.notes);

        // Convert Palm priority (1-5) to iCal priority (1-9)
        // Palm 1->iCal 1, Palm 2->iCal 3, Palm 3->iCal 5, Palm 4->iCal 7, Palm 5->iCal 9
        int icalPriority = (task.priority - 1) * 2 + 1;
        todo->setPriority(icalPriority);

        if (!task.category.isEmpty() && task.category != CategoryManager::unfiledCategoryName()) {
            todo->setCategories({task.category});
        }

        if (task.hasDueDate) {
            QDateTime dtDue(task.dueDate, QTime(0, 0), QTimeZone::systemTimeZone());
            todo->setDtDue(dtDue);
        }

        if (task.isComplete) {
            todo->setCompleted(true);
        }

        // Store record ID as custom property (0 for new records)
        todo->setCustomProperty("QPILOTSYNC", "RECORD_ID",
                                 QString::number(task.recordId));

        calendar->addTodo(todo);

        // Generate filename if needed
        if (task.filePath.isEmpty()) {
            QString safeName = task.summary.left(50);
            static QRegularExpression invalidChars(QStringLiteral("[/\\\\:*?\"<>|]"));
            safeName.replace(invalidChars, QStringLiteral("_"));
            safeName = safeName.trimmed();
            if (safeName.isEmpty()) {
                safeName = QStringLiteral("todo_%1").arg(task.uid.left(8));
            }

            // Ensure unique filename
            QString basePath = todosPath + QStringLiteral("/") + safeName;
            task.filePath = basePath + QStringLiteral(".ics");
            int counter = 1;
            while (QFile::exists(task.filePath)) {
                task.filePath = basePath + QStringLiteral("_%1.ics").arg(counter++);
            }
        }

        if (!format.save(calendar, task.filePath)) {
            QMessageBox::warning(this, i18n("Error"),
                                 i18n("Could not save task: %1", task.summary));
            allSuccess = false;
            continue;
        }

        task.isDirty = false;
    }

    // Remove deleted tasks from list
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                  [](const TaskItem &t) { return t.isDeleted; }),
                   m_tasks.end());

    m_isDirty = false;
    return allSuccess;
}

int TaskView::findTaskByUid(const QString &uid) const
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].uid == uid) {
            return i;
        }
    }
    return -1;
}

QString TaskView::newUid() const
{
    return QStringLiteral("palm-todo-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int TaskView::newRecordId() const
{
    int maxId = 0;
    for (const TaskItem &task : m_tasks) {
        if (task.recordId > maxId) {
            maxId = task.recordId;
        }
    }
    return maxId + 1;
}

bool TaskView::hasUnsavedChanges() const
{
    return m_isDirty;
}

bool TaskView::saveAll()
{
    if (m_isDirty) {
        if (saveToFile()) {
            populateModel();  // Refresh to remove dirty markers
            Q_EMIT tasksSaved();
            return true;
        }
        return false;
    }
    return true;
}
