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

void CalendarConduit::loadCategories(SyncContext *context)
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
        emit logMessage("Loaded calendar categories");
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

    return record;
}

PilotRecord* CalendarConduit::backendToPalm(BackendRecord *backendRecord,
                                             SyncContext *context)
{
    Q_UNUSED(context)

    if (!backendRecord) return nullptr;

    // Parse iCalendar content
    QString content = QString::fromUtf8(backendRecord->data);
    CalendarMapper::Event event = CalendarMapper::iCalToEvent(content);

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

} // namespace Sync
