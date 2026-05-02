#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
#include <QList>
#include <QString>
#include <memory>

#include "palmrunresult.h"

class KPilotLink;

namespace Kalburator::Sync {
    class SyncEngine;
    class BackendRegistry;
    class ISyncHost;
    struct SyncMapping;
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

    // Test seams
    void setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>);
    void registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2>);
    void setMappingsForTest(QList<Kalburator::Sync::SyncMapping>);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void runLog(QString message);
    void runFinished(PalmRunResult);

private:
    QString                                                      m_profilePath;
    std::unique_ptr<PalmDeviceAccess>                            m_device;
    std::unique_ptr<Kalburator::Sync::BackendRegistry>           m_registry;
    std::unique_ptr<Kalburator::Sync::SyncEngine>                m_engine;
    QList<std::shared_ptr<WildPalms::IBackendPluginV2>>          m_plugins;
    QList<Kalburator::Sync::SyncMapping>                         m_mappings;
};

}  // namespace WildPalms::Runtime

#endif
