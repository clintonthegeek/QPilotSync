#ifndef WILDPALMS_RUNTIME_HOTSYNCCOORDINATOR_H
#define WILDPALMS_RUNTIME_HOTSYNCCOORDINATOR_H

#include <QObject>
#include <QStringList>
#include <optional>

#include "syncenginefuture.h"

namespace Kalburator::Sync {
class SyncEngine;
class BackendRegistry;
}

class PalmDeviceConnection;

namespace WildPalms::Runtime {

/**
 * @brief Replaces SyncRunner_wp: reacts to device connection events and
 *        delegates to SyncEngine via runSyncFuture().
 *
 * On deviceConnected, queries all configured SyncMappings for those whose
 * backends report a "palm-device:" resourceId, then fires runSyncFuture()
 * for exactly those mappings. On deviceDisconnected, cancels the in-flight
 * future with CancellationReason::ResourceLost.
 *
 * G.7 Task 48-49 (2026-05-01).
 */
class HotSyncCoordinator : public QObject
{
    Q_OBJECT
public:
    HotSyncCoordinator(Kalburator::Sync::SyncEngine *engine,
                       Kalburator::Sync::BackendRegistry *registry,
                       PalmDeviceConnection *device,
                       QObject *parent = nullptr);

signals:
    void syncStarted();
    void syncFinished(bool success);
    void syncCancelled();

private slots:
    void onDeviceConnected();
    void onDeviceDisconnected();

private:
    QStringList mappingsForPalmDevice();

    Kalburator::Sync::SyncEngine              *m_engine;
    Kalburator::Sync::BackendRegistry         *m_registry;
    PalmDeviceConnection                      *m_device;
    std::optional<Kalburator::Sync::SyncEngineFuture> m_currentFuture;
    QString                                    m_activeResource;
};

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_HOTSYNCCOORDINATOR_H
