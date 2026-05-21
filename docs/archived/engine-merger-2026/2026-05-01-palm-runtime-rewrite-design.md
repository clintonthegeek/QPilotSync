# Palm runtime rewrite — design

**Date:** 2026-05-01
**Status:** Draft, awaiting review
**Scope:** Cross-repo (libkalburator + WildPalms). PlanStan unaffected.
**Successor to:** Phase G's deferred Tasks 55/58 (SyncRunner_wp deletion + F1 facade deletion).

## 1. Why this exists

Phase G unified libkalburator's sync engine onto a mapping-driven,
futures-based API and built a shape pipeline rich enough to express
arbitrary domain transformations. WildPalms was carried through G
in incremental steps, but the cost of keeping it compiling at every
intermediate state was high enough that several pieces were
deferred:

- `SyncRunner_wp` (517 LOC) still drives the six legacy sync modes
  via `SyncEngine::runBlobMirror`/`runBlobTwoWay` — the F1 facade
  Phase G was supposed to delete.
- `DeviceSession`/`DeviceWorker` are mode-dispatched rather than
  generic, because `SyncRunner::run(mode)` is what they're shaped
  around.
- The Palm backends (`PalmCalendarBackend` etc.) call
  `IPalmDatabaseAccess` synchronously and trust their caller to be
  on the link thread. This is fine when `SyncRunner_wp` invokes
  them via `DeviceWorker::doSyncRunner`, but **broken** when the
  unified `SyncEngine`'s worker thread invokes them via
  `HotSyncCoordinator → runSyncFuture(palmMappings)`. The bug is
  latent because it's only exercised by tests that use
  `MockBlobBackend`, never by real-device runs.

Rather than continue the incremental-with-glue strategy, this
design rewrites WildPalms's runtime and shell from scratch on a
new branch. The conduit plugin system and the `KPilotLink` wrapper
are preserved (in modified form for the plugin contract); almost
everything else is replaced. libkalburator gets one small
extension and one small API addition.

## 2. End-state architecture

### Layers

```
┌─────────────────────────────────────────────────────────────┐
│ WildPalms GUI (KF6MainWindow / KXmlGuiWindow + KActions     │  shell
│   + Tools menu + plugin views)                              │
├─────────────────────────────────────────────────────────────┤
│ PalmRuntime  (sync modes, mapping config, plugin registry)  │  app
├──────────────────────┬──────────────────────────────────────┤
│ PalmDeviceAccess     │ IBackendPlugin instances (loaded     │
│ (link-thread owner;  │  via KPluginFactory)                 │  app
│  wraps KPilotLink)   │   ↳ each provides Palm IBlobBackend  │
│                      │     and may register a DomainPlugin  │
├──────────────────────┴──────────────────────────────────────┤
│ KPilotLink (pilot-link C library wrapper)                   │  device
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ uses
┌─────────────────────────────────────────────────────────────┐
│ libkalburator                                               │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ SyncEngine (mapping-driven, QFuture-based)          │    │
│  ├─────────────────────────────────────────────────────┤    │
│  │ DomainAdapter (calendar | blob)                     │    │
│  ├─────────────────────────────────────────────────────┤    │
│  │ Shape pipeline: TransformationRegistry +            │    │
│  │   DomainRegistry (now dynamic)                      │    │
│  ├─────────────────────────────────────────────────────┤    │
│  │ Backends: SyncBackend, IBlobBackend, stock impls    │    │
│  │ (Local, RawFiles, GenericSqlite, Remote, Org, ...)  │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### What dies (in WildPalms)

- `SyncRunner_wp` (517 LOC) — replaced by `PalmRuntime`.
- `DeviceSession` (~280 LOC) — link-thread responsibility moves to
  `PalmDeviceAccess`; tickle into a separate small class.
- `DeviceWorker` (~190 LOC) — generic command-runner replaces
  mode-dispatched worker.
- `TickleWorker` — kept in spirit; folded into `PalmDeviceAccess`
  or kept as a small sibling.
- The mode-dispatch and runner-management code inside
  `KF6MainWindow` (~600 LOC of `m_session->requestSync(mode,
  m_syncRunner)` wiring, mode-conditional setup, runner ownership
  bookkeeping). The `KF6MainWindow` class itself stays — it's a
  `KXmlGuiWindow` subclass providing the KDE app shell (KActions,
  toolbars, KXmlGui menu layout, per-plugin view hosting). Tools-menu
  actions get rewired to call `PalmRuntime` methods directly.
- `Sync::SyncMode` enum's coupling to the runtime — the modes are
  now methods on `PalmRuntime`, not enum-dispatched values.

### What dies (in libkalburator)

- `SyncEngine::runBlobMirror`/`runBlobTwoWay` (the F1 facade,
  ~140 LOC).
- `BlobSyncResult`/`BlobSyncStats` if no callers remain.
- `tst_engine_blob_one_shot.cpp` (533 LOC). Coverage relevant to
  the engine moves into mapping-driven tests; coverage relevant to
  Palm-specific Backup/Restore semantics moves into WildPalms's
  `PalmRuntime` tests.
- `runBlobTwoWay_*`/`runBlobMirror_*` cases in
  `tst_engine_unified_boundary.cpp`.
- `SyncEngineWorker::dispatchFirstSync` self-call to
  `runBlobMirror` — inlined as a direct loop over `IBlobBackend`.

### What stays

- `KPilotLink` — the pilot-link wrapper. Unchanged; it's the only
  thing speaking actual DLP and there's no reason to touch it.
- `KF6MainWindow` (the class) — its KDE app-shell surface
  (`KXmlGuiWindow` base, `KActionCollection`, `KStandardAction`,
  KXmlGui menu/toolbar layout, per-plugin view hosting) is the
  KPilot-successor's user-facing shape. Internals get rewritten to
  call `PalmRuntime` instead of orchestrating runners directly, but
  the shell pattern stays.
- `IBackendPlugin` (modified). The plugin discovery/loading
  machinery (`BackendPluginManager`, KPluginFactory + .json
  metadata) is preserved.
- The five existing plugins (calendar, memo, contacts, todos,
  webcal). They get a small migration to the new contract; their
  Palm-side code is preserved.
- `PalmDeviceConnection`'s public interface as observed by plugins
  — but the backing implementation moves into `PalmDeviceAccess`.
- All of libkalburator's stock domains and backends. PlanStan
  unaffected.

### What's new

- `PalmRuntime` — top-level orchestrator, lives in WildPalms.
- `PalmDeviceAccess` — link-thread owner, wraps `KPilotLink`,
  exposes the synchronous-blocking API plugins call.
- New `IBackendPlugin` contract (Palm-only, plus optional
  `DomainPlugin` registration hook).
- Mapping config as a first-class persistent object.
- Inside the existing `KF6MainWindow` shell: a `PalmRuntime`-driven
  Tools menu (KActionCollection-registered), a sync-log dock widget,
  a mapping-editor settings dialog, a conflict-resolution dialog.
  Per-plugin view hosting unchanged.

## 3. libkalburator changes

Two small landings.

### 3.1 Dynamic `DomainPlugin` registration

Today, `DomainRegistry::initialize()` is called once at static-init
and the registry is treated as immutable thereafter. Callers can
re-`initialize()` but the contract assumes a fixed set of stock
plugins.

The change: allow `DomainRegistry::registerPlugin(plugin)` and the
underlying `TransformationRegistry::registerEdge`/`registerShape`
calls to be invoked at any point before the first `compile()` call
that touches the affected domain. After that point, the registry
treats the affected domain as frozen.

Implementation sketch:

- Add `DomainRegistry::registerPlugin(std::unique_ptr<DomainPlugin>)`
  as a public mutator.
- `TransformationRegistry` gains a per-domain "frozen" bit set on
  first successful `compile()` for a shape in that domain.
  Re-registration of edges/shapes within a frozen domain asserts
  in debug, returns false (and logs) in release.
- Multi-plugin contribution to a single domain: peer shapes and
  edges from multiple plugins are unioned. `declareCanonical(domain,
  shape)` remains set-once — only one plugin may declare canonical
  per domain. Subsequent calls with the same canonical are
  idempotent; conflicting calls are an error.
- `DomainPlugin::registerEdges(registry)` is the existing hook; no
  signature change.

Test (one new file):

- `tests/shape/tst_dynamic_domain_registration.cpp` covering:
  - Register a plugin after init, compile a pipeline that uses its
    shapes, succeeds.
  - Register two plugins for the same domain (one provides
    canonical + ical peer; the other provides a vCard peer).
    Compile pipelines that span both, succeeds.
  - Attempt to register after `compile()` of the affected domain
    asserts in debug.
  - Attempt to redeclare canonical with a different shape errors.

Estimated effort: 1-2 days including tests.

### 3.2 Mirror-direction override on `runSyncFuture`

The `Copy*` modes need a per-call execution override that says
"this mapping run is a one-way mirror in this direction, regardless
of how the mapping is normally configured." Today the engine
only supports the mapping's stored direction (which is implicitly
bidirectional).

The change: add an optional `ExecutionOverride` parameter to
`SyncEngine::runSyncFuture(mappingId, override)`:

```cpp
struct ExecutionOverride {
    enum class Direction { Default, MirrorAToB, MirrorBToA };
    Direction direction = Direction::Default;
    // future fields might include: forceFirstSync, suppressConflicts
};
```

Adapter implementations honor it: `Direction::MirrorAToB` means
"treat target's view as authoritative-to-overwrite, push everything
from source unconditionally, delete target records not in source."

Implementation: thread the override through
`SyncEngineWorker::processSync` to the `IDomainAdapter`. The blob
adapter's mirror semantics are already proven (the facade does
this); reuse the implementation, drop the facade.

Test (one new file):

- `tests/calendar/tst_engine_mirror_direction.cpp` covering both
  directions, against `MockBlobBackend` pairs, including the
  "delete records not in source" behavior that distinguishes mirror
  from twoway.

Estimated effort: 2-3 days including tests + facade deletion +
inlining `dispatchFirstSync`.

### 3.3 What does NOT change in libkalburator

- `SyncMapping` does not gain a `direction` field. Direction is a
  per-call concern, not a mapping property. (PlanStan's mappings
  remain bidirectional-implicit; nothing to migrate.)
- `SyncMapping` does not gain a `mode` field. Backup/Restore are
  WildPalms concerns, not engine concerns.
- `ISyncHost` is not extended. The G.9 narrowing stands.
- The engine worker thread is not the link thread. Backends are
  responsible for their own thread affinity (see §5.2 below).

## 4. New `IBackendPlugin` contract

Replaces today's `WildPalms::IBackendPlugin`.

```cpp
class IBackendPlugin : public IPlugin {
public:
    // ── Identity ────────────────────────────────────────────────
    QString pluginId() const override = 0;          // unchanged
    QString displayName() const = 0;                // unchanged

    // ── Database claims ─────────────────────────────────────────
    QStringList claimedDatabases() const = 0;       // unchanged

    // ── Palm-side backend ───────────────────────────────────────
    // The plugin's IBlobBackend for the Palm side. Must declare
    // nativeShapes() correctly so the engine can compile pipelines
    // to whatever PC-side backend the user has mapped to. The
    // returned backend MUST be safe to call from any thread —
    // typically by self-marshalling its IPalmDatabaseAccess calls
    // to the link thread (see §5.2). Caller takes ownership.
    virtual std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(PalmDeviceAccess *device) = 0;

    // ── Optional domain registration ────────────────────────────
    // Called once at plugin load time, before any sync. Allows
    // plugins that introduce non-stock domains (e.g. a DocsToGo
    // plugin introducing an "office" domain) to register a
    // DomainPlugin with libkalburator's DomainRegistry. Default
    // is no-op for plugins using stock domains.
    virtual void registerDomain(
        Kalburator::Shape::DomainRegistry &registry) {}

    // ── Conduit ordering ────────────────────────────────────────
    QStringList runBefore() const { return {}; }    // unchanged
    QStringList runAfter() const  { return {}; }    // unchanged

    // ── Optional conflict handler ───────────────────────────────
    virtual Kalburator::Sync::QSyncCore::ConflictHandler *
        createConflictHandler() { return nullptr; }   // unchanged

    // ── GUI surface ─────────────────────────────────────────────
    virtual bool     hasMainView() const { return false; }
    virtual QString  mainViewName() const { return {}; }
    virtual QIcon    mainViewIcon() const { return {}; }
    virtual QWidget *createMainView(QWidget *parent) const {
        return nullptr;
    }
};
```

### What changed from today

- `createBackends(host, device)` returning `{IBlobBackend*, SyncBackend*}`
  → `createPalmBackend(device)` returning a single Palm-side
  `IBlobBackend`. The PC side is no longer the plugin's concern.
- The optional `SyncBackend*` (typed calendar backend) goes away.
  Plugins that need typed-record semantics implement `SyncBackend`
  directly under the hood of their `IBlobBackend` (this is what
  `PalmCalendarBackend` already does).
- `registerDomain()` is new. Stock plugins (calendar, memo,
  contacts, todo) use the default no-op because their domains are
  stock-registered by libkalburator itself. Third-party plugins
  introducing new domains use this hook.
- The host pointer is gone. `PalmDeviceAccess` carries everything
  the plugin needs; the host abstraction was vestigial.

### Migration of existing plugins

For each of the five existing plugins:

- Drop `host` parameter from `createBackends`; rename to
  `createPalmBackend(device)`.
- Return only the Palm `IBlobBackend`.
- Audit `nativeShapes()` to ensure correct shape declaration.
- Ensure the backend self-marshals (§5.2). For backends that
  inherit from `SyncBackend` (e.g. `PalmCalendarBackend`), the
  marshalling lives in the `IBlobBackend` virtuals.

Estimated per-plugin migration: half a day each.

## 5. The new runtime: `PalmRuntime`

Lives in `WildPalms/src/runtime/palmruntime.{h,cpp}`. Replaces
`SyncRunner_wp` + most of `DeviceSession` + most of `DeviceWorker`.

### 5.1 Components

```
PalmRuntime
├── PalmDeviceAccess           ← owns the link thread; wraps KPilotLink
├── PluginRegistry             ← loads/enumerates IBackendPlugins
├── MappingStore               ← persistent mapping config
├── SyncEngine* (libkalburator)
├── BackendRegistry (libkalburator)
└── ConflictHandlerRegistry (libkalburator)
```

`PalmRuntime` exposes:

```cpp
class PalmRuntime : public QObject {
public:
    PalmRuntime(const QString &profilePath, QObject *parent = nullptr);

    // Lifecycle
    void connectDevice(KPilotLink *link);
    void disconnectDevice();
    bool isDeviceConnected() const;

    // The six modes. Each returns a future the GUI can await/cancel.
    // RunResult shape: aggregated per-plugin SyncStats plus error
    // list; concrete fields finalized during M2 plan-writing.
    QFuture<RunResult> hotSync();
    QFuture<RunResult> fullSync();
    QFuture<RunResult> copyPalmToPC();
    QFuture<RunResult> copyPCToPalm();
    QFuture<RunResult> backup();
    QFuture<RunResult> restore();

    // Plugin + mapping inspection (for the GUI settings dialog)
    QList<PluginInfo> plugins() const;
    QList<SyncMapping> mappings() const;
    void setMappings(QList<SyncMapping>);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void runLog(QString message);
    void runFinished(RunResult);
    void conflictRaised(/* … */);
};
```

### 5.2 Threading model

There is exactly one link thread, owned by `PalmDeviceAccess`. All
DLP operations execute on this thread. Everything else — the GUI,
the engine worker, the runtime's own logic — lives elsewhere.

`PalmDeviceAccess` exposes its `IPalmDatabaseAccess` via the same
sync-blocking interface plugins use today, but **callers may invoke
it from any thread**. Internally, each method dispatches to the
link thread via `QMetaObject::invokeMethod(this, …,
Qt::BlockingQueuedConnection, …)`. Caller blocks until link thread
returns.

This means each Palm `IBlobBackend` (`PalmCalendarBackend` etc.)
becomes self-marshalling without each backend implementing the
marshalling — they call `PalmDeviceAccess::readAllRecords(...)` and
get correct behavior whether called from the engine worker thread
or a `QtConcurrent::run` thread or wherever.

For the Group-C modes (Backup/Restore), `PalmRuntime` implements
the loops itself in `QtConcurrent::run`-style code and trusts the
self-marshalling Palm backends to do the right thing.

For the Group-A/B modes (HotSync/FullSync/Copy*), `PalmRuntime`
calls `engine->runSyncFuture(palmMappingIds, override)` and trusts
the same.

### 5.3 The six modes

**HotSync.** Look up mappings whose source or target backend is a
Palm backend. Call
`engine->runSyncFuture(palmMappingIds)`. Forward signals/lifecycle
events to the GUI.

**FullSync.** Same as HotSync, except first reach the engine's
baseline store (via a small accessor added during M1 if not already
exposed) and call `clearMappingV3(mappingId)` for each Palm mapping
to force a fresh-on-source treatment.

**CopyPalmToPC.** Same as HotSync, but pass
`ExecutionOverride{direction = MirrorAToB}` (where A is the Palm
side per the mapping's `sourceBackend` field).

**CopyPCToPalm.** Same, with `MirrorBToA`.

**Backup.** Pure runtime loop, no engine involvement. For each
enabled plugin: get its Palm backend; instantiate a
`RawFilesBackend` rooted at `<backupRoot>/<pluginId>/<collectionId>/`
(one tree per plugin per collection — see §6); for each Palm
collection, walk records; for each record present on Palm but
absent or different on the PC backup, write it. Never delete from
the PC backup. Return stats.

**Restore.** Pure runtime loop. For each enabled plugin: get its
Palm backend; instantiate the backup-side backend at the configured
backup root; clear the Palm side of each collection; write every
record from backup. Destructive on Palm.

### 5.4 What about the conduit `runBefore`/`runAfter` ordering?

The runtime sorts the active mappings/plugins respecting the
declared order before invoking the engine. For
`engine->runSyncFuture(mappingIds)`, the order of the IDs
parameter dictates dispatch order; the engine's
`MappingScheduler` handles intra-batch resource contention.
Plugin-author intent (`runBefore`/`runAfter`) is preserved.

## 6. Configuration model

`SyncMapping` is the primary user-facing config object. The user
configures (per profile):

- **Plugins enabled** — which plugin .so's to load and activate.
- **Mappings** — for each Palm plugin × collection × PC backend
  the user wants synced, one mapping. Fields:
  - `id`: stable identifier.
  - `sourceBackend`, `targetBackend`: backend instance ids (one is
    Palm-side, one is PC-side).
  - `sourceCollection`, `targetCollection`.
  - `conflictPolicy`, `lossPolicy` (Phase G).
  - `pcBackendKind`: enum/string identifying which PC-side backend
    type to instantiate (`RawFiles`, `GenericSqlite`, `Akonadi`,
    `OrgFile`, etc.) plus its config (root path, collection name,
    preferred encoding for that pluggable backend).
- **Backup config** — root path for Backup mode's per-plugin
  per-collection RawFiles dump. Distinct from mappings; this is
  Backup-specific config.

Persistence: profile-level config (sync paths, plugin enable list,
device options) goes through `KConfigGroup` since the rest of the
app uses KDE settings infrastructure. Mappings and backup config
are persisted as a structured payload (JSON) at
`<profilePath>/wildpalms-sync.json`, since they're a typed
collection of records rather than user-tweakable preferences. Open
to consolidating to KConfig-only if the JSON layer adds no value
during M5 implementation.

### Defaults / wizard

For first-time setup, a wizard creates default mappings
matching the legacy KPilot expectation:

- Calendar plugin → Akonadi calendar collection (if Akonadi
  available) or a `~/.local/share/wildpalms/calendar.ics` file.
- Contacts → Akonadi (if available) or vCard files.
- Memo → plain text files in `~/.local/share/wildpalms/memos/`.
- Todo → Akonadi tasks (if available) or VTODO file.

The wizard is post-MVP. Initial release ships with mapping
configuration via the settings dialog only.

## 7. GUI shell

Keep `KF6MainWindow` (a `KXmlGuiWindow` subclass) and the KDE app
shell affordances it provides:

- **`KActionCollection`** for Tools-menu action registration so
  actions are user-rebindable via the standard KDE shortcut-config
  dialog.
- **`KStandardAction`** for any action that has a KDE-canonical
  spelling (Quit, Configure Shortcuts, Configure Toolbars,
  Preferences).
- **`KXmlGui`** XML-driven menu/toolbar layout so the user can
  customize toolbars via the standard "Configure Toolbars" dialog.
- **`KConfigGroup`** for any user-facing settings persisted via
  KDE's settings infrastructure (rather than ad-hoc JSON for those
  bits).
- **Per-plugin view extension** preserved: `IBackendPlugin::hasMainView()`
  / `createMainView()` continue to contribute KPageWidget pages or
  dock widgets to the main window — the affordance that today lets
  the calendar plugin show a calendar viewer, memo plugin show a
  memo browser, etc.

What changes inside `KF6MainWindow`'s implementation: the
mode-dispatched `m_session->requestSync(mode, m_syncRunner)`
wiring and the runner-owning member soup go away. Tools-menu
actions are simple `connect(action, &QAction::triggered, this,
[this]{ m_runtime->hotSync(); })`-style invocations on the new
`PalmRuntime`. The main window itself remains a KDE-shaped app
shell.

What we deliberately skip: full KParts hosting (`KParts::MainWindow`,
`KPart` plugins). The existing per-plugin views aren't full KParts
today — they're QWidget contributions. Keep that. KParts remains a
follow-up only if a plugin ever needs more (e.g. embedding KOrganizer
proper as a part).

### Layout

```
┌────────────────────────────────────────────────────┐
│ Tools  Settings  Help                              │ menubar
├────────────────────────────────────────────────────┤
│ Device: [icon] Connected (Palm Vx, PalmID 0xab...) │ status
├────────────────────────────────────────────────────┤
│ ┌──────┐  ┌────────────────────────────────────┐   │
│ │ Plug │  │                                    │   │
│ │ -in  │  │  Per-plugin main view              │   │
│ │ list │  │  (calendar / memo / contacts /     │   │
│ │      │  │   todo / ... — KPageWidget-style)  │   │ central
│ │      │  │                                    │   │
│ │      │  │                                    │   │
│ └──────┘  └────────────────────────────────────┘   │
├────────────────────────────────────────────────────┤
│ [▶ Sync log dock — collapsible]                    │ dock
└────────────────────────────────────────────────────┘
```

### Tools menu

- HotSync
- Full Sync
- Backup
- Restore...
- Copy Palm → PC...
- Copy PC → Palm...
- ─────
- Cancel running operation

Each Tools-menu action calls the corresponding `PalmRuntime`
method, attaches a `QFutureWatcher` to track progress, populates
the log dock, and disables conflicting actions while running.

### Settings dialog

- Profile selection (multiple profiles per user).
- Plugin enable/disable list.
- Mapping editor (table of mappings, add/edit/delete).
- Backup-root configuration.
- Device options (link type, port, connection timeout).

### Conflict dialog

When a mapping's conflict policy is `AskUser` and the engine emits
`conflictRaised`, a modal dialog presents the conflict (source
record / target record / baseline) with three buttons (Keep
source / Keep target / Skip) or a custom resolver provided by the
plugin's `createConflictHandler`. Closing the dialog resumes the
engine via the existing conflict-pause channel.

### Per-plugin views

A `KPageWidget` (or `QTabWidget`) hosts one page per loaded plugin
that returns `hasMainView() == true`, populated by
`createMainView(parent)`. This is the only place WildPalms is
opinionated about what the user does *between* syncs.

## 8. Migration plan: branch-and-rewrite

### Branch setup

- Branch `palm-rewrite` off `refactor/engine-merger` in WildPalms
  worktree.
- Branch of same name off `refactor/engine-merger` in libkalburator
  worktree.
- PlanStan stays on `refactor/engine-merger`. No PlanStan changes.

The libkalburator branch carries the §3 changes (dynamic domain
registration + mirror-direction override). It can land
independently of WildPalms; once merged back to
`refactor/engine-merger`, the WildPalms branch rebases and
continues.

### Milestones

**M1 — libkalburator changes land.** §3.1 + §3.2 + facade
deletion + `dispatchFirstSync` inlining + tests. Gate:
verify-all green for libkalburator + PlanStan; WildPalms ignored
(may not compile). Tag: `v0.17-dynamic-domains`. ~1 week.

**M2 — Calendar-only MVP in WildPalms.** New `PalmRuntime`,
`PalmDeviceAccess`, new `IBackendPlugin` contract, new minimal
shell (just HotSync action + log dock + status indicator).
Calendar plugin migrated. Real-device verification: connect a
real Palm with a known calendar dataset, HotSync, verify
round-trip. ~2 weeks.

**M3 — All sync modes wired.** Add FullSync, Copy*, Backup,
Restore. Each verified against real device. ~1 week.

**M4 — Other four plugins migrated.** Memo, contacts, todo, webcal
each migrated to new `IBackendPlugin` contract + verified.
~1 week.

**M5 — Settings dialog + mapping editor + conflict dialog +
per-plugin views.** Full GUI surface. ~1.5 weeks.

**M6 — Old code deleted.** `SyncRunner_wp`, `DeviceSession`,
`DeviceWorker`, `TickleWorker`, and the old test suite for them.
The mode-dispatch / runner-management code inside `KF6MainWindow`
gets ripped out and replaced with `PalmRuntime` calls; the class
itself stays. ~2 days.

**M7 — Merge back to `refactor/engine-merger`.** WildPalms
verify-all green; cross-repo verify-all green. Tag
`v0.17-palm-rewrite` on WildPalms's commit.

Total: ~6 weeks of focused work. Compare to the ~1.5 weeks for
Option C as previously estimated — this is bigger, but it lands a
clean architecture rather than another layer of glue.

### Order rationale

Calendar first because it's the most exercised plugin (best
test coverage upstream), it uses the typed `SyncBackend` path
(forces the runtime to handle the harder case), and it's where
the threading bug bites hardest (it's what `HotSyncCoordinator`
nominally invokes).

Memo, contacts, todo are all simple `IBlobBackend` plugins — they
should each take half a day to a day.

Webcal last because it's a one-way-only plugin and it doesn't
exercise the interesting code paths.

## 9. Testing strategy

### Library-side (libkalburator)

- New: `tst_dynamic_domain_registration.cpp` (per §3.1).
- New: `tst_engine_mirror_direction.cpp` (per §3.2).
- Migrate-or-delete: cases in `tst_engine_blob_one_shot.cpp` and
  `runBlobTwoWay_*`/`runBlobMirror_*` cases in
  `tst_engine_unified_boundary.cpp`. For each case, decide:
  - Tests engine behavior reachable via mappings → migrate to a
    new mapping-driven test.
  - Tests facade-specific plumbing → delete.
  - Tests Palm Backup/Restore semantics → delete; the equivalent
    coverage moves to WildPalms's `tst_palm_runtime_backup` and
    `tst_palm_runtime_restore`.

### App-side (WildPalms)

- `tst_palm_device_access.cpp` — proves marshalling. Construct
  `PalmDeviceAccess` with a mock `IPalmDatabaseAccess` injected,
  invoke from non-link threads, assert calls land on the link
  thread (e.g. via thread-id capture in the mock).
- `tst_palm_runtime_hotsync.cpp` etc., one per mode — uses
  `MockBlobBackend` for both Palm and PC sides, verifies each
  mode's semantics end-to-end.
- `tst_palm_runtime_orchestration.cpp` — verifies cross-mapping
  ordering (`runBefore`/`runAfter`), cancellation propagation,
  conflict-pause flow.
- Per-plugin migration tests stay where they are today (each
  plugin's `tst_<plugin>_v2.cpp`); they migrate to the new
  contract along with their plugins.

### Real-device test plan

Each milestone has a real-device gate. Minimal test corpus:

- **Calendar**: 5 events spanning one-time / recurring / all-day /
  with-alarm / with-attachment.
- **Memo**: 3 memos, one with non-ASCII text, one >1KB.
- **Contacts**: 3 contacts, one with all fields populated.
- **Todo**: 3 items, one completed, one with priority + due date.

Verification per milestone: mutate on Palm, sync, observe PC; mutate
on PC, sync, observe Palm; mutate both with non-conflicting changes,
sync, observe both reflect both changes; mutate both with
conflicting changes, observe conflict dialog and chosen resolution.

## 10. Open questions / deferred

These are real choices that don't block this design but need
resolution during plan-writing or early implementation.

1. **Mapping-list persistence format.** Per §6, profile prefs go
   through KConfigGroup; mappings + backup config use JSON. Revisit
   during M5 if JSON adds no value over a `KConfigGroup`-only
   approach.

2. **Plugin loading mechanism.** Keep KPluginFactory + .json
   metadata, or simplify to plain `QPluginLoader` + a sidecar JSON
   manifest? KPluginFactory adds a KCoreAddons dep but gives us
   per-plugin metadata discovery for free. Default: keep.

3. **Backup format on PC.** Default to `RawFilesBackend` per plugin,
   or one big SQLite via `GenericSqliteBackend`? Per-mapping config
   override? Default: `RawFilesBackend` per plugin per collection.

4. **Tickle handling location.** Inside `PalmDeviceAccess` or a
   sibling `PalmTickle` class? Default: sibling.

5. **`PalmRuntime` library extraction.** Should `PalmRuntime`
   become its own library so other apps could embed it (e.g. a
   CLI sync tool, or a system-tray daemon)? Default: keep internal
   to WildPalms; revisit only if a second consumer materialises.

6. **Multiple devices simultaneously.** Today WildPalms assumes
   one Palm at a time. Should `PalmRuntime` model a fleet?
   Default: one at a time, matching the cradle reality.

7. **Migration of users' existing `.wildpalms-sync.db`.** Phase G's
   v3 baseline schema is forward-compatible; the rewrite preserves
   the DB. But mapping config is new — first-launch on an upgraded
   profile presents the wizard (§6). Migration of existing
   per-plugin sync paths into the new mapping shape is a
   one-shot script.

8. **Coexistence with old WildPalms during the rewrite.** Users
   on the old code path keep working; the new branch is parallel
   until M7. No on-disk format changes that would prevent rolling
   back.

## 11. Performance and concurrency

User-facing requirement: syncing should be as fast and parallel as
possible without blocking the host apps. Audit of the design
against this:

### Three threads, by construction

- **GUI thread**: paints, handles input, owns the QFutureWatchers
  that observe `PalmRuntime`'s returned futures. Never blocks on
  sync work.
- **Engine worker thread**: `SyncEngine`'s private `QThread`.
  Drives mapping execution, calls into adapters, calls into Palm
  `IBlobBackend`s. Blocks on link-thread DLP via
  `BlockingQueuedConnection` — but the GUI doesn't.
- **Link thread**: `PalmDeviceAccess`'s private thread. Owns
  `KPilotLink`, executes DLP serially (the hardware constraint).

The GUI is structurally unable to block on sync work. The host app
(KOrganizer, KAddressBook, anything reading the PC-side data) is
also unblocked — it's a separate process.

For the Backup/Restore loops, `PalmRuntime` uses `QtConcurrent::run`
to dispatch the loop body to the global thread pool, then `await`s
its own `QFuture` from there. Same property: GUI doesn't block;
link thread serialises the DLP calls underneath via the
self-marshalling `IBlobBackend`s.

### Inter-mapping parallelism: ready, gated by capacity

`MappingScheduler` already tracks per-mapping resource sets via
each backend's `resourceId()`. Two mappings whose resource sets
are disjoint are *eligible* to run in parallel.

What's gating it today: the scheduler is hardcoded to global
capacity-1 (one mapping at a time, even if disjoint). The header
comment is explicit: *"The resource graph is maintained so that v2
can enable concurrent disjoint-component execution without API
changes."*

For WildPalms specifically, the parallelism win is small:
**every Palm mapping shares one resource (`palm-device:<id>`),
so they serialise inherently**. This is correct — the Palm
hardware is single-occupancy DLP. Lifting the scheduler from v1
to v2 buys WildPalms nothing during HotSync of multiple
collections.

The win shows up where it matters: a future consumer (or even
PlanStan) running mappings against unrelated backends in parallel.
Lifting `MappingScheduler` to v2 capacity-N is a small,
API-compatible change — a half-day to a day, including a
parallelism test. **Not part of this rewrite's critical path; flag
for follow-up.**

### Intra-mapping pipelining: room without API change

Today the calendar adapter fetches source records, fetches target
records, diffs, and applies — sequential phases per mapping. For
slow-fetch backends (e.g. a remote calendar over the network) this
leaves I/O bandwidth on the table.

The `QFuture`-based adapter contract allows pipelining (start
target fetch while source fetch is still streaming, start
applying writes as soon as the diff identifies them) without any
public API change — it's an internal adapter rewrite. Again, not
on this rewrite's critical path; for Palm specifically the DLP
serialism makes pipelining moot. **Flag for follow-up if any
backend's profile says "wait time dominates."**

### Batched writes: already there

`IBlobBackend::beginBatch`/`commitBatch`/`rollbackBatch` exists for
backends that benefit from coalesced writes (the SQLite-backed
ones). The engine already calls these around per-mapping write
phases. The Palm backends should opt in if `KPilotLink` exposes
multi-record DLP transactions; otherwise leave as no-ops.

### What this means for the rewrite

Nothing additional to design, build, or worry about. Three
threads by construction, the resource graph already exists, the
batch hooks already exist. The architecture is at "we can get
there from here" with two well-isolated follow-ups
(`MappingScheduler` v2; intra-mapping pipelining) that don't
require touching the public API or the rewrite's milestones.

The one thing to **explicitly verify during M2's real-device
test**: that the GUI actually stays responsive during a long
HotSync. If the QFutureWatcher signal cadence ends up overwhelming
the event loop (a real Qt6 footgun if `runProgress` fires
per-record on a 10k-record sync), throttle progress emissions on
the engine side. Observable via the existing log dock; trivial to
fix.

## 12. Out of scope

- Anything in PlanStan. PlanStan's mappings already use the
  unified engine API; nothing here touches it.
- New domains beyond what existing plugins need. The DocsToGo
  example was illustrative; if anyone actually wants to write
  that plugin, this design supports it but the work is separate.
- Akonadi/CardDAV stock backends (Phase G Tasks 84-87, deferred
  because kf6pim isn't installed). These are independent additions
  to libkalburator's stock backend list and unrelated to the
  WildPalms rewrite. They can land before, during, or after.
- Any change to PlanStan or libkalburator's core sync algorithm.
- Native USB device support beyond what KPilotLink already provides.
