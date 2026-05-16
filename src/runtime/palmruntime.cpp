#include "palmruntime.h"
#include "palmdeviceaccess.h"
#include "palm/kpilotlink.h"
#include "palm/kpilotdevicelink.h"

#include <QPromise>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
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
#include <isynchost.h>
#include <baselinestore.h>
#include <isyncconfigstore.h>
#include "shape.h"
// K.8b T13: ibackendplugin_v2.h include removed — V2 plugin ABI deleted.
#include "palm/device/pilotlinkpalmdatabaseaccess.h"

// K.8b T6: in-process plugin loading via PluginManager.
// Kalburator::Sync exposes src/plugin/ on its PUBLIC include path,
// so headers are reachable without a path prefix.
#include "pluginmanager.h"
#include "manifest.h"
#include "stock_plugins.h"
#include "domainregistry.h"
#include "plugins/calendar/calendarbackendplugin.h"
#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/memo/memobackendplugin.h"
#include "plugins/todos/todobackendplugin.h"
#include "plugins/webcalendar/webcalbackendplugin.h"

#include <rawfilesbackend.h>
#include <caldavbackendcontribution.h>
#include <carddavbackendcontribution.h>

namespace {

static QString sanitizeForFilesystem(const QString &id)
{
    QString s = id;
    s.replace(QLatin1Char(':'), QLatin1Char('_'))
     .replace(QLatin1Char('/'), QLatin1Char('_'));
    return s;
}

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

// K.8b T6: helper for building PluginManifests for the static Palm plugins.
// K.8b T6 fix: Palm plugins have no definesDomains / shapeContributions /
// domainOperations — applyPlugin() is a no-op for them.  requiresDomains
// must NOT be set here: PluginManager::resolve() only checks within the
// same batch, so a "calendar" requirement would fail with MissingDependency
// whenever stock plugins are already loaded from a prior loadInProcess()
// call (common in tests).  The domain parameter is kept for callers that
// may need it in future, but ignored in the manifest.
static Kalburator::PluginManifest mkPalmManifest(const QString &id,
                                                  const QString & /*domain*/)
{
    Kalburator::PluginManifest m;
    m.id                    = id;
    m.version               = QStringLiteral("1.0");
    m.displayName           = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    // definesDomains and requiresDomains intentionally empty: Palm plugins
    // do not participate in the libkalburator domain/shape system.
    return m;
}

}  // namespace

#include "palmruntime.moc"

namespace WildPalms::Runtime {

PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , m_profilePath(profilePath)
    , m_backupRoot(QDir(profilePath).filePath(QStringLiteral("backup")))
    , m_registry(std::make_unique<Kalburator::Sync::BackendRegistry>())
    , m_baselineStore(std::make_unique<Kalburator::Storage::BaselineStore>(
          QDir(profilePath).filePath(QStringLiteral(".wildpalms-blob-baselines.db"))))
{
    qRegisterMetaType<PalmRunResult>();

    // Register provider contributions into this runtime's local registry.
    // ProviderManager no longer auto-registers these (K.8a T6); the
    // application layer is now responsible for seeding contributions.
    m_registry->registerContribution(
        std::make_shared<Kalburator::Sync::CalDavBackendContribution>());
    m_registry->registerContribution(
        std::make_shared<Kalburator::Sync::CardDavBackendContribution>());

    m_syncHost = std::make_unique<PalmSyncHost>(m_registry.get());
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get());
    m_engine->setBaselineStore(m_baselineStore.get());
    // No-op today (handler set after construction), but keeps this consistent
    // with the re-install pattern required at every engine-construction site.
    if (m_conflictHandler)
        m_engine->conflictRegistry()->setDefaultHandler(m_conflictHandler);

    // K.8b T6: load the five static Palm plugins in-process.
    registerPalmPlugins();

    connect(this, &PalmRuntime::runStarted,
            this, [this]() { m_running = true; });
    connect(this, &PalmRuntime::runFinished,
            this, [this]() { m_running = false; });
}

PalmRuntime::~PalmRuntime() = default;

void PalmRuntime::registerPalmPlugins()
{
    using namespace WildPalms::CalendarPlugin;
    using namespace WildPalms::ContactsPlugin;
    using namespace WildPalms::Memo;
    using namespace WildPalms::TodoPlugin;
    using namespace WildPalms::WebcalPlugin;

    // Always create fresh plugin instances — each PalmRuntime owns its own
    // set so that createPalmBackend() and createConflictHandler() can hold
    // per-runtime state (device pointer, category store, etc.).
    auto cal  = std::make_unique<CalendarBackendPlugin>();
    auto con  = std::make_unique<ContactsBackendPlugin>();
    auto memo = std::make_unique<MemoPlugin>();
    auto todo = std::make_unique<TodoBackendPlugin>();
    auto webc = std::make_unique<WebcalBackendPlugin>();

    // K.8b T6: loadInProcess registers the plugins into the process-wide
    // singletons (DomainRegistry, TransformationRegistry, BackendRegistry).
    // These singletons live for the process lifetime, so a second PalmRuntime
    // in the same process (common in tests) must NOT call loadInProcess()
    // again — the singletons reject duplicate ids with CanonicalConflict /
    // DoubleBinding.  Guard with a process-level flag; registrations from
    // the first instance remain valid for all subsequent instances.
    static bool s_globalRegistrationDone = false;
    if (!s_globalRegistrationDone) {
        m_pluginManager = std::make_unique<Kalburator::PluginManager>();

        // Stock plugins own the DomainDefinitions (blob/calendar/contacts/
        // memo/todo). Without this, SyncEngine::dispatchSync() bails with
        // "no definition for domain '<X>'" on every mapping because the
        // process-wide DomainRegistry is empty. Tests already seed the
        // registry themselves in initTestCase(); calling registerStockPlugins
        // a second time exercises a stock-plugin re-registration path that
        // intermittently corrupts the heap at teardown. Guard with a presence
        // check on the calendar definition — cheap, and sidesteps the path
        // entirely when tests have pre-loaded the stock plugins.
        using Kalburator::Shape::DomainId;
        if (!Kalburator::Shape::DomainRegistry::instance().definitionFor(
                DomainId{QStringLiteral("calendar")})) {
            Kalburator::registerStockPlugins(*m_pluginManager);
        }

        QList<QPair<Kalburator::Plugin *, Kalburator::PluginManifest>> items{
            { cal.get(),  mkPalmManifest(QStringLiteral("wildpalms.calendar"),    QStringLiteral("calendar")) },
            { con.get(),  mkPalmManifest(QStringLiteral("wildpalms.contacts"),    QStringLiteral("contacts")) },
            { memo.get(), mkPalmManifest(QStringLiteral("wildpalms.memo"),        QStringLiteral("memo"))     },
            { todo.get(), mkPalmManifest(QStringLiteral("wildpalms.todo"),        QStringLiteral("todo"))     },
            { webc.get(), mkPalmManifest(QStringLiteral("wildpalms.webcalendar"), QStringLiteral("calendar")) },
        };

        if (!m_pluginManager->loadInProcess(items)) {
            qWarning() << "[PalmRuntime] Failed to load Palm plugins via PluginManager";
            return;
        }
        s_globalRegistrationDone = true;
    }

    m_palmPlugins.push_back(std::move(cal));
    m_palmPlugins.push_back(std::move(con));
    m_palmPlugins.push_back(std::move(memo));
    m_palmPlugins.push_back(std::move(todo));
    m_palmPlugins.push_back(std::move(webc));
}

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

void PalmRuntime::cancelSync()
{
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
    }
}

void PalmRuntime::finishConnect()
{
    if (!m_device) return;

    // K.8b T6: iterate the five statically-loaded Palm plugins registered
    // by registerPalmPlugins(). Replaces the old KPluginMetaData .so
    // discovery loop.
    for (auto &plugin : m_palmPlugins) {
        std::unique_ptr<Kalburator::Sync::SyncBackend> ownedBackend;
        QString id;

        // Dynamic dispatch to each concrete plugin type for
        // createPalmBackend(). The method is non-virtual on Kalburator::Plugin
        // — each concrete class declares it independently.
        using namespace WildPalms::CalendarPlugin;
        using namespace WildPalms::ContactsPlugin;
        using namespace WildPalms::Memo;
        using namespace WildPalms::TodoPlugin;
        using namespace WildPalms::WebcalPlugin;

        if (auto *p = dynamic_cast<CalendarBackendPlugin *>(plugin.get())) {
            id = p->pluginId();
            ownedBackend = p->createPalmBackend(m_device.get());
        } else if (auto *p = dynamic_cast<ContactsBackendPlugin *>(plugin.get())) {
            id = p->pluginId();
            ownedBackend = p->createPalmBackend(m_device.get());
        } else if (auto *p = dynamic_cast<MemoPlugin *>(plugin.get())) {
            id = p->pluginId();
            ownedBackend = p->createPalmBackend(m_device.get());
        } else if (auto *p = dynamic_cast<TodoBackendPlugin *>(plugin.get())) {
            id = p->pluginId();
            ownedBackend = p->createPalmBackend(m_device.get());
        } else if (auto *p = dynamic_cast<WebcalBackendPlugin *>(plugin.get())) {
            id = p->pluginId();
            // K.8b T7: WebcalBlobBackend now inherits SyncBackend directly;
            // no adapter needed.
            ownedBackend = p->createPalmBackend(m_device.get());
        }

        if (!ownedBackend) {
            if (!id.isEmpty())
                qWarning() << "[PalmRuntime::finishConnect] Plugin" << id
                           << "returned null backend";
            continue;
        }

        // Read available collections before any ownership transfer.
        const auto palmCollections = ownedBackend->availableCollections();

        m_registry->registerBackendInstance(id, ownedBackend.get());
        m_ownedBackends.push_back(std::move(ownedBackend));

        qDebug() << "[PalmRuntime::finishConnect] Registered backend plugin:" << id;

        // Per-slot default: only create a RawFiles mapping for slots not
        // already covered by a user-configured mapping.
        //
        // K.9: the RawFiles mirror declares the source's shape per
        // collection. Pre-K.9 it declared Shape::Any(), which made
        // SyncEngine::dispatchSync bail with "cross-domain mappings
        // not supported" on the first real sync.
        auto *registeredSrc = m_registry->backendInstance(id);
        for (const auto &palmCol : palmCollections) {
            const bool alreadyCovered = std::any_of(
                m_mappings.cbegin(), m_mappings.cend(),
                [&](const Kalburator::Sync::SyncMapping &m) {
                    return m.sourceBackend == id && m.sourceCalendar == palmCol.id;
                });
            if (alreadyCovered)
                continue;

            QString safeColId = palmCol.id;
            safeColId.replace(QLatin1Char(':'), QLatin1Char('_'))
                     .replace(QLatin1Char('/'), QLatin1Char('_'));

            const QString pcId = QStringLiteral("rawfiles-%1-%2").arg(id, safeColId);
            const QString rootPath = QDir(m_profilePath).filePath(
                QStringLiteral("rawfiles/%1/%2").arg(id, safeColId));

            const auto palmShape = registeredSrc
                ? registeredSrc->shapeFor(palmCol.id)
                : Kalburator::Shape::Shape::Any();

            auto pcBackend = std::make_unique<Kalburator::Sinks::RawFilesBackend>(rootPath);
            Kalburator::Sync::CollectionInfo pcCol;
            pcCol.id   = safeColId;
            pcCol.name = palmCol.name;
            pcBackend->createCollection(pcCol, palmShape);

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

            qDebug() << "[PalmRuntime::finishConnect] Default mapping:"
                     << palmCol.id << "->" << rootPath;
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
    if (m_running) {
        // Cannot disconnect while a sync/backup/restore is in flight — the
        // running lambda holds a raw KPilotLink* captured from m_device->link().
        // Tearing down m_device here would leave that pointer dangling.
        // The UI should disable the disconnect action while isRunning(); this
        // is the safety net for any caller that bypasses that guard.
        qWarning() << "[PalmRuntime] disconnectDevice() ignored — run in flight";
        return;
    }
    if (m_device) m_device->disconnectDevice();   // emits deviceDisconnected via forward
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
    // K.8b T6: palm backends are now registered in finishConnect(), not via
    // registerPluginForTest().  Invoke finishConnect() here so tests that
    // inject a pre-built device still get their backends wired up.
    // finishConnect() already emits deviceConnected() at its end — no second
    // emit here (that would be a double-emission and confuse any listeners
    // that gate on that signal arriving exactly once).
    finishConnect();
}

// K.8b T13: registerPluginForTest overloads removed (signatures deleted in
// palmruntime.h). Bodies were no-ops since K.8b T6; no live callers.

void PalmRuntime::registerBackendInstanceForTest(const QString &id,
                                                  std::unique_ptr<Kalburator::Sync::SyncBackend> backend)
{
    if (!backend) return;
    m_registry->registerBackendInstance(id, backend.get());
    m_ownedBackends.push_back(std::move(backend));
}

void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
    m_engine->setSyncMappings(m_mappings);
}

void PalmRuntime::setConflictHandler(
    Kalburator::Conflict::ConflictHandler *handler)
{
    m_conflictHandler = handler;
    if (m_engine) {
        m_engine->conflictRegistry()->setDefaultHandler(handler);
    }
}

Kalburator::Conflict::ConflictHandler *
PalmRuntime::conflictHandlerForTest() const
{
    if (!m_engine) return nullptr;
    return m_engine->conflictRegistry()->handlerFor(QString{});
}

QList<QString> PalmRuntime::enabledPluginIds() const {
    QList<QString> ids;
    if (m_pluginManager) {
        for (const auto &lp : m_pluginManager->loaded())
            ids.append(lp.id);
    }
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
    if (m_device) m_device->pauseTickle();

    auto engineFuture = m_engine->runSyncFuture(
        ids, Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored);

    // K.8b T16: install cancellation watcher on the engine future so
    // cancelSync() can propagate cancel() into SyncEngine::onCancelObserved.
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    m_activeSyncWatcher = new QFutureWatcher<void>(this);
    connect(m_activeSyncWatcher, &QFutureWatcher<void>::finished,
            this, [this]() {
                if (m_activeSyncWatcher) {
                    m_activeSyncWatcher->deleteLater();
                    m_activeSyncWatcher = nullptr;
                }
            });
    m_activeSyncWatcher->setFuture(engineFuture);

    return engineFuture.then([this](QList<Kalburator::Sync::SyncResult> results) {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        PalmRunResult::PluginStats stats;
        for (const auto &sr : results) {
            if (!sr.success && !sr.cancelled && !sr.skipped) {
                r.success = false;
                // K.9: surface the first real failure to the UI. Without
                // this, runFinished arrived with an empty errorMessage
                // and the log read "HotSync finished with errors: "
                // with nothing after the colon.
                if (r.errorMessage.isEmpty() && !sr.errorMessage.isEmpty())
                    r.errorMessage = sr.errorMessage;
            }
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
            if (m_device) m_device->resumeTickle();
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
    if (m_device) m_device->pauseTickle();

    // For M3: calendar-only, single mapping. Dispatch only the first enabled
    // mapping; Plan 3 (M4) will add multi-mapping iteration once other plugins
    // are re-enabled.
    auto engineFuture = m_engine->runSyncFuture(ids.first(), ov);

    // K.8b T16: install cancellation watcher (same pattern as runAllMappings).
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    m_activeSyncWatcher = new QFutureWatcher<void>(this);
    connect(m_activeSyncWatcher, &QFutureWatcher<void>::finished,
            this, [this]() {
                if (m_activeSyncWatcher) {
                    m_activeSyncWatcher->deleteLater();
                    m_activeSyncWatcher = nullptr;
                }
            });
    m_activeSyncWatcher->setFuture(engineFuture);

    return engineFuture.then([this](Kalburator::Sync::SyncResult sr) {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = sr.success;
        // K.9: propagate engine error message to the UI (see runAllMappings).
        if (!sr.success)
            r.errorMessage = sr.errorMessage;

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
            if (m_device) m_device->resumeTickle();
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

    KPilotLink *link = m_device ? m_device->link() : nullptr;
    if (!link) {
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
    m_device->pauseTickle();

    // Snapshot the DB list on the calling (main) thread — KPilotLink is not
    // thread-safe; all link calls must happen before we hand off to the pool.
    const QStringList databases = link->listDatabases();
    const QString backupDir = m_backupRoot;

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
            if (m_device) m_device->resumeTickle();
            emit runFinished(r);
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::restore()
{
    emit runStarted(QStringLiteral("Restore"));

    KPilotLink *link = m_device ? m_device->link() : nullptr;
    if (!link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("restore: no device connected");
        emit runFinished(r);
        return QtFuture::makeReadyValueFuture(r);
    }

    const QString backupDir = m_backupRoot;

    m_device->pauseTickle();

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
            if (m_device) m_device->resumeTickle();
            emit runFinished(r);
        });
        return r;
    });
}

}  // namespace WildPalms::Runtime
