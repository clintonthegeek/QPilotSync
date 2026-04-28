#ifndef WILDPALMS_RUNTIME_SYNCHOST_WP_H
#define WILDPALMS_RUNTIME_SYNCHOST_WP_H

#include <QDateTime>
#include <QHash>
#include <QString>
#include <KCalendarCore/Incidence>

#include <isynchost.h>

namespace Kalburator::Sync {
class SyncBackend;
class IIncidenceSource;
class IIncidenceRegistry;
}

namespace WildPalms::FullSync {

class CalendarCollection_WP;
class SyncConfigStore_WP;

// Phase D impl of Kalburator::Sync::ISyncHost. Phase F replaces the
// applyIncidence* counters with real dispatch into WP's calendar model
// and swaps the nullptr incidenceSource/Registry for concrete adapters.
class SyncHost_WP : public Kalburator::Sync::ISyncHost
{
public:
    SyncHost_WP(CalendarCollection_WP *collection, SyncConfigStore_WP *configStore);
    ~SyncHost_WP() override;

    void registerBackend(const QString &id, Kalburator::Sync::SyncBackend *backend);

    // Kalburator::Sync::ISyncHost
    Kalburator::Sync::SyncBackend* backendById(const QString &id) override;
    QHash<QString, Kalburator::Sync::SyncBackend*> backends() override;

    bool applyIncidenceAddition(const QString &calendarId,
                                const KCalendarCore::Incidence::Ptr &inc,
                                bool stageForSync = true) override;
    bool applyIncidenceRemoval(const QString &calendarId,
                               const QString &uid,
                               bool stageForSync = true,
                               const QDateTime &recurrenceId = {}) override;
    bool applyIncidenceUpdate(const QString &calendarId,
                              const KCalendarCore::Incidence::Ptr &inc,
                              bool stageForSync = true) override;

    Kalburator::Sync::ICalendarCollection* collection() override;
    Kalburator::Sync::IIncidenceSource* incidenceSource() override;
    Kalburator::Sync::IIncidenceRegistry* incidenceRegistry() override;
    Kalburator::Sync::ISyncConfigStore* configStore() override;

    void unloadCalendar(const QString &calendarId) override;
    void generateSyncMappingsFromLogicalCalendars() override;

    // Phase D counters — used by tests to verify dispatch happened.
    int applyAdditionCount() const { return m_applyAdditionCount; }
    int applyRemovalCount() const { return m_applyRemovalCount; }
    int applyUpdateCount() const { return m_applyUpdateCount; }
    int unloadCount() const { return m_unloadCount; }
    int regenerateMappingsCount() const { return m_regenerateMappingsCount; }

private:
    CalendarCollection_WP *m_collection;
    SyncConfigStore_WP *m_configStore;
    QHash<QString, Kalburator::Sync::SyncBackend*> m_backends;

    int m_applyAdditionCount = 0;
    int m_applyRemovalCount = 0;
    int m_applyUpdateCount = 0;
    int m_unloadCount = 0;
    int m_regenerateMappingsCount = 0;
};

} // namespace WildPalms::FullSync

#endif
