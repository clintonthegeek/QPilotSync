#include "todoconduit.h"
#include "../../mappers/todomapper.h"
#include "../../palm/pilotrecord.h"
#include "../../palm/categoryinfo.h"
#include "../../palm/kpilotdevicelink.h"
#include "../localfilebackend.h"

#include <QDebug>

namespace Sync {

TodoConduit::TodoConduit(QObject *parent)
    : Conduit(parent)
{
}

void TodoConduit::loadCategories(SyncContext *context)
{
    if (m_categories) {
        delete m_categories;
        m_categories = nullptr;
    }

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        return;
    }

    m_categories = new CategoryInfo();

    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);

    if (context->deviceLink->readAppBlock(m_dbHandle, appInfoBuf, &appInfoSize)) {
        m_categories->parse(appInfoBuf, appInfoSize);
        emit logMessage("Loaded todo categories");
    }
}

QString TodoConduit::categoryName(int categoryIndex) const
{
    if (m_categories) {
        return m_categories->categoryName(categoryIndex);
    }
    return QString();
}

BackendRecord* TodoConduit::palmToBackend(PilotRecord *palmRecord,
                                           SyncContext *context)
{
    if (!palmRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Unpack Palm todo
    TodoMapper::Todo todo = TodoMapper::unpackTodo(palmRecord);

    // Convert to iCalendar VTODO
    QString catName = categoryName(todo.category);
    QString ical = TodoMapper::todoToICal(todo, catName);

    // Create backend record
    BackendRecord *record = new BackendRecord();
    record->data = ical.toUtf8();
    record->type = "todo";
    record->contentHash = LocalFileBackend::calculateHash(record->data);
    record->lastModified = QDateTime::currentDateTime();

    // Set display name from task description
    QString displayName = todo.description;
    if (displayName.isEmpty()) {
        displayName = "Task";
    }
    if (displayName.length() > 50) {
        displayName = displayName.left(50);
    }
    record->displayName = displayName;

    return record;
}

PilotRecord* TodoConduit::backendToPalm(BackendRecord *backendRecord,
                                         SyncContext *context)
{
    if (!backendRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Parse iCalendar content
    QString content = QString::fromUtf8(backendRecord->data);
    TodoMapper::Todo todo = TodoMapper::iCalToTodo(content);

    // Look up category index from name if available
    if (!todo.categoryName.isEmpty() && m_categories) {
        todo.category = m_categories->categoryIndex(todo.categoryName);
    }

    // Pack to Palm record
    PilotRecord *record = TodoMapper::packTodo(todo);

    return record;
}

bool TodoConduit::recordsEqual(PilotRecord *palm, BackendRecord *backend) const
{
    if (!palm || !backend) return false;

    // Unpack Palm todo
    TodoMapper::Todo palmTodo = TodoMapper::unpackTodo(palm);

    // Parse backend content
    QString backendContent = QString::fromUtf8(backend->data);
    TodoMapper::Todo backendTodo = TodoMapper::iCalToTodo(backendContent);

    // Compare key fields
    if (palmTodo.description != backendTodo.description) return false;
    if (palmTodo.isComplete != backendTodo.isComplete) return false;
    if (palmTodo.priority != backendTodo.priority) return false;

    // Compare due date (if both have one)
    if (!palmTodo.hasIndefiniteDue && !backendTodo.hasIndefiniteDue) {
        if (palmTodo.due.date() != backendTodo.due.date()) return false;
    } else if (palmTodo.hasIndefiniteDue != backendTodo.hasIndefiniteDue) {
        return false;
    }

    return true;
}

QString TodoConduit::palmRecordDescription(PilotRecord *record) const
{
    if (!record) return QString();

    TodoMapper::Todo todo = TodoMapper::unpackTodo(record);

    QString desc = todo.description;
    if (desc.isEmpty()) {
        desc = "<Untitled Task>";
    }

    // Add completion status
    if (todo.isComplete) {
        desc = "[x] " + desc;
    } else {
        desc = "[ ] " + desc;
    }

    return desc;
}

} // namespace Sync
