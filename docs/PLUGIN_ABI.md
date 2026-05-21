# WildPalms Plugin ABI

**Status:** post-merger reality (engine-merger campaign landed 2026-05-21).
**Supersedes:** `docs/archived/CONDUIT_PLUGIN_DESIGN.md`, `docs/archived/plugin-developer-guide.md`, `docs/archived/sdk-plugin-guide.md`.

WildPalms has two plugin kinds: **sync plugins** (move records between a Palm device and a PC-side backend) and **action plugins** (one-shot operations like file install, with no record-level sync). Sync plugins implement an upstream interface from libkalburator (`Kalburator::Plugin`); action plugins implement a WildPalms-local interface (`WildPalms::IPluginAction`). Both kinds are loaded in-process by `PalmRuntime` at startup — there is no on-disk plugin discovery loop and no `.so` boundary between the application and a plugin.

## Where things live

| Layer | Code |
| --- | --- |
| Common plugin metadata interface | `src/core/iplugin.h` (`WildPalms::IPlugin`) |
| Action plugin interface | `src/core/ipluginaction.h` (`WildPalms::IPluginAction`, `WildPalms::IActionContext`) |
| Sync plugin upstream interface | libkalburator `Kalburator::Plugin` (`src/plugin/plugin.h`) |
| Plugin loader | libkalburator `Kalburator::PluginManager` (`src/plugin/pluginmanager.h`); WP wraps via `PalmRuntime::registerPalmPlugins()` |
| Action dispatcher | `src/runtime/pluginactionmanager.{h,cpp}` |
| Built-in sync plugins | `src/plugins/{calendar,contacts,memo,todos}/` — git submodules |
| Built-in action plugin | `src/plugins/install/` — in-tree |
| Device aggregator passed to plugins | `src/palm/palmdeviceconnection.h` (`PalmDeviceConnection`) |

`docs/archived/plugin-developer-guide.md` and `sdk-plugin-guide.md` document the pre-Phase-E `IConduit` ABI. That ABI no longer exists. Refer to this document instead.

## Sync plugins: `Kalburator::Plugin`

A sync plugin is a concrete subclass of `Kalburator::Plugin` (libkalburator) that contributes domain-specific behaviour to the shared sync engine. The contract is small:

```cpp
class Plugin {
public:
    virtual ~Plugin() = default;

    virtual QList<std::shared_ptr<Shape::DomainDefinition>>
        domainDefinitions() const { return {}; }
    virtual QList<std::shared_ptr<Shape::ShapeContribution>>
        shapeContributions() const { return {}; }
    virtual QList<std::shared_ptr<Shape::DomainOperations>>
        domainOperations() const { return {}; }
    virtual QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const { return {}; }
};
```

Each contribution is registered with a process-wide singleton inside libkalburator (`DomainRegistry`, `TransformationRegistry`, `BackendRegistry`) when `PluginManager::loadInProcess()` is called. The four built-in WildPalms sync plugins all return `{}` from these — the domain/shape/backend contributions for `calendar`, `contacts`, `memo`, `todo`, and `blob` are owned by libkalburator's **stock plugins**, registered once via `Kalburator::registerStockPlugins(*m_pluginManager)` at the top of `PalmRuntime::registerPalmPlugins()`.

What WildPalms sync plugins *do* expose, in addition to the upstream surface, is a non-virtual pair of factory methods specific to each concrete plugin class:

```cpp
class MemoPlugin : public Kalburator::Plugin {
public:
    std::unique_ptr<Kalburator::Sync::SyncBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device);

    // some plugins also expose:
    Kalburator::Conflict::ConflictHandler *createConflictHandler();
};
```

`createPalmBackend(device)` is called once per connected Palm by `PalmRuntime::finishConnect()`. It returns a `Kalburator::Sync::SyncBackend` instance that wraps a `PalmBackend` (which in turn marshals all DLP traffic to the link thread via `IPalmDatabaseAccess`). The returned backend is registered with `PalmRuntime`'s `BackendRegistry` under the plugin's id (e.g. `wildpalms.memo`).

Because `createPalmBackend` is not virtual on `Kalburator::Plugin`, `PalmRuntime::finishConnect()` dispatches to it via `dynamic_cast<ConcretePluginType*>()` against each known WildPalms sync plugin class. Adding a new sync plugin therefore requires editing `PalmRuntime::finishConnect()` to add a branch — there is no purely-pluggable loader path for sync plugins today.

Plugins that need a custom conflict overlay (`CalendarConflictHandler` is the canonical example) expose `createConflictHandler()` returning a `Kalburator::Conflict::ConflictHandler *` registered with the runtime's conflict handler. Plugins that need no overlay simply omit the method.

### What a sync plugin sees at runtime

`createPalmBackend(PalmDeviceAccess *)` receives a borrowed pointer to the runtime's `PalmDeviceAccess`. The plugin uses this to obtain an `IPalmDatabaseAccess` for the databases it claims. Recommended pattern: the plugin's `*BlobBackend` (e.g. `MemoBlobBackend`, `PalmCalendarBackend`) is constructed with the `PalmDeviceAccess` and queries Palm records lazily via `IPalmDatabaseAccess::openDatabase()` + `readAllRecords()` on demand.

Plugins should **not** hold a `KPilotDeviceLink *` directly. The `IPalmDatabaseAccess` abstraction exists so plugin code is thread-safe by construction (all DLP calls cross to the dedicated link thread under the hood) and so plugin tests can substitute `MockPalmDatabaseAccess`.

## Action plugins: `WildPalms::IPluginAction`

Actions are one-shot operations triggered from the Tools menu or programmatically. They do not participate in record-level sync. The contract:

```cpp
namespace WildPalms {

class IActionContext : public QObject {
    Q_OBJECT
public:
    virtual void setTotal(int total)     = 0;
    virtual void setCurrent(int current) = 0;
    virtual void log(const QString &msg) = 0;
    virtual bool isCancelled() const     = 0;
Q_SIGNALS:
    void progress(int current, int total);
    void message(const QString &msg);
};

class IPluginAction : public IPlugin {
public:
    using ActionContext = IActionContext;

    virtual bool execute(ActionContext       *ctx,
                         PalmDeviceConnection *device,
                         const QJsonObject   &parameters) = 0;

    struct Preconditions {
        bool        requiresDeviceConnection = true;
        QStringList requiresFiles;
    };
    virtual Preconditions preconditions() const = 0;
};

} // namespace WildPalms
```

`execute()` runs on a worker thread; actions must not assume a GUI thread. Progress and log output surface through the `ActionContext` proxy (a `QObject`) so the action implementation itself can remain a plain interface without inheriting `QObject`.

`Preconditions::requiresDeviceConnection` gates menu visibility (set to `false` for actions that only touch local files). `requiresFiles` lists file extensions the action consumes (e.g. `{"prc", "pdb"}` for Install); used by the cross-plugin file drain (see `InstallSourceCollector`).

The only currently-built action plugin is `InstallActionPlugin` (`src/plugins/install/`), which installs `.prc` / `.pdb` files onto the connected Palm.

## The `IPlugin` base

Both kinds inherit a common metadata-only base:

```cpp
namespace WildPalms {

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual QString pluginId() const     = 0;
    virtual QString displayName() const  = 0;
    virtual QIcon   icon() const         = 0;
    virtual QString description() const  = 0;
    virtual QString version() const      = 0;

    virtual bool     hasSettings() const          { return false; }
    virtual QWidget *createSettingsWidget(QWidget *parent) { return nullptr; }
    virtual void        loadSettings(const QJsonObject &)  {}
    virtual QJsonObject saveSettings() const               { return {}; }
};

} // namespace WildPalms
```

`IPlugin` is a pure abstract C++ class (not a `QObject`) so concrete plugin classes can multi-inherit `QObject + IPlugin` without diamond inheritance. Sync plugins do not use this base directly — `Kalburator::Plugin` is their root — but the four built-in sync plugins still expose `pluginId()` / `displayName()` for symmetry with action plugins and for `PalmRuntime`'s registration calls.

## A historical note on `WildPalms::IBackendPlugin`

The original Phase E.8 plan introduced a `WildPalms::IBackendPlugin` interface as the sync-plugin contract. That interface is gone. Phase K.8b (in libkalburator) replaced it with the contribution-based `Kalburator::Plugin` model, and Phase E.16 migrated the four WP sync plugins onto the new model. Comments in `src/core/iplugin.h` and a stale local copy of the Plucker plugin (in `src/plugins/plucker/`, currently untracked) still reference the old spelling; they are historical and not active.

## Lifecycle

```
PalmRuntime ctor
  ↓
registerPalmPlugins()                       ← static; first call only:
  ├── new Kalburator::PluginManager
  ├── Kalburator::registerStockPlugins(...) ← seeds DomainRegistry + co.
  ├── instantiate 4 sync plugins
  └── PluginManager::loadInProcess(items)
  ↓
... user opens device ...
  ↓
PalmRuntime::connectDevice(paths)
  ↓
PalmDeviceAccess::connectDevice (async)
  ↓
PalmRuntime::finishConnect()                ← per-connection setup:
  ├── for each loaded plugin:
  │     b = plugin.createPalmBackend(device)
  │     m_registry.registerBackendInstance(plugin.id, b)
  │   end for
  ├── generate default RawFiles mappings for uncovered Palm slots
  └── construct Kalburator::Engine::SyncEngine
  ↓
... user invokes Tools menu sync mode ...
  ↓
PalmRuntime::hotSync()/fullSync()/copyPalmToPC()/copyPCToPalm()/backup()/restore()
  ↓
m_engine->runSyncFuture(mappingId, executionOverride)
                                            ← libkalburator's BlobSyncEngine
                                              + ConflictHandler + Baseline
```

Action plugins are loaded the same way but never participate in the sync engine. They are invoked via `PluginActionManager::runAction(actionId, parameters, device)`, which constructs a `SimpleActionContext`, posts the call onto a worker thread, and surfaces progress/log/cancel through the context's `QObject` signals.

## Adding a new plugin (sketch)

1. Decide kind. Record-level sync against a Palm DB → sync plugin. One-shot operation → action plugin.
2. **Sync plugin:**
   - Create a subclass of `Kalburator::Plugin` in a new directory under `src/plugins/<name>/` (use one of the existing four as a template — Memo is the smallest).
   - Implement `createPalmBackend(PalmDeviceAccess *)` returning a `Kalburator::Sync::SyncBackend`.
   - If your domain (`calendar`, `contacts`, `memo`, `todo`) is not one of the four already shipped by libkalburator's stock plugins, you also need to add a `DomainDefinition` + matching shape + transcoding work upstream in libkalburator. New domains are a much bigger lift than new backends.
   - Add the plugin to `src/plugins/CMakeLists.txt`.
   - Edit `PalmRuntime::registerPalmPlugins()` to instantiate the new plugin and add it to the `loadInProcess(items)` list.
   - Edit `PalmRuntime::finishConnect()` to add the `dynamic_cast` branch for `createPalmBackend`.
3. **Action plugin:**
   - Create a subclass of `WildPalms::IPluginAction` in `src/plugins/<name>/`.
   - Implement `execute(ctx, device, parameters)` for the worker-thread body.
   - Implement `preconditions()` for menu gating.
   - Add the plugin to `src/plugins/CMakeLists.txt`.
   - Register the plugin with `PluginActionManager` in `KF6MainWindow::loadProfile()` (or wherever the action manager is wired up for the new profile).
4. Add a `tst_<name>_v2.cpp` (or `_e2e`) under `tests/plugins/<name>/` exercising the plugin against `MockBlobBackend` (sync) or a fake device (action). Follow the existing pattern in `tst_memo_v2.cpp` / `tst_install_v2_e2e.cpp`.

## See also

- `docs/SYNC_ENGINE_ARCHITECTURE.md` — how `Kalburator::Engine::SyncEngine` drives a plugin's backend.
- `docs/ARCHITECTURE_2026.md` — overall WildPalms architecture, including how `PalmRuntime` slots into the application.
- `docs/LIBKALBURATOR.md` — how WildPalms consumes libkalburator (FetchContent pin, build options).
- libkalburator's `docs/phase0/` — upstream design docs for `Kalburator::Plugin`, `BlobSyncEngine`, `BackendRegistry`, conflict handling.
