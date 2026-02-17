# Conduit Plugin Architecture Design

Date: 2026-02-16
Branch: feature/kcalendarcore-backend
Status: Approved

## Goal

Refactor QPilotSync so the main application is a thin frontend to pilot-link
and all conduit logic (sync, data viewing, external tools) is encapsulated in
KDE plugins. This makes the system extensible to third-party conduits,
including external tools like Plucker that fetch live data and produce Palm
database files.

## Design Decisions

| Decision | Choice |
|---|---|
| Plugin framework | KPluginFactory (KDE native) |
| Interface design | Layered: IConduit base, ISyncConduit and IToolConduit sub-interfaces |
| Built-in conduits | Real .so plugin files, not compiled into the app |
| External tools (Plucker) | Invoked as subprocess via QProcess |
| Config pages | KCModule-style, unified settings dialog |
| Execution ordering | JSON metadata (X-QPilotSync-RunBefore/RunAfter) |
| Plugin discovery paths | System (kf6/qpilotsync/conduits/) + user (~/.local/lib/qpilotsync/conduits/) |
| Plugin UI contribution | Plugins provide QWidget* views inserted into KPageWidget |

## Interface Hierarchy

### IConduit (minimal base - all conduits implement)

```cpp
class IConduit
{
public:
    virtual ~IConduit() = default;

    // Identity
    virtual QString conduitId() const = 0;
    virtual QString displayName() const = 0;
    virtual QIcon icon() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;

    // Capabilities
    virtual bool requiresDevice() const = 0;

    // Sync entry point
    virtual SyncResult sync(SyncContext *context) = 0;
    virtual bool canSync(const SyncContext *context) const = 0;
    virtual bool shouldRun(const SyncContext *context) const = 0;

    // UI contribution
    virtual bool hasView() const = 0;
    virtual QWidget *createView(QWidget *parent) = 0;
    virtual QString viewName() const = 0;
    virtual QIcon viewIcon() const = 0;
    virtual KXMLGUIClient *createGUIClient() = 0;

    // Configuration
    virtual int configPages() const = 0;
    virtual QWidget *createConfigPage(int index, QWidget *parent) = 0;
    virtual void loadSettings() = 0;
    virtual void saveSettings() = 0;
};
```

### ISyncConduit : IConduit (bidirectional Palm sync)

```cpp
class ISyncConduit : public IConduit
{
public:
    // Palm database
    virtual QString palmDatabaseName() const = 0;
    virtual QString fileExtension() const = 0;
    virtual bool canSyncToPalm() const = 0;
    virtual bool canSyncFromPalm() const = 0;

    // Record conversion
    virtual BackendRecord *palmToBackend(PilotRecord *record,
                                         SyncContext *context) = 0;
    virtual PilotRecord *backendToPalm(BackendRecord *record,
                                        SyncContext *context) = 0;
    virtual bool recordsEqual(PilotRecord *palmRecord,
                               BackendRecord *backendRecord) const = 0;
    virtual QString palmRecordDescription(PilotRecord *record) const = 0;
    virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                      const QList<BackendRecord *> &candidates) = 0;

    // Categories
    virtual QString categoryNameForIndex(int categoryIndex) const = 0;
    virtual bool writeModifiedCategories(SyncContext *context) = 0;
};
```

### IToolConduit : IConduit (external tool wrappers)

```cpp
class IToolConduit : public IConduit
{
public:
    virtual QString toolPath() const = 0;
    virtual bool prepareExecution(SyncContext *context) = 0;
    virtual bool installResults(SyncContext *context) = 0;
};
```

### SyncConduitBase (convenience class)

The existing `Conduit` base class becomes `SyncConduitBase`, implementing
`ISyncConduit` with all current sync algorithms: hotSync, fullSync, firstSync,
copyPalmToPC, copyPCToPalm, backup, restore. The four built-in conduits
subclass `SyncConduitBase` just as they subclass `Conduit` today.

## Plugin Metadata

Each plugin embeds a JSON metadata file via `K_PLUGIN_FACTORY_WITH_JSON`.

### Sync conduit example (Calendar)

```json
{
    "KPlugin": {
        "Name": "Calendar Conduit",
        "Description": "Synchronizes Palm DatebookDB with iCalendar files",
        "Icon": "view-calendar",
        "Authors": [{ "Name": "QPilotSync contributors" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-QPilotSync-ConduitId": "calendar",
    "X-QPilotSync-ConduitType": "sync",
    "X-QPilotSync-PalmDatabase": "DatebookDB",
    "X-QPilotSync-RequiresDevice": true,
    "X-QPilotSync-RunBefore": [],
    "X-QPilotSync-RunAfter": ["webcalendar"],
    "X-QPilotSync-DefaultEnabled": true,
    "X-QPilotSync-SortOrder": 0
}
```

### Tool conduit example (Plucker)

```json
{
    "KPlugin": {
        "Name": "Plucker Conduit",
        "Description": "Fetches web content and installs as Plucker documents",
        "Icon": "text-html",
        "Authors": [{ "Name": "QPilotSync contributors" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Tool"
    },
    "X-QPilotSync-ConduitId": "plucker",
    "X-QPilotSync-ConduitType": "tool",
    "X-QPilotSync-RequiresDevice": true,
    "X-QPilotSync-RunBefore": [],
    "X-QPilotSync-RunAfter": [],
    "X-QPilotSync-DefaultEnabled": false,
    "X-QPilotSync-SortOrder": 100
}
```

## ConduitManager

Replaces hard-coded conduit registration. Responsibilities:

- **Discovery**: `KPluginMetaData::findPlugins("qpilotsync/conduits")` across
  system and user paths
- **Loading**: `KPluginFactory::loadFactory` then `create<IConduit>`
- **Lifecycle**: load, unload, enable, disable at runtime
- **Ordering**: topological sort from JSON metadata without loading .so files
- **Config persistence**: enabled/disabled state via KConfig

```
ConduitManager
    discoverConduits()
    loadConduit(pluginId) → IConduit*
    unloadConduit(pluginId)
    enabledConduits() → QList<IConduit*>
    conduitMetaData(id) → KPluginMetaData
    resolveExecutionOrder() → QStringList
    loadConfig()
    saveConfig()
    conduitList() → QList<PluginInfo>

Signals:
    conduitLoaded(IConduit *conduit)
    conduitUnloading(IConduit *conduit)
```

## Plugin UI Contribution

Plugins contribute UI to the main window:

1. **KPageWidget pages**: If `hasView()` returns true, the main window calls
   `createView()` and adds the returned QWidget as a new KPageWidgetItem in
   the icon sidebar.
2. **XMLGUI actions**: If `createGUIClient()` returns non-null, the main window
   registers it with `guiFactory()->addClient()` so plugin menu items and
   toolbar actions appear.
3. **Config pages**: Each plugin provides KCModule-style config pages that
   appear in a unified conduit settings dialog.

On unload, pages are removed and GUI clients deregistered.

## SyncContext Changes

Two additions to SyncContext:

- `installQueue` (QStringList): tool conduits push .pdb/.prc file paths here.
  The Install conduit drains this queue and pushes files to the device.
- `syncFolderPath` (QString): base path for the profile's sync folder, so
  conduits don't need to derive it.

## Plugin Lifecycle

### App startup

1. `ConduitManager::discoverConduits()` scans plugin paths
2. JSON metadata read for all found plugins (no .so loading)
3. `loadConfig()` reads enabled/disabled state from KConfig
4. For each enabled conduit:
   - Load .so via KPluginFactory
   - `create<IConduit>(parent)`
   - `conduit->loadSettings()`
   - If `hasView()`: main window adds KPageWidgetItem
   - If `createGUIClient()`: main window registers with guiFactory
   - Register with SyncEngine

### Sync triggered

1. `SyncEngine::syncAll(mode)` called
2. `ConduitManager::resolveExecutionOrder()` — topological sort from JSON
3. For each conduit in order:
   - `shouldRun(context)` — skip if not due
   - `canSync(context)` — skip if preconditions unmet
   - `sync(context)` — execute
4. Install conduit runs last, drains installQueue

### Plugin disabled at runtime

1. Unregister from SyncEngine
2. Remove KPageWidgetItem from main window
3. `guiFactory()->removeClient()`
4. `conduit->saveSettings()`
5. Delete plugin instance, unload .so

### App shutdown

1. Save enabled/disabled state to KConfig
2. Unload all plugins in reverse order

## File Layout

```
src/
    app/                            Main application (thin shell)
        main.cpp
    kf6/                            KF6 main window, action manager
        kf6mainwindow.cpp/h         Dashboard + plugin-provided pages
        actionmanager.cpp/h          Core app actions only
        conduitmanager.cpp/h         Plugin discovery & lifecycle
        kf6settings.cpp/h
    core/                           Shared library (QPilotSyncCore)
        iconduit.h                   IConduit interface
        isyncconduit.h               ISyncConduit interface
        itoolconduit.h               IToolConduit interface
        syncconduitbase.h/cpp        Base class with sync algorithms
        syncengine.h/cpp             Orchestrator
        synccontext.h                SyncContext, SyncResult
        syncbackend.h/cpp            Backend storage
        syncstate.h/cpp              ID mappings & baselines
        conflicthandler.h/cpp        Conflict resolution
        conflictstore.h/cpp          Deferred conflicts
        pilotrecord.h/cpp            pilot-link wrappers
        backendrecord.h/cpp
        ...
    widgets/common/                  Shared widgets for plugins
        categorymanager.cpp/h
        categorymodel.cpp/h
        categoryfilterwidget.cpp/h
        categoryeditordialog.cpp/h
    plugins/
        memo/
            memoconduit.cpp/h        ISyncConduit implementation
            memoview.cpp/h           Browser widget
            memomapper.cpp/h         Format conversion
            memo-conduit.json        KPlugin metadata
            CMakeLists.txt           -> qpilotsync_memo.so
        contacts/
            contactconduit.cpp/h
            contactview.cpp/h
            contactmapper.cpp/h
            contacts-conduit.json
            CMakeLists.txt           -> qpilotsync_contacts.so
        calendar/
            calendarconduit.cpp/h
            calendarview.cpp/h
            calendarmapper.cpp/h
            calendar-conduit.json
            CMakeLists.txt           -> qpilotsync_calendar.so
        todos/
            todoconduit.cpp/h
            taskview.cpp/h
            todomapper.cpp/h
            todos-conduit.json
            CMakeLists.txt           -> qpilotsync_todos.so
        webcalendar/
            webcalendarconduit.cpp/h
            webcalendar-conduit.json
            CMakeLists.txt           -> qpilotsync_webcalendar.so
        install/
            installconduit.cpp/h
            install-conduit.json
            CMakeLists.txt           -> qpilotsync_install.so
        plucker/
            pluckerconduit.cpp/h     IToolConduit, invokes plucker-build
            pluckerview.cpp/h        Feed URL list, fetch status UI
            plucker-conduit.json
            CMakeLists.txt           -> qpilotsync_plucker.so
```

## Build Dependencies

```
QPilotSyncCore (shared library)
    Interfaces: IConduit, ISyncConduit, IToolConduit
    SyncConduitBase (sync algorithm implementations)
    SyncEngine, SyncContext, SyncState
    SyncBackend, LocalFileBackend
    ConflictHandler, ConflictStore
    Common widgets (CategoryManager, etc.)
    pilot-link wrappers (PilotRecord, KPilotDeviceLink, DeviceSession)

qpilotsync (executable)
    Links: QPilotSyncCore
    Contains: KF6MainWindow, Dashboard, ConduitManager, ActionManager, LogWidget
    No conduit logic

Each plugin .so
    Links: QPilotSyncCore
    Contains: conduit implementation, mapper, view widget, JSON metadata
```

## Migration Pattern for Existing Conduits

For each of Memo, Contact, Calendar, Todo:

1. Move `conduits/{type}conduit.*` to `plugins/{type}/{type}conduit.*`
2. Move `mappers/{type}mapper.*` to `plugins/{type}/{type}mapper.*`
3. Move `widgets/browser/{type}view.*` to `plugins/{type}/{type}view.*`
4. Change base class from `Conduit` to `SyncConduitBase`
5. Add `createView()` returning the moved view widget
6. Add `K_PLUGIN_FACTORY_WITH_JSON` macro and JSON metadata file
7. Add per-plugin CMakeLists.txt

For WebCalendar: same pattern, no view, `requiresDevice = false` in JSON.

For Install: special conduit that drains installQueue,
`X-QPilotSync-SortOrder: 9999` to run last.

For Plucker (new): implements IToolConduit, invokes plucker-build via QProcess,
pushes .pdb to installQueue, provides a feed management view.

## What Stays in the Main App

- KF6MainWindow (shell: KPageWidget, log dock, menus/toolbars)
- Dashboard page (sync status, device info, conduit overview)
- ConduitManager (plugin discovery & lifecycle)
- ActionManager (core app actions, not conduit-specific)
- LogWidget (shared log panel)
- Profile management (loading, saving, switching)
