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

TodoConduit::~TodoConduit()
{
    delete m_categories;
}

void TodoConduit::loadCategories(SyncContext *context)
{
    if (m_categories) {
        delete m_categories;
        m_categories = nullptr;
    }
    m_originalAppInfo.clear();

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        return;
    }

    m_categories = new CategoryInfo();

    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);

    if (context->deviceLink->readAppBlock(m_dbHandle, appInfoBuf, &appInfoSize)) {
        // Store original AppInfo block for later write-back
        m_originalAppInfo = QByteArray(reinterpret_cast<const char*>(appInfoBuf), appInfoSize);

        m_categories->parse(appInfoBuf, appInfoSize);
        emit logMessage(QString("Loaded %1 categories").arg(m_categories->usedCategories().size()));
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
    qDebug() << "[TodoConduit] Parsing iCal content, length:" << content.length();

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(content);
    qDebug() << "[TodoConduit] Parsed todo - description:" << todo.description
             << "priority:" << todo.priority
             << "complete:" << todo.isComplete
             << "hasDue:" << !todo.hasIndefiniteDue;

    // Look up or create category from name
    if (!todo.categoryName.isEmpty() && m_categories) {
        todo.category = m_categories->getOrCreateCategory(todo.categoryName);
        qDebug() << "[TodoConduit] Category" << todo.categoryName << "-> index" << todo.category;
    }

    // Pack to Palm record
    PilotRecord *record = TodoMapper::packTodo(todo);
    if (!record) {
        qWarning() << "[TodoConduit] packTodo() returned null!";
    }

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

bool TodoConduit::writeModifiedCategories(SyncContext *context)
{
    // Check if we have categories that were modified
    if (!m_categories || !m_categories->isDirty()) {
        return true;  // Nothing to write
    }

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        emit logMessage("Warning: Cannot write categories - no device connection");
        return false;
    }

    emit logMessage("Writing modified categories back to Palm...");

    // The AppInfo block contains more than just categories for ToDoDB
    // We need to preserve the app-specific portion and only update categories

    // For ToDoDB, the AppInfo structure is:
    //   - CategoryAppInfo_t (276 bytes typically)
    //   - App-specific data (dirty flag, sort order, etc.)

    size_t catSize = m_categories->packSize();

    if (m_originalAppInfo.isEmpty()) {
        // No original - just write categories
        QByteArray buffer(catSize, 0);
        int packed = m_categories->pack(reinterpret_cast<unsigned char*>(buffer.data()), buffer.size());
        if (packed < 0) {
            emit logMessage("Warning: Failed to pack categories");
            return false;
        }

        if (!context->deviceLink->writeAppBlock(m_dbHandle,
                reinterpret_cast<const unsigned char*>(buffer.constData()), packed)) {
            emit logMessage("Warning: Failed to write categories to Palm");
            return false;
        }
    } else {
        // We have original AppInfo - update category portion and preserve the rest
        QByteArray buffer = m_originalAppInfo;

        // Pack categories into the beginning of the buffer
        int packed = m_categories->pack(reinterpret_cast<unsigned char*>(buffer.data()),
                                         qMin(static_cast<size_t>(buffer.size()), catSize));
        if (packed < 0) {
            emit logMessage("Warning: Failed to pack categories");
            return false;
        }

        if (!context->deviceLink->writeAppBlock(m_dbHandle,
                reinterpret_cast<const unsigned char*>(buffer.constData()), buffer.size())) {
            emit logMessage("Warning: Failed to write AppInfo block to Palm");
            return false;
        }
    }

    m_categories->clearDirty();
    emit logMessage("Categories updated on Palm");
    return true;
}

} // namespace Sync
