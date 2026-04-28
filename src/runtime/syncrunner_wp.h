#ifndef WILDPALMS_RUNTIME_SYNCRUNNER_WP_H
#define WILDPALMS_RUNTIME_SYNCRUNNER_WP_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>

#include "core/synctypes.h"

namespace Kalburator::Sync {
class IBlobBackend;
class ISyncHost;
}

class PalmDeviceConnection;

namespace WildPalms {
class BackendPluginManager;
}

namespace WildPalms::Runtime {

/**
 * @brief Single orchestrator that drives Tools-menu sync actions through
 *        the new IBackendPlugin ABI plus Kalburator::Sync::BlobSyncEngine.
 *
 * Replaces Sync::SyncEngine + Sync::Conduit (deleted in E.16). Each call
 * to run() iterates the loaded plugins, gets each plugin's blob backend,
 * pairs it with a per-plugin LocalBlobBackend rooted under syncPath, and
 * dispatches BlobSyncEngine::twoWayWithBaseline (or a one-way mirror for
 * Backup/Restore/CopyXxx modes).
 *
 * Threading: run() is synchronous. Callers that want it off the UI
 * thread (i.e. DeviceWorker on the Palm-link thread) post the call onto
 * that thread and consume signals.
 */
class SyncRunner : public QObject
{
    Q_OBJECT
public:
    /// Factory that builds the PC-side blob backend for one plugin id.
    /// Default constructs a Kalburator::Sync::LocalBlobBackend rooted
    /// at <syncPath>/<plugin-id>/. Tests inject a different factory to
    /// substitute MockBlobBackend (the V2 plugin tests' established
    /// pattern; production-grade cross-id-space pairing for
    /// LocalBlobBackend is still tracked in E.17/E.18).
    using LocalBackendFactory =
        std::function<std::unique_ptr<Kalburator::Sync::IBlobBackend>(const QString &rootPath,
                                                                       const QString &pluginId)>;

    SyncRunner(BackendPluginManager *plugins,
               PalmDeviceConnection *device,
               Kalburator::Sync::ISyncHost *host,
               QString syncPath,
               QString stateDir,
               QObject *parent = nullptr);

    ~SyncRunner() override;

    /// Test/test-fixture seam: replace the default factory with one
    /// that builds, e.g., MockBlobBackend instances.
    void setLocalBackendFactory(LocalBackendFactory factory);

    /// Run sync against `enabledPluginIds` (empty list = run against
    /// every loaded plugin). Emits started/progress/log/finished.
    /// Returns the aggregated result.
    Sync::SyncResult run(Sync::SyncMode mode, const QStringList &enabledPluginIds = {});

    /// Cooperative cancellation. Checked between plugins / between
    /// records. Safe to call from any thread.
    void requestCancel();
    bool isCancelled() const;
    void resetCancel();

    QString syncPath() const  { return m_syncPath; }
    QString stateDir() const  { return m_stateDir; }

signals:
    void started(int mode);
    void progress(int current, int total, const QString &message);
    void logMessage(const QString &message);
    void errorOccurred(const QString &message);
    void finished(const Sync::SyncResult &result);

private:
    Sync::SyncResult runTwoWay(Sync::SyncMode mode, const QStringList &pluginIds);
    Sync::SyncResult runMirror(Sync::SyncMode mode, const QStringList &pluginIds);
    Sync::SyncResult runBackup(const QStringList &pluginIds);
    Sync::SyncResult runRestore(const QStringList &pluginIds);

    QStringList resolvePluginIds(const QStringList &requested) const;
    QString     pluginLocalPath(const QString &pluginId) const;
    QString     baselineDbPath() const;
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
                makeLocalBackend(const QString &pluginId) const;

    BackendPluginManager          *m_plugins = nullptr;
    PalmDeviceConnection          *m_device  = nullptr;
    Kalburator::Sync::ISyncHost   *m_host    = nullptr;
    QString                        m_syncPath;
    QString                        m_stateDir;
    LocalBackendFactory            m_localBackendFactory;

    std::atomic<bool>              m_cancelled{false};
};

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_SYNCRUNNER_WP_H
