#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <memory>
#include <vector>

namespace Kalburator { class PluginManager; class Plugin; }

#include "palmrunresult.h"

class KPilotLink;
class KPilotDeviceLink;

namespace Kalburator::Sync {
    class BackendRegistry;
    class ISyncHost;
    struct SyncMapping;
    class SyncBackend;
}

namespace Kalburator::Engine {
    class SyncEngine;
}

namespace Kalburator::Conflict {
    class ConflictHandler;
}

namespace Kalburator::Storage {
    class BaselineStore;
}

namespace Kalburator::Shape {
    struct Shape;
}

// K.8b T13: IBackendPluginV2 forward-decl dropped — the V2 plugin ABI is
// gone. registerPluginForTest overloads removed below (they had no live
// callers after K.8b T6 turned them into no-ops).

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
    /// Load the five static Palm plugins via PluginManager::loadInProcess().
    /// Called from the constructor; replaces the old KPluginMetaData
    /// .so discovery loop in finishConnect().
    void registerPalmPlugins();

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

    /// Read-only view of the loaded Palm plugin instances.
    /// Valid after registerPalmPlugins() (called from the constructor).
    const std::vector<std::unique_ptr<Kalburator::Plugin>> &palmPlugins() const
        { return m_palmPlugins; }

    // Replace the live mapping list. Caller must ensure isRunning() == false.
    // JSON shape is the same as Profile::syncMappingsJson() — array of objects
    // each round-trippable via syncMappingToJson()/syncMappingFromJson().
    void reloadMappings(const QJsonArray &json);

    // Non-owning. Caller must ensure the handler outlives this PalmRuntime
    // (or call setConflictHandler(nullptr) before destroying the handler).
    void setConflictHandler(Kalburator::Conflict::ConflictHandler *handler);
    Kalburator::Conflict::ConflictHandler *conflictHandlerForTest() const;

    // Test seams
    void setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>);
    // K.8b T13: registerPluginForTest(IBackendPluginV2) overloads removed
    // along with the V2 plugin ABI (the bodies were no-ops since K.8b T6).
    void setMappingsForTest(QList<Kalburator::Sync::SyncMapping>);
    // K.8b T7: BlobBackendAdapter deleted; tests inject SyncBackend directly.
    void registerBackendInstanceForTest(const QString &id,
                                        std::unique_ptr<Kalburator::Sync::SyncBackend> backend);
    void setLinkForTest(KPilotLink *link);

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

    Kalburator::Conflict::ConflictHandler                *m_conflictHandler = nullptr;
    QString                                                      m_profilePath;
    QString                                                      m_backupRoot;
    KPilotLink                                                  *m_link = nullptr;
    std::unique_ptr<PalmDeviceAccess>                            m_device;
    std::unique_ptr<Kalburator::Sync::BackendRegistry>           m_registry;
    std::unique_ptr<Kalburator::Sync::ISyncHost>                 m_syncHost;
    std::unique_ptr<Kalburator::Engine::SyncEngine>              m_engine;
    std::unique_ptr<Kalburator::Storage::BaselineStore>          m_baselineStore;
    // K.8b T6: PluginManager + owned plugin instances for the five static
    // Palm plugins. m_pluginManager MUST be declared before m_palmPlugins
    // (C++ destructs in reverse declaration order — plugins destruct before
    // manager, which is the correct teardown sequence).
    std::unique_ptr<Kalburator::PluginManager>                   m_pluginManager;
    std::vector<std::unique_ptr<Kalburator::Plugin>>             m_palmPlugins;
    QList<Kalburator::Sync::SyncMapping>                         m_mappings;
    bool                                                         m_running = false;
    std::vector<std::unique_ptr<Kalburator::Sync::SyncBackend>>  m_ownedBackends;
};

}  // namespace WildPalms::Runtime

#endif
