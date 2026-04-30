#include "syncrunner_wp.h"
#include "pilotlinkconnectionfactory.h"

#include <QDir>
#include <QFileInfo>

#include <syncengine.h>
#include <iblobbackend.h>
#include <localblobbackend.h>
#include <conflicthandlerregistry.h>
#include <conflictpolicy.h>
#include <conflictstore.h>
#include <blobbaselinestore.h>
#include <backendrecord.h>
#include <collectioninfo.h>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "runtime/backendpluginmanager.h"

namespace WildPalms::Runtime {

namespace {

QString sanitizeForPath(const QString &id)
{
    QString out = id;
    for (QChar &c : out) {
        if (c == QLatin1Char('/') || c == QLatin1Char(':') || c == QLatin1Char('\\')) {
            c = QLatin1Char('_');
        }
    }
    return out;
}

// Build a ConflictPolicy whose autoResolve drives SyncEngine's blob
// facade to a deterministic resolution. The engine treats the first
// IBlobBackend argument to runBlobTwoWay as the "source" side; we
// therefore feed Palm-as-source consistently in this runner.
Kalburator::Sync::QSyncCore::ConflictPolicy policyFor(Sync::SyncMode mode)
{
    using Kalburator::Sync::QSyncCore::AutoResolveStrategy;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    switch (mode) {
    case Sync::SyncMode::CopyPalmToPC:
        return ConflictPolicy::autoSourceWins();
    case Sync::SyncMode::CopyPCToPalm:
        return ConflictPolicy::autoTargetWins();
    case Sync::SyncMode::HotSync:
    case Sync::SyncMode::FullSync:
    default: {
        ConflictPolicy p;
        p.autoResolve = AutoResolveStrategy::NewerWins;
        return p;
    }
    }
}

QString modeLabel(Sync::SyncMode mode)
{
    switch (mode) {
    case Sync::SyncMode::HotSync:      return QStringLiteral("HotSync");
    case Sync::SyncMode::FullSync:     return QStringLiteral("FullSync");
    case Sync::SyncMode::CopyPalmToPC: return QStringLiteral("CopyPalmToPC");
    case Sync::SyncMode::CopyPCToPalm: return QStringLiteral("CopyPCToPalm");
    case Sync::SyncMode::Backup:       return QStringLiteral("Backup");
    case Sync::SyncMode::Restore:      return QStringLiteral("Restore");
    }
    return QStringLiteral("Sync");
}

void mergeStats(Sync::SyncStats &agg, const Kalburator::Sync::BlobSyncStats &part)
{
    agg.created   += part.created;
    agg.updated   += part.updated;
    agg.deleted   += part.deleted;
    agg.unchanged += part.unchanged;
    agg.conflicts += part.conflicts;
    agg.errors    += part.errors;
}

} // namespace

SyncRunner::SyncRunner(BackendPluginManager *plugins,
                       PalmDeviceConnection *device,
                       Kalburator::Sync::ISyncHost *host,
                       QString syncPath,
                       QString stateDir,
                       QObject *parent)
    : QObject(parent)
    , m_plugins(plugins)
    , m_device(device)
    , m_host(host)
    , m_syncPath(std::move(syncPath))
    , m_stateDir(std::move(stateDir))
{
    static const int registered = qRegisterMetaType<WildPalms::Runtime::SyncRunner *>();
    Q_UNUSED(registered);
    m_localBackendFactory = [](const QString &rootPath, const QString &) {
        return std::unique_ptr<Kalburator::Sync::IBlobBackend>(
            new Kalburator::Sync::LocalBlobBackend(rootPath));
    };
}

SyncRunner::~SyncRunner()
{
    if (m_ownedBundle) {
        m_ownedBundle->destroy();
        delete m_ownedBundle;
        m_ownedBundle = nullptr;
    }
}

void SyncRunner::setKPilotLink(KPilotLink *link, QObject *bundleParent)
{
    // Always tear down the previous bundle first — the device pointer
    // it gave us would dangle if we kept it across a reconnect.
    if (m_ownedBundle) {
        m_device = nullptr;
        m_ownedBundle->destroy();
        delete m_ownedBundle;
        m_ownedBundle = nullptr;
    }
    if (!link) return;
    m_ownedBundle = new PalmConnectionBundle(makePalmConnection(link, bundleParent));
    m_device = m_ownedBundle->connection;
}

void SyncRunner::setLocalBackendFactory(LocalBackendFactory factory)
{
    m_localBackendFactory = std::move(factory);
}

std::unique_ptr<Kalburator::Sync::IBlobBackend>
SyncRunner::makeLocalBackend(const QString &pluginId) const
{
    const QString root = pluginLocalPath(pluginId);
    QDir().mkpath(root);
    if (m_localBackendFactory) {
        return m_localBackendFactory(root, pluginId);
    }
    return std::unique_ptr<Kalburator::Sync::IBlobBackend>(
        new Kalburator::Sync::LocalBlobBackend(root));
}

void SyncRunner::requestCancel() { m_cancelled.store(true); }
bool SyncRunner::isCancelled() const { return m_cancelled.load(); }
void SyncRunner::resetCancel() { m_cancelled.store(false); }

QStringList SyncRunner::resolvePluginIds(const QStringList &requested) const
{
    if (!m_plugins) return {};
    QStringList ids;
    if (requested.isEmpty()) {
        // Catalogue entries from KPluginFactory carry a non-empty
        // KPluginMetaData::pluginId(); test fixtures injected via
        // registerInstanceForTest leave metaData blank, so fall back
        // to the IPlugin::pluginId() reported by the loaded instance.
        for (const auto &info : m_plugins->catalogue()) {
            if (!info.instance) continue;
            QString id = info.metaData.pluginId();
            if (id.isEmpty()) id = info.instance->pluginId();
            if (!id.isEmpty()) ids << id;
        }
    } else {
        for (const QString &id : requested) {
            if (m_plugins->plugin(id)) ids << id;
        }
    }
    return ids;
}

QString SyncRunner::pluginLocalPath(const QString &pluginId) const
{
    QDir root(m_syncPath);
    return root.filePath(sanitizeForPath(pluginId));
}

QString SyncRunner::baselineDbPath() const
{
    QDir state(m_stateDir);
    return state.filePath(QStringLiteral(".wildpalms-sync.db"));
}

Sync::SyncResult SyncRunner::run(Sync::SyncMode mode, const QStringList &enabledPluginIds)
{
    resetCancel();
    Sync::SyncResult result;
    result.startTime = QDateTime::currentDateTimeUtc();

    emit started(static_cast<int>(mode));
    emit logMessage(QStringLiteral("=== %1 starting ===").arg(modeLabel(mode)));

    if (!m_plugins) {
        result.success = false;
        result.errorMessage = QStringLiteral("SyncRunner has no plugin manager");
        emit errorOccurred(result.errorMessage);
        emit finished(result);
        return result;
    }

    const QStringList pluginIds = resolvePluginIds(enabledPluginIds);

    if (m_syncPath.isEmpty() || m_stateDir.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("SyncRunner is missing syncPath / stateDir");
        emit errorOccurred(result.errorMessage);
        emit finished(result);
        return result;
    }

    QDir().mkpath(m_syncPath);
    QDir().mkpath(m_stateDir);

    // Capture startTime separately so the per-mode helpers' return
    // value (which constructs a fresh SyncResult) doesn't trample it.
    const QDateTime startedAt = result.startTime;

    switch (mode) {
    case Sync::SyncMode::HotSync:
    case Sync::SyncMode::FullSync:
        result = runTwoWay(mode, pluginIds);
        break;
    case Sync::SyncMode::CopyPalmToPC:
    case Sync::SyncMode::CopyPCToPalm:
        result = runMirror(mode, pluginIds);
        break;
    case Sync::SyncMode::Backup:
        result = runBackup(pluginIds);
        break;
    case Sync::SyncMode::Restore:
        result = runRestore(pluginIds);
        break;
    }

    result.startTime = startedAt;
    result.endTime = QDateTime::currentDateTimeUtc();
    emit logMessage(QStringLiteral("=== %1 %2 (%3 ms) ===")
                        .arg(modeLabel(mode),
                             result.success ? QStringLiteral("complete")
                                            : QStringLiteral("failed"))
                        .arg(result.durationMs()));
    emit finished(result);
    return result;
}

Sync::SyncResult SyncRunner::runTwoWay(Sync::SyncMode mode, const QStringList &pluginIds)
{
    Sync::SyncResult result;
    result.success = true;

    Kalburator::Sync::BlobBaselineStore baseline(baselineDbPath());
    if (!baseline.isOpen()) {
        result.success = false;
        result.errorMessage = QStringLiteral("Failed to open baseline DB: %1")
                                  .arg(baseline.lastError());
        emit errorOccurred(result.errorMessage);
        return result;
    }

    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry handlers;
    Kalburator::Sync::QSyncCore::ConflictStore           conflicts;
    const auto policy = policyFor(mode);

    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    int total = pluginIds.size();
    int idx = 0;
    for (const QString &pluginId : pluginIds) {
        if (isCancelled()) {
            result.errorMessage = QStringLiteral("cancelled");
            result.success = false;
            return result;
        }
        ++idx;
        emit progress(idx, total, pluginId);
        emit logMessage(QStringLiteral("[%1] %2").arg(pluginId, modeLabel(mode)));

        IBackendPlugin *plugin = m_plugins->plugin(pluginId);
        if (!plugin) continue;
        auto provided = plugin->createBackends(m_host, m_device);
        if (!provided.blob) {
            emit logMessage(QStringLiteral("[%1] no blob backend; skipping").arg(pluginId));
            continue;
        }
        std::unique_ptr<Kalburator::Sync::IBlobBackend> palmBlob(provided.blob);

        auto localOwn = makeLocalBackend(pluginId);
        Kalburator::Sync::IBlobBackend &localBlob = *localOwn;

        const auto cols = palmBlob->availableCollections();
        for (const auto &col : cols) {
            if (isCancelled()) break;

            // Ensure target collection exists with same metadata.
            localBlob.createCollection(col);

            const QString mappingId = QStringLiteral("%1::%2").arg(pluginId, col.id);
            if (mode == Sync::SyncMode::FullSync) {
                // Reset both the baseline AND the PC-side mirror so the
                // engine treats every Palm record as fresh-on-source.
                // SyncEngine::runBlobTwoWay does not currently handle
                // the (record-on-both, baseline-absent) case — it falls
                // through silently — so we clear B first to avoid stale
                // data sticking.
                baseline.clearMapping(mappingId);
                const auto stale = localBlob.loadRecords(col.id);
                for (const auto &r : stale) localBlob.deleteRecord(r.id);
            }

            const auto r = engine.runBlobTwoWay(
                palmBlob.get(), &localBlob,
                col.id, mappingId,
                &baseline, &handlers, &conflicts, policy);
            if (!r.success) {
                result.success = false;
                result.errorMessage += (result.errorMessage.isEmpty()
                                            ? QString()
                                            : QStringLiteral("; "))
                                       + QStringLiteral("[%1/%2] %3")
                                             .arg(pluginId, col.id, r.errorMessage);
                ++result.palmStats.errors;
                continue;
            }
            mergeStats(result.palmStats, r.sourceStats);
            mergeStats(result.pcStats,   r.targetStats);
        }
    }

    return result;
}

Sync::SyncResult SyncRunner::runMirror(Sync::SyncMode mode, const QStringList &pluginIds)
{
    Sync::SyncResult result;
    result.success = true;

    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    int total = pluginIds.size();
    int idx = 0;
    for (const QString &pluginId : pluginIds) {
        if (isCancelled()) {
            result.errorMessage = QStringLiteral("cancelled");
            result.success = false;
            return result;
        }
        ++idx;
        emit progress(idx, total, pluginId);
        emit logMessage(QStringLiteral("[%1] %2").arg(pluginId, modeLabel(mode)));

        IBackendPlugin *plugin = m_plugins->plugin(pluginId);
        if (!plugin) continue;
        auto provided = plugin->createBackends(m_host, m_device);
        if (!provided.blob) continue;
        std::unique_ptr<Kalburator::Sync::IBlobBackend> palmBlob(provided.blob);

        auto localOwn = makeLocalBackend(pluginId);
        Kalburator::Sync::IBlobBackend &localBlob = *localOwn;

        const auto cols = palmBlob->availableCollections();
        for (const auto &col : cols) {
            if (isCancelled()) break;
            localBlob.createCollection(col);

            Kalburator::Sync::IBlobBackend *src = nullptr;
            Kalburator::Sync::IBlobBackend *dst = nullptr;
            if (mode == Sync::SyncMode::CopyPalmToPC) {
                src = palmBlob.get();
                dst = &localBlob;
            } else {
                src = &localBlob;
                dst = palmBlob.get();
            }

            const auto r = engine.runBlobMirror(src, dst, col.id);
            if (!r.success) {
                result.success = false;
                result.errorMessage += (result.errorMessage.isEmpty()
                                            ? QString()
                                            : QStringLiteral("; "))
                                       + QStringLiteral("[%1/%2] %3")
                                             .arg(pluginId, col.id, r.errorMessage);
                ++result.palmStats.errors;
                continue;
            }
            // mirror reports source/target stats; tally onto our
            // palm/pc buckets per direction.
            if (mode == Sync::SyncMode::CopyPalmToPC) {
                mergeStats(result.palmStats, r.sourceStats);
                mergeStats(result.pcStats,   r.targetStats);
            } else {
                mergeStats(result.palmStats, r.targetStats);
                mergeStats(result.pcStats,   r.sourceStats);
            }
        }
    }
    return result;
}

Sync::SyncResult SyncRunner::runBackup(const QStringList &pluginIds)
{
    // Backup: copy every Palm record into the per-plugin local store
    // additively (target-only records are NOT deleted). This preserves
    // the legacy SyncMode::Backup property "Old files not on Palm will
    // be preserved".
    Sync::SyncResult result;
    result.success = true;

    int total = pluginIds.size();
    int idx = 0;
    for (const QString &pluginId : pluginIds) {
        if (isCancelled()) {
            result.errorMessage = QStringLiteral("cancelled");
            result.success = false;
            return result;
        }
        ++idx;
        emit progress(idx, total, pluginId);
        emit logMessage(QStringLiteral("[%1] Backup").arg(pluginId));

        IBackendPlugin *plugin = m_plugins->plugin(pluginId);
        if (!plugin) continue;
        auto provided = plugin->createBackends(m_host, m_device);
        if (!provided.blob) continue;
        std::unique_ptr<Kalburator::Sync::IBlobBackend> palmBlob(provided.blob);

        auto localOwn = makeLocalBackend(pluginId);
        Kalburator::Sync::IBlobBackend &localBlob = *localOwn;

        const auto cols = palmBlob->availableCollections();
        for (const auto &col : cols) {
            if (isCancelled()) break;
            localBlob.createCollection(col);

            const auto palmRecs = palmBlob->loadRecords(col.id);
            for (const auto &rec : palmRecs) {
                auto existing = localBlob.loadRecord(rec.id);
                if (existing && existing->contentHash == rec.contentHash) {
                    ++result.pcStats.unchanged;
                    continue;
                }
                if (existing) {
                    if (localBlob.updateRecord(rec)) {
                        ++result.pcStats.updated;
                    } else {
                        ++result.pcStats.errors;
                    }
                } else {
                    if (!localBlob.createRecord(col.id, rec).isEmpty()) {
                        ++result.pcStats.created;
                    } else {
                        ++result.pcStats.errors;
                    }
                }
            }
        }
    }

    return result;
}

Sync::SyncResult SyncRunner::runRestore(const QStringList &pluginIds)
{
    // Restore: write every locally-stored record back to Palm. Palm
    // records not in the backup ARE deleted (legacy "FULL RESTORE …
    // Palm records not in the backup WILL BE DELETED.").
    Sync::SyncResult result;
    result.success = true;

    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    int total = pluginIds.size();
    int idx = 0;
    for (const QString &pluginId : pluginIds) {
        if (isCancelled()) {
            result.errorMessage = QStringLiteral("cancelled");
            result.success = false;
            return result;
        }
        ++idx;
        emit progress(idx, total, pluginId);
        emit logMessage(QStringLiteral("[%1] Restore").arg(pluginId));

        IBackendPlugin *plugin = m_plugins->plugin(pluginId);
        if (!plugin) continue;
        auto provided = plugin->createBackends(m_host, m_device);
        if (!provided.blob) continue;
        std::unique_ptr<Kalburator::Sync::IBlobBackend> palmBlob(provided.blob);

        auto localOwn = makeLocalBackend(pluginId);
        Kalburator::Sync::IBlobBackend &localBlob = *localOwn;

        const auto cols = palmBlob->availableCollections();
        for (const auto &col : cols) {
            if (isCancelled()) break;
            const auto r = engine.runBlobMirror(&localBlob, palmBlob.get(), col.id);
            if (!r.success) {
                result.success = false;
                result.errorMessage += (result.errorMessage.isEmpty()
                                            ? QString()
                                            : QStringLiteral("; "))
                                       + QStringLiteral("[%1/%2] %3")
                                             .arg(pluginId, col.id, r.errorMessage);
                ++result.palmStats.errors;
                continue;
            }
            mergeStats(result.pcStats,   r.sourceStats);
            mergeStats(result.palmStats, r.targetStats);
        }
    }

    return result;
}

} // namespace WildPalms::Runtime
