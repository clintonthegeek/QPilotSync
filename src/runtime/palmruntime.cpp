#include "palmruntime.h"
#include "palmdeviceaccess.h"
#include "palm/kpilotlink.h"
#include "palm/kpilotdevicelink.h"

#include <QPromise>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <QtConcurrent>

#include "backendregistry.h"
#include "syncengine.h"
#include "conflicthandlerregistry.h"
#include "syncbackend.h"
#include "synctypes.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include <iblobbackend.h>
#include <isynchost.h>
#include <blobbaselinestore.h>
#include <isyncconfigstore.h>
#include "shape.h"
#include "core/ibackendplugin_v2.h"
#include "palm/device/pilotlinkpalmdatabaseaccess.h"

#include <rawfilesbackend.h>

#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Incidence>
#include <KPluginFactory>
#include <KPluginMetaData>

namespace {

static QString sanitizeForFilesystem(const QString &id)
{
    QString s = id;
    s.replace(QLatin1Char(':'), QLatin1Char('_'))
     .replace(QLatin1Char('/'), QLatin1Char('_'));
    return s;
}

// ──────────────────────────────────────────────────────────────────────────────
// BlobBackendAdapter
//
// Wraps an IBlobBackend as a SyncBackend with blob domain.
// nativeShapes() returns {"blob","blob"} ≠ "calendar" domain →
// SyncEngineWorker::processSync routes to dispatchBlobSync, which:
//   - does NOT need ICalendarCollection or CalendarBaselineStore
//   - calls IBlobBackend::loadRecords / createRecord / updateRecord via BlockingQueued
//
// Calendar pure-virtuals are empty stubs — never invoked via dispatchBlobSync.
// ──────────────────────────────────────────────────────────────────────────────
class BlobBackendAdapter final : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    BlobBackendAdapter(std::unique_ptr<Kalburator::Sync::IBlobBackend> blob,
                       const QString &id,
                       QObject *parent = nullptr)
        : Kalburator::Sync::SyncBackend(parent)
        , m_blob(std::move(blob))
        , m_id(id)
        // Phase Ia.5 follow-up: default shape must match the blob domain
        // plugin's canonical shape (blob, raw) so the unified dispatchSync
        // can compile an identity Pipeline (blob,raw) -> (blob,raw). The
        // previous default (blob, blob) had no registered edge to canonical
        // and made dispatchSync fail with "no edge path" for any test that
        // registered a backend without an explicit shape override.
        , m_shape{ Kalburator::Shape::DomainId{QStringLiteral("blob")},
                   Kalburator::Shape::EncodingId{QStringLiteral("raw")} }
    {}

    // Phase Ia.5 Task 19: optional shape override so cross-shape
    // mappings (e.g. contacts/palm -> contacts/vcard4) can compile a
    // Pipeline through the registered TransformationRegistry edges.
    BlobBackendAdapter(std::unique_ptr<Kalburator::Sync::IBlobBackend> blob,
                       const QString &id,
                       const Kalburator::Shape::Shape &shape,
                       QObject *parent = nullptr)
        : Kalburator::Sync::SyncBackend(parent)
        , m_blob(std::move(blob))
        , m_id(id)
        , m_shape(shape)
    {}

    // ── SyncBackend identity ──────────────────────────────────────────────────
    QString backendType() const override { return m_id; }
    QList<Kalburator::Shape::Shape> nativeShapes() const override {
        return { m_shape };
    }

    // ── IBlobBackend identity ─────────────────────────────────────────────────
    QString backendId()   const override { return m_id; }
    QString displayName() const override { return m_blob->displayName(); }
    bool    isAvailable() const override { return m_blob->isAvailable(); }

    // ── IBlobBackend collections ──────────────────────────────────────────────
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override
        { return m_blob->availableCollections(); }
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &id) override
        { return m_blob->collectionInfo(id); }
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override
        { return m_blob->createCollection(info); }

    // ── IBlobBackend records ──────────────────────────────────────────────────
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &colId) override
        { return m_blob->loadRecords(colId); }
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &id) override
        { return m_blob->loadRecord(id); }
    QString createRecord(const QString &colId,
                         const Kalburator::Sync::BackendRecord &rec) override
        { return m_blob->createRecord(colId, rec); }
    bool updateRecord(const Kalburator::Sync::BackendRecord &rec) override
        { return m_blob->updateRecord(rec); }
    bool deleteRecord(const QString &id) override
        { return m_blob->deleteRecord(id); }

    // ── IBlobBackend change detection ─────────────────────────────────────────
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &colId, const QDateTime &dt) override
        { return m_blob->modifiedSince(colId, dt); }
    QStringList deletedSince(const QString &colId, const QDateTime &dt) override
        { return m_blob->deletedSince(colId, dt); }
    bool supportsDeleteTracking() const override
        { return m_blob->supportsDeleteTracking(); }

    // ── SyncBackend calendar pure-virtuals — stubs (blob path never calls these)
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &,
        const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }

private:
    std::unique_ptr<Kalburator::Sync::IBlobBackend> m_blob;
    QString m_id;
    Kalburator::Shape::Shape m_shape;
};

// ──────────────────────────────────────────────────────────────────────────────
// PalmSyncHost
// Minimal ISyncHost backed by BackendRegistry.
// ──────────────────────────────────────────────────────────────────────────────
class PalmSyncHost final : public Kalburator::Sync::ISyncHost {
public:
    explicit PalmSyncHost(Kalburator::Sync::BackendRegistry *registry)
        : m_registry(registry) {}

    Kalburator::Sync::SyncBackend* backendById(const QString &id) override {
        return m_registry ? m_registry->backendInstance(id) : nullptr;
    }
    QHash<QString, Kalburator::Sync::SyncBackend*> backends() override {
        QHash<QString, Kalburator::Sync::SyncBackend*> result;
        if (!m_registry) return result;
        for (const QString &id : m_registry->registeredInstanceIds())
            result.insert(id, m_registry->backendInstance(id));
        return result;
    }
    Kalburator::Sync::ISyncConfigStore* configStore() override { return nullptr; }

private:
    Kalburator::Sync::BackendRegistry *m_registry = nullptr;
};

}  // namespace

#include "palmruntime.moc"

namespace WildPalms::Runtime {

PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , m_profilePath(profilePath)
    , m_backupRoot(QDir(profilePath).filePath(QStringLiteral("backup")))
    , m_registry(std::make_unique<Kalburator::Sync::BackendRegistry>())
    , m_baselineStore(std::make_unique<Kalburator::Sync::BlobBaselineStore>(
          QDir(profilePath).filePath(QStringLiteral(".wildpalms-blob-baselines.db"))))
{
    qRegisterMetaType<PalmRunResult>();

    m_syncHost = std::make_unique<PalmSyncHost>(m_registry.get());
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get());
    m_engine->setBlobBaselineStore(m_baselineStore.get());
    // No-op today (handler set after construction), but keeps this consistent
    // with the re-install pattern required at every engine-construction site.
    if (m_conflictHandler)
        m_engine->conflictRegistry()->setDefaultHandler(m_conflictHandler);

    connect(this, &PalmRuntime::runStarted,
            this, [this]() { m_running = true; });
    connect(this, &PalmRuntime::runFinished,
            this, [this]() { m_running = false; });
}

PalmRuntime::~PalmRuntime() = default;

void PalmRuntime::connectDevice(const QStringList &devicePaths)
{
    if (!m_device) {
        m_device = std::make_unique<PalmDeviceAccess>(this);

        connect(m_device.get(), &PalmDeviceAccess::connectionStarted,
                this, &PalmRuntime::connectionStarted);

        connect(m_device.get(), &PalmDeviceAccess::connectionComplete,
                this, [this](bool ok, const QString &err) {
                    emit connectionComplete(ok, err);
                    if (ok) {
                        finishConnect();
                    }
                });

        connect(m_device.get(), &PalmDeviceAccess::deviceDisconnected,
                this, &PalmRuntime::deviceDisconnected);

        connect(m_device.get(), &PalmDeviceAccess::logMessage,
                this, &PalmRuntime::logMessage);
    }

    m_device->connectDevice(devicePaths);
}

void PalmRuntime::cancelConnect()
{
    if (m_device) m_device->cancelConnect();
}

void PalmRuntime::finishConnect()
{
    if (!m_device) return;
    // Cache the link from PalmDeviceAccess so backup/restore (which read
    // m_link directly) keep working.
    if (!m_link) m_link = m_device->link();

    // Discover and load IBackendPluginV2 plugins from wildpalms/plugins/
    const auto metaDatas = KPluginMetaData::findPlugins(
        QStringLiteral("wildpalms/plugins"),
        [](const KPluginMetaData &md) {
            return md.value(QStringLiteral("X-WildPalms-PluginType"))
                   == QStringLiteral("backend");
        });

    for (const KPluginMetaData &meta : metaDatas) {
        auto factoryResult = KPluginFactory::loadFactory(meta);
        if (!factoryResult) {
            qWarning() << "[PalmRuntime::connectDevice] Failed to load factory for"
                       << meta.pluginId() << ":" << factoryResult.errorString;
            continue;
        }

        QObject *obj = factoryResult.plugin->create<QObject>(this);
        if (!obj) {
            qWarning() << "[PalmRuntime::connectDevice] Factory returned nullptr for"
                       << meta.pluginId();
            continue;
        }

        // Try to cast to IBackendPluginV2.
        auto *v2 = qobject_cast<WildPalms::IBackendPluginV2 *>(obj);
        if (!v2) {
            qDebug() << "[PalmRuntime::connectDevice] Plugin" << meta.pluginId()
                     << "does not implement IBackendPluginV2 -- skipping";
            delete obj;
            continue;
        }

        // Create the Palm backend for this plugin.
        auto backend = v2->createPalmBackend(m_device.get());
        if (!backend) {
            qWarning() << "[PalmRuntime::connectDevice] Plugin" << meta.pluginId()
                       << "returned null backend";
            delete obj;
            continue;
        }

        // Read available collections before the backend is moved into the adapter.
        // CalendarBlobBackend::availableCollections() reads the in-memory category
        // store (populated during createPalmBackend) — no live Palm I/O.
        const auto palmCollections = backend->availableCollections();

        // Wrap the backend in BlobBackendAdapter and register with BackendRegistry.
        const QString id = v2->pluginId();
        auto adapter = std::make_unique<BlobBackendAdapter>(std::move(backend), id);
        m_registry->registerBackendInstance(id, adapter.get());
        m_ownedBackends.push_back(std::move(adapter));

        // Keep the QObject alive (parented to this, will be cleaned up on destruction).
        m_v2PluginObjects.append(obj);

        qDebug() << "[PalmRuntime::connectDevice] Registered backend plugin:" << id;

        // Auto-create RawFiles defaults the first time we connect, but only
        // if the user has not already loaded their own mappings via
        // reloadMappings(). User-saved mappings win.
        if (m_mappings.isEmpty()) {
            // Build a default RawFiles PC-side backend + SyncMapping for each Palm
            // collection.
            for (const auto &palmCol : palmCollections) {
                // Sanitize the collection ID for filesystem safety.
                QString safeColId = palmCol.id;
                safeColId.replace(QLatin1Char(':'), QLatin1Char('_'))
                         .replace(QLatin1Char('/'), QLatin1Char('_'));

                const QString pcId = QStringLiteral("rawfiles-%1-%2").arg(id, safeColId);
                const QString rootPath = QDir(m_profilePath).filePath(
                    QStringLiteral("rawfiles/%1/%2").arg(id, safeColId));

                auto pcBackend = std::make_unique<Kalburator::Sinks::RawFilesBackend>(rootPath);
                Kalburator::Sync::CollectionInfo pcCol;
                pcCol.id   = safeColId;
                pcCol.name = palmCol.name;
                pcBackend->createCollection(pcCol);

                m_registry->registerBackendInstance(pcId, pcBackend.get());
                m_ownedBackends.push_back(std::move(pcBackend));

                Kalburator::Sync::SyncMapping m;
                m.id             = QStringLiteral("default-%1-%2").arg(id, safeColId);
                m.sourceBackend  = id;
                m.targetBackend  = pcId;
                m.sourceCalendar = palmCol.id;
                m.targetCalendar = safeColId;
                m.mode           = Kalburator::Sync::SyncMode::TwoWay;
                m.enabled        = true;
                m_mappings.append(m);

                qDebug() << "[PalmRuntime::connectDevice] Default mapping:"
                         << palmCol.id << "->" << rootPath;
            }
        }
    }

    m_engine->setSyncMappings(m_mappings);

    emit deviceConnected();
    emit readyForSync();
}

void PalmRuntime::reloadMappings(const QJsonArray &json)
{
    m_mappings.clear();
    for (const auto &v : json) {
        if (!v.isObject())
            continue;
        m_mappings.append(Kalburator::Sync::syncMappingFromJson(v.toObject()));
    }
    if (m_engine)
        m_engine->setSyncMappings(m_mappings);
}

void PalmRuntime::disconnectDevice() {
    if (m_device) m_device->disconnectDevice();   // emits deviceDisconnected via forward
    m_link = nullptr;
    m_device.reset();
}

bool PalmRuntime::isDeviceConnected() const {
    return m_device != nullptr;
}

KPilotDeviceLink *PalmRuntime::deviceLink() const {
    if (!m_device) return nullptr;
    return qobject_cast<KPilotDeviceLink*>(m_device->link());
}

void PalmRuntime::setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess> device) {
    m_device = std::move(device);
    emit deviceConnected();
}

void PalmRuntime::registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2> plugin) {
    m_plugins.append(plugin);
    if (!m_device) return;
    auto blob = plugin->createPalmBackend(m_device.get());
    if (!blob) return;
    const QString id = plugin->pluginId();
    auto adapter = std::make_unique<BlobBackendAdapter>(std::move(blob), id);
    m_registry->registerBackendInstance(id, adapter.get());
    m_ownedBackends.push_back(std::move(adapter));
}

void PalmRuntime::registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2> plugin,
                                         const Kalburator::Shape::Shape &shape) {
    m_plugins.append(plugin);
    if (!m_device) return;
    auto blob = plugin->createPalmBackend(m_device.get());
    if (!blob) return;
    const QString id = plugin->pluginId();
    auto adapter = std::make_unique<BlobBackendAdapter>(std::move(blob), id, shape);
    m_registry->registerBackendInstance(id, adapter.get());
    m_ownedBackends.push_back(std::move(adapter));
}

void PalmRuntime::registerBlobBackendForTest(const QString &id,
                                              std::unique_ptr<Kalburator::Sync::IBlobBackend> backend)
{
    if (!backend) return;
    auto adapter = std::make_unique<BlobBackendAdapter>(std::move(backend), id);
    m_registry->registerBackendInstance(id, adapter.get());
    m_ownedBackends.push_back(std::move(adapter));
}

void PalmRuntime::registerBlobBackendForTest(const QString &id,
                                              std::unique_ptr<Kalburator::Sync::IBlobBackend> backend,
                                              const Kalburator::Shape::Shape &shape)
{
    if (!backend) return;
    auto adapter = std::make_unique<BlobBackendAdapter>(std::move(backend), id, shape);
    m_registry->registerBackendInstance(id, adapter.get());
    m_ownedBackends.push_back(std::move(adapter));
}

void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
    m_engine->setSyncMappings(m_mappings);
}

void PalmRuntime::setLinkForTest(KPilotLink *link) {
    m_link = link;
}

void PalmRuntime::setConflictHandler(
    Kalburator::Sync::QSyncCore::ConflictHandler *handler)
{
    m_conflictHandler = handler;
    if (m_engine) {
        m_engine->conflictRegistry()->setDefaultHandler(handler);
    }
}

Kalburator::Sync::QSyncCore::ConflictHandler *
PalmRuntime::conflictHandlerForTest() const
{
    if (!m_engine) return nullptr;
    return m_engine->conflictRegistry()->handlerFor(QString{});
}

QList<QString> PalmRuntime::enabledPluginIds() const {
    QList<QString> ids;
    for (const auto &p : m_plugins) ids.append(p->pluginId());
    return ids;
}

QList<Kalburator::Sync::SyncMapping> PalmRuntime::palmMappings() const {
    return m_mappings;
}

static QFuture<WildPalms::Runtime::PalmRunResult> makeSuccessFuture() {
    QPromise<WildPalms::Runtime::PalmRunResult> p;
    auto f = p.future();
    p.start();
    WildPalms::Runtime::PalmRunResult r;
    r.success = true;
    r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
    p.addResult(std::move(r));
    p.finish();
    return f;
}

QFuture<PalmRunResult> PalmRuntime::runAllMappings()
{
    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (m.enabled)
            ids.append(m.id);
    }
    if (ids.isEmpty())
        return makeSuccessFuture();

    // Pause the TickleWorker before the engine starts issuing DLP calls.
    // readAllRecords() for 500+ records takes > 5 s; the tickle fires
    // mid-stream and corrupts the DLP session.
    if (m_link) m_link->pauseTickle();

    auto engineFuture = m_engine->runSyncFuture(
        ids, Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored);

    return engineFuture.then([this](QList<Kalburator::Sync::SyncResult> results) {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        PalmRunResult::PluginStats stats;
        for (const auto &sr : results) {
            if (!sr.success && !sr.cancelled && !sr.skipped)
                r.success = false;
            stats.created   += sr.targetStats.created;
            stats.updated   += sr.targetStats.updated;
            stats.deleted   += sr.targetStats.deleted;
            stats.unchanged += sr.targetStats.unchanged;
            stats.errors    += (sr.success ? 0 : 1);
        }
        if (!results.isEmpty())
            r.perPluginStats.insert(QStringLiteral("calendar"), stats);

        r.endTime = QDateTime::currentDateTimeUtc();
        // Resume tickle on the main thread (this callback runs on the
        // engine worker thread).
        QMetaObject::invokeMethod(this, [this, r]() {
            if (m_link) m_link->resumeTickle();
            emit runFinished(r);
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::hotSync() {
    emit runStarted(QStringLiteral("HotSync"));
    if (m_mappings.isEmpty())
        return makeSuccessFuture();
    return runAllMappings();
}

QFuture<PalmRunResult> PalmRuntime::fullSync()
{
    emit runStarted(QStringLiteral("FullSync"));
    // Clear all baselines so the engine treats this as a fresh first sync.
    for (const auto &m : m_mappings)
        m_baselineStore->clearMappingV3(m.id);
    return runAllMappings();
}

QFuture<PalmRunResult> PalmRuntime::runMirror(MirrorDir dir, const QString &modeLabel)
{
    emit runStarted(modeLabel);

    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (m.enabled) ids.append(m.id);
    }
    if (ids.isEmpty())
        return makeSuccessFuture();

    using Direction = Kalburator::Sync::ExecutionOverride::Direction;
    Kalburator::Sync::ExecutionOverride ov;
    ov.direction = (dir == MirrorDir::PalmToPC) ? Direction::MirrorAToB
                                                 : Direction::MirrorBToA;

    // Pause the TickleWorker before the engine starts issuing DLP calls —
    // same race as runAllMappings().
    if (m_link) m_link->pauseTickle();

    // For M3: calendar-only, single mapping. Dispatch only the first enabled
    // mapping; Plan 3 (M4) will add multi-mapping iteration once other plugins
    // are re-enabled.
    auto engineFuture = m_engine->runSyncFuture(ids.first(), ov);

    return engineFuture.then([this](Kalburator::Sync::SyncResult sr) {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = sr.success;

        PalmRunResult::PluginStats stats;
        stats.created   = sr.targetStats.created;
        stats.updated   = sr.targetStats.updated;
        stats.deleted   = sr.targetStats.deleted;
        stats.unchanged = sr.targetStats.unchanged;
        stats.errors    = r.success ? 0 : 1;
        r.perPluginStats.insert(QStringLiteral("calendar"), stats);

        r.endTime = QDateTime::currentDateTimeUtc();
        // Resume tickle on the main thread (this callback runs on the
        // engine worker thread).
        QMetaObject::invokeMethod(this, [this, r]() {
            if (m_link) m_link->resumeTickle();
            emit runFinished(r);
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::copyPalmToPC()
{
    return runMirror(MirrorDir::PalmToPC, QStringLiteral("CopyPalmToPC"));
}

QFuture<PalmRunResult> PalmRuntime::copyPCToPalm()
{
    return runMirror(MirrorDir::PCToPalm, QStringLiteral("CopyPCToPalm"));
}

QFuture<PalmRunResult> PalmRuntime::backup()
{
    emit runStarted(QStringLiteral("Backup"));

    if (!m_link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("backup: no device connected");
        emit runFinished(r);
        return QtFuture::makeReadyValueFuture(r);
    }

    // Pause the TickleWorker BEFORE the first DLP call (listDatabases).
    // dlp_ReadDBList() iterates 100+ databases and takes several seconds;
    // the 5-second tickle timer fires mid-loop and corrupts the DLP session.
    // pauseTickle() uses BlockingQueuedConnection so the tickle thread is
    // fully stopped before we proceed.
    m_link->pauseTickle();

    // Snapshot the DB list on the calling (main) thread — KPilotLink is not
    // thread-safe; all link calls must happen before we hand off to the pool.
    const QStringList databases = m_link->listDatabases();
    const QString backupDir = m_backupRoot;
    KPilotLink *link = m_link;

    return QtConcurrent::run([this, databases, backupDir, link]() -> PalmRunResult {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        QDir().mkpath(backupDir);

        auto &stats = r.perPluginStats[QStringLiteral("device")];
        for (const QString &dbName : databases) {
            const QString destPath = QDir(backupDir).filePath(
                sanitizeForFilesystem(dbName) + QStringLiteral(".pdb"));
            if (link->retrieveDatabase(dbName, destPath)) {
                ++stats.created;
            } else {
                // Hard failure — the DLP session state may be corrupted.
                // Stop the loop rather than issuing further DLP calls.
                ++stats.errors;
                r.success = false;
                break;
            }
        }

        r.endTime = QDateTime::currentDateTimeUtc();
        QMetaObject::invokeMethod(this, [this, r]() {
            m_link->resumeTickle();
            emit runFinished(r);
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::restore()
{
    emit runStarted(QStringLiteral("Restore"));

    if (!m_link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("restore: no device connected");
        emit runFinished(r);
        return QtFuture::makeReadyValueFuture(r);
    }

    const QString backupDir = m_backupRoot;
    KPilotLink *link = m_link;

    m_link->pauseTickle();

    return QtConcurrent::run([this, backupDir, link]() -> PalmRunResult {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        const QStringList files = QDir(backupDir).entryList(
            QStringList{QStringLiteral("*.pdb"), QStringLiteral("*.prc")},
            QDir::Files);

        auto &stats = r.perPluginStats[QStringLiteral("device")];
        for (const QString &fileName : files) {
            const QString filePath = QDir(backupDir).filePath(fileName);
            if (link->installFile(filePath)) {
                ++stats.created;
            } else {
                ++stats.errors;
                r.success = false;
                break;
            }
        }

        r.endTime = QDateTime::currentDateTimeUtc();
        QMetaObject::invokeMethod(this, [this, r]() {
            m_link->resumeTickle();
            emit runFinished(r);
        });
        return r;
    });
}

}  // namespace WildPalms::Runtime
