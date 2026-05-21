# Wild Palms Architecture Overview

**Version:** 3.0
**Date:** 2026-05-21
**Status:** Post-merger reality. The `refactor/engine-merger` campaign landed 2026-05-21; this document reflects the unified `PalmRuntime` + libkalburator world. Supersedes the v2.0 (2026-01-08) revision, which described the pre-Phase-E `SyncEngine` + `IConduit` model that no longer exists.

---

## Executive summary

Wild Palms is a Qt6 / KDE Frameworks 6 application that synchronises Palm OS devices (Palm Pilot, Visor, Tungsten, Treo, etc.) with PC-side storage on Linux. It is the successor to KPilot for modern KDE desktops, and it shares its sync engine with PlanStan via the standalone **libkalburator** library — the same engine that drives PlanStan's calendar/contact sync also drives Wild Palms's Palm sync.

Wild Palms ships four built-in sync plugins (Memo, Contacts, Calendar, ToDo), one built-in action plugin (Install), and a host application that wires them together. The application talks to a connected Palm via `pilot-link` (DLP protocol) over USB or serial; it talks to PC-side storage via libkalburator's `SyncBackend` abstraction, with `RawFilesBackend` as the default sink.

---

## Top-level layout

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Qt6 / KF6 GUI Layer                          │
│  KF6MainWindow · LogWidget · SettingsDialog · ConflictDialog ·       │
│  AccountsPage · ConflictReviewWidget · per-plugin views              │
└──────────────────────────────────────────────────────────────────────┘
                                  │
┌──────────────────────────────────────────────────────────────────────┐
│                       Runtime Orchestration                          │
│  PalmRuntime (owns: device, plugins, engine, backends, registry)     │
│  PluginActionManager · AccountController · KalburatorInteractive-    │
│  ConflictHandler · ConflictPresenter_WP                              │
└──────────────────────────────────────────────────────────────────────┘
                                  │
        ┌─────────────────────────┴──────────────────────────┐
        ▼                                                    ▼
┌─────────────────────────┐                  ┌──────────────────────────┐
│  Sync engine            │                  │  Palm device I/O          │
│  (libkalburator)        │                  │  (WildPalms)              │
│                         │                  │                          │
│  SyncEngine             │ ←── SyncBackend  │  PalmDeviceAccess         │
│  BlobSyncEngine         │     interface    │  (link-thread marshaller) │
│  BackendRegistry        │                  │                          │
│  BaselineStore (SQLite) │                  │  PalmBackend (per-DB      │
│  ConflictHandler API    │                  │  cache + DLP wrapper)     │
│  Stock plugins:         │                  │                          │
│   - blob domain         │                  │  KPilotDeviceLink         │
│   - calendar/contacts/  │                  │  (pilot-link binding)     │
│     memo/todo domains   │                  │                          │
└─────────────────────────┘                  └──────────────────────────┘
                                                            │
                                                            ▼
                                                   ┌────────────────┐
                                                   │  pilot-link    │
                                                   │  (libpisock)   │
                                                   │  + USB/serial  │
                                                   └────────────────┘
```

There are **two storage subsystems**, not one:

1. **libkalburator `Kalburator::Storage::BaselineStore`** — SQLite (`blob_baselines_v3` table, keyed by mapping + record). Primary store; populated and read by `BlobSyncEngine::twoWayWithBaseline` on every sync.
2. **WildPalms-local JSON `BaselineStore` + `IDMappingStore`** (`src/sync/journal/`, namespace `WildPalms::Sync`). Used only by `SyncState::pendingConflictCount` to count deferred conflicts; not on the hot sync path. Lives behind the same `journal/` folder name as the pre-Phase-E stores so existing user data round-trips.

---

## Process and thread model

Wild Palms runs in a single OS process with three logical threads:

| Thread | Purpose | Created by |
| --- | --- | --- |
| GUI / main | UI, signal/slot dispatch, ownership of all top-level QObjects | `main()` |
| Palm link | All DLP traffic — `KPilotDeviceLink`, `PalmDeviceAccess`, tickle pings | `PalmDeviceAccess` (lazily on first connect) |
| Engine worker | `BlobSyncEngine::twoWayWithBaseline` and downstream backend I/O | `Kalburator::Engine::SyncEngine` (libkalburator) |

Plugins do not own threads. Plugin code (`createPalmBackend`, `createConflictHandler`, blob backend reads/writes) runs on whichever thread the engine drives it on. `PalmDeviceAccess` is the only seam that crosses to the link thread; all DLP calls flow through it via `BlockingQueuedConnection`, so plugin code can treat the device as a synchronous interface without violating Qt thread affinity.

`KalburatorInteractiveConflictHandler` runs on the GUI thread (it has to — it shows a `ConflictDialog`). The engine pauses its worker thread on a `QEventLoop` while the user is deciding, and the conflict handler resumes it through libkalburator's keep-alive callback channel — that's also why `KF6MainWindow` connects the handler's `keepAliveRequested` signal to `onPalmConflictHandlerKeepAlive()`.

---

## Component map

### GUI layer (`src/app/`, `src/kf6/`, `src/widgets/`, `src/ui/`)

| Class | Purpose |
| --- | --- |
| `KF6MainWindow` (`src/kf6/`) | Main window: profile menu, Tools menu (the six sync modes), MDI host for per-plugin views. Owns `PalmRuntime`. |
| `LogWidget` (`src/app/`) | Timestamped log display, fed by `PalmRuntime::logMessage`. |
| `SettingsDialog` (`src/`) | Global app settings + per-profile settings + per-plugin settings widgets. |
| `ConflictDialog` (`src/app/`) | Modal three-way diff UI; constructed directly by `KalburatorInteractiveConflictHandler`. |
| `ConflictReviewWidget` (`src/app/`) | Read-only review of deferred conflicts (the `SyncState` pending-conflict counter). |
| `AccountsPage` (`src/app/accounts/`) | Lists configured remote accounts. Hosts libkalburator's `AccountsListWidget`. |
| `MappingEditor` (`src/app/mapping/`) | Lets users edit / add `Kalburator::Sync::SyncMapping` entries. |
| per-plugin views | E.g. `CalendarView`, `MemoView`, `PluckerChannelEditor` — live inside each plugin's directory and are wired into the MDI host by `KF6MainWindow::loadProfile()`. |

### Runtime layer (`src/runtime/`)

| Class | Purpose |
| --- | --- |
| `PalmRuntime` | The central orchestrator. Owns everything below. One instance per opened profile. |
| `PalmDeviceAccess` | Thread-marshalled `IPalmDatabaseAccess` implementation. Wraps `KPilotDeviceLink` on a dedicated link thread; exposes a synchronous-looking interface to plugin code via `BlockingQueuedConnection`. |
| `PluginActionManager` | Discovers and dispatches `WildPalms::IPluginAction`s (currently just Install). |
| `KalburatorInteractiveConflictHandler` (`src/app/conflict/`) | The UI-side `ConflictHandler` for libkalburator. Constructs `ConflictDialog` directly. |
| `ConflictPresenter_WP` / `ConflictResolver_WP` | Thin libkalburator-side adapters for the conflict pipeline. |
| `SyncConfigStore_WP` / `SyncHost_WP` | WildPalms's implementations of libkalburator's `ISyncConfigStore` / `ISyncHost`. |
| `AccountController` | Owns the list of remote accounts (CalDAV / CardDAV / Akonadi etc.) and registers their provider-supplied backends with the runtime's `BackendRegistry`. |
| `InstallSourceCollector` | Cross-plugin drain for files queued under `<profile>/install/`; consumed by `InstallActionPlugin`. |
| `PalmTickle` | DLP keep-alive (idle pings every few seconds while no command is in flight). |
| `PilotLinkConnectionFactory` | Constructs a real `PalmDeviceConnection` from a `KPilotDeviceLink` — the seam that exists so `WildPalmsCore` doesn't have to link `WildPalmsRuntime`. |

`PalmRuntime` is constructed by `KF6MainWindow::loadProfile()`, calls `registerPalmPlugins()` immediately (which loads the four sync plugins in-process via `Kalburator::PluginManager::loadInProcess()`), and then sits idle until the user invokes `connectDevice()`. On a successful connect, `finishConnect()` walks the loaded plugins, calls `createPalmBackend(device)` on each, and registers the resulting `SyncBackend` instances with the runtime's `BackendRegistry`.

### Sync plugins (`src/plugins/{calendar,contacts,memo,todos}/`)

Each is a submodule (`wildpalms-conduit-<name>`) tracking its own history. Each plugin inherits `Kalburator::Plugin` and exposes a non-virtual `createPalmBackend(PalmDeviceAccess *)` factory.

| Plugin | Palm DB(s) | Backend class | PC format (default) | Notes |
| --- | --- | --- | --- | --- |
| Calendar | DatebookDB (multi-category aware) | `PalmCalendarBackend` | iCalendar VEVENT (`.ics`) | Multi-collection: one virtual sub-calendar per Palm category slot. |
| Contacts | AddressDB | `PalmContactsBackend` | vCard 4.0 (`.vcf`) | Field-union conflict overlay. |
| Memo | MemoDB | `MemoBlobBackend` | Markdown + YAML frontmatter (`.md`) | First plugin migrated to the new ABI (E.9). |
| ToDo | ToDoDB | `TodoBlobBackend` | iCalendar VTODO (`.ics`) | Shares `CategoryAppInfoReader` with Calendar. |

Action plugin:

| Plugin | Purpose |
| --- | --- |
| Install (`src/plugins/install/`) | Installs `.prc` / `.pdb` files onto the connected Palm. Consumes the drain folder populated by `InstallSourceCollector`. |

See `docs/PLUGIN_ABI.md` for the full plugin contract and how to add a new one.

### Palm device layer (`src/palm/`)

| Class | Purpose |
| --- | --- |
| `KPilotDeviceLink` | pilot-link wrapper. Wraps `dlp_*` calls in a Qt-friendly interface. Owns the `pi_socket`. |
| `IPalmDatabaseAccess` (`src/palm/sync/`) | Thread-marshalled interface plugin code uses to read/write Palm databases. |
| `PalmBackend` (`src/palm/sync/`) | `Kalburator::Sync::SyncBackend` implementation that maps Palm record IDs → libkalburator's blob domain. Caches the result of `loadPalmRecords` per DB across the lifetime of a sync. |
| `PalmDeviceConnection` (`src/palm/`) | Aggregator passed to plugins. Owns a `PalmBackend` wrapping the supplied `IPalmDatabaseAccess`; borrows the file installer. |
| `CategoryInfo`, `CategoryAppInfoReader` | Palm AppInfo block parsing — category names + record-slot mapping. |
| Codecs / transformations / adapters (`src/palm/codecs`, `transformations`, `adapters`) | Per-domain Palm-record ↔ libkalburator-shape conversions. The transcoders that used to live in `src/mappers/` collapsed into per-plugin code here. |

### Sync engine (libkalburator)

WildPalms does not own a sync engine. `Kalburator::Engine::SyncEngine` from libkalburator drives every sync mode. See `docs/SYNC_ENGINE_ARCHITECTURE.md` for engine internals and `docs/LIBKALBURATOR.md` for how WildPalms consumes the library.

### Legacy code still present but dormant (`src/sync/`)

| File | Status |
| --- | --- |
| `src/sync/syncstate.{h,cpp}` | Still live — feeds `KF6MainWindow`'s deferred-conflict counter via `SyncState::pendingConflictCount()`. Uses the JSON stores in `src/sync/journal/`. |
| `src/sync/syncbackend.h` | Forward-decl shim only; the real `SyncBackend` is `Kalburator::Sync::SyncBackend`. |
| `src/sync/journal/` | JSON `BaselineStore` + `IDMappingStore` + `journalcommon.h`. Used by `SyncState`. **Not** on the hot sync path — that's the SQLite store in libkalburator. |

Everything that was once in `src/sync/` and that the pre-merger architecture diagram showed as "Sync Engine Layer" — `SyncEngine`, `Conduit`, `LocalFileBackend`, `SyncContext`, `ConduitManager` — is gone.

---

## Directory structure (post-merger)

```
WildPalms/
├── src/
│   ├── main.cpp
│   ├── settingsdialog.{cpp,h}        # global SettingsDialog
│   ├── profile.{cpp,h}               # per-profile config
│   │
│   ├── core/                         # interface headers (no .cpp)
│   │   ├── iplugin.h                 # WildPalms::IPlugin metadata base
│   │   ├── ipluginaction.h           # WildPalms::IPluginAction (one-shot)
│   │   └── synctypes.h               # WildPalms::Sync::SyncMode enum
│   │
│   ├── runtime/                      # PalmRuntime + supporting types
│   │   ├── palmruntime.{h,cpp}
│   │   ├── palmdeviceaccess.{h,cpp}
│   │   ├── pluginactionmanager.{h,cpp}
│   │   ├── accountcontroller.{h,cpp}
│   │   ├── synchost_wp.{h,cpp}
│   │   ├── syncconfigstore_wp.{h,cpp}
│   │   ├── conflictresolver_wp.{h,cpp}
│   │   ├── conflictpresenter_wp.{h,cpp}
│   │   ├── installsourcecollector.{h,cpp}
│   │   ├── palmtickle.{h,cpp}
│   │   └── pilotlinkconnectionfactory.{h,cpp}
│   │
│   ├── palm/                         # Palm device I/O
│   │   ├── kpilotdevicelink.{h,cpp}  # pilot-link binding
│   │   ├── kpilotlink.{h,cpp}        # abstract device interface
│   │   ├── palmdeviceconnection.{h,cpp}
│   │   ├── palmdevicemonitor.{h,cpp}
│   │   ├── pilotrecord.{h,cpp}
│   │   ├── categoryinfo.{h,cpp}
│   │   ├── sync/                     # IPalmDatabaseAccess + PalmBackend
│   │   ├── conflict/                 # PalmConflictHandler base + config
│   │   ├── codecs/                   # encoding utilities (Windows-1252)
│   │   ├── transformations/          # Palm-record ↔ shape helpers
│   │   ├── adapters/                 # category / mapping adapters
│   │   └── {calendar,contacts,memo,todo}/  # per-domain Palm-side glue
│   │
│   ├── plugins/                      # The plugins themselves
│   │   ├── calendar/                 # submodule wildpalms-conduit-calendar
│   │   ├── contacts/                 # submodule wildpalms-conduit-contacts
│   │   ├── memo/                     # submodule wildpalms-conduit-memo
│   │   ├── todos/                    # submodule wildpalms-conduit-todos
│   │   └── install/                  # in-tree action plugin
│   │
│   ├── app/                          # GUI components
│   │   ├── conflictdialog.{h,cpp}
│   │   ├── conflictreviewwidget.{h,cpp}
│   │   ├── logwidget.{h,cpp}
│   │   ├── conflict/                 # KalburatorInteractiveConflictHandler
│   │   ├── accounts/                 # AccountsPage
│   │   └── mapping/                  # SyncMapping editor
│   │
│   ├── kf6/                          # KF6 host shell
│   │   ├── kf6mainwindow.{h,cpp}
│   │   ├── kf6settings.{h,cpp}
│   │   ├── actionmanager.{h,cpp}
│   │   └── autosyncorchestrator.{h,cpp}
│   │
│   ├── sync/                         # Legacy holdovers (see "dormant" above)
│   │   ├── syncstate.{h,cpp}
│   │   ├── syncbackend.h
│   │   └── journal/                  # JSON BaselineStore + IDMappingStore
│   │
│   ├── widgets/, ui/, models/, backends/   # misc UI/data helpers
│   └── CMakeLists.txt
│
├── lib/                              # vendored pilot-link build (fallback)
│
├── docs/                             # documentation
│   ├── ARCHITECTURE_2026.md          # this file
│   ├── PLUGIN_ABI.md
│   ├── SYNC_ENGINE_ARCHITECTURE.md
│   ├── LIBKALBURATOR.md
│   ├── PROJECT_VISION.md
│   ├── plans/                        # active plan documents (gitignored)
│   ├── superpowers/                  # design specs + execution plans
│   └── archived/                     # historical / superseded docs
│
└── build-fetchcontent/               # primary build dir
    └── _deps/libkalburator-src/      # fetched libkalburator pinned to a tag
```

`build-fetchcontent/` is the default build directory because `CMakeLists.txt` uses `FetchContent` to pull libkalburator (see `docs/LIBKALBURATOR.md`). The older `build-dev/` and `build/` directories are no longer the convention; existing references to `build-dev/` in `.clangd` or scripts are migration artifacts.

---

## Sync modes (Tools menu)

All six modes are methods on `PalmRuntime` that delegate to libkalburator's engine with the appropriate execution policy:

| Mode | `PalmRuntime` method | Engine behaviour |
| --- | --- | --- |
| HotSync | `hotSync()` | `runSyncFuture(mapping)` for every enabled mapping. Uses existing baseline; diffs both sides. |
| FullSync | `fullSync()` | `clearMappingV3(mapping.id)` on the baseline store, then `runSyncFuture` for every mapping. Equivalent to "first sync" semantics. |
| CopyPalmToPC | `copyPalmToPC()` | `runMirror(MirrorDir::PalmToPC, ...)` → `ExecutionOverride::Direction::MirrorAToB`. Engine treats the source as authoritative; target writes are unconditional. |
| CopyPCToPalm | `copyPCToPalm()` | `runMirror(MirrorDir::PCToPalm, ...)` → `ExecutionOverride::Direction::MirrorBToA`. |
| Backup | `backup()` | Raw `.pdb` device dump; bypasses the sync engine entirely. Writes into `<profile>/backup/`. |
| Restore | `restore()` | Raw `.pdb` upload from `<profile>/backup/`; bypasses the sync engine. |

The first four modes go through libkalburator's `BlobSyncEngine::twoWayWithBaseline`. Backup and Restore are record-blob byte-for-byte device operations and run on the link thread directly.

---

## Profile + sync folder layout

```
<profile dir>/                        # whatever the user picked
├── .wildpalms.conf                   # profile settings (KConfig INI)
├── .kalb                             # libkalburator collection config (JSON)
├── .kalb.providers                   # account provider sidecar (KConfig)
├── .state/                           # state stores
│   ├── baselines.sqlite              # libkalburator BaselineStore (SQLite)
│   └── <username>/<plugin>/          # WP-local JSON stores (deferred conflicts)
│       ├── mappings.json
│       └── baseline.json
├── rawfiles/                         # default PC-side sink (RawFilesBackend)
│   ├── wildpalms.calendar/
│   ├── wildpalms.contacts/
│   ├── wildpalms.memo/
│   └── wildpalms.todo/
├── backup/                           # raw .pdb dumps from Backup/Restore
└── install/                          # queue for Install action plugin
    └── installed/                    # successfully installed files moved here
```

A user can replace `rawfiles/` with any provider-supplied backend (CalDAV / CardDAV / Akonadi) by editing `.kalb` through the Accounts UI; the engine doesn't care which `SyncBackend` is on the PC side.

---

## External dependencies

| Library | Version | Purpose |
| --- | --- | --- |
| Qt | 6.5+ | GUI, threading, signals/slots, QFuture |
| KDE Frameworks 6 | 6.x | KConfig, KCoreAddons, KCalendarCore, KContacts, KXmlGui |
| libkalburator | pinned tag (e.g. `v0.52-phase-p-merge-ready`) via FetchContent | Sync engine + storage + provider lifecycle |
| pilot-link | 0.12.5 (system package or vendored fallback in `lib/`) | Palm USB / serial DLP transport |
| Optional: Akonadi | — | Only relevant for Akonadi-backed accounts; off by default |

Build system: CMake 3.16+. Default build dir is `build-fetchcontent/`; libkalburator is fetched as a subproject the first time CMake configures.

---

## See also

- `docs/PLUGIN_ABI.md` — the sync-plugin and action-plugin contracts.
- `docs/SYNC_ENGINE_ARCHITECTURE.md` — how the engine drives a mapping end to end.
- `docs/LIBKALBURATOR.md` — consumption story for the shared library.
- `docs/ASYNC_DLP_DESIGN.md` — the link-thread / tickle / cancellation design.
- `docs/DATA_LOSS_HANDLING.md` — invariants and safety nets for first-sync and conflict cases.
- `docs/archived/engine-merger-2026/` — historical phase plans and findings that produced this architecture.
