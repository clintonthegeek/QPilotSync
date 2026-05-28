#include "palmruntime.h"
#include "palmticklephase.h"
#include "palmdeviceaccess.h"
#include "palm/kpilotlink.h"
#include "palm/kpilotdevicelink.h"

#include <QPromise>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QtConcurrent>

#include "backendregistry.h"
#include "syncengine.h"
#include "conflicthandlerregistry.h"
#include "syncbackend.h"
#include "synctypes.h"
#include "collectioninfo.h"
#include "conflictrecord.h"
#include <isynchost.h>
#include <baselinestore.h>
#include <isyncconfigstore.h>
#include <imassdeleteguard.h>
#include "shape.h"
// K.8b T13: ibackendplugin_v2.h include removed — V2 plugin ABI deleted.
#include "palm/device/pilotlinkpalmdatabaseaccess.h"

// O7: stock domain/infra plugins, loaded in one batch with WP's plugins
// (mirrors PlanStan's composition root). DAV provider plugins are omitted —
// registerStandardContributions() seeds those backend contributions.
#include <universalstorageplugin.h>
#include <blobplugin.h>
#include <noteplugin.h>
#include <todoplugin.h>
#include <contactsplugin.h>
#include <calendarplugin.h>

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

#include "profile.h"

#include <genericsqlitebackend.h>
#include <logicalcalendar.h>
#include <syncmappinggenerator.h>

#include "standardcontributions.h"

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

// O7: stock domain/infra plugins DEFINE their canonical domain.
static Kalburator::PluginManifest mkStockManifest(const QString &id,
                                                  QStringList defines = {})
{
    Kalburator::PluginManifest m;
    m.id                      = id;
    m.version                 = QStringLiteral("1.0");
    m.displayName             = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.definesDomains          = std::move(defines);
    return m;
}

// O7: WP Palm plugins contribute a (domain,palm) peer shape + palm<->peer edges
// via shapeContributions(); applyPlugin requires the augmented domain to be in
// the manifest, so each REQUIRES its canonical domain. The stock definer must be
// in the SAME loadInProcess batch (resolve() only sees the current batch and
// orders requirers after definers) — see registerPalmPlugins().
static Kalburator::PluginManifest mkPalmManifest(const QString &id,
                                                 const QString &domain)
{
    Kalburator::PluginManifest m;
    m.id                      = id;
    m.version                 = QStringLiteral("1.0");
    m.displayName             = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.requiresDomains         = { domain };
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
          QDir(profilePath).filePath(QStringLiteral(".state/.wildpalms-blob-baselines.db"))))
{
    qRegisterMetaType<PalmRunResult>();

    // Register provider contributions into this runtime's local registry.
    // ProviderManager no longer auto-registers these (K.8a T6); the
    // application layer is responsible for seeding contributions.
    // F.1c.1 T2: the same registration is needed for KF6MainWindow's
    // app-level registry (used by NewProfileWizard for pre-profile
    // discovery), so the calls live in a shared free function.
    WildPalms::Runtime::registerStandardContributions(m_registry.get());

    // C Task 2: stand up the per-profile SQLite hub. The .state/ dir already
    // exists because BaselineStore uses it (.state/.wildpalms-blob-baselines.db).
    m_hub = std::make_unique<Kalburator::Sinks::GenericSqliteBackend>(
        QDir(profilePath).filePath(QStringLiteral(".state/hub.db")));
    m_registry->registerBackendInstance(QStringLiteral("wp-hub"), m_hub.get());

    m_syncHost = std::make_unique<PalmSyncHost>(m_registry.get());
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get(), m_shape);
    m_engine->setBaselineStore(m_baselineStore.get());
    // No-op today (handler set after construction), but keeps this consistent
    // with the re-install pattern required at every engine-construction site.
    if (m_conflictHandler)
        m_engine->conflictRegistry()->setDefaultHandler(m_conflictHandler);

    QObject::connect(m_engine.get(),
                     &Kalburator::Sync::SyncEngine::conflictDetected,
                     this, &PalmRuntime::conflictDetected);

    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::syncStarted,
                     this, [this](const QString &mappingId) {
        m_activeMappingId = mappingId;
        QString label, icon;
        resolveMappingIdentity(mappingId, label, icon);

        // P2: Determine whether Palm is source, target, or both in this
        // mapping.  Palm backend IDs match plugin->pluginId() values
        // ("calendar", "contacts", "memo", "todo") — collect them once
        // from m_palmPlugins, then compare against the mapping's backends.
        using namespace WildPalms::CalendarPlugin;
        using namespace WildPalms::ContactsPlugin;
        using namespace WildPalms::Memo;
        using namespace WildPalms::TodoPlugin;
        QSet<QString> palmIds;
        for (const auto &plugin : m_palmPlugins) {
            if (auto *p = dynamic_cast<CalendarBackendPlugin *>(plugin.get()))
                palmIds.insert(p->pluginId());
            else if (auto *p = dynamic_cast<ContactsBackendPlugin *>(plugin.get()))
                palmIds.insert(p->pluginId());
            else if (auto *p = dynamic_cast<MemoPlugin *>(plugin.get()))
                palmIds.insert(p->pluginId());
            else if (auto *p = dynamic_cast<TodoBackendPlugin *>(plugin.get()))
                palmIds.insert(p->pluginId());
        }
        m_currentPalmIsSource = false;
        m_currentPalmIsTarget = false;
        for (const auto &m : m_mappings) {
            if (m.id == mappingId) {
                m_currentPalmIsSource = palmIds.contains(m.sourceBackend);
                m_currentPalmIsTarget = palmIds.contains(m.targetBackend);
                break;
            }
        }

        Q_EMIT mappingSyncStarted(mappingId, label, icon);
    });

    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::phaseChanged,
                     this, [this](Kalburator::Engine::SyncEngine::SyncPhase phase) {
        // P2: Pause tickle only during phases that issue DLP calls to the
        // Palm device. CalDAV / network fetch phases keep the tickle alive
        // so the Palm doesn't think the connection dropped.
        if (m_device) {
            if (shouldPauseTickle(phase, m_currentPalmIsSource, m_currentPalmIsTarget))
                m_device->pauseTickle();
            else
                m_device->resumeTickle();
        }
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::progressUpdated,
                     this, [this](int current, int total, const QString &message) {
        Q_EMIT runProgress(current, total, message);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::fetchProgress,
                     this, [this](const QString &, int current, int total) {
        if (!m_activeMappingId.isEmpty())
            Q_EMIT mappingSyncProgress(m_activeMappingId, /*phase=*/0, current, total);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::writeProgress,
                     this, [this](const QString &, int current, int total) {
        if (!m_activeMappingId.isEmpty())
            Q_EMIT mappingSyncProgress(m_activeMappingId, /*phase=*/1, current, total);
    });

    // K.8b T6: load the five static Palm plugins in-process.
    registerPalmPlugins();

    // C Task 2: create per-domain canon collections in the hub.
    ensureHubCollections();

    QObject::connect(this, &PalmRuntime::runStarted,
            this, [this]() { m_running = true; });
    QObject::connect(this, &PalmRuntime::runFinished,
            this, [this]() { m_running = false; });
}

PalmRuntime::~PalmRuntime() = default;

void PalmRuntime::registerPalmPlugins()
{
    using namespace WildPalms::CalendarPlugin;
    using namespace WildPalms::ContactsPlugin;
    using namespace WildPalms::Memo;
    using namespace WildPalms::TodoPlugin;

    // Always create fresh plugin instances — each PalmRuntime owns its own
    // set so that createPalmBackend() and createConflictHandler() can hold
    // per-runtime state (device pointer, category store, etc.).
    auto cal  = std::make_unique<CalendarBackendPlugin>();
    auto con  = std::make_unique<ContactsBackendPlugin>();
    auto memo = std::make_unique<MemoPlugin>();
    auto todo = std::make_unique<TodoBackendPlugin>();

    // O7 (v0.57): one PluginManager + ONE loadInProcess batch, populating this
    // PalmRuntime's own m_shape (no process-global singletons). The batch holds
    // the stock domain/infra plugins (which DEFINE the canonical domains + peer
    // shapes ical/vcard4/canon/ical-vtodo) plus WP's four plugins (which REQUIRE
    // those domains and contribute the (domain,palm) peer + palm<->peer edges).
    // resolve() orders requirers after definers within the batch. Mirrors
    // PlanStan/src/app/appcontroller.cpp. DAV provider plugins are intentionally
    // omitted — registerStandardContributions() (above) already seeds the
    // CalDav/CardDav/Akonadi backend contributions. Per-instance m_shape means a
    // second PalmRuntime re-runs this into its own fresh registries (the old
    // s_globalRegistrationDone guard is no longer needed).
    m_pluginManager =
        std::make_unique<Kalburator::PluginManager>(m_registry.get(), m_shape);

    static Kalburator::UniversalStoragePlugin  s_universal;
    static Kalburator::Blob::BlobPlugin        s_blob;
    static Kalburator::Note::NotePlugin        s_note;
    static Kalburator::Todo::TodoPlugin        s_todo;
    static Kalburator::Contacts::ContactsPlugin s_contacts;
    static Kalburator::Calendar::CalendarPlugin s_calendar;

    QList<QPair<Kalburator::Plugin *, Kalburator::PluginManifest>> items{
        { &s_universal, mkStockManifest(QStringLiteral("kalburator.universal-storage")) },
        { &s_blob,      mkStockManifest(QStringLiteral("kalburator.blob"),     {QStringLiteral("blob")}) },
        { &s_note,      mkStockManifest(QStringLiteral("kalburator.note"),     {QStringLiteral("note")}) },
        { &s_todo,      mkStockManifest(QStringLiteral("kalburator.todo"),     {QStringLiteral("todo")}) },
        { &s_contacts,  mkStockManifest(QStringLiteral("kalburator.contacts"), {QStringLiteral("contacts")}) },
        { &s_calendar,  mkStockManifest(QStringLiteral("kalburator.calendar"), {QStringLiteral("calendar")}) },
        { cal.get(),    mkPalmManifest(QStringLiteral("wildpalms.calendar"), QStringLiteral("calendar")) },
        { con.get(),    mkPalmManifest(QStringLiteral("wildpalms.contacts"), QStringLiteral("contacts")) },
        { memo.get(),   mkPalmManifest(QStringLiteral("wildpalms.memo"),     QStringLiteral("note"))     },
        { todo.get(),   mkPalmManifest(QStringLiteral("wildpalms.todo"),     QStringLiteral("todo"))     },
    };

    if (!m_pluginManager->loadInProcess(items)) {
        qWarning() << "[PalmRuntime] plugin load rejected:"
                   << m_pluginManager->rejected().size();
        return;
    }

    m_palmPlugins.push_back(std::move(cal));
    m_palmPlugins.push_back(std::move(con));
    m_palmPlugins.push_back(std::move(memo));
    m_palmPlugins.push_back(std::move(todo));
}

void PalmRuntime::ensureHubCollections()
{
    using Kalburator::Shape::Shape;
    using Kalburator::Shape::DomainId;
    using Kalburator::Shape::EncodingId;

    const std::pair<const char *, const char *> domains[] = {
        { "calendar", "calendar" }, { "contacts", "contacts" },
        { "todo",     "todo"     }, { "note",     "note"     },
    };

    for (const auto &[colId, dom] : domains) {
        Kalburator::Sync::CollectionInfo info;
        info.id   = QString::fromLatin1(colId);
        info.name = QString::fromLatin1(colId);
        info.type = QString::fromLatin1(dom);
        m_hub->createCollection(
            info,
            Shape{ DomainId{QString::fromLatin1(dom)},
                   EncodingId{QStringLiteral("canon")} });
    }
}

void PalmRuntime::connectDevice(const QStringList &devicePaths)
{
    if (!m_device) {
        m_device = std::make_unique<PalmDeviceAccess>(this);

        QObject::connect(m_device.get(), &PalmDeviceAccess::connectionStarted,
                this, &PalmRuntime::connectionStarted);

        QObject::connect(m_device.get(), &PalmDeviceAccess::connectionComplete,
                this, [this](bool ok, const QString &err) {
                    Q_EMIT connectionComplete(ok, err);
                    if (ok) {
                        finishConnect();
                    }
                });

        QObject::connect(m_device.get(), &PalmDeviceAccess::deviceDisconnected,
                this, &PalmRuntime::deviceDisconnected);

        QObject::connect(m_device.get(), &PalmDeviceAccess::logMessage,
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

// P4 Investigation finding (2026-05-27):
// DAV mappings ARE persisted in mappings.conf and ARE loaded into m_mappings by
// loadMappingsFromProfile() at the top of finishConnect(). This is Branch B, not A:
// the mapping rows reference CalDAV backend IDs (e.g. "<uuid>:TBS") that are only
// registered into BackendRegistry after AccountController's async ProviderManager
// connectAll() completes — which happens long after finishConnect() emits readyForSync.
// Result: the first auto-sync runs with the mapping list intact but the DAV SyncBackend
// pointers unresolvable, so the engine silently skips those mappings and only syncs
// the rawfiles fallbacks. Fix = gate readyForSync (or auto-sync) until all provider
// connections have completed (i.e. until BackendRegistry holds the DAV backend entries).
void PalmRuntime::finishConnect()
{
    if (!m_device) return;

    // TODO(C-remote): in sub-project C the Star-mapping block below REPLACES
    // m_mappings unconditionally, so the user's persisted remote (CalDAV/etc.)
    // mappings are intentionally superseded by Palm<->hub for now. The next
    // sub-project reintroduces them as hub<->remote bindings; this load call is
    // kept as the seam where that merge will happen.
    loadMappingsFromProfile();

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
        }

        if (!ownedBackend) {
            if (!id.isEmpty())
                qWarning() << "[PalmRuntime::finishConnect] Plugin" << id
                           << "returned null backend";
            continue;
        }

        // F.3: write the category-slot snapshot for this plugin's primary
        // database into the borrowed Profile, if one is set. Each plugin's
        // createPalmBackend has already populated its internal
        // CategoryMappingStore from the live AppInfo block.
        if (m_profile) {
            QString primaryDbName;
            QStringList slotNames;
            if (auto *p = dynamic_cast<CalendarBackendPlugin *>(plugin.get())) {
                primaryDbName = p->primaryDbName();
                slotNames = p->categorySlotNames();
            } else if (auto *p = dynamic_cast<ContactsBackendPlugin *>(plugin.get())) {
                primaryDbName = p->primaryDbName();
                slotNames = p->categorySlotNames();
            } else if (auto *p = dynamic_cast<MemoPlugin *>(plugin.get())) {
                primaryDbName = p->primaryDbName();
                slotNames = p->categorySlotNames();
            } else if (auto *p = dynamic_cast<TodoBackendPlugin *>(plugin.get())) {
                primaryDbName = p->primaryDbName();
                slotNames = p->categorySlotNames();
            }
            if (!primaryDbName.isEmpty() && slotNames.size() == 16) {
                m_profile->setCategorySlotNames(primaryDbName, slotNames);
            }
        }

        m_registry->registerBackendInstance(id, ownedBackend.get());
        m_ownedBackends.push_back(std::move(ownedBackend));

        qDebug() << "[PalmRuntime::finishConnect] Registered backend plugin:" << id;
    }

    // C Task 4: build domain-level Palm<->hub Star mappings via generateMappings.
    // Each connected Palm backend is wired to its corresponding hub collection
    // using LogicalCalendar/generateMappings (Star topology = hub-and-spoke).
    // The hub (wp-hub) is Primary; each Palm backend collection is Sync1.
    using Kalburator::Sync::LogicalCalendar;
    using Kalburator::Sync::CalendarBackendBinding;
    using Kalburator::Sync::BackendRole;
    QList<LogicalCalendar> lcs;
    // (palmBackendId, hubCollectionId, palmDomainCollectionId)
    const std::tuple<QString, QString, QString> wiring[] = {
        { QStringLiteral("calendar"), QStringLiteral("calendar"), QStringLiteral("palm:calendar") },
        { QStringLiteral("contacts"), QStringLiteral("contacts"), QStringLiteral("palm:contacts") },
        { QStringLiteral("memo"),     QStringLiteral("note"),     QStringLiteral("palm:note")     },
        { QStringLiteral("todo"),     QStringLiteral("todo"),     QStringLiteral("palm:todo")     },
    };
    for (const auto &[palmId, hubCol, palmCol] : wiring) {
        if (!m_registry->backendInstance(palmId)) continue;   // backend not connected this session
        LogicalCalendar lc;
        lc.id = QStringLiteral("wp-%1").arg(hubCol);
        lc.domain = Kalburator::Shape::DomainId{hubCol};
        lc.displayName = hubCol;
        lc.syncEnabled = true;
        CalendarBackendBinding hubB;
        hubB.backendId = QStringLiteral("wp-hub");
        hubB.calendarId = hubCol;
        hubB.role = BackendRole::Primary;
        lc.bindings.append(hubB);
        CalendarBackendBinding palmB;
        palmB.backendId = palmId;
        palmB.calendarId = palmCol;
        palmB.role = BackendRole::Sync1;
        palmB.syncOrder = 1;
        lc.bindings.append(palmB);
        lcs.append(lc);
    }
    // generateMappings emits TwoWay mappings with conflictPolicy=AskUser. This
    // is a deliberate change from the old per-slot RawFiles default
    // (LastWriteWins): on the hub topology, Palm<->hub conflicts surface to
    // WildPalms' existing conflict handlers / deferred conflict store for review
    // rather than silently auto-resolving and risking data loss.
    m_mappings = Kalburator::Sync::generateMappings(lcs, Kalburator::Sync::SyncTopology::Star);
    m_engine->setSyncMappings(m_mappings);

    Q_EMIT deviceConnected();
    Q_EMIT readyForSync();
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

void PalmRuntime::setProfile(Profile *profile)
{
    m_profile = profile;
    // Make saved mappings available immediately (before device connect), so
    // palmMappings() and any pre-connect logic reflect the persisted set.
    // finishConnect() reloads again to stay authoritative across reconnects.
    loadMappingsFromProfile();
}

void PalmRuntime::loadMappingsFromProfile()
{
    if (!m_profile) return;   // test/no-profile: keep injected mappings as-is
    m_mappings.clear();
    const QJsonArray saved = m_profile->syncMappingsJson();
    for (const auto &v : saved) {
        if (v.isObject())
            m_mappings.append(Kalburator::Sync::syncMappingFromJson(v.toObject()));
    }
    if (m_engine)
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

void PalmRuntime::setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard)
{
    if (m_engine) {
        m_engine->setMassDeleteGuard(guard);
    }
}

Kalburator::Conflict::ConflictHandler *
PalmRuntime::conflictHandlerForTest() const
{
    if (!m_engine) return nullptr;
    return m_engine->conflictRegistry()->handlerFor(QString{});
}

Kalburator::Sync::SyncConflictStore *
PalmRuntime::syncConflictStore() const
{
    return m_engine ? m_engine->syncConflictStore() : nullptr;
}

Kalburator::Conflict::ConflictRecord
PalmRuntime::toConflictRecord(const Kalburator::Sync::ConflictInfo &info)
{
    Kalburator::Conflict::ConflictRecord rec;
    rec.conflictId  = info.conflictId.isEmpty()
                      ? Kalburator::Conflict::ConflictRecord::generateId()
                      : info.conflictId;
    rec.conduitId     = info.mappingId;
    rec.syncSessionId = info.mappingId;
    rec.detectedAt    = info.detectedAt.isValid()
                        ? info.detectedAt
                        : QDateTime::currentDateTime();
    rec.type        = Kalburator::Conflict::ConflictType::BothModified;

    rec.source.id            = info.sourceId;
    rec.source.description   = info.sourceDescription;
    rec.source.content       = info.sourceIcalData.toUtf8();
    rec.source.contentType   = QStringLiteral("text/calendar");
    rec.source.lastModified  = info.sourceModified;

    rec.target.id            = info.targetId;
    rec.target.description   = info.targetDescription;
    rec.target.content       = info.targetIcalData.toUtf8();
    rec.target.contentType   = QStringLiteral("text/calendar");
    rec.target.lastModified  = info.targetModified;

    return rec;
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

void PalmRuntime::resolveMappingIdentity(const QString &mappingId,
                                         QString &outLabel,
                                         QString &outIconName) const
{
    using namespace WildPalms::CalendarPlugin;
    using namespace WildPalms::ContactsPlugin;
    using namespace WildPalms::Memo;
    using namespace WildPalms::TodoPlugin;

    outLabel = mappingId;
    outIconName = QStringLiteral("view-list-details");
    static const QHash<QString, QString> kIcons = {
        { QStringLiteral("calendar"), QStringLiteral("office-calendar") },
        { QStringLiteral("contacts"), QStringLiteral("x-office-address-book") },
        { QStringLiteral("memo"),     QStringLiteral("text-x-generic") },
        { QStringLiteral("todo"),     QStringLiteral("view-task") },
        { QStringLiteral("plucker"),  QStringLiteral("text-html") },
    };
    for (const auto &m : m_mappings) {
        if (m.id != mappingId)
            continue;
        // Base Kalburator::Plugin has no pluginId()/displayName(); the concrete
        // Palm plugin subclasses do. Match m.sourceBackend (a bare plugin id
        // like "calendar") against each plugin's pluginId() via dynamic_cast,
        // mirroring the dispatch pattern in finishConnect().
        for (const auto &plugin : m_palmPlugins) {
            QString pid;
            QString name;
            if (auto *p = dynamic_cast<CalendarBackendPlugin *>(plugin.get())) {
                pid = p->pluginId(); name = p->displayName();
            } else if (auto *p = dynamic_cast<ContactsBackendPlugin *>(plugin.get())) {
                pid = p->pluginId(); name = p->displayName();
            } else if (auto *p = dynamic_cast<MemoPlugin *>(plugin.get())) {
                pid = p->pluginId(); name = p->displayName();
            } else if (auto *p = dynamic_cast<TodoBackendPlugin *>(plugin.get())) {
                pid = p->pluginId(); name = p->displayName();
            } else {
                continue;
            }
            if (pid == m.sourceBackend) {
                outLabel = name;
                outIconName = kIcons.value(m.sourceBackend,
                                           QStringLiteral("view-list-details"));
                return;
            }
        }
        return;
    }
}

QVector<PalmRuntime::ConduitDescriptor> PalmRuntime::conduitDescriptors() const
{
    QVector<ConduitDescriptor> out;
    for (const auto &m : m_mappings) {
        if (!m.enabled)
            continue;
        ConduitDescriptor d;
        d.mappingId = m.id;
        resolveMappingIdentity(m.id, d.label, d.iconName);
        out.append(d);
    }
    return out;
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

    // P2 Investigation (2026-05-27): pauseTickle() rationale
    // Historical context (May 2, 2026, commit 0fefb6b): The original TickleWorker
    // ran on a separate thread and interleaved dlp_GetSysDateTime() with DLP calls
    // from QtConcurrent pool threads, corrupting the DLP session state. The fix was
    // to synchronize by pausing the tickle before bulk DLP work.
    //
    // Current architecture: PalmDeviceAccess owns m_linkThread (a QThread). All
    // DLP calls marshal to m_implOwner on m_linkThread via BlockingQueuedConnection,
    // and PalmTickle is parented to m_implOwner, so both tickle timer and DLP calls
    // run on m_linkThread's event loop. BlockingQueuedConnection blocks the caller
    // until the link thread finishes, guaranteeing serialization — the tickle timer
    // can ONLY fire between (or outside) DLP operations, never during them.
    //
    // Hazard assessment: Cross-thread race (the original issue) is eliminated.
    // However, if dlp_GetSysDateTime and in-progress DLP operations have protocol-level
    // conflicts on the Palm wire (independent of thread safety), the pause is still
    // necessary. Task 2.2 narrowed this to apply-phase only via phaseChanged signal;
    // the blanket pre-run pause is removed — the tickle is now paused/resumed per
    // phase via the phaseChanged lambda in the constructor.

    auto engineFuture = m_engine->runSyncFuture(
        ids, Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored);

    // K.8b T16: install cancellation watcher on the engine future so
    // cancelSync() can propagate cancel() into SyncEngine::onCancelObserved.
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    m_activeSyncWatcher = new QFutureWatcher<void>(this);
    QObject::connect(m_activeSyncWatcher, &QFutureWatcher<void>::finished,
            this, [this]() {
                if (m_activeSyncWatcher) {
                    m_activeSyncWatcher->deleteLater();
                    m_activeSyncWatcher = nullptr;
                }
            });
    m_activeSyncWatcher->setFuture(engineFuture);

    return engineFuture.then([this, ids](QList<Kalburator::Sync::SyncResult> results) {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        PalmRunResult::PluginStats stats;
        int linkLostCount = 0;
        for (const auto &sr : results) {
            if (!sr.success && !sr.cancelled && !sr.skipped) {
                r.success = false;
                if (sr.errorMessage.contains(QLatin1String("Palm link"),
                                             Qt::CaseInsensitive)) {
                    ++linkLostCount;
                } else if (r.errorMessage.isEmpty() && !sr.errorMessage.isEmpty()) {
                    r.errorMessage = sr.errorMessage;
                }
            }
            stats.created   += sr.targetStats.created;
            stats.updated   += sr.targetStats.updated;
            stats.deleted   += sr.targetStats.deleted;
            stats.unchanged += sr.targetStats.unchanged;
            stats.errors    += (sr.success ? 0 : 1);
        }
        // Layer B: collapse N "Palm link lost" errors into one summary so
        // the UI shows a single message instead of repeating the same string.
        if (linkLostCount > 0 && r.errorMessage.isEmpty()) {
            r.errorMessage = QStringLiteral(
                "HotSync aborted: Palm device disconnected "
                "(%1 of %2 mappings affected)").arg(linkLostCount).arg(results.size());
        }
        if (!results.isEmpty())
            r.perPluginStats.insert(QStringLiteral("calendar"), stats);

        // Per-mapping finished — chips fill their counts here (run-end only;
        // the engine has no per-mapping completion signal).
        for (int i = 0; i < results.size() && i < ids.size(); ++i) {
            const auto &sr = results[i];
            const auto &ts = sr.targetStats;
            QMetaObject::invokeMethod(this, [this, id = ids[i], ts, sr]() {
                Q_EMIT mappingSyncFinished(id, ts.created, ts.updated, ts.deleted,
                                           sr.success && !sr.cancelled);
            });
        }

        r.endTime = QDateTime::currentDateTimeUtc();
        QMetaObject::invokeMethod(this, [this, r]() {
            if (m_device) m_device->flushWrites();    // close last mapping's DB before EndOfSync
            if (m_device) m_device->resumeTickle();
            m_activeMappingId.clear();
            Q_EMIT runFinished(r);
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::hotSync() {
    Q_EMIT runStarted(QStringLiteral("HotSync"));
    if (m_mappings.isEmpty())
        return makeSuccessFuture();
    return runAllMappings();
}

QFuture<PalmRunResult> PalmRuntime::fullSync()
{
    Q_EMIT runStarted(QStringLiteral("FullSync"));
    // Clear all baselines so the engine treats this as a fresh first sync.
    for (const auto &m : m_mappings)
        m_baselineStore->clearMappingV3(m.id);
    return runAllMappings();
}

QFuture<PalmRunResult> PalmRuntime::runMirror(MirrorDir dir, const QString &modeLabel)
{
    Q_EMIT runStarted(modeLabel);

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

    // P2: Blanket pauseTickle() removed here (same as runAllMappings).
    // Tickle is now paused/resumed per phase via the phaseChanged lambda
    // in the constructor. The resumeTickle() in
    // the then() lambda below remains as a safety net.

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
    QObject::connect(m_activeSyncWatcher, &QFutureWatcher<void>::finished,
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
            if (m_device) m_device->flushWrites();    // close last mapping's DB before EndOfSync
            if (m_device) m_device->resumeTickle();
            Q_EMIT runFinished(r);
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
    Q_EMIT runStarted(QStringLiteral("Backup"));

    KPilotLink *link = m_device ? m_device->link() : nullptr;
    if (!link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("backup: no device connected");
        Q_EMIT runFinished(r);
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
            Q_EMIT runFinished(r);
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::restore()
{
    Q_EMIT runStarted(QStringLiteral("Restore"));

    KPilotLink *link = m_device ? m_device->link() : nullptr;
    if (!link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("restore: no device connected");
        Q_EMIT runFinished(r);
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
            Q_EMIT runFinished(r);
        });
        return r;
    });
}

}  // namespace WildPalms::Runtime
