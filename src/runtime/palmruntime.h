#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
#include <QList>
#include <QString>
#include <memory>
#include <vector>

#include "palmrunresult.h"

class KPilotLink;

namespace Kalburator::Sync {
    class SyncEngine;
    class BackendRegistry;
    class ISyncHost;
    struct SyncMapping;
    class IBlobBackend;
    class BlobBaselineStore;
    class SyncBackend;
    namespace QSyncCore { class ConflictHandler; }
}

namespace WildPalms {
    class IBackendPluginV2;
}

namespace WildPalms::Runtime {

class PalmDeviceAccess;

class PalmRuntime : public QObject {
    Q_OBJECT
public:
    explicit PalmRuntime(const QString &profilePath,
                         QObject *parent = nullptr);
    ~PalmRuntime() override;

    void connectDevice(KPilotLink *link);
    void disconnectDevice();
    bool isDeviceConnected() const;

    QFuture<PalmRunResult> hotSync();
    QFuture<PalmRunResult> fullSync();
    QFuture<PalmRunResult> copyPalmToPC();
    QFuture<PalmRunResult> copyPCToPalm();
    QFuture<PalmRunResult> backup();
    QFuture<PalmRunResult> restore();

    QList<QString> enabledPluginIds() const;
    QList<Kalburator::Sync::SyncMapping> palmMappings() const;

    // Non-owning. Caller must ensure the handler outlives this PalmRuntime
    // (or call setConflictHandler(nullptr) before destroying the handler).
    void setConflictHandler(Kalburator::Sync::QSyncCore::ConflictHandler *handler);
    Kalburator::Sync::QSyncCore::ConflictHandler *conflictHandlerForTest() const;

    // Test seams
    void setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>);
    void registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2>);
    void setMappingsForTest(QList<Kalburator::Sync::SyncMapping>);
    void registerBlobBackendForTest(const QString &id,
                                     std::unique_ptr<Kalburator::Sync::IBlobBackend> backend);
    void setLinkForTest(KPilotLink *link);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void runLog(QString message);
    void runFinished(PalmRunResult);

private:
    // Mirror direction — local enum avoids pulling synctypes.h into this header.
    enum class MirrorDir { PalmToPC, PCToPalm };

    QFuture<PalmRunResult> runAllMappings();
    QFuture<PalmRunResult> runMirror(MirrorDir dir, const QString &modeLabel);

    Kalburator::Sync::QSyncCore::ConflictHandler                *m_conflictHandler = nullptr;
    QString                                                      m_profilePath;
    QString                                                      m_backupRoot;
    KPilotLink                                                  *m_link = nullptr;
    std::unique_ptr<PalmDeviceAccess>                            m_device;
    std::unique_ptr<Kalburator::Sync::BackendRegistry>           m_registry;
    std::unique_ptr<Kalburator::Sync::ISyncHost>                 m_syncHost;
    std::unique_ptr<Kalburator::Sync::SyncEngine>                m_engine;
    std::unique_ptr<Kalburator::Sync::BlobBaselineStore>         m_baselineStore;
    QList<std::shared_ptr<WildPalms::IBackendPluginV2>>          m_plugins;
    QList<Kalburator::Sync::SyncMapping>                         m_mappings;
    std::vector<std::unique_ptr<Kalburator::Sync::SyncBackend>>  m_ownedBackends;
    QList<QObject*>                                              m_v2PluginObjects;
};

}  // namespace WildPalms::Runtime

#endif
