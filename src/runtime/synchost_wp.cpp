#include "synchost_wp.h"

#include "syncconfigstore_wp.h"

namespace WildPalms::FullSync {

SyncHost_WP::SyncHost_WP(SyncConfigStore_WP *configStore)
    : m_configStore(configStore)
{
}

SyncHost_WP::~SyncHost_WP() = default;

void SyncHost_WP::registerBackend(const QString &id, Kalburator::Sync::SyncBackend *backend)
{
    if (id.isEmpty() || !backend)
        return;
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

Kalburator::Sync::ISyncConfigStore* SyncHost_WP::configStore()
{
    return m_configStore;
}

void SyncHost_WP::recordChanged(const QString &mappingId,
                                const QString &recordId,
                                Kalburator::Sync::ISyncHost::ChangeKind kind)
{
    Q_UNUSED(mappingId)
    Q_UNUSED(recordId)
    Q_UNUSED(kind)
    ++m_recordChangedCount;
}

} // namespace WildPalms::FullSync
