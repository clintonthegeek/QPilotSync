#include "calendarconduit.h"
#include "../../mappers/calendarmapper.h"
#include "../../palm/pilotrecord.h"
#include "../../palm/categoryinfo.h"
#include "../../palm/kpilotdevicelink.h"
#include "../localfilebackend.h"

#include <QDebug>

namespace Sync {

CalendarConduit::CalendarConduit(QObject *parent)
    : Conduit(parent)
{
}

CalendarConduit::~CalendarConduit()
{
    delete m_categories;
}

void CalendarConduit::loadCategories(SyncContext *context)
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

QString CalendarConduit::categoryName(int categoryIndex) const
{
    if (m_categories) {
        return m_categories->categoryName(categoryIndex);
    }
    return QString();
}

BackendRecord* CalendarConduit::palmToBackend(PilotRecord *palmRecord,
                                               SyncContext *context)
{
    if (!palmRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Unpack Palm event
    CalendarMapper::Event event = CalendarMapper::unpackEvent(palmRecord);

    // Convert to iCalendar
    QString catName = categoryName(event.category);
    QString ical = CalendarMapper::eventToICal(event, catName);

    // Create backend record
    BackendRecord *record = new BackendRecord();
    record->data = ical.toUtf8();
    record->type = "event";
    record->contentHash = LocalFileBackend::calculateHash(record->data);
    record->lastModified = QDateTime::currentDateTime();

    // Set display name: "Title YYYY-MM-DD" or just date if no title
    QString displayName = event.description;
    if (displayName.isEmpty()) {
        displayName = "Event";
    }
    if (displayName.length() > 40) {
        displayName = displayName.left(40);
    }
    if (event.begin.isValid()) {
        displayName += " " + event.begin.toString("yyyy-MM-dd");
    }
    record->displayName = displayName;

    return record;
}

PilotRecord* CalendarConduit::backendToPalm(BackendRecord *backendRecord,
                                             SyncContext *context)
{
    if (!backendRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Parse iCalendar content
    QString content = QString::fromUtf8(backendRecord->data);
    CalendarMapper::Event event = CalendarMapper::iCalToEvent(content);

    // Look up or create category from name
    if (!event.categoryName.isEmpty() && m_categories) {
        event.category = m_categories->getOrCreateCategory(event.categoryName);
        qDebug() << "[CalendarConduit] Category" << event.categoryName << "-> index" << event.category;
    }

    // Pack to Palm record
    PilotRecord *record = CalendarMapper::packEvent(event);

    return record;
}

bool CalendarConduit::recordsEqual(PilotRecord *palm, BackendRecord *backend) const
{
    if (!palm || !backend) return false;

    // Unpack Palm event
    CalendarMapper::Event palmEvent = CalendarMapper::unpackEvent(palm);

    // Parse backend content
    QString backendContent = QString::fromUtf8(backend->data);
    CalendarMapper::Event backendEvent = CalendarMapper::iCalToEvent(backendContent);

    // Compare key fields
    if (palmEvent.description != backendEvent.description) return false;
    if (palmEvent.begin != backendEvent.begin) return false;
    if (palmEvent.end != backendEvent.end) return false;
    if (palmEvent.isUntimed != backendEvent.isUntimed) return false;

    return true;
}

QString CalendarConduit::palmRecordDescription(PilotRecord *record) const
{
    if (!record) return QString();

    CalendarMapper::Event event = CalendarMapper::unpackEvent(record);

    QString desc = event.description;
    if (desc.isEmpty()) {
        desc = "<Untitled Event>";
    }

    // Add date info
    if (event.begin.isValid()) {
        desc += QString(" (%1)").arg(event.begin.toString("yyyy-MM-dd"));
    }

    return desc;
}

bool CalendarConduit::writeModifiedCategories(SyncContext *context)
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
