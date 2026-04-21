#include "synchost_wp.h"

#include "calendarcollection_wp.h"
#include "syncconfigstore_wp.h"

namespace WildPalms::FullSync {

SyncHost_WP::SyncHost_WP(CalendarCollection_WP *collection, SyncConfigStore_WP *configStore)
    : m_collection(collection)
    , m_configStore(configStore)
{
}

SyncHost_WP::~SyncHost_WP() = default;

void SyncHost_WP::registerBackend(const QString &id, Kalburator::Sync::SyncBackend *backend)
{
    if (id.isEmpty() || !backend) {
        return;
    }
    m_backends.insert(id, backend);
}

Kalburator::Sync::SyncBackend* SyncHost_WP::backendById(const QString &id)
{
    return m_backends.value(id, nullptr);
}

QHash<QString, Kalburator::Sync::SyncBackend*> SyncHost_WP::backends()
{
    return m_backends;
}

bool SyncHost_WP::applyIncidenceAddition(const QString &calendarId,
                                         const KCalendarCore::Incidence::Ptr &inc,
                                         bool stageForSync)
{
    Q_UNUSED(calendarId);
    Q_UNUSED(inc);
    Q_UNUSED(stageForSync);
    ++m_applyAdditionCount;
    return true;
}

bool SyncHost_WP::applyIncidenceRemoval(const QString &calendarId,
                                        const QString &uid,
                                        bool stageForSync,
                                        const QDateTime &recurrenceId)
{
    Q_UNUSED(calendarId);
    Q_UNUSED(uid);
    Q_UNUSED(stageForSync);
    Q_UNUSED(recurrenceId);
    ++m_applyRemovalCount;
    return true;
}

bool SyncHost_WP::applyIncidenceUpdate(const QString &calendarId,
                                       const KCalendarCore::Incidence::Ptr &inc,
                                       bool stageForSync)
{
    Q_UNUSED(calendarId);
    Q_UNUSED(inc);
    Q_UNUSED(stageForSync);
    ++m_applyUpdateCount;
    return true;
}

Kalburator::Sync::ICalendarCollection* SyncHost_WP::collection()
{
    return m_collection;
}

Kalburator::Sync::IIncidenceSource* SyncHost_WP::incidenceSource()
{
    return nullptr;
}

Kalburator::Sync::IIncidenceRegistry* SyncHost_WP::incidenceRegistry()
{
    return nullptr;
}

Kalburator::Sync::ISyncConfigStore* SyncHost_WP::configStore()
{
    return m_configStore;
}

void SyncHost_WP::unloadCalendar(const QString &calendarId)
{
    Q_UNUSED(calendarId);
    ++m_unloadCount;
}

void SyncHost_WP::generateSyncMappingsFromLogicalCalendars()
{
    ++m_regenerateMappingsCount;
}

} // namespace WildPalms::FullSync
