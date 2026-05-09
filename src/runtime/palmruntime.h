#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <memory>
#include <vector>

#include "palmrunresult.h"

class KPilotLink;
class KPilotDeviceLink;

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

namespace Kalburator::Shape {
    struct Shape;
}

namespace WildPalms {
    class IBackendPluginV2;
}

namespace WildPalms::FullSync { class CalendarCollection_WP; }

namespace WildPalms::Runtime {

class PalmDeviceAccess;

class PalmRuntime : public QObject {
    Q_OBJECT
public:
    explicit PalmRuntime(const QString &profilePath,
                         QObject *parent = nullptr);
    ~PalmRuntime() override;

    /// Open a Palm device on one of the supplied paths. Async — emits
    /// connectionComplete(true, "") on success or connectionComplete(false,
    /// error) on failure. Internally drives PalmDeviceAccess; on success,
    /// loads plugins + sets up engine via finishConnect().
    void connectDevice(const QStringList &devicePaths);

    /// Cancel an in-progress connect.
    void cancelConnect();

    void disconnectDevice();
    bool isDeviceConnected() const;

    /// Borrowed pointer to the underlying KPilotDeviceLink. Only valid after
    /// readyForSync(). Returns nullptr if not yet connected. KF6MainWindow uses
    /// this to read handshake info and to feed the legacy m_syncEngine.
    KPilotDeviceLink *deviceLink() const;

    QFuture<PalmRunResult> hotSync();
    QFuture<PalmRunResult> fullSync();
    QFuture<PalmRunResult> copyPalmToPC();
    QFuture<PalmRunResult> copyPCToPalm();
    QFuture<PalmRunResult> backup();
    QFuture<PalmRunResult> restore();

    QList<QString> enabledPluginIds() const;
    QList<Kalburator::Sync::SyncMapping> palmMappings() const;

    bool isRunning() const { return m_running; }

    // Replace the live mapping list. Caller must ensure isRunning() == false.
    // JSON shape is the same as Profile::syncMappingsJson() — array of objects
    // each round-trippable via syncMappingToJson()/syncMappingFromJson().
    void reloadMappings(const QJsonArray &json);

    // Non-owning. Caller must ensure the handler outlives this PalmRuntime
    // (or call setConflictHandler(nullptr) before destroying the handler).
    void setConflictHandler(Kalburator::Sync::QSyncCore::ConflictHandler *handler);
    Kalburator::Sync::QSyncCore::ConflictHandler *conflictHandlerForTest() const;

    // Test seams
    void setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>);
    void registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2>);
    // Phase Ia.5 Task 19 overload: explicitly declare the plugin's
    // native palm-side Shape so unified dispatchSync can compile a
    // Pipeline through registered TransformationRegistry edges.
    void registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2>,
                                const Kalburator::Shape::Shape &shape);
    void setMappingsForTest(QList<Kalburator::Sync::SyncMapping>);
    void registerBlobBackendForTest(const QString &id,
                                     std::unique_ptr<Kalburator::Sync::IBlobBackend> backend);
    // Phase Ia.5 Task 19 overload: optionally declare the adapter's
    // native Shape so the unified dispatchSync can compile a Pipeline
    // between source and target shapes. The default overload above
    // declares blob/blob, which matches no DomainPlugin and triggers
    // "no edge path" on cross-shape mappings.
    void registerBlobBackendForTest(const QString &id,
                                     std::unique_ptr<Kalburator::Sync::IBlobBackend> backend,
                                     const Kalburator::Shape::Shape &shape);
    void setLinkForTest(KPilotLink *link);

    /// Expose the CalendarCollection_WP for E2E tests to seed and inspect.
    WildPalms::FullSync::CalendarCollection_WP *calendarCollectionForTest() const
        { return m_calendarCollection.get(); }

    /// Borrowed reference to PalmRuntime's BackendRegistry. Lifetime ==
    /// PalmRuntime's. AccountController borrows this for provider-supplied
    /// backend registration; AC is constructed AFTER PalmRuntime in
    /// KF6MainWindow::loadProfile() and torn down BEFORE PalmRuntime in
    /// closeProfile() / loadProfile().
    Kalburator::Sync::BackendRegistry &backendRegistry() { return *m_registry; }

signals:
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void runLog(QString message);
    void runFinished(PalmRunResult);

    // M6b additions — replace DeviceSession's signal surface for KF6MainWindow.
    void connectionStarted();
    void connectionComplete(bool success, QString error);
    void readyForSync();         // emitted right after deviceConnected once
                                 // plugins are loaded and engine is ready
    void logMessage(QString message);
    // TODO(M6b/Task 5): wire the next three from PalmDeviceAccess /
    // KPilotDeviceLink once KF6MainWindow subscribes to PalmRuntime.
    void errorOccurred(QString error);
    void progressUpdated(int current, int total, QString message);
    void palmScreenMessage(QString message);

private:
    /// Run after PalmDeviceAccess emits connectionComplete(true, "").
    /// Loads plugins, registers backends, sets up default mappings if
    /// none exist, sets up the engine. Emits deviceConnected + readyForSync.
    void finishConnect();

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
    bool                                                         m_running = false;
    std::vector<std::unique_ptr<Kalburator::Sync::SyncBackend>>  m_ownedBackends;
    std::unique_ptr<WildPalms::FullSync::CalendarCollection_WP>  m_calendarCollection;
    QList<QObject*>                                              m_v2PluginObjects;
};

}  // namespace WildPalms::Runtime

#endif
