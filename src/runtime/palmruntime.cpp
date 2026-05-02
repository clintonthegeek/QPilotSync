#include "palmruntime.h"
#include "palmdeviceaccess.h"

#include <QPromise>
#include <QDir>
#include <QDateTime>

#include "backendregistry.h"
#include "syncengine.h"
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

#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Incidence>

namespace {

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
    {}

    // ── SyncBackend identity ──────────────────────────────────────────────────
    QString backendType() const override { return m_id; }
    QList<Kalburator::Shape::Shape> nativeShapes() const override {
        return {{ Kalburator::Shape::DomainId{QStringLiteral("blob")},
                  Kalburator::Shape::EncodingId{QStringLiteral("blob")} }};
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
    , m_registry(std::make_unique<Kalburator::Sync::BackendRegistry>())
    , m_baselineStore(std::make_unique<Kalburator::Sync::BlobBaselineStore>(
          QDir(profilePath).filePath(QStringLiteral(".wildpalms-blob-baselines.db"))))
{
    qRegisterMetaType<PalmRunResult>();

    m_syncHost = std::make_unique<PalmSyncHost>(m_registry.get());
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get());
    m_engine->setBlobBaselineStore(m_baselineStore.get());
}

PalmRuntime::~PalmRuntime() = default;

void PalmRuntime::connectDevice(KPilotLink *link) {
    Q_UNUSED(link);
    // Implemented in Task 10.
}

void PalmRuntime::disconnectDevice() {
    m_device.reset();
    emit deviceDisconnected();
}

bool PalmRuntime::isDeviceConnected() const {
    return m_device != nullptr;
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

void PalmRuntime::registerBlobBackendForTest(const QString &id,
                                              std::unique_ptr<Kalburator::Sync::IBlobBackend> backend)
{
    if (!backend) return;
    auto adapter = std::make_unique<BlobBackendAdapter>(std::move(backend), id);
    m_registry->registerBackendInstance(id, adapter.get());
    m_ownedBackends.push_back(std::move(adapter));
}

void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
    m_engine->setSyncMappings(m_mappings);
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

QFuture<PalmRunResult> PalmRuntime::hotSync() {
    emit runStarted(QStringLiteral("HotSync"));

    if (m_mappings.isEmpty())
        return makeSuccessFuture();

    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (m.enabled)
            ids.append(m.id);
    }
    if (ids.isEmpty())
        return makeSuccessFuture();

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
        emit runFinished(r);
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::fullSync()     { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::copyPalmToPC() { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::copyPCToPalm() { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::backup()       { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::restore()      { return makeSuccessFuture(); }

}  // namespace WildPalms::Runtime
