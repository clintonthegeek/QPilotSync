#include "hotsynccoordinator.h"

#include "backendregistry.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "synctypes.h"
#include "palm/palmdeviceconnection.h"

namespace WildPalms::Runtime {

HotSyncCoordinator::HotSyncCoordinator(
    Kalburator::Sync::SyncEngine    *engine,
    Kalburator::Sync::BackendRegistry *registry,
    PalmDeviceConnection            *device,
    QObject                         *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_registry(registry)
    , m_device(device)
{
    connect(device, &PalmDeviceConnection::connected,
            this, &HotSyncCoordinator::onDeviceConnected);
    connect(device, &PalmDeviceConnection::disconnected,
            this, &HotSyncCoordinator::onDeviceDisconnected);
}

QStringList HotSyncCoordinator::mappingsForPalmDevice()
{
    static constexpr auto kPalmPrefix = "palm-device:";
    QStringList ids;
    m_activeResource.clear();

    for (const Kalburator::Sync::SyncMapping &mapping : m_engine->syncMappings()) {
        auto *src = m_registry->backendInstance(mapping.sourceBackend);
        auto *tgt = m_registry->backendInstance(mapping.targetBackend);

        const bool srcPalm = src && src->resourceId().startsWith(QLatin1String(kPalmPrefix));
        const bool tgtPalm = tgt && tgt->resourceId().startsWith(QLatin1String(kPalmPrefix));

        if (srcPalm || tgtPalm) {
            ids.append(mapping.id);
            if (m_activeResource.isEmpty())
                m_activeResource = srcPalm ? src->resourceId() : tgt->resourceId();
        }
    }
    return ids;
}

void HotSyncCoordinator::onDeviceConnected()
{
    if (m_currentFuture && !m_currentFuture->isFinished())
        return;

    const QStringList ids = mappingsForPalmDevice();
    if (ids.isEmpty())
        return;

    using namespace Kalburator::Sync;
    m_currentFuture.emplace(SyncEngineFuture(m_engine->runSyncFuture(ids)));
    emit syncStarted();
}

void HotSyncCoordinator::onDeviceDisconnected()
{
    if (m_currentFuture && !m_currentFuture->isFinished()) {
        m_currentFuture->cancelWithReason(
            Kalburator::Sync::CancellationReason::ResourceLost,
            m_activeResource);
        emit syncCancelled();
    }
}

} // namespace WildPalms::Runtime
