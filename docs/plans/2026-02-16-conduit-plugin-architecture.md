# Conduit Plugin Architecture Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor all conduits into KPluginFactory-based .so plugins with layered interfaces, plugin-provided UI views, and support for external tool conduits.

**Architecture:** Layered interfaces (IConduit → ISyncConduit / IToolConduit) with a ConduitManager handling KDE plugin discovery and lifecycle. The main app becomes a thin shell with only a Dashboard page; all conduit logic and views live in plugins. SyncConduitBase preserves all existing sync algorithms.

**Tech Stack:** KF6 (KPluginFactory, KPluginMetaData, KXmlGui, KConfig), Qt6, CMake, pilot-link

**Design doc:** `docs/plans/2026-02-16-conduit-plugin-architecture-design.md`

---

## Phase 1: Interfaces & Shared Library Foundation

### Task 1: Create IConduit Interface Header

**Files:**
- Create: `src/core/iconduit.h`
- Test: Build compiles with new header included

**Step 1: Create the interface header**

Create `src/core/iconduit.h` with the minimal base interface that ALL conduits must implement. This is a pure abstract class (no QObject, just a C++ interface) so plugins can inherit it alongside QObject.

```cpp
#pragma once

#include <QString>
#include <QIcon>
#include <QWidget>

class KXMLGUIClient;
class SyncContext;
class SyncResult;

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
    virtual KXMLGUIClient *createGUIClient() { return nullptr; }

    // Configuration
    virtual int configPages() const { return 0; }
    virtual QWidget *createConfigPage(int index, QWidget *parent) {
        Q_UNUSED(index) Q_UNUSED(parent) return nullptr;
    }
    virtual void loadSettings() {}
    virtual void saveSettings() {}
};

Q_DECLARE_INTERFACE(IConduit, "org.qpilotsync.IConduit/1.0")
```

**Step 2: Verify it compiles**

Run: `make -C build -j$(($(nproc)-1))`
Expected: PASS (header not yet included anywhere, but syntax-valid)

**Step 3: Commit**

```bash
git add src/core/iconduit.h
git commit -m "feat: add IConduit base interface for plugin system"
```

---

### Task 2: Create ISyncConduit Interface Header

**Files:**
- Create: `src/core/isyncconduit.h`

**Step 1: Create the interface header**

Create `src/core/isyncconduit.h` extending IConduit with bidirectional Palm sync methods. These are extracted from the current `Conduit` class (src/sync/conduit.h:254-282).

```cpp
#pragma once

#include "iconduit.h"

class PilotRecord;
class BackendRecord;

class ISyncConduit : public IConduit
{
public:
    // Palm database identity
    virtual QString palmDatabaseName() const = 0;
    virtual QString fileExtension() const = 0;
    virtual bool canSyncToPalm() const = 0;
    virtual bool canSyncFromPalm() const = 0;

    // Record conversion (bidirectional)
    virtual BackendRecord *palmToBackend(PilotRecord *record,
                                         SyncContext *context) = 0;
    virtual PilotRecord *backendToPalm(BackendRecord *record,
                                        SyncContext *context) = 0;
    virtual bool recordsEqual(PilotRecord *palmRecord,
                               BackendRecord *backendRecord) const = 0;
    virtual QString palmRecordDescription(PilotRecord *record) const = 0;
    virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                      const QList<BackendRecord *> &candidates) = 0;

    // Category support
    virtual QString categoryNameForIndex(int categoryIndex) const = 0;
    virtual bool writeModifiedCategories(SyncContext *context) = 0;
};

Q_DECLARE_INTERFACE(ISyncConduit, "org.qpilotsync.ISyncConduit/1.0")
```

**Step 2: Commit**

```bash
git add src/core/isyncconduit.h
git commit -m "feat: add ISyncConduit interface for bidirectional Palm sync"
```

---

### Task 3: Create IToolConduit Interface Header

**Files:**
- Create: `src/core/itoolconduit.h`

**Step 1: Create the interface header**

Create `src/core/itoolconduit.h` for external tool wrappers like Plucker.

```cpp
#pragma once

#include "iconduit.h"

class IToolConduit : public IConduit
{
public:
    // External tool path
    virtual QString toolPath() const = 0;

    // Prepare config files, working directory, etc. before execution
    virtual bool prepareExecution(SyncContext *context) = 0;

    // After tool runs: collect output files, push to installQueue
    virtual bool installResults(SyncContext *context) = 0;
};

Q_DECLARE_INTERFACE(IToolConduit, "org.qpilotsync.IToolConduit/1.0")
```

**Step 2: Commit**

```bash
git add src/core/itoolconduit.h
git commit -m "feat: add IToolConduit interface for external tool wrappers"
```

---

### Task 4: Add installQueue and syncFolderPath to SyncContext

**Files:**
- Modify: `src/sync/conduit.h:31-54` (SyncContext struct)

**Step 1: Add new fields to SyncContext**

Add two fields to the SyncContext struct at `src/sync/conduit.h`:

```cpp
// After existing fields in SyncContext (around line 51):
QStringList installQueue;   // .pdb/.prc files for Install conduit to push to device
QString syncFolderPath;     // Base path for the profile's sync folder
```

**Step 2: Populate syncFolderPath in SyncEngine**

Modify `src/sync/syncengine.cpp` in the `syncConduit()` method (around line 270) where SyncContext is built. Add:

```cpp
context.syncFolderPath = m_syncPath;  // or wherever the path comes from
```

Also in `syncAll()` (around line 185) where context is built, add the same.

**Step 3: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`
Expected: PASS

**Step 4: Commit**

```bash
git add src/sync/conduit.h src/sync/syncengine.cpp
git commit -m "feat: add installQueue and syncFolderPath to SyncContext"
```

---

### Task 5: Restructure CMake — Create QPilotSyncCore Shared Library

This is the most critical infrastructure task. The current `QPilotCore` static library needs to become a shared library that plugins can link against. The existing conduit/mapper/widget sources will be removed from the core library since they'll move into plugins later.

**Files:**
- Modify: `src/CMakeLists.txt` (restructure library)
- Modify: `CMakeLists.txt` (root, update linking)
- Create: `src/core/CMakeLists.txt` (new core lib target)

**Step 1: Create src/core/ directory and move interface headers**

Move the interface headers created in Tasks 1-3 into `src/core/`. Also create a `CMakeLists.txt` that builds the shared library from core sync infrastructure.

The core library should contain:
- Interface headers (iconduit.h, isyncconduit.h, itoolconduit.h)
- Sync infrastructure (conduit.h/cpp, syncengine.h/cpp, syncstate.h/cpp, syncbackend.h/cpp)
- Conflict handling (conflicthandler.h/cpp, conflictstore.h/cpp, conflictresolution.h)
- Data types (pilotrecord.h/cpp, backendrecord.h/cpp)
- Mappers base classes
- Common widgets (categorymanager, categorymodel, categoryfilterwidget, etc.)
- Device wrappers (devicelink, devicesession)
- Profile management

**Important:** Do NOT remove conduit implementations yet. They stay in the core library until each one is individually migrated to a plugin in Phase 3. This avoids a big-bang migration.

**Step 2: Add `src/core/` include path and export macros**

Ensure `target_include_directories` includes `src/core/` so plugins can find interface headers.

Add export macros for the shared library (or use `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` / visibility attributes on Linux).

**Step 3: Build and run tests**

Run: `make -C build -j$(($(nproc)-1))`
Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: All existing tests still pass

**Step 4: Commit**

```bash
git add src/CMakeLists.txt src/core/ CMakeLists.txt
git commit -m "refactor: restructure build for QPilotSyncCore shared library"
```

---

## Phase 2: Plugin Infrastructure

### Task 6: Create ConduitManager

The ConduitManager discovers, loads, and manages conduit plugins. It replaces the hard-coded `registerConduit()` calls in `kf6mainwindow.cpp:515-519`.

**Files:**
- Create: `src/kf6/conduitmanager.h`
- Create: `src/kf6/conduitmanager.cpp`
- Test: `tests/test_conduitmanager.cpp`

**Step 1: Write tests for ConduitManager**

Test plugin discovery (mock), loading, enabling/disabling, execution order resolution. Use the JSON metadata format defined in the design doc. Key test cases:

- `test_discoverConduits` — finds plugins in search path
- `test_loadAndUnloadConduit` — loads a plugin, verifies IConduit* returned, unloads cleanly
- `test_enableDisableConduit` — toggle enabled state, persists to KConfig
- `test_resolveExecutionOrder` — topological sort from RunBefore/RunAfter metadata
- `test_resolveExecutionOrder_circularDependency` — detects and reports cycles
- `test_conduitListReturnsAll` — returns both loaded and unloaded plugin info

**Step 2: Run tests to verify they fail**

Run: `ctest --test-dir build --output-on-failure -R test_conduitmanager`
Expected: FAIL (files don't exist yet)

**Step 3: Implement ConduitManager**

```cpp
// conduitmanager.h - key structure
#pragma once

#include <QObject>
#include <KPluginMetaData>

class IConduit;

class ConduitManager : public QObject
{
    Q_OBJECT

public:
    struct PluginInfo {
        KPluginMetaData metaData;
        IConduit *instance = nullptr;
        bool defaultEnabled = false;
        bool enabled = false;
        int sortOrder = 0;
    };

    explicit ConduitManager(QObject *parent = nullptr);
    ~ConduitManager() override;

    void discoverConduits();
    bool loadConduit(const QString &pluginId);
    void unloadConduit(const QString &pluginId);

    IConduit *conduit(const QString &pluginId) const;
    QList<IConduit *> enabledConduits() const;
    QList<PluginInfo> conduitList() const;
    KPluginMetaData conduitMetaData(const QString &pluginId) const;

    bool isConduitEnabled(const QString &pluginId) const;
    void setConduitEnabled(const QString &pluginId, bool enabled);

    QStringList resolveExecutionOrder() const;

    void loadConfig();
    void saveConfig();

Q_SIGNALS:
    void conduitLoaded(IConduit *conduit);
    void conduitUnloading(IConduit *conduit);

private:
    QList<PluginInfo> m_plugins;
};
```

Key implementation details:
- `discoverConduits()` calls `KPluginMetaData::findPlugins("qpilotsync/conduits")`
- `loadConduit()` calls `KPluginFactory::loadFactory()` then `create<IConduit>()`
- `resolveExecutionOrder()` reads `X-QPilotSync-RunBefore` and `X-QPilotSync-RunAfter` from metadata JSON, performs Kahn's topological sort (port from current `SyncEngine::resolveConduitOrder()` at syncengine.cpp:419-493)
- `loadConfig()`/`saveConfig()` use `KSharedConfig` with group "Conduits"

**Step 4: Run tests**

Run: `ctest --test-dir build --output-on-failure -R test_conduitmanager`
Expected: PASS

**Step 5: Commit**

```bash
git add src/kf6/conduitmanager.h src/kf6/conduitmanager.cpp tests/test_conduitmanager.cpp
git commit -m "feat: add ConduitManager for KDE plugin discovery and lifecycle"
```

---

### Task 7: Update SyncEngine to Accept IConduit Interface

Currently SyncEngine works with `Conduit*` (the concrete base class). It needs to work with `IConduit*` so it can run both sync conduits and tool conduits.

**Files:**
- Modify: `src/sync/syncengine.h` (change Conduit* to IConduit*)
- Modify: `src/sync/syncengine.cpp` (update all methods)

**Step 1: Update SyncEngine interface**

Change all references from `Conduit*` to `IConduit*` in syncengine.h:
- `registerConduit(Conduit*)` → `registerConduit(IConduit*)`
- `conduit()` return type → `IConduit*`
- Internal storage `QMap<QString, Conduit*>` → `QMap<QString, IConduit*>`

**Step 2: Update SyncEngine implementation**

In syncengine.cpp, the key change is in `syncAll()` (lines 109-234) and `syncConduit()` (lines 236-321). Where the engine currently calls Conduit-specific methods like `palmDatabaseName()`, it needs to check if the IConduit is actually an ISyncConduit:

```cpp
// In syncConduit(), before setting up Palm database context:
ISyncConduit *syncConduit = dynamic_cast<ISyncConduit *>(conduit);
if (syncConduit) {
    context.palmDatabase = syncConduit->palmDatabaseName();
    // ... existing Palm-specific setup
}
// For IToolConduit or plain IConduit, just call sync(context)
```

**Step 3: Remove ordering logic from SyncEngine**

The topological sort (syncengine.cpp:419-493) and cycle detection (syncengine.cpp:495-569) move to ConduitManager. SyncEngine now receives an already-ordered list from ConduitManager and just iterates it.

**Step 4: Build and run all existing tests**

Run: `make -C build -j$(($(nproc)-1))`
Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: All tests pass (existing conduits still work since Conduit implements IConduit via SyncConduitBase)

**Step 5: Commit**

```bash
git add src/sync/syncengine.h src/sync/syncengine.cpp
git commit -m "refactor: update SyncEngine to work with IConduit interface"
```

---

### Task 8: Update KF6MainWindow to Use ConduitManager

Replace hard-coded conduit creation (kf6mainwindow.cpp:510-543) with ConduitManager. Wire up dynamic page creation from plugin views.

**Files:**
- Modify: `src/kf6/kf6mainwindow.h` (add ConduitManager member, remove hard-coded conduit pointers)
- Modify: `src/kf6/kf6mainwindow.cpp` (replace initializeSyncEngine with plugin-driven setup)

**Step 1: Add ConduitManager to KF6MainWindow**

In kf6mainwindow.h, add:
```cpp
class ConduitManager;

// In private members:
ConduitManager *m_conduitManager;
```

Remove individual conduit view pointers (m_calendarView, m_taskView, m_contactView, m_memoView) since these will come from plugins.

**Step 2: Replace initializeSyncEngine()**

In kf6mainwindow.cpp, replace the hard-coded conduit registration (lines 510-543) with:

```cpp
void KF6MainWindow::initializeSyncEngine()
{
    m_syncEngine = new SyncEngine(this);
    m_conduitManager = new ConduitManager(this);

    // Discover and load plugins
    m_conduitManager->discoverConduits();
    m_conduitManager->loadConfig();

    // When a conduit is loaded, add its view and register with engine
    connect(m_conduitManager, &ConduitManager::conduitLoaded,
            this, &KF6MainWindow::onConduitLoaded);
    connect(m_conduitManager, &ConduitManager::conduitUnloading,
            this, &KF6MainWindow::onConduitUnloading);

    // Load all enabled conduits
    for (const auto &info : m_conduitManager->conduitList()) {
        if (info.enabled) {
            m_conduitManager->loadConduit(info.metaData.pluginId());
        }
    }
}
```

**Step 3: Implement onConduitLoaded / onConduitUnloading slots**

```cpp
void KF6MainWindow::onConduitLoaded(IConduit *conduit)
{
    // Add view page if conduit has one
    if (conduit->hasView()) {
        QWidget *view = conduit->createView(this);
        KPageWidgetItem *page = new KPageWidgetItem(view, conduit->viewName());
        page->setIcon(conduit->viewIcon());
        page->setHeaderVisible(false);
        m_pageWidget->addPage(page);
        m_conduitPages[conduit->conduitId()] = page;
    }

    // Register GUI client if provided
    KXMLGUIClient *guiClient = conduit->createGUIClient();
    if (guiClient) {
        guiFactory()->addClient(guiClient);
        m_conduitGUIClients[conduit->conduitId()] = guiClient;
    }

    // Register with sync engine
    m_syncEngine->registerConduit(conduit);
}

void KF6MainWindow::onConduitUnloading(IConduit *conduit)
{
    // Remove view page
    if (m_conduitPages.contains(conduit->conduitId())) {
        m_pageWidget->removePage(m_conduitPages.take(conduit->conduitId()));
    }

    // Remove GUI client
    if (m_conduitGUIClients.contains(conduit->conduitId())) {
        guiFactory()->removeClient(m_conduitGUIClients.take(conduit->conduitId()));
    }

    m_syncEngine->unregisterConduit(conduit->conduitId());
}
```

**Step 4: Update createCentralLayout()**

Remove the hard-coded data view widget creation (lines 228-233 in kf6mainwindow.cpp) and the hard-coded page additions (lines 244-270). Keep only the Dashboard page as built-in. Plugin pages are added dynamically via onConduitLoaded.

**Step 5: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`
Expected: PASS (at this point, conduits are still compiled into QPilotSyncCore, ConduitManager won't find any .so plugins yet — the app will start with just the Dashboard)

**Step 6: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "refactor: replace hard-coded conduits with ConduitManager plugin loading"
```

---

### Task 9: Create SyncConduitBase from Existing Conduit Class

Rename the existing `Conduit` class to `SyncConduitBase` and make it implement `ISyncConduit`. This preserves all sync algorithms (hotSync, fullSync, etc.) while conforming to the new interface.

**Files:**
- Modify: `src/sync/conduit.h` (rename class, inherit ISyncConduit)
- Modify: `src/sync/conduit.cpp` (rename class)
- Modify: All existing conduit subclasses (update base class name)

**Step 1: Rename Conduit → SyncConduitBase**

In `src/sync/conduit.h`:
- Rename `class Conduit` to `class SyncConduitBase`
- Add `: public QObject, public ISyncConduit` as base classes
- The existing virtual methods already match ISyncConduit's interface

In `src/sync/conduit.cpp`:
- Rename all `Conduit::` to `SyncConduitBase::`

**Step 2: Update subclasses**

In each of these files, change `Conduit` base to `SyncConduitBase`:
- `src/sync/conduits/memoconduit.h/cpp`
- `src/sync/conduits/contactconduit.h/cpp`
- `src/sync/conduits/calendarconduit.h/cpp`
- `src/sync/conduits/todoconduit.h/cpp`
- `src/sync/conduits/webcalendarconduit.h/cpp`

**Step 3: Add IConduit view/config stubs to SyncConduitBase**

SyncConduitBase needs default implementations of the IConduit methods that aren't part of the current Conduit class:

```cpp
// In SyncConduitBase (default implementations):
bool hasView() const override { return false; }
QWidget *createView(QWidget *) override { return nullptr; }
QString viewName() const override { return displayName(); }
QIcon viewIcon() const override { return icon(); }
```

These will be overridden by the concrete conduit plugins when they gain their views.

**Step 4: Build and run all tests**

Run: `make -C build -j$(($(nproc)-1))`
Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: All tests pass (pure rename, no behavior change)

**Step 5: Commit**

```bash
git add src/sync/conduit.h src/sync/conduit.cpp src/sync/conduits/
git commit -m "refactor: rename Conduit to SyncConduitBase implementing ISyncConduit"
```

---

## Phase 3: Migrate Built-in Conduits to Plugins

Each conduit migration follows the same pattern. Task 10 documents the full pattern with Memo (the simplest conduit). Tasks 11-15 follow the same pattern with conduit-specific details noted.

### Task 10: Migrate Memo Conduit to Plugin

**Files:**
- Create: `src/plugins/memo/memoconduit.cpp` (moved from `src/sync/conduits/memoconduit.cpp`)
- Create: `src/plugins/memo/memoconduit.h` (moved from `src/sync/conduits/memoconduit.h`)
- Create: `src/plugins/memo/memomapper.cpp` (moved from `src/sync/mappers/memomapper.cpp`)
- Create: `src/plugins/memo/memomapper.h` (moved from `src/sync/mappers/memomapper.h`)
- Create: `src/plugins/memo/memoview.cpp` (moved from `src/widgets/browser/memoview.cpp`)
- Create: `src/plugins/memo/memoview.h` (moved from `src/widgets/browser/memoview.h`)
- Create: `src/plugins/memo/memo-conduit.json`
- Create: `src/plugins/memo/CMakeLists.txt`
- Modify: `src/CMakeLists.txt` (remove memo sources from core library)
- Modify: `src/plugins/CMakeLists.txt` (add memo subdirectory)

**Step 1: Create plugin directory and JSON metadata**

```bash
mkdir -p src/plugins/memo
```

Create `src/plugins/memo/memo-conduit.json`:
```json
{
    "KPlugin": {
        "Name": "Memo Conduit",
        "Description": "Synchronizes Palm MemoDB with Markdown files",
        "Icon": "view-pim-notes",
        "Authors": [{ "Name": "QPilotSync contributors" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-QPilotSync-ConduitId": "memos",
    "X-QPilotSync-ConduitType": "sync",
    "X-QPilotSync-PalmDatabase": "MemoDB",
    "X-QPilotSync-RequiresDevice": true,
    "X-QPilotSync-RunBefore": [],
    "X-QPilotSync-RunAfter": [],
    "X-QPilotSync-DefaultEnabled": true,
    "X-QPilotSync-SortOrder": 0
}
```

**Step 2: Move source files**

Move (git mv) the memo conduit, mapper, and view files into the plugin directory. Update include paths in each file.

**Step 3: Add K_PLUGIN_FACTORY_WITH_JSON macro**

In `memoconduit.cpp`, add at the bottom:
```cpp
#include <KPluginFactory>
K_PLUGIN_FACTORY_WITH_JSON(MemoConduitFactory, "memo-conduit.json",
                           registerPlugin<MemoConduit>();)
#include "memoconduit.moc"
```

**Step 4: Override view methods**

In `memoconduit.h`, add:
```cpp
bool hasView() const override { return true; }
QWidget *createView(QWidget *parent) override;
QString viewName() const override { return i18n("Memos"); }
QIcon viewIcon() const override {
    return QIcon::fromTheme(QStringLiteral("view-pim-notes"));
}
```

In `memoconduit.cpp`, implement:
```cpp
QWidget *MemoConduit::createView(QWidget *parent)
{
    return new MemoView(parent);
}
```

**Step 5: Create plugin CMakeLists.txt**

Create `src/plugins/memo/CMakeLists.txt`:
```cmake
add_library(qpilotsync_memo MODULE
    memoconduit.cpp
    memoconduit.h
    memomapper.cpp
    memomapper.h
    memoview.cpp
    memoview.h
)

target_link_libraries(qpilotsync_memo
    QPilotSyncCore
    KF6::CalendarCore
    KF6::I18n
    KF6::XmlGui
    KF6::WidgetsAddons
)

install(TARGETS qpilotsync_memo
    DESTINATION ${KDE_INSTALL_PLUGINDIR}/qpilotsync/conduits)
```

**Step 6: Update parent CMakeLists.txt files**

Create `src/plugins/CMakeLists.txt`:
```cmake
add_subdirectory(memo)
```

Add to `src/CMakeLists.txt`:
```cmake
add_subdirectory(plugins)
```

Remove memo sources from the QPilotCore source list in `src/CMakeLists.txt`.

**Step 7: Build and test**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
Run: `make -C build -j$(($(nproc)-1))`
Expected: Builds successfully, produces `build/src/plugins/memo/qpilotsync_memo.so`

**Step 8: Test plugin loading**

Run the app manually, verify the Memo page appears in the KPageWidget sidebar (loaded dynamically from plugin).

**Step 9: Run existing tests**

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: test_memomapper and any memo-related tests still pass

**Step 10: Commit**

```bash
git add src/plugins/memo/ src/plugins/CMakeLists.txt src/CMakeLists.txt
git commit -m "feat: migrate Memo conduit to KDE plugin (.so)"
```

---

### Task 11: Migrate Contact Conduit to Plugin

Follow the exact same pattern as Task 10 (Memo).

**Files:**
- Create: `src/plugins/contacts/` directory
- Move: `contactconduit.cpp/h`, `contactmapper.cpp/h`, `contactview.cpp/h`
- Create: `contacts-conduit.json` (ConduitId: "contacts", PalmDatabase: "AddressDB")
- Create: `CMakeLists.txt` → `qpilotsync_contacts.so`

**Conduit-specific notes:**
- ContactView has rich vCard parsing and category filtering (moved as-is)
- The mapper depends on vCard format handling
- JSON: `"X-QPilotSync-SortOrder": 1`

**Commit message:** `feat: migrate Contact conduit to KDE plugin (.so)`

---

### Task 12: Migrate Calendar Conduit to Plugin

Follow the same pattern as Task 10.

**Files:**
- Create: `src/plugins/calendar/` directory
- Move: `calendarconduit.cpp/h`, `calendarmapper.cpp/h`, `calendarview.cpp/h`
- Create: `calendar-conduit.json` (ConduitId: "calendar", PalmDatabase: "DatebookDB")
- Create: `CMakeLists.txt` → `qpilotsync_calendar.so`

**Conduit-specific notes:**
- CalendarView has date highlighting, recurrence expansion, detail panel (moved as-is)
- JSON: `"X-QPilotSync-RunAfter": ["webcalendar"]` (webcalendar feeds must be fetched first)
- JSON: `"X-QPilotSync-SortOrder": 2`

**Commit message:** `feat: migrate Calendar conduit to KDE plugin (.so)`

---

### Task 13: Migrate Todo Conduit to Plugin

Follow the same pattern as Task 10.

**Files:**
- Create: `src/plugins/todos/` directory
- Move: `todoconduit.cpp/h`, `todomapper.cpp/h`, `taskview.cpp/h`
- Create: `todos-conduit.json` (ConduitId: "todos", PalmDatabase: "ToDoDB")
- Create: `CMakeLists.txt` → `qpilotsync_todos.so`

**Conduit-specific notes:**
- TaskView is the viewer widget (named taskview but conceptually a todo viewer)
- JSON: `"X-QPilotSync-SortOrder": 3`

**Commit message:** `feat: migrate Todo conduit to KDE plugin (.so)`

---

### Task 14: Migrate WebCalendar Conduit to Plugin

Follow the same pattern as Task 10, but simpler (no view widget).

**Files:**
- Create: `src/plugins/webcalendar/` directory
- Move: `webcalendarconduit.cpp/h`
- Create: `webcalendar-conduit.json`
- Create: `CMakeLists.txt` → `qpilotsync_webcalendar.so`

**Conduit-specific notes:**
- `hasView()` returns false (no browser view, or optionally add a simple feed-list view later)
- `requiresDevice = false` in JSON
- JSON: `"X-QPilotSync-RunBefore": ["calendar"]`
- JSON: `"X-QPilotSync-SortOrder": -100` (runs early)
- Links against `Qt::Network` for HTTP fetching

**Commit message:** `feat: migrate WebCalendar conduit to KDE plugin (.so)`

---

### Task 15: Migrate Install Conduit to Plugin

The Install conduit is special — it doesn't subclass Conduit currently (it's a standalone QObject). It needs to implement IConduit directly.

**Files:**
- Create: `src/plugins/install/` directory
- Move: `installconduit.cpp/h`
- Create: `install-conduit.json`
- Create: `CMakeLists.txt` → `qpilotsync_install.so`

**Conduit-specific notes:**
- Implement IConduit directly (not ISyncConduit)
- `sync()` drains `context->installQueue` and pushes files to device via pilot-link
- `hasView()` returns false
- `requiresDevice = true`
- JSON: `"X-QPilotSync-SortOrder": 9999` (always runs last)
- JSON: `"X-QPilotSync-RunAfter": ["memos", "contacts", "calendar", "todos", "webcalendar", "plucker"]`

**Commit message:** `feat: migrate Install conduit to KDE plugin (.so)`

---

## Phase 4: Cleanup & New Conduit

### Task 16: Remove Migrated Sources from QPilotSyncCore

After all conduits are migrated, clean up the core library.

**Files:**
- Modify: `src/CMakeLists.txt` (remove all conduit/mapper/view sources)

**Step 1: Remove from source list**

Remove these from `src/CMakeLists.txt` QPilotCore sources (lines 83-102):
- All `sync/conduits/*.cpp/h`
- All `sync/mappers/*.cpp/h` (if mappers moved with their conduits)
- All `widgets/browser/*.cpp/h`
- All `kf6/` sources that moved
- All `widgets/dashboard/`, `widgets/dialogs/`, `widgets/sidebar/` that moved

Keep in core:
- `sync/conduit.h/cpp` (SyncConduitBase)
- `sync/syncengine.h/cpp`
- `sync/syncstate.h/cpp`
- `sync/syncbackend.h/cpp`
- `sync/qsynccore/*` (conflict handling)
- `core/` interfaces
- `widgets/common/` (shared widgets)
- Device/profile management

**Step 2: Build and run all tests**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
Run: `make -C build -j$(($(nproc)-1))`
Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: All tests pass. Test executables link against both QPilotSyncCore and the relevant plugin .so (or the mapper tests move into plugin directories).

**Step 3: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "cleanup: remove migrated conduit sources from core library"
```

---

### Task 17: Create Plucker Conduit Plugin

The first external tool conduit. Invokes `plucker-build` as a subprocess.

**Files:**
- Create: `src/plugins/plucker/pluckerconduit.h`
- Create: `src/plugins/plucker/pluckerconduit.cpp`
- Create: `src/plugins/plucker/pluckerview.h`
- Create: `src/plugins/plucker/pluckerview.cpp`
- Create: `src/plugins/plucker/plucker-conduit.json`
- Create: `src/plugins/plucker/CMakeLists.txt`

**Step 1: Create JSON metadata**

```json
{
    "KPlugin": {
        "Name": "Plucker Conduit",
        "Description": "Fetches web content and installs as Plucker documents on Palm",
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
    "X-QPilotSync-SortOrder": 50
}
```

**Step 2: Implement PluckerConduit**

Key behavior:
- `toolPath()` returns configurable path to `plucker-build` (default: search PATH)
- `prepareExecution()` writes a temporary `.pluckerrc` config from user settings (feed URLs, depth, image settings)
- `sync()` calls `prepareExecution()`, then runs `plucker-build` via `QProcess` for each configured feed, then calls `installResults()`
- `installResults()` finds the generated `.pdb` files in the plucker output directory and pushes their paths onto `context->installQueue`
- `hasView() = true`

**Step 3: Implement PluckerView**

A QWidget showing:
- QListWidget of configured feed URLs with name, URL, last-fetched date
- Add/Edit/Remove buttons for feeds
- "Fetch Now" button (triggers individual feed fetch outside of sync)
- Status display per feed

Settings stored via KConfig:
- `pluckerBuildPath` — path to plucker-build executable
- `feeds` — list of {name, url, maxdepth, category} objects
- `defaultMaxDepth`, `defaultBpp`, `compression`

**Step 4: Create CMakeLists.txt**

```cmake
add_library(qpilotsync_plucker MODULE
    pluckerconduit.cpp
    pluckerconduit.h
    pluckerview.cpp
    pluckerview.h
)

target_link_libraries(qpilotsync_plucker
    QPilotSyncCore
    KF6::I18n
    KF6::ConfigCore
    KF6::WidgetsAddons
)

install(TARGETS qpilotsync_plucker
    DESTINATION ${KDE_INSTALL_PLUGINDIR}/qpilotsync/conduits)
```

**Step 5: Build and test manually**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
Run: `make -C build -j$(($(nproc)-1))`
Expected: Produces `qpilotsync_plucker.so`

Run the app, enable Plucker conduit in settings, verify the Plucker page appears.

**Step 6: Commit**

```bash
git add src/plugins/plucker/
git commit -m "feat: add Plucker conduit plugin for web content fetching"
```

---

### Task 18: Add Conduit Settings Dialog

A unified settings dialog where users can enable/disable conduits and access each conduit's config page.

**Files:**
- Create: `src/kf6/conduitsettingsdialog.h`
- Create: `src/kf6/conduitsettingsdialog.cpp`
- Modify: `src/kf6/actionmanager.cpp` (wire preferences action to open this dialog)

**Step 1: Implement the dialog**

A KPageDialog (or similar) that:
- Lists all discovered conduits (from ConduitManager) with enable/disable checkboxes
- Shows conduit name, icon, description, version from JSON metadata
- For enabled conduits with config pages: embeds their `createConfigPage()` widget
- Apply/OK saves enable/disable state and calls each conduit's `saveSettings()`

**Step 2: Wire to preferences action**

In `actionmanager.cpp`, the existing `KStandardAction::preferences` lambda (around line 30) should open this dialog:

```cpp
KStandardAction::preferences(this, [this]() {
    ConduitSettingsDialog dialog(m_conduitManager, m_window);
    dialog.exec();
}, m_actionCollection);
```

**Step 3: Build and test manually**

Run: `make -C build -j$(($(nproc)-1))`
Launch app, open Settings > Configure QPilotSync, verify conduit list appears.

**Step 4: Commit**

```bash
git add src/kf6/conduitsettingsdialog.h src/kf6/conduitsettingsdialog.cpp src/kf6/actionmanager.cpp
git commit -m "feat: add unified conduit settings dialog"
```

---

### Task 19: Final Integration Test & Cleanup

**Step 1: Full build from clean**

```bash
rm -rf build
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -C build -j$(($(nproc)-1))
```

**Step 2: Run all tests**

```bash
ctest --test-dir build --output-on-failure -j$(($(nproc)-1))
```

**Step 3: Manual integration test**

1. Launch app — Dashboard visible, all conduit pages appear in sidebar
2. Navigate between pages with Ctrl+1-5
3. Open Settings > Configure QPilotSync — see all conduits with checkboxes
4. Disable a conduit — its page disappears from sidebar
5. Re-enable — page reappears
6. Verify sync still works with a connected Palm device (if available)

**Step 4: Update compile_commands.json symlink**

```bash
ln -sf build/compile_commands.json compile_commands.json
```

**Step 5: Final commit**

```bash
git add -A
git commit -m "chore: final cleanup after conduit plugin migration"
```
