# Phase E.8 — Plugin ABI Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the new WP plugin ABI (`IPlugin`, `IBackendPlugin`, `IPluginAction`) plus the runtime managers (`BackendPluginManager`, `PluginActionManager`) that discover and load plugins filtered by `X-WildPalms-PluginType`. Prove the round-trip with a dummy backend plugin and a dummy action plugin under `tests/plugins/`. Strictly additive: the old `IConduit`/`ISyncConduit`/`IToolConduit` surface and `ConduitManager` keep running until E.16 deletes them.

**Architecture:**
- `src/core/` gains three new header-only interfaces next to the existing `iconduit.h` family. Interfaces reference `Kalburator::Sync` types only via forward declarations so `WildPalmsCore`'s public surface stays Kalburator-free (matching the decision pinned in `src/CMakeLists.txt:148-151`).
- `src/runtime/` is a **new** directory; it hosts `WildPalmsRuntime`, a static lib that depends on `WildPalmsCore` + `Kalburator::Sync`. The `src/fullsync/` → `src/runtime/` relocation called out in the spec is explicitly **deferred to E.15** (see "Scope excluded" below).
- Plugin discovery uses `KPluginMetaData::findPlugins("wildpalms/plugins")` (new subdir — old `wildpalms/conduits` coexists with it until E.16).
- Dummy plugins build as real `.so` files via `kcoreaddons_add_plugin(... INSTALL_NAMESPACE "wildpalms_test/plugins")` under `tests/plugins/`; the integration test points `QCoreApplication::setLibraryPaths()` at the build output.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), KF6::CoreAddons (`KPluginMetaData`, `KPluginFactory`, `kcoreaddons_add_plugin`), `Kalburator::Sync` (forward-declared types only — `ISyncHost`, `IBlobBackend`, `SyncBackend`, `SyncCoordinator`, `QSyncCore::ConflictHandler`). No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` — see §"Plugin ABI" (lines 170-314), §"Directory layout" (lines 528-563), §"Sub-phases" row E.8 (line 586).

**Parent integration plan:** `docs/plans/2026-04-20-libkalburator-integration.md`, Phase E row.

**Repo:** All work in `~/dev/WildPalms/`. Build directory: `build-dev/` (preset project). No upstream libkalburator changes.

**Scope explicitly excluded:**

- **Rewriting any plugin** to the new ABI. E.9–E.15 do that, one plugin at a time. E.8 delivers only the ABI + managers + dummy test plugins.
- **Deleting `iconduit.h`, `isyncconduit.h`, `itoolconduit.h`, `ConduitManager`, or `SyncConduitBase`.** They stay alive through E.15 and get deleted in E.16.
- **Relocating `src/fullsync/` to `src/runtime/`.** The spec's §"Directory layout" and E.16 row note this relocation "in E.8", but the E.8 exit gate (spec line 586) only requires the new managers + dummy load/unload. Relocating `fullsync` is a mechanical `git mv` that (a) is blast-radius-heavy for no E.8 correctness benefit and (b) is cleaner bundled with E.15 (the unified runtime sub-phase). **Decision pinned here:** E.8 creates `src/runtime/` fresh; `src/fullsync/` stays put. E.15's plan will do the `git mv` + CMake rewire.
- **Parsing Palm `AppInfo` blocks or implementing `X-WildPalms-ClaimDescriptions` lookup.** Claim-map queries on the new manager reuse the exact logic already in `ConduitManager` (see `src/kf6/conduitmanager.cpp:188-266`); that logic is factored out into a pure helper, reused by both managers, tested once.
- **UI wiring** (menus, config pages, settings widgets). `IPlugin::createSettingsWidget` has a default `nullptr` impl; tests cover the ABI but no widget is instantiated.
- **Defining `PalmDeviceConnection`** concretely. It's forward-declared in the interface headers and treated as opaque. The real type lands when a real plugin needs it (E.10 or later).

---

## File Structure

**Files to CREATE:**

Headers (new plugin ABI):

- `src/core/iplugin.h` — base plugin interface (id, name, icon, version, settings).
- `src/core/ibackendplugin.h` — `IBackendPlugin : IPlugin`; provides `ProvidedBackends` + `createBackends()`.
- `src/core/ipluginaction.h` — `IPluginAction : IPlugin`; one-shot `execute()` + `ActionContext` QObject.

Runtime managers:

- `src/runtime/CMakeLists.txt` — defines `WildPalmsRuntime` static lib.
- `src/runtime/pluginmetadatahelpers.h` — reusable metadata readers (`metaBool`, `metaInt`, `metaStringList`).
- `src/runtime/pluginmetadatahelpers.cpp`
- `src/runtime/backendpluginmanager.h`
- `src/runtime/backendpluginmanager.cpp`
- `src/runtime/pluginactionmanager.h`
- `src/runtime/pluginactionmanager.cpp`
- `src/runtime/simpleactioncontext.h` — concrete `IPluginAction::ActionContext` impl used by `PluginActionManager::runAction()` and tests.
- `src/runtime/simpleactioncontext.cpp`

Dummy test plugins:

- `tests/plugins/CMakeLists.txt` — parent; pulls in each dummy.
- `tests/plugins/dummy_backend/CMakeLists.txt`
- `tests/plugins/dummy_backend/dummybackendplugin.h`
- `tests/plugins/dummy_backend/dummybackendplugin.cpp`
- `tests/plugins/dummy_backend/dummy-backend-plugin.json`
- `tests/plugins/dummy_action/CMakeLists.txt`
- `tests/plugins/dummy_action/dummyactionplugin.h`
- `tests/plugins/dummy_action/dummyactionplugin.cpp`
- `tests/plugins/dummy_action/dummy-action-plugin.json`

Runtime tests:

- `tests/runtime/CMakeLists.txt`
- `tests/runtime/tst_pluginmetadatahelpers.cpp`
- `tests/runtime/tst_backendpluginmanager.cpp`
- `tests/runtime/tst_pluginactionmanager.cpp`
- `tests/runtime/tst_simpleactioncontext.cpp`

**Files to MODIFY:**

- `src/CMakeLists.txt` — (a) append `add_subdirectory(runtime)` after `add_subdirectory(palm/adapters)`; (b) append `iplugin.h`, `ibackendplugin.h`, `ipluginaction.h` to the `install(FILES ...)` block for `include/wildpalms/core` at line 162-166.
- `tests/CMakeLists.txt` — append `add_subdirectory(runtime)` and `add_subdirectory(plugins)` at end.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` — flip row E.8 to `✅ **E.8**` at end.
- `docs/plans/2026-04-20-libkalburator-integration.md` — mark E.8 landed in the Phase E sub-phases table (same pattern as `4ae8099` for E.7).
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + a new `project_plugin_abi_e8.md` — record that the new ABI is live alongside the old one; record the pinned decision about deferring the `fullsync → runtime` rename.

**Files to LEAVE UNTOUCHED:**

- `src/core/iconduit.h`, `src/core/isyncconduit.h`, `src/core/itoolconduit.h` — deleted in E.16, not here.
- `src/kf6/conduitmanager.{h,cpp}` — stays alive until E.16.
- `src/plugins/**` — no plugin rewrites in E.8.
- `src/fullsync/**` — relocation deferred to E.15.
- `pilot-link/`, `pilot-link-git/` — `PROJECT_VISION.md:105`: pilot-link is READ-ONLY.

---

## Task 1: Add `IPlugin` base interface header

Goal: land `src/core/iplugin.h` as a pure abstract interface with Qt-only (no Kalburator) dependencies. Header-only; no tests yet (tested transitively via Task 4+).

**Files:**

- Create: `src/core/iplugin.h`
- Modify: `src/CMakeLists.txt` (install list — done in Task 3, not here)

- [ ] **Step 1.1: Create `src/core/iplugin.h`**

```cpp
#ifndef WILDPALMS_IPLUGIN_H
#define WILDPALMS_IPLUGIN_H

#include <QIcon>
#include <QJsonObject>
#include <QString>
#include <QWidget>

namespace WildPalms {

/**
 * @brief Base metadata-only plugin interface for WildPalms.
 *
 * Pure abstract C++ class (not a QObject) so concrete plugin classes
 * can multi-inherit QObject + IPlugin without diamond inheritance.
 *
 * Concrete plugin *kinds* extend this:
 *   - IBackendPlugin : IPlugin — syncs records via libkalburator backends.
 *   - IPluginAction  : IPlugin — one-shot triggerable operation (Install,
 *                                Restore, etc.), no record-level sync.
 *
 * Discriminated at load time by the plugin manifest key
 * `X-WildPalms-PluginType` (values: "backend" | "action").
 *
 * Replaces IConduit (Phase E.8). Old IConduit surface stays alive until
 * E.16 so the ABI rewrite can land incrementally.
 */
class IPlugin
{
public:
    virtual ~IPlugin() = default;

    // ========== Identity ==========
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QIcon   icon() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;

    // ========== Optional configuration surface ==========
    virtual bool     hasSettings() const { return false; }
    virtual QWidget *createSettingsWidget(QWidget *parent)
    {
        Q_UNUSED(parent)
        return nullptr;
    }
    virtual void        loadSettings(const QJsonObject &settings) { Q_UNUSED(settings) }
    virtual QJsonObject saveSettings() const { return {}; }
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IPlugin, "ca.vibekoder.WildPalms.IPlugin/1.0")

#endif // WILDPALMS_IPLUGIN_H
```

- [ ] **Step 1.2: Verify the header compiles standalone**

Run: `cd build-dev && cmake --build . --target WildPalmsCore 2>&1 | tail -20`

Expected: no build errors. The header isn't included from any TU yet — this only confirms it doesn't break existing sources when sitting in the tree. (The file is not yet added to `add_library(WildPalmsCore ...)`; that happens in Task 3.)

- [ ] **Step 1.3: Commit**

```bash
git add src/core/iplugin.h
git commit -m "$(cat <<'EOF'
feat(core): IPlugin base interface (Phase E.8)

Pure abstract interface for the new WildPalms plugin ABI.
Qt-only dependencies (no Kalburator::Sync leak into WildPalmsCore).
Concrete plugin kinds (IBackendPlugin, IPluginAction) extend this.
Discrimination via X-WildPalms-PluginType manifest key.

Per docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
§Plugin ABI. Old IConduit surface stays until E.16.

EOF
)"
```

---

## Task 2: Add `IBackendPlugin` and `IPluginAction` headers

Goal: land the two concrete plugin-kind interfaces. Both forward-declare their `Kalburator::Sync` and `PalmDeviceConnection` dependencies so the headers stay lean.

**Files:**

- Create: `src/core/ibackendplugin.h`
- Create: `src/core/ipluginaction.h`

- [ ] **Step 2.1: Create `src/core/ibackendplugin.h`**

```cpp
#ifndef WILDPALMS_IBACKENDPLUGIN_H
#define WILDPALMS_IBACKENDPLUGIN_H

#include "iplugin.h"

#include <QStringList>

// Forward-declare upstream types so this header stays Kalburator-free.
namespace Kalburator::Sync {
    class ISyncHost;
    class IBlobBackend;
    class SyncBackend;
    namespace QSyncCore {
        class ConflictHandler;
    }
}

class PalmDeviceConnection; // concrete type lands in a later sub-phase (E.10+)

namespace WildPalms {

/**
 * @brief Plugin that provides one or more libkalburator backends.
 *
 * The manager calls createBackends() once per session; the plugin
 * returns a ProvidedBackends struct holding (at minimum) an
 * IBlobBackend* for transport and optionally a typed SyncBackend*
 * for record-typed consumers (e.g. PalmCalendarBackend).
 *
 * Ownership: the caller (BackendPluginManager) takes ownership of
 * the returned backend pointers and parents them to a suitable
 * QObject. The plugin MUST construct each backend fresh per call.
 */
class IBackendPlugin : public IPlugin
{
public:
    // ========== Database claims ==========
    //
    // Which Palm databases this plugin claims. Keys mirror the legacy
    // X-WildPalms-PalmDatabases / X-WildPalms-ClaimDescriptions JSON.
    virtual QStringList claimedDatabases() const = 0;

    // ========== Backend construction ==========
    struct ProvidedBackends {
        Kalburator::Sync::IBlobBackend *blob     = nullptr; // required
        Kalburator::Sync::SyncBackend  *calendar = nullptr; // optional
    };

    virtual ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                            PalmDeviceConnection         *device) = 0;

    // ========== Optional conflict handler ==========
    //
    // Return nullptr to use the default handler; otherwise the manager
    // registers the returned handler with the SyncCoordinator's
    // ConflictHandlerRegistry under this plugin's backend id.
    virtual Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler()
    {
        return nullptr;
    }

    // ========== Ordering hints ==========
    //
    // Replaces SyncConduitBase::runBefore / runAfter. Values are plugin
    // ids of other backend plugins.
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter() const  { return {}; }
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IBackendPlugin,
                    "ca.vibekoder.WildPalms.IBackendPlugin/1.0")

#endif // WILDPALMS_IBACKENDPLUGIN_H
```

- [ ] **Step 2.2: Create `src/core/ipluginaction.h`**

```cpp
#ifndef WILDPALMS_IPLUGINACTION_H
#define WILDPALMS_IPLUGINACTION_H

#include "iplugin.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class PalmDeviceConnection;

namespace WildPalms {

/**
 * @brief One-shot triggerable plugin kind.
 *
 * Actions do not participate in record-level sync. Used for
 * Install-file, Backup/Restore, Format-card, etc. Execution is
 * synchronous from the caller's perspective; callers run it on a
 * worker thread. Progress and log output surface through a QObject
 * proxy (ActionContext) rather than the IPluginAction itself, so
 * actions remain cheap to load (no QObject overhead per plugin).
 */
class IPluginAction : public IPlugin
{
public:
    class ActionContext : public QObject
    {
        Q_OBJECT
    public:
        using QObject::QObject;
        ~ActionContext() override = default;

        virtual void setTotal(int total)      = 0;
        virtual void setCurrent(int current)  = 0;
        virtual void log(const QString &msg)  = 0;
        virtual bool isCancelled() const      = 0;

    Q_SIGNALS:
        void progress(int current, int total);
        void message(const QString &msg);
    };

    /// Runs the action. Returns true on success. Called on a worker
    /// thread by PluginActionManager::runAction(); actions MUST NOT
    /// assume a GUI thread.
    virtual bool execute(ActionContext       *ctx,
                         PalmDeviceConnection *device,
                         const QJsonObject   &parameters) = 0;

    /// What must be true before the manager offers this action.
    struct Preconditions {
        bool        requiresDeviceConnection = true;
        QStringList requiresFiles;
    };
    virtual Preconditions preconditions() const = 0;
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IPluginAction,
                    "ca.vibekoder.WildPalms.IPluginAction/1.0")

#endif // WILDPALMS_IPLUGINACTION_H
```

- [ ] **Step 2.3: Verify both headers compile cleanly under WildPalmsCore**

Run: `cd build-dev && cmake --build . --target WildPalmsCore 2>&1 | tail -20`

Expected: no errors.

- [ ] **Step 2.4: Commit**

```bash
git add src/core/ibackendplugin.h src/core/ipluginaction.h
git commit -m "$(cat <<'EOF'
feat(core): IBackendPlugin + IPluginAction interfaces (Phase E.8)

Two concrete plugin-kind interfaces extending IPlugin:
  - IBackendPlugin: provides Kalburator::Sync backends (blob + optional
    calendar-typed) plus an optional per-backend ConflictHandler.
  - IPluginAction: one-shot triggerable (Install/Restore/...), synchronous,
    progress via an ActionContext QObject proxy.

Kalburator::Sync and PalmDeviceConnection types forward-declared only
so WildPalmsCore's public surface stays Kalburator-free (policy pinned
in src/CMakeLists.txt:148-151).

Per docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
§Plugin ABI.

EOF
)"
```

---

## Task 3: Wire new headers into `WildPalmsCore` sources + install list

Goal: get the three new headers tracked by CMake so they ship with `install(TARGETS WildPalmsCore EXPORT WildPalmsTargets ...)` and are visible to downstream consumers (`WildPalmsRuntime`, later `WildPalmsRuntime`-dependent plugins).

**Files:**

- Modify: `src/CMakeLists.txt`

- [ ] **Step 3.1: Check current lines we will modify**

Run: `grep -n 'iconduit\|install(FILES' /home/clinton/dev/WildPalms/src/CMakeLists.txt`

Expected: line ~157-166 has the `install(FILES core/iconduit.h core/isyncconduit.h core/itoolconduit.h DESTINATION include/wildpalms/core)` block. No source entry for the `.h`-only interfaces (they're header-only; WildPalmsCore only lists them for install).

- [ ] **Step 3.2: Add the three new headers to the install block**

In `src/CMakeLists.txt`, change this block:

```cmake
install(FILES
    core/iconduit.h
    core/isyncconduit.h
    core/itoolconduit.h
    DESTINATION include/wildpalms/core
)
```

to:

```cmake
install(FILES
    core/iconduit.h
    core/isyncconduit.h
    core/itoolconduit.h
    core/iplugin.h
    core/ibackendplugin.h
    core/ipluginaction.h
    DESTINATION include/wildpalms/core
)
```

- [ ] **Step 3.3: Confirm the full project still configures and builds**

Run: `cd build-dev && cmake --build . --target WildPalmsCore 2>&1 | tail -20`

Expected: no errors.

- [ ] **Step 3.4: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "$(cat <<'EOF'
build(core): install new plugin-ABI headers (Phase E.8)

Add iplugin.h, ibackendplugin.h, ipluginaction.h to the
install(FILES ... DESTINATION include/wildpalms/core) block so
downstream consumers (WildPalmsRuntime, ported plugins in E.9+)
can #include them from installed headers.

EOF
)"
```

---

## Task 4: Scaffold `WildPalmsRuntime` static lib + metadata helpers (TDD)

Goal: create the empty `src/runtime/` directory with its CMake wiring and one minimal TU (`pluginmetadatahelpers.{h,cpp}`) carrying the JSON-reading utilities `ConduitManager` currently has as private statics. These helpers are shared between both new managers, so factoring them out first keeps the manager implementations short and testable.

**Files:**

- Create: `src/runtime/CMakeLists.txt`
- Create: `src/runtime/pluginmetadatahelpers.h`
- Create: `src/runtime/pluginmetadatahelpers.cpp`
- Create: `tests/runtime/CMakeLists.txt`
- Create: `tests/runtime/tst_pluginmetadatahelpers.cpp`
- Modify: `src/CMakeLists.txt` (append `add_subdirectory(runtime)`)
- Modify: `tests/CMakeLists.txt` (append `add_subdirectory(runtime)`)

- [ ] **Step 4.1: Create `src/runtime/CMakeLists.txt`**

```cmake
# WildPalmsRuntime — managers for the new WP plugin ABI (Phase E.8 of the
# libkalburator integration). Houses BackendPluginManager and
# PluginActionManager plus the shared metadata-reading helpers. Keeps
# KPluginFactory / KPluginMetaData wiring out of WildPalmsCore and
# confines the Kalburator::Sync dependency to a single static lib.
#
# Deliberately does NOT absorb src/fullsync/ yet — that relocation is
# deferred to E.15 per docs/superpowers/plans/2026-04-23-phase-e8-plugin-abi.md.

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

add_library(WildPalmsRuntime STATIC
    pluginmetadatahelpers.h
    pluginmetadatahelpers.cpp
)

target_include_directories(WildPalmsRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        KF6::CoreAddons
        WildPalmsCore
)

set_target_properties(WildPalmsRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

- [ ] **Step 4.2: Create `src/runtime/pluginmetadatahelpers.h`**

```cpp
#ifndef WILDPALMS_PLUGINMETADATAHELPERS_H
#define WILDPALMS_PLUGINMETADATAHELPERS_H

#include <QString>
#include <QStringList>

class KPluginMetaData;

namespace WildPalms::Runtime {

/// Read a custom string value from plugin JSON metadata. Returns an
/// empty QString if the key is absent.
QString metaString(const KPluginMetaData &md, const QString &key);

/// Read a custom bool value. Returns `defaultValue` if the key is
/// absent; case-insensitive "true" is the only affirmative spelling.
bool metaBool(const KPluginMetaData &md, const QString &key, bool defaultValue);

/// Read a custom int value. Returns `defaultValue` if the key is absent
/// or not parseable as an int.
int metaInt(const KPluginMetaData &md, const QString &key, int defaultValue);

/// Read a custom string-array value. Accepts either a JSON array of
/// strings or a single string (returned as a one-element list).
QStringList metaStringList(const KPluginMetaData &md, const QString &key);

} // namespace WildPalms::Runtime

#endif // WILDPALMS_PLUGINMETADATAHELPERS_H
```

- [ ] **Step 4.3: Create `tests/runtime/CMakeLists.txt` and a failing test**

```cmake
# Phase E.8 — WildPalmsRuntime tests (BackendPluginManager,
# PluginActionManager, plugin metadata helpers).

function(add_wildpalms_runtime_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            KF6::CoreAddons
            WildPalmsCore
            WildPalmsRuntime
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_wildpalms_runtime_test(tst_pluginmetadatahelpers tst_pluginmetadatahelpers.cpp)
```

Create `tests/runtime/tst_pluginmetadatahelpers.cpp`:

```cpp
#include <QtTest/QtTest>

#include <KPluginMetaData>
#include <QJsonObject>

#include "runtime/pluginmetadatahelpers.h"

using namespace WildPalms::Runtime;

class TestPluginMetadataHelpers : public QObject
{
    Q_OBJECT
private slots:
    void missingStringReturnsEmpty();
    void presentStringRoundTrips();
    void boolTrueCaseInsensitive();
    void boolFalsyFallsBack();
    void intValidRoundTrips();
    void intInvalidFallsBack();
    void stringListArrayReadsAll();
    void stringListSingleStringWrapsInList();
    void stringListMissingReturnsEmpty();

private:
    static KPluginMetaData metaFor(const QJsonObject &obj)
    {
        return KPluginMetaData(obj, QStringLiteral("synthetic"));
    }
};

void TestPluginMetadataHelpers::missingStringReturnsEmpty()
{
    const KPluginMetaData md = metaFor({});
    QCOMPARE(metaString(md, QStringLiteral("X-Foo")), QString());
}

void TestPluginMetadataHelpers::presentStringRoundTrips()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Foo"), QStringLiteral("bar")}});
    QCOMPARE(metaString(md, QStringLiteral("X-Foo")), QStringLiteral("bar"));
}

void TestPluginMetadataHelpers::boolTrueCaseInsensitive()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Enabled"), QStringLiteral("TRUE")}});
    QVERIFY(metaBool(md, QStringLiteral("X-Enabled"), false));
}

void TestPluginMetadataHelpers::boolFalsyFallsBack()
{
    const KPluginMetaData md = metaFor({});
    QVERIFY(!metaBool(md, QStringLiteral("X-Missing"), false));
    QVERIFY(metaBool(md, QStringLiteral("X-Missing"), true));
}

void TestPluginMetadataHelpers::intValidRoundTrips()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Order"), QStringLiteral("42")}});
    QCOMPARE(metaInt(md, QStringLiteral("X-Order"), 0), 42);
}

void TestPluginMetadataHelpers::intInvalidFallsBack()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Order"), QStringLiteral("not-an-int")}});
    QCOMPARE(metaInt(md, QStringLiteral("X-Order"), 7), 7);
}

void TestPluginMetadataHelpers::stringListArrayReadsAll()
{
    QJsonArray arr{QStringLiteral("a"), QStringLiteral("b")};
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Many"), arr}});
    QCOMPARE(metaStringList(md, QStringLiteral("X-Many")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestPluginMetadataHelpers::stringListSingleStringWrapsInList()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-One"), QStringLiteral("solo")}});
    QCOMPARE(metaStringList(md, QStringLiteral("X-One")),
             (QStringList{QStringLiteral("solo")}));
}

void TestPluginMetadataHelpers::stringListMissingReturnsEmpty()
{
    const KPluginMetaData md = metaFor({});
    QCOMPARE(metaStringList(md, QStringLiteral("X-Nope")), QStringList());
}

QTEST_MAIN(TestPluginMetadataHelpers)
#include "tst_pluginmetadatahelpers.moc"
```

- [ ] **Step 4.4: Wire both new subdirs into the tree**

Append to `src/CMakeLists.txt` (at the end, after `add_subdirectory(palm/adapters)`):

```cmake
# New-ABI plugin managers (Phase E.8 of libkalburator integration).
# Consumes the plugin-ABI headers from src/core/ and Kalburator::Sync.
# Leaves src/fullsync/ alone — the fullsync → runtime relocation lands
# in E.15.
add_subdirectory(runtime)
```

Append to `tests/CMakeLists.txt` (at the end):

```cmake
# Phase E.8 — new-ABI plugin manager tests + dummy plugins.
add_subdirectory(plugins)
add_subdirectory(runtime)
```

`tests/plugins/CMakeLists.txt` does not exist yet, so temporarily disable its inclusion:

Change the block to:

```cmake
# Phase E.8 — new-ABI plugin manager tests.
add_subdirectory(runtime)
# Dummy plugins land in Task 9 / 10.
# add_subdirectory(plugins)
```

The `add_subdirectory(plugins)` line is un-commented in Task 9.

- [ ] **Step 4.5: Create `src/runtime/pluginmetadatahelpers.cpp` (minimal stub — make the test fail)**

```cpp
#include "pluginmetadatahelpers.h"

#include <KPluginMetaData>

namespace WildPalms::Runtime {

QString metaString(const KPluginMetaData &, const QString &) { return {}; }
bool metaBool(const KPluginMetaData &, const QString &, bool defaultValue) { return defaultValue; }
int  metaInt(const KPluginMetaData &, const QString &, int defaultValue) { return defaultValue; }
QStringList metaStringList(const KPluginMetaData &, const QString &) { return {}; }

} // namespace WildPalms::Runtime
```

- [ ] **Step 4.6: Configure + build + run the test; expect most of it to FAIL**

Run:

```bash
cd build-dev && cmake --build . --target tst_pluginmetadatahelpers 2>&1 | tail -5 && ctest -R '^tst_pluginmetadatahelpers$' --output-on-failure
```

Expected: `tst_pluginmetadatahelpers` builds and runs. `presentStringRoundTrips`, `boolTrueCaseInsensitive`, `intValidRoundTrips`, `stringListArrayReadsAll`, `stringListSingleStringWrapsInList` **FAIL** (stub returns empty). `missingStringReturnsEmpty`, `boolFalsyFallsBack`, `intInvalidFallsBack`, `stringListMissingReturnsEmpty` pass (stub's fallbacks happen to match).

- [ ] **Step 4.7: Implement `pluginmetadatahelpers.cpp` — port logic from `ConduitManager`**

Replace the stub with the real logic (adapted from `src/kf6/conduitmanager.cpp:373-425`):

```cpp
#include "pluginmetadatahelpers.h"

#include <KPluginMetaData>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace WildPalms::Runtime {

QString metaString(const KPluginMetaData &md, const QString &key)
{
    return md.value(key);
}

bool metaBool(const KPluginMetaData &md, const QString &key, bool defaultValue)
{
    const QString val = md.value(key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    return val.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

int metaInt(const KPluginMetaData &md, const QString &key, int defaultValue)
{
    const QString val = md.value(key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    bool ok = false;
    const int v = val.toInt(&ok);
    return ok ? v : defaultValue;
}

QStringList metaStringList(const KPluginMetaData &md, const QString &key)
{
    QStringList result;
    const QJsonObject raw = md.rawData();
    const QJsonValue val = raw.value(key);

    if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        result.reserve(arr.size());
        for (const QJsonValue &v : arr) {
            const QString s = v.toString();
            if (!s.isEmpty()) {
                result.append(s);
            }
        }
    } else if (val.isString()) {
        const QString s = val.toString();
        if (!s.isEmpty()) {
            result.append(s);
        }
    }

    return result;
}

} // namespace WildPalms::Runtime
```

- [ ] **Step 4.8: Re-run the test — expect all green**

Run: `cd build-dev && cmake --build . --target tst_pluginmetadatahelpers 2>&1 | tail -5 && ctest -R '^tst_pluginmetadatahelpers$' --output-on-failure`

Expected: 9/9 pass.

- [ ] **Step 4.9: Commit**

```bash
git add src/runtime/CMakeLists.txt src/runtime/pluginmetadatahelpers.h src/runtime/pluginmetadatahelpers.cpp \
        tests/runtime/CMakeLists.txt tests/runtime/tst_pluginmetadatahelpers.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): scaffold WildPalmsRuntime + metadata helpers (Phase E.8)

Create src/runtime/ with the new-ABI managers' home and land the shared
JSON metadata readers (metaString / metaBool / metaInt / metaStringList)
factored out of ConduitManager.

TDD'd against tst_pluginmetadatahelpers (9 cases: present/missing,
case-insensitive bool, int fallback, array-vs-single-string stringlist).
All pass.

Leaves src/fullsync/ in place; the fullsync → runtime relocation lands
in E.15.

EOF
)"
```

---

## Task 5: `BackendPluginManager` — discovery + filter by `PluginType` (TDD)

Goal: implement `BackendPluginManager::loadPlugins()` scanning `wildpalms/plugins/` and populating an internal catalogue of `KPluginMetaData` filtered by `X-WildPalms-PluginType == "backend"`. Instantiation (`KPluginFactory`) comes in Task 6.

**Files:**

- Create: `src/runtime/backendpluginmanager.h`
- Create: `src/runtime/backendpluginmanager.cpp`
- Modify: `src/runtime/CMakeLists.txt` (append new sources)
- Create: `tests/runtime/tst_backendpluginmanager.cpp`
- Modify: `tests/runtime/CMakeLists.txt` (append new test)

- [ ] **Step 5.1: Create `src/runtime/backendpluginmanager.h`**

```cpp
#ifndef WILDPALMS_BACKENDPLUGINMANAGER_H
#define WILDPALMS_BACKENDPLUGINMANAGER_H

#include <KPluginMetaData>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {
    class ISyncHost;
    class SyncCoordinator;
}

class PalmDeviceConnection;

namespace WildPalms {

class IBackendPlugin;

/**
 * @brief Discovers, loads, and owns IBackendPlugin instances.
 *
 * Replaces ConduitManager for the new plugin ABI. ConduitManager keeps
 * running against IConduit plugins under wildpalms/conduits/ until E.16
 * deletes it. BackendPluginManager scans wildpalms/plugins/ and filters
 * by X-WildPalms-PluginType == "backend".
 *
 * Host / device / coordinator are borrowed pointers whose lifetimes the
 * runtime guarantees to exceed the manager's. May be nullptr in tests.
 */
class BackendPluginManager : public QObject
{
    Q_OBJECT
public:
    struct PluginInfo {
        KPluginMetaData  metaData;
        IBackendPlugin  *instance = nullptr;
        QStringList      claimedDatabases;
        bool             defaultEnabled = true;
        int              sortOrder      = 0;
    };

    explicit BackendPluginManager(Kalburator::Sync::ISyncHost       *host,
                                   PalmDeviceConnection              *device,
                                   Kalburator::Sync::SyncCoordinator *coordinator,
                                   QObject                           *parent = nullptr);
    ~BackendPluginManager() override;

    /// Scan plugin dirs (default subdir: "wildpalms/plugins"), filter by
    /// X-WildPalms-PluginType == "backend", and populate the catalogue.
    /// Does NOT instantiate plugins — call loadPlugin(id) for that.
    /// Idempotent: re-scanning refreshes metadata but preserves instances.
    void discoverPlugins();

    /// Instantiate a single plugin by id via KPluginFactory. Returns true
    /// on success. Emits pluginLoaded().
    bool loadPlugin(const QString &pluginId);

    /// Instantiate every discovered plugin (subject to defaultEnabled).
    /// Convenience; the runtime may prefer selective loadPlugin() calls.
    void loadPlugins();

    /// Destroy a loaded plugin. Emits pluginUnloading() first.
    void unloadPlugin(const QString &pluginId);

    // ========== Queries ==========
    QList<IBackendPlugin *> plugins() const;
    QList<PluginInfo>       catalogue() const;
    IBackendPlugin         *plugin(const QString &pluginId) const;
    IBackendPlugin         *pluginForDatabase(const QString &palmDbName) const;

    // ========== Test / customisation seams ==========
    /// Override the plugin subdir scanned by discoverPlugins. Default:
    /// "wildpalms/plugins". Tests point this at a test-only subdir.
    void setPluginSubdir(const QString &subdir);

Q_SIGNALS:
    void pluginLoaded(IBackendPlugin *plugin);
    void pluginUnloading(IBackendPlugin *plugin);

private:
    QString m_subdir;
    QMap<QString, PluginInfo> m_plugins;

    Kalburator::Sync::ISyncHost       *m_host        = nullptr;
    PalmDeviceConnection              *m_device      = nullptr;
    Kalburator::Sync::SyncCoordinator *m_coordinator = nullptr;
};

} // namespace WildPalms

#endif // WILDPALMS_BACKENDPLUGINMANAGER_H
```

- [ ] **Step 5.2: Create minimal `src/runtime/backendpluginmanager.cpp` (stub)**

```cpp
#include "backendpluginmanager.h"

#include "core/ibackendplugin.h"
#include "pluginmetadatahelpers.h"

#include <KPluginFactory>

#include <QDebug>

namespace WildPalms {

BackendPluginManager::BackendPluginManager(Kalburator::Sync::ISyncHost       *host,
                                             PalmDeviceConnection              *device,
                                             Kalburator::Sync::SyncCoordinator *coordinator,
                                             QObject                           *parent)
    : QObject(parent)
    , m_subdir(QStringLiteral("wildpalms/plugins"))
    , m_host(host)
    , m_device(device)
    , m_coordinator(coordinator)
{
}

BackendPluginManager::~BackendPluginManager()
{
    const QStringList ids = m_plugins.keys();
    for (const QString &id : ids) {
        if (m_plugins[id].instance) {
            unloadPlugin(id);
        }
    }
}

void BackendPluginManager::setPluginSubdir(const QString &subdir) { m_subdir = subdir; }

void BackendPluginManager::discoverPlugins() {}
bool BackendPluginManager::loadPlugin(const QString &) { return false; }
void BackendPluginManager::loadPlugins() {}
void BackendPluginManager::unloadPlugin(const QString &) {}

QList<IBackendPlugin *> BackendPluginManager::plugins() const { return {}; }
QList<BackendPluginManager::PluginInfo> BackendPluginManager::catalogue() const { return m_plugins.values(); }
IBackendPlugin *BackendPluginManager::plugin(const QString &) const { return nullptr; }
IBackendPlugin *BackendPluginManager::pluginForDatabase(const QString &) const { return nullptr; }

} // namespace WildPalms
```

- [ ] **Step 5.3: Append sources to `src/runtime/CMakeLists.txt`**

Change the `add_library(WildPalmsRuntime STATIC ...)` block to:

```cmake
add_library(WildPalmsRuntime STATIC
    pluginmetadatahelpers.h
    pluginmetadatahelpers.cpp
    backendpluginmanager.h
    backendpluginmanager.cpp
)
```

- [ ] **Step 5.4: Write the first failing test — discovery with zero plugins yields empty catalogue**

Create `tests/runtime/tst_backendpluginmanager.cpp`:

```cpp
#include <QtTest/QtTest>

#include "runtime/backendpluginmanager.h"

using WildPalms::BackendPluginManager;

class TestBackendPluginManager : public QObject
{
    Q_OBJECT
private slots:
    void discoverWithEmptyPathYieldsEmptyCatalogue();
    // Populated in later steps.
};

void TestBackendPluginManager::discoverWithEmptyPathYieldsEmptyCatalogue()
{
    BackendPluginManager mgr(nullptr, nullptr, nullptr);
    // Point at a subdir that definitely holds no plugins.
    mgr.setPluginSubdir(QStringLiteral("wildpalms_e8_nonexistent"));
    mgr.discoverPlugins();

    QCOMPARE(mgr.catalogue().size(), 0);
    QCOMPARE(mgr.plugins().size(), 0);
}

QTEST_MAIN(TestBackendPluginManager)
#include "tst_backendpluginmanager.moc"
```

Append to `tests/runtime/CMakeLists.txt`:

```cmake
add_wildpalms_runtime_test(tst_backendpluginmanager tst_backendpluginmanager.cpp)
```

- [ ] **Step 5.5: Build + run — expect PASS (the stub trivially satisfies the empty case)**

Run: `cd build-dev && cmake --build . --target tst_backendpluginmanager 2>&1 | tail -5 && ctest -R '^tst_backendpluginmanager$' --output-on-failure`

Expected: 1/1 pass. The stub's `discoverPlugins()` is a no-op, so the catalogue stays empty.

- [ ] **Step 5.6: Implement `discoverPlugins()` for real**

Replace the stub body in `backendpluginmanager.cpp`:

```cpp
void BackendPluginManager::discoverPlugins()
{
    const QList<KPluginMetaData> found =
        KPluginMetaData::findPlugins(m_subdir);

    for (const KPluginMetaData &md : found) {
        // Filter: only keep plugins declaring themselves as "backend".
        const QString pluginType =
            Runtime::metaString(md, QStringLiteral("X-WildPalms-PluginType"));
        if (pluginType != QStringLiteral("backend")) {
            continue;
        }

        QString pluginId = md.pluginId();
        if (pluginId.isEmpty()) {
            qWarning() << "[BackendPluginManager] Skipping plugin with empty id:"
                       << md.fileName();
            continue;
        }

        // Re-discovery: preserve running instance, refresh metadata.
        if (m_plugins.contains(pluginId)) {
            m_plugins[pluginId].metaData = md;
            continue;
        }

        PluginInfo info;
        info.metaData        = md;
        info.instance        = nullptr;
        info.claimedDatabases = Runtime::metaStringList(
            md, QStringLiteral("X-WildPalms-PalmDatabases"));
        info.defaultEnabled  = Runtime::metaBool(
            md, QStringLiteral("X-WildPalms-DefaultEnabled"), true);
        info.sortOrder       = Runtime::metaInt(
            md, QStringLiteral("X-WildPalms-SortOrder"), 0);

        m_plugins.insert(pluginId, info);

        qDebug() << "[BackendPluginManager] Discovered backend plugin:" << pluginId
                 << "databases:" << info.claimedDatabases
                 << "sortOrder:" << info.sortOrder;
    }
}
```

- [ ] **Step 5.7: Re-run test, expect still-green**

Run: `cd build-dev && cmake --build . --target tst_backendpluginmanager 2>&1 | tail -5 && ctest -R '^tst_backendpluginmanager$' --output-on-failure`

Expected: pass. (Real logic now, but with no discoverable plugins, catalogue is still empty.)

- [ ] **Step 5.8: Commit**

```bash
git add src/runtime/backendpluginmanager.h src/runtime/backendpluginmanager.cpp \
        src/runtime/CMakeLists.txt \
        tests/runtime/tst_backendpluginmanager.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): BackendPluginManager discovery (Phase E.8)

Scan wildpalms/plugins/, filter by X-WildPalms-PluginType == "backend",
and populate a PluginInfo catalogue (metadata + claim list + sortOrder +
defaultEnabled). Instance slot stays nullptr until loadPlugin(id) lands
in the next commit.

setPluginSubdir() is a test seam — tests point at a nonexistent subdir
to confirm empty-state behaviour without needing a real .so.

EOF
)"
```

---

## Task 6: `BackendPluginManager` — load / unload / plugins() (TDD via dummy stub)

Goal: implement `loadPlugin(id)`, `loadPlugins()`, `unloadPlugin(id)`, `plugins()`, `plugin(id)`, and destructor cleanup. Correctness is proven later by the end-to-end dummy-plugin test (Task 11); this task tests the purely in-process slice via a **direct-injection test seam**: we temporarily add a `protected` `registerInstanceForTest(...)` hook so we can assert the lifecycle without `KPluginFactory` loading.

Rationale: `KPluginFactory::loadFactory` requires an actual `.so`. Building a dummy plugin needs several moving parts (CMake, JSON, QObject). Rather than interleave that with manager implementation, we first prove manager lifecycle logic in isolation against an injected instance, then prove the factory path separately in Task 11.

**Files:**

- Modify: `src/runtime/backendpluginmanager.h` (add protected seam)
- Modify: `src/runtime/backendpluginmanager.cpp` (implement real methods)
- Modify: `tests/runtime/tst_backendpluginmanager.cpp` (add in-process tests)

- [ ] **Step 6.1: Add the injection seam + implement `loadPlugin` (factory path) + real lifecycle**

In `backendpluginmanager.h`, before the `private:` block, add:

```cpp
protected:
    /// Test-only hook: inject a pre-built IBackendPlugin* into the
    /// catalogue as if KPluginFactory had loaded it. The manager takes
    /// ownership of `instance` via QObject parenting. Returns false if
    /// `pluginId` already has a live instance.
    bool registerInstanceForTest(const QString &pluginId, IBackendPlugin *instance);
```

In `backendpluginmanager.cpp`, replace the stubs with:

```cpp
bool BackendPluginManager::loadPlugin(const QString &pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) {
        qWarning() << "[BackendPluginManager] Unknown plugin:" << pluginId;
        return false;
    }
    if (it->instance) {
        return true; // already loaded
    }

    auto factoryResult = KPluginFactory::loadFactory(it->metaData);
    if (!factoryResult) {
        qWarning() << "[BackendPluginManager] Factory load failed:" << pluginId
                   << factoryResult.errorString;
        return false;
    }

    QObject *obj = factoryResult.plugin->create<QObject>(this);
    if (!obj) {
        qWarning() << "[BackendPluginManager] Factory returned nullptr:" << pluginId;
        return false;
    }

    auto *plug = dynamic_cast<IBackendPlugin *>(obj);
    if (!plug) {
        qWarning() << "[BackendPluginManager] Plugin does not implement IBackendPlugin:"
                   << pluginId;
        delete obj;
        return false;
    }

    it->instance = plug;
    qDebug() << "[BackendPluginManager] Loaded plugin:" << plug->pluginId()
             << "(" << plug->displayName() << ")";
    emit pluginLoaded(plug);
    return true;
}

void BackendPluginManager::loadPlugins()
{
    const QStringList ids = m_plugins.keys();
    for (const QString &id : ids) {
        if (m_plugins[id].defaultEnabled && !m_plugins[id].instance) {
            loadPlugin(id);
        }
    }
}

void BackendPluginManager::unloadPlugin(const QString &pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end() || !it->instance) {
        return;
    }
    emit pluginUnloading(it->instance);
    QObject *obj = dynamic_cast<QObject *>(it->instance);
    delete obj;
    it->instance = nullptr;
    qDebug() << "[BackendPluginManager] Unloaded plugin:" << pluginId;
}

QList<IBackendPlugin *> BackendPluginManager::plugins() const
{
    QList<IBackendPlugin *> out;
    for (const PluginInfo &info : m_plugins) {
        if (info.instance) {
            out.append(info.instance);
        }
    }
    return out;
}

IBackendPlugin *BackendPluginManager::plugin(const QString &pluginId) const
{
    auto it = m_plugins.constFind(pluginId);
    return (it != m_plugins.constEnd()) ? it->instance : nullptr;
}

IBackendPlugin *BackendPluginManager::pluginForDatabase(const QString &palmDbName) const
{
    for (const PluginInfo &info : m_plugins) {
        if (info.claimedDatabases.contains(palmDbName) && info.instance) {
            return info.instance;
        }
    }
    return nullptr;
}

bool BackendPluginManager::registerInstanceForTest(const QString &pluginId,
                                                     IBackendPlugin *instance)
{
    if (!instance) return false;

    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) {
        // Synthesise a PluginInfo with minimal metadata so lifecycle
        // and queries work. claimedDatabases pulled from the instance.
        PluginInfo info;
        info.instance         = instance;
        info.claimedDatabases = instance->claimedDatabases();
        info.defaultEnabled   = true;
        info.sortOrder        = 0;
        m_plugins.insert(pluginId, info);
    } else {
        if (it->instance) return false;
        it->instance = instance;
        if (it->claimedDatabases.isEmpty()) {
            it->claimedDatabases = instance->claimedDatabases();
        }
    }

    if (auto *obj = dynamic_cast<QObject *>(instance)) {
        obj->setParent(this);
    }
    emit pluginLoaded(instance);
    return true;
}
```

- [ ] **Step 6.2: Expose the test seam via a friend subclass in the test file**

Append to `tests/runtime/tst_backendpluginmanager.cpp`:

```cpp
#include "core/ibackendplugin.h"

#include <QIcon>

namespace {

class FakeBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    QString pluginId() const override    { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QIcon   icon() const override        { return {}; }
    QString description() const override { return {}; }
    QString version() const override     { return QStringLiteral("1.0"); }
    QStringList claimedDatabases() const override
    {
        return {QStringLiteral("FakeDB")};
    }
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                     PalmDeviceConnection *) override
    {
        return {}; // both pointers nullptr; this test doesn't run the engine.
    }
};

class TestableBackendPluginManager : public WildPalms::BackendPluginManager
{
public:
    using BackendPluginManager::BackendPluginManager;
    using BackendPluginManager::registerInstanceForTest;
};

} // namespace
```

Also add these test slot declarations in the class:

```cpp
    void registerInjectedPluginShowsUpInQueries();
    void unloadInjectedPluginClearsInstance();
    void pluginForDatabaseMatchesClaim();
    void destructorUnloadsAll();
```

And their implementations:

```cpp
void TestBackendPluginManager::registerInjectedPluginShowsUpInQueries()
{
    TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
    auto *fake = new FakeBackendPlugin();
    QVERIFY(mgr.registerInstanceForTest(QStringLiteral("fake"), fake));

    QCOMPARE(mgr.plugins().size(), 1);
    QCOMPARE(mgr.plugin(QStringLiteral("fake")), fake);
    QCOMPARE(mgr.catalogue().size(), 1);
}

void TestBackendPluginManager::unloadInjectedPluginClearsInstance()
{
    TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
    auto *fake = new FakeBackendPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake"), fake);

    QSignalSpy unloadSpy(&mgr,
        &WildPalms::BackendPluginManager::pluginUnloading);

    mgr.unloadPlugin(QStringLiteral("fake"));

    QCOMPARE(unloadSpy.count(), 1);
    QCOMPARE(mgr.plugins().size(), 0);
    QCOMPARE(mgr.plugin(QStringLiteral("fake")), nullptr);
}

void TestBackendPluginManager::pluginForDatabaseMatchesClaim()
{
    TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
    auto *fake = new FakeBackendPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake"), fake);

    QCOMPARE(mgr.pluginForDatabase(QStringLiteral("FakeDB")), fake);
    QCOMPARE(mgr.pluginForDatabase(QStringLiteral("OtherDB")), nullptr);
}

void TestBackendPluginManager::destructorUnloadsAll()
{
    QPointer<FakeBackendPlugin> fakeGuard;
    {
        TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
        auto *fake = new FakeBackendPlugin();
        fakeGuard = fake;
        mgr.registerInstanceForTest(QStringLiteral("fake"), fake);
        QVERIFY(!fakeGuard.isNull());
    }
    // Manager went out of scope; destructor should have deleted fake.
    QVERIFY(fakeGuard.isNull());
}
```

Add `#include <QSignalSpy>` and `#include <QPointer>` near the other includes.

- [ ] **Step 6.3: Build + run — expect all new cases green**

Run: `cd build-dev && cmake --build . --target tst_backendpluginmanager 2>&1 | tail -10 && ctest -R '^tst_backendpluginmanager$' --output-on-failure`

Expected: 5/5 pass.

- [ ] **Step 6.4: Commit**

```bash
git add src/runtime/backendpluginmanager.h src/runtime/backendpluginmanager.cpp \
        tests/runtime/tst_backendpluginmanager.cpp
git commit -m "$(cat <<'EOF'
feat(runtime): BackendPluginManager load/unload lifecycle (Phase E.8)

Implement loadPlugin(id) via KPluginFactory, loadPlugins() convenience,
unloadPlugin(id) with pluginUnloading signal, plugins() / plugin(id) /
pluginForDatabase(name) queries, and destructor cleanup.

Add a protected registerInstanceForTest seam so manager lifecycle can
be proven in-process without building a .so. Factory path is exercised
end-to-end by the dummy plugin test that lands in Task 11.

Four new test cases: registration visibility, unload clears + signals,
database-claim routing, destructor unloads orphans.

EOF
)"
```

---

## Task 7: `PluginActionManager` + `SimpleActionContext` (TDD)

Goal: land the action-plugin twin of `BackendPluginManager`. Also land `SimpleActionContext`, a minimal concrete `IPluginAction::ActionContext` used by `runAction()` and by tests.

**Files:**

- Create: `src/runtime/simpleactioncontext.h`
- Create: `src/runtime/simpleactioncontext.cpp`
- Create: `src/runtime/pluginactionmanager.h`
- Create: `src/runtime/pluginactionmanager.cpp`
- Modify: `src/runtime/CMakeLists.txt` (append new sources)
- Create: `tests/runtime/tst_simpleactioncontext.cpp`
- Create: `tests/runtime/tst_pluginactionmanager.cpp`
- Modify: `tests/runtime/CMakeLists.txt` (append new tests)

- [ ] **Step 7.1: Create `src/runtime/simpleactioncontext.h`**

```cpp
#ifndef WILDPALMS_SIMPLEACTIONCONTEXT_H
#define WILDPALMS_SIMPLEACTIONCONTEXT_H

#include "core/ipluginaction.h"

namespace WildPalms {

/**
 * @brief Minimal concrete ActionContext: emits signals, stores totals,
 *        supports cancellation via a flag toggled from any thread.
 *
 * Suitable for tests and for CLI-style action runs. UI callers may
 * prefer a richer subclass that routes log() into a log widget.
 */
class SimpleActionContext : public IPluginAction::ActionContext
{
    Q_OBJECT
public:
    explicit SimpleActionContext(QObject *parent = nullptr);

    void setTotal(int total) override;
    void setCurrent(int current) override;
    void log(const QString &msg) override;
    bool isCancelled() const override;

    void cancel();
    int  total() const   { return m_total; }
    int  current() const { return m_current; }

private:
    int  m_total     = 0;
    int  m_current   = 0;
    bool m_cancelled = false;
};

} // namespace WildPalms

#endif // WILDPALMS_SIMPLEACTIONCONTEXT_H
```

- [ ] **Step 7.2: Create `src/runtime/simpleactioncontext.cpp`**

```cpp
#include "simpleactioncontext.h"

#include <QAtomicInt>

namespace WildPalms {

SimpleActionContext::SimpleActionContext(QObject *parent)
    : IPluginAction::ActionContext(parent)
{
}

void SimpleActionContext::setTotal(int total)
{
    m_total = total;
    emit progress(m_current, m_total);
}

void SimpleActionContext::setCurrent(int current)
{
    m_current = current;
    emit progress(m_current, m_total);
}

void SimpleActionContext::log(const QString &msg)
{
    emit message(msg);
}

bool SimpleActionContext::isCancelled() const { return m_cancelled; }

void SimpleActionContext::cancel() { m_cancelled = true; }

} // namespace WildPalms
```

- [ ] **Step 7.3: Create `src/runtime/pluginactionmanager.h`**

```cpp
#ifndef WILDPALMS_PLUGINACTIONMANAGER_H
#define WILDPALMS_PLUGINACTIONMANAGER_H

#include <KPluginMetaData>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

class PalmDeviceConnection;

namespace WildPalms {

class IPluginAction;

/**
 * @brief Discovers, loads, and exposes IPluginAction plugins.
 *
 * Sister manager to BackendPluginManager. Shares the
 * wildpalms/plugins/ discovery subdir; filters by
 * X-WildPalms-PluginType == "action".
 */
class PluginActionManager : public QObject
{
    Q_OBJECT
public:
    struct ActionInfo {
        KPluginMetaData  metaData;
        IPluginAction   *instance = nullptr;
    };

    explicit PluginActionManager(PalmDeviceConnection *device,
                                  QObject              *parent = nullptr);
    ~PluginActionManager() override;

    void discoverActions();
    bool loadAction(const QString &pluginId);
    void loadActions();
    void unloadAction(const QString &pluginId);

    QList<IPluginAction *> actions() const;
    IPluginAction         *action(const QString &pluginId) const;

    void setPluginSubdir(const QString &subdir);

Q_SIGNALS:
    void actionLoaded(IPluginAction *action);
    void actionUnloading(IPluginAction *action);

protected:
    bool registerInstanceForTest(const QString &pluginId, IPluginAction *instance);

private:
    QString m_subdir;
    QMap<QString, ActionInfo> m_actions;
    PalmDeviceConnection *m_device = nullptr;
};

} // namespace WildPalms

#endif // WILDPALMS_PLUGINACTIONMANAGER_H
```

- [ ] **Step 7.4: Create `src/runtime/pluginactionmanager.cpp`**

```cpp
#include "pluginactionmanager.h"

#include "core/ipluginaction.h"
#include "pluginmetadatahelpers.h"

#include <KPluginFactory>

#include <QDebug>

namespace WildPalms {

PluginActionManager::PluginActionManager(PalmDeviceConnection *device, QObject *parent)
    : QObject(parent)
    , m_subdir(QStringLiteral("wildpalms/plugins"))
    , m_device(device)
{
}

PluginActionManager::~PluginActionManager()
{
    const QStringList ids = m_actions.keys();
    for (const QString &id : ids) {
        if (m_actions[id].instance) unloadAction(id);
    }
}

void PluginActionManager::setPluginSubdir(const QString &subdir) { m_subdir = subdir; }

void PluginActionManager::discoverActions()
{
    const QList<KPluginMetaData> found = KPluginMetaData::findPlugins(m_subdir);
    for (const KPluginMetaData &md : found) {
        const QString type = Runtime::metaString(md, QStringLiteral("X-WildPalms-PluginType"));
        if (type != QStringLiteral("action")) continue;

        const QString id = md.pluginId();
        if (id.isEmpty()) continue;

        if (m_actions.contains(id)) {
            m_actions[id].metaData = md;
            continue;
        }
        ActionInfo info;
        info.metaData = md;
        m_actions.insert(id, info);
        qDebug() << "[PluginActionManager] Discovered action:" << id;
    }
}

bool PluginActionManager::loadAction(const QString &pluginId)
{
    auto it = m_actions.find(pluginId);
    if (it == m_actions.end()) return false;
    if (it->instance) return true;

    auto factoryResult = KPluginFactory::loadFactory(it->metaData);
    if (!factoryResult) {
        qWarning() << "[PluginActionManager] Factory load failed:" << pluginId
                   << factoryResult.errorString;
        return false;
    }
    QObject *obj = factoryResult.plugin->create<QObject>(this);
    if (!obj) return false;

    auto *action = dynamic_cast<IPluginAction *>(obj);
    if (!action) {
        delete obj;
        return false;
    }

    it->instance = action;
    emit actionLoaded(action);
    return true;
}

void PluginActionManager::loadActions()
{
    const QStringList ids = m_actions.keys();
    for (const QString &id : ids) {
        if (!m_actions[id].instance) loadAction(id);
    }
}

void PluginActionManager::unloadAction(const QString &pluginId)
{
    auto it = m_actions.find(pluginId);
    if (it == m_actions.end() || !it->instance) return;
    emit actionUnloading(it->instance);
    delete dynamic_cast<QObject *>(it->instance);
    it->instance = nullptr;
}

QList<IPluginAction *> PluginActionManager::actions() const
{
    QList<IPluginAction *> out;
    for (const ActionInfo &info : m_actions) {
        if (info.instance) out.append(info.instance);
    }
    return out;
}

IPluginAction *PluginActionManager::action(const QString &pluginId) const
{
    auto it = m_actions.constFind(pluginId);
    return (it != m_actions.constEnd()) ? it->instance : nullptr;
}

bool PluginActionManager::registerInstanceForTest(const QString &pluginId,
                                                    IPluginAction *instance)
{
    if (!instance) return false;
    auto it = m_actions.find(pluginId);
    if (it != m_actions.end() && it->instance) return false;
    if (it == m_actions.end()) {
        ActionInfo info;
        info.instance = instance;
        m_actions.insert(pluginId, info);
    } else {
        it->instance = instance;
    }
    if (auto *obj = dynamic_cast<QObject *>(instance)) obj->setParent(this);
    emit actionLoaded(instance);
    return true;
}

} // namespace WildPalms
```

- [ ] **Step 7.5: Append new sources to `src/runtime/CMakeLists.txt`**

```cmake
add_library(WildPalmsRuntime STATIC
    pluginmetadatahelpers.h
    pluginmetadatahelpers.cpp
    backendpluginmanager.h
    backendpluginmanager.cpp
    pluginactionmanager.h
    pluginactionmanager.cpp
    simpleactioncontext.h
    simpleactioncontext.cpp
)
```

- [ ] **Step 7.6: Create `tests/runtime/tst_simpleactioncontext.cpp`**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "runtime/simpleactioncontext.h"

using WildPalms::SimpleActionContext;

class TestSimpleActionContext : public QObject
{
    Q_OBJECT
private slots:
    void setTotalEmitsProgress();
    void setCurrentEmitsProgress();
    void logEmitsMessage();
    void cancelFlipsFlag();
};

void TestSimpleActionContext::setTotalEmitsProgress()
{
    SimpleActionContext ctx;
    QSignalSpy spy(&ctx, &SimpleActionContext::progress);
    ctx.setTotal(10);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 0);  // current
    QCOMPARE(spy.first().at(1).toInt(), 10); // total
    QCOMPARE(ctx.total(), 10);
}

void TestSimpleActionContext::setCurrentEmitsProgress()
{
    SimpleActionContext ctx;
    ctx.setTotal(10);
    QSignalSpy spy(&ctx, &SimpleActionContext::progress);
    ctx.setCurrent(3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 3);
    QCOMPARE(spy.first().at(1).toInt(), 10);
    QCOMPARE(ctx.current(), 3);
}

void TestSimpleActionContext::logEmitsMessage()
{
    SimpleActionContext ctx;
    QSignalSpy spy(&ctx, &SimpleActionContext::message);
    ctx.log(QStringLiteral("hello"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("hello"));
}

void TestSimpleActionContext::cancelFlipsFlag()
{
    SimpleActionContext ctx;
    QVERIFY(!ctx.isCancelled());
    ctx.cancel();
    QVERIFY(ctx.isCancelled());
}

QTEST_MAIN(TestSimpleActionContext)
#include "tst_simpleactioncontext.moc"
```

- [ ] **Step 7.7: Create `tests/runtime/tst_pluginactionmanager.cpp`**

```cpp
#include <QtTest/QtTest>
#include <QPointer>
#include <QSignalSpy>

#include "core/ipluginaction.h"
#include "runtime/pluginactionmanager.h"
#include "runtime/simpleactioncontext.h"

namespace {

class FakeActionPlugin : public QObject, public WildPalms::IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)
public:
    QString pluginId() const override    { return QStringLiteral("fake-action"); }
    QString displayName() const override { return QStringLiteral("Fake Action"); }
    QIcon   icon() const override        { return {}; }
    QString description() const override { return {}; }
    QString version() const override     { return QStringLiteral("1.0"); }

    bool execute(ActionContext *ctx, PalmDeviceConnection *,
                 const QJsonObject &) override
    {
        if (ctx) {
            ctx->setTotal(2);
            ctx->setCurrent(1);
            ctx->log(QStringLiteral("did work"));
            ctx->setCurrent(2);
        }
        return true;
    }

    Preconditions preconditions() const override
    {
        Preconditions p;
        p.requiresDeviceConnection = false;
        return p;
    }
};

class TestablePluginActionManager : public WildPalms::PluginActionManager
{
public:
    using PluginActionManager::PluginActionManager;
    using PluginActionManager::registerInstanceForTest;
};

} // namespace

class TestPluginActionManager : public QObject
{
    Q_OBJECT
private slots:
    void registerInjectedActionShowsUpInQueries();
    void unloadInjectedActionClearsInstance();
    void executeInjectedActionDrivesContext();
};

void TestPluginActionManager::registerInjectedActionShowsUpInQueries()
{
    TestablePluginActionManager mgr(nullptr);
    auto *fake = new FakeActionPlugin();
    QVERIFY(mgr.registerInstanceForTest(QStringLiteral("fake-action"), fake));
    QCOMPARE(mgr.actions().size(), 1);
    QCOMPARE(mgr.action(QStringLiteral("fake-action")), fake);
}

void TestPluginActionManager::unloadInjectedActionClearsInstance()
{
    TestablePluginActionManager mgr(nullptr);
    auto *fake = new FakeActionPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake-action"), fake);

    QSignalSpy spy(&mgr, &WildPalms::PluginActionManager::actionUnloading);
    mgr.unloadAction(QStringLiteral("fake-action"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mgr.actions().size(), 0);
}

void TestPluginActionManager::executeInjectedActionDrivesContext()
{
    TestablePluginActionManager mgr(nullptr);
    auto *fake = new FakeActionPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake-action"), fake);

    WildPalms::SimpleActionContext ctx;
    QSignalSpy progressSpy(&ctx, &WildPalms::SimpleActionContext::progress);
    QSignalSpy messageSpy(&ctx, &WildPalms::SimpleActionContext::message);

    QVERIFY(fake->execute(&ctx, nullptr, QJsonObject()));

    QCOMPARE(ctx.total(), 2);
    QCOMPARE(ctx.current(), 2);
    QCOMPARE(progressSpy.count(), 3);   // setTotal + setCurrent(1) + setCurrent(2)
    QCOMPARE(messageSpy.count(), 1);
}

QTEST_MAIN(TestPluginActionManager)
#include "tst_pluginactionmanager.moc"
```

- [ ] **Step 7.8: Register new tests in `tests/runtime/CMakeLists.txt`**

Append:

```cmake
add_wildpalms_runtime_test(tst_simpleactioncontext tst_simpleactioncontext.cpp)
add_wildpalms_runtime_test(tst_pluginactionmanager tst_pluginactionmanager.cpp)
```

- [ ] **Step 7.9: Build + run all runtime tests**

Run: `cd build-dev && cmake --build . -j 2>&1 | tail -10 && ctest -R '^tst_(simpleactioncontext|pluginactionmanager|pluginmetadatahelpers|backendpluginmanager)$' --output-on-failure`

Expected: all pass (9 + 5 + 4 + 3 = 21 cases).

- [ ] **Step 7.10: Commit**

```bash
git add src/runtime/simpleactioncontext.h src/runtime/simpleactioncontext.cpp \
        src/runtime/pluginactionmanager.h src/runtime/pluginactionmanager.cpp \
        src/runtime/CMakeLists.txt \
        tests/runtime/tst_simpleactioncontext.cpp tests/runtime/tst_pluginactionmanager.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): PluginActionManager + SimpleActionContext (Phase E.8)

PluginActionManager is the action-plugin twin of BackendPluginManager.
Shares the wildpalms/plugins/ discovery subdir; filters by
X-WildPalms-PluginType == "action". Same test-only registerInstanceForTest
seam as BackendPluginManager.

SimpleActionContext: minimal concrete IPluginAction::ActionContext that
stores totals, emits progress/message signals, and supports cancellation.

Three action-manager tests + four SimpleActionContext tests. All pass.

EOF
)"
```

---

## Task 8: Dummy **backend** plugin under `tests/plugins/dummy_backend/`

Goal: build a real `.so` backend plugin so the end-to-end factory load path can be exercised in Task 11. The plugin returns both a nullptr blob and a nullptr calendar backend from `createBackends()` — sufficient for proving the manager's factory path without wiring a real backend.

**Files:**

- Create: `tests/plugins/CMakeLists.txt`
- Create: `tests/plugins/dummy_backend/CMakeLists.txt`
- Create: `tests/plugins/dummy_backend/dummybackendplugin.h`
- Create: `tests/plugins/dummy_backend/dummybackendplugin.cpp`
- Create: `tests/plugins/dummy_backend/dummy-backend-plugin.json`
- Modify: `tests/CMakeLists.txt` (un-comment `add_subdirectory(plugins)`)

- [ ] **Step 8.1: Create `tests/plugins/CMakeLists.txt`**

```cmake
# Phase E.8 — dummy plugins for exercising the new-ABI managers' factory
# load path. Install namespace "wildpalms_test/plugins" avoids clashing
# with real plugins (wildpalms/conduits, wildpalms/plugins).

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

add_subdirectory(dummy_backend)
add_subdirectory(dummy_action)
```

- [ ] **Step 8.2: Create `tests/plugins/dummy_backend/dummy-backend-plugin.json`**

```json
{
    "KPlugin": {
        "Id": "dummy_backend",
        "Name": "Dummy Backend Plugin",
        "Description": "E.8 test fixture — does nothing useful.",
        "Icon": "application-x-executable",
        "Version": "1.0"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["DummyDB"],
    "X-WildPalms-ClaimDescriptions": {
        "DummyDB": "E.8 fixture claim."
    },
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 100
}
```

- [ ] **Step 8.3: Create `tests/plugins/dummy_backend/dummybackendplugin.h`**

```cpp
#ifndef WILDPALMS_DUMMYBACKENDPLUGIN_H
#define WILDPALMS_DUMMYBACKENDPLUGIN_H

#include "core/ibackendplugin.h"

#include <QObject>

class DummyBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit DummyBackendPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QIcon   icon() const override;
    QString description() const override;
    QString version() const override;

    QStringList claimedDatabases() const override;
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                     PalmDeviceConnection         *device) override;
};

#endif // WILDPALMS_DUMMYBACKENDPLUGIN_H
```

- [ ] **Step 8.4: Create `tests/plugins/dummy_backend/dummybackendplugin.cpp`**

```cpp
#include "dummybackendplugin.h"

#include <KPluginFactory>

#include <QIcon>

DummyBackendPlugin::DummyBackendPlugin(QObject *parent) : QObject(parent) {}

QString DummyBackendPlugin::pluginId() const    { return QStringLiteral("dummy_backend"); }
QString DummyBackendPlugin::displayName() const { return QStringLiteral("Dummy Backend Plugin"); }
QIcon   DummyBackendPlugin::icon() const        { return QIcon::fromTheme(QStringLiteral("application-x-executable")); }
QString DummyBackendPlugin::description() const { return QStringLiteral("E.8 test fixture."); }
QString DummyBackendPlugin::version() const     { return QStringLiteral("1.0"); }

QStringList DummyBackendPlugin::claimedDatabases() const
{
    return {QStringLiteral("DummyDB")};
}

WildPalms::IBackendPlugin::ProvidedBackends
DummyBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *, PalmDeviceConnection *)
{
    return {}; // no real backends; the factory-path test only needs the object to exist.
}

K_PLUGIN_FACTORY_WITH_JSON(DummyBackendPluginFactory,
                           "dummy-backend-plugin.json",
                           registerPlugin<DummyBackendPlugin>();)

#include "dummybackendplugin.moc"
```

- [ ] **Step 8.5: Create `tests/plugins/dummy_backend/CMakeLists.txt`**

```cmake
kcoreaddons_add_plugin(dummy_backend_plugin
    SOURCES
        dummybackendplugin.cpp
        dummybackendplugin.h
    INSTALL_NAMESPACE "wildpalms_test/plugins"
)

target_link_libraries(dummy_backend_plugin
    WildPalmsCore
    KF6::CoreAddons
    Qt::Core
)

target_include_directories(dummy_backend_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
```

- [ ] **Step 8.6: Un-comment `add_subdirectory(plugins)` in `tests/CMakeLists.txt`**

Change:

```cmake
# Phase E.8 — new-ABI plugin manager tests.
add_subdirectory(runtime)
# Dummy plugins land in Task 9 / 10.
# add_subdirectory(plugins)
```

to:

```cmake
# Phase E.8 — new-ABI plugin manager tests + dummy plugins.
add_subdirectory(runtime)
add_subdirectory(plugins)
```

- [ ] **Step 8.7: Configure + build the dummy plugin**

Run: `cd build-dev && cmake --build . --target dummy_backend_plugin 2>&1 | tail -10`

Expected: builds. Locate the output `.so`:

Run: `find build-dev -name 'dummy_backend_plugin*.so' -o -name 'dummy_backend_plugin*.dylib' 2>/dev/null`

Record the path printed; it is needed for `QCoreApplication::setLibraryPaths` in Task 11. Typical output: `build-dev/bin/wildpalms_test/plugins/dummy_backend_plugin.so`.

- [ ] **Step 8.8: Commit**

```bash
git add tests/plugins/CMakeLists.txt \
        tests/plugins/dummy_backend/CMakeLists.txt \
        tests/plugins/dummy_backend/dummybackendplugin.h \
        tests/plugins/dummy_backend/dummybackendplugin.cpp \
        tests/plugins/dummy_backend/dummy-backend-plugin.json \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(plugins): dummy backend plugin fixture (Phase E.8)

Real .so under tests/plugins/dummy_backend/ built via kcoreaddons_add_plugin
with INSTALL_NAMESPACE "wildpalms_test/plugins" so it does not collide
with real plugins.

Manifest declares X-WildPalms-PluginType="backend" and
X-WildPalms-PalmDatabases=["DummyDB"]. createBackends returns an empty
ProvidedBackends — sufficient for exercising the factory load path in
the end-to-end test (Task 11).

EOF
)"
```

---

## Task 9: Dummy **action** plugin under `tests/plugins/dummy_action/`

Goal: mirror of Task 8 for action plugins. Plugin's `execute()` drives a supplied `ActionContext` in a minimal way so the round-trip test can assert on observed side-effects.

**Files:**

- Create: `tests/plugins/dummy_action/CMakeLists.txt`
- Create: `tests/plugins/dummy_action/dummyactionplugin.h`
- Create: `tests/plugins/dummy_action/dummyactionplugin.cpp`
- Create: `tests/plugins/dummy_action/dummy-action-plugin.json`

- [ ] **Step 9.1: Create `tests/plugins/dummy_action/dummy-action-plugin.json`**

```json
{
    "KPlugin": {
        "Id": "dummy_action",
        "Name": "Dummy Action Plugin",
        "Description": "E.8 test fixture — triggerable no-op.",
        "Icon": "system-run",
        "Version": "1.0"
    },
    "X-WildPalms-PluginType": "action"
}
```

- [ ] **Step 9.2: Create `tests/plugins/dummy_action/dummyactionplugin.h`**

```cpp
#ifndef WILDPALMS_DUMMYACTIONPLUGIN_H
#define WILDPALMS_DUMMYACTIONPLUGIN_H

#include "core/ipluginaction.h"

#include <QObject>

class DummyActionPlugin : public QObject, public WildPalms::IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)
public:
    explicit DummyActionPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QIcon   icon() const override;
    QString description() const override;
    QString version() const override;

    bool execute(ActionContext       *ctx,
                 PalmDeviceConnection *device,
                 const QJsonObject   &parameters) override;
    Preconditions preconditions() const override;
};

#endif // WILDPALMS_DUMMYACTIONPLUGIN_H
```

- [ ] **Step 9.3: Create `tests/plugins/dummy_action/dummyactionplugin.cpp`**

```cpp
#include "dummyactionplugin.h"

#include <KPluginFactory>

#include <QIcon>

DummyActionPlugin::DummyActionPlugin(QObject *parent) : QObject(parent) {}

QString DummyActionPlugin::pluginId() const    { return QStringLiteral("dummy_action"); }
QString DummyActionPlugin::displayName() const { return QStringLiteral("Dummy Action Plugin"); }
QIcon   DummyActionPlugin::icon() const        { return QIcon::fromTheme(QStringLiteral("system-run")); }
QString DummyActionPlugin::description() const { return QStringLiteral("E.8 test fixture."); }
QString DummyActionPlugin::version() const     { return QStringLiteral("1.0"); }

bool DummyActionPlugin::execute(ActionContext       *ctx,
                                PalmDeviceConnection *,
                                const QJsonObject   &)
{
    if (ctx) {
        ctx->setTotal(1);
        ctx->log(QStringLiteral("dummy_action running"));
        ctx->setCurrent(1);
    }
    return true;
}

WildPalms::IPluginAction::Preconditions DummyActionPlugin::preconditions() const
{
    Preconditions p;
    p.requiresDeviceConnection = false;
    return p;
}

K_PLUGIN_FACTORY_WITH_JSON(DummyActionPluginFactory,
                           "dummy-action-plugin.json",
                           registerPlugin<DummyActionPlugin>();)

#include "dummyactionplugin.moc"
```

- [ ] **Step 9.4: Create `tests/plugins/dummy_action/CMakeLists.txt`**

```cmake
kcoreaddons_add_plugin(dummy_action_plugin
    SOURCES
        dummyactionplugin.cpp
        dummyactionplugin.h
    INSTALL_NAMESPACE "wildpalms_test/plugins"
)

target_link_libraries(dummy_action_plugin
    WildPalmsCore
    KF6::CoreAddons
    Qt::Core
)

target_include_directories(dummy_action_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
```

- [ ] **Step 9.5: Build the dummy action plugin**

Run: `cd build-dev && cmake --build . --target dummy_action_plugin 2>&1 | tail -10 && find build-dev -name 'dummy_action_plugin*.so' 2>/dev/null`

Expected: builds; the .so is placed beside the dummy_backend .so in the same `wildpalms_test/plugins` subdir.

- [ ] **Step 9.6: Commit**

```bash
git add tests/plugins/dummy_action/CMakeLists.txt \
        tests/plugins/dummy_action/dummyactionplugin.h \
        tests/plugins/dummy_action/dummyactionplugin.cpp \
        tests/plugins/dummy_action/dummy-action-plugin.json
git commit -m "$(cat <<'EOF'
test(plugins): dummy action plugin fixture (Phase E.8)

Mirror of dummy_backend_plugin for IPluginAction. Manifest declares
X-WildPalms-PluginType="action". execute() drives the supplied
ActionContext with a one-step progress + log sequence so the
round-trip test can assert on observed side-effects.

EOF
)"
```

---

## Task 10: End-to-end round-trip test — real `.so` load/unload through both managers

Goal: prove the factory path. Point both managers at the `wildpalms_test/plugins` subdir (via `QCoreApplication::setLibraryPaths`), call discover + load, assert each plugin instantiated with expected identity, then unload and assert deletion. This test is the explicit exit gate for E.8 ("manager loads/unloads dummy plugin").

**Files:**

- Create: `tests/runtime/tst_plugin_factory_roundtrip.cpp`
- Modify: `tests/runtime/CMakeLists.txt` (append new test + depend on both dummy .so targets)

- [ ] **Step 10.1: Create the test**

```cpp
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QSignalSpy>

#include "core/ibackendplugin.h"
#include "core/ipluginaction.h"
#include "runtime/backendpluginmanager.h"
#include "runtime/pluginactionmanager.h"
#include "runtime/simpleactioncontext.h"

/// Phase E.8 round-trip: real .so plugins built under tests/plugins/ get
/// discovered, loaded, queried, and unloaded through the new-ABI managers.

class TestPluginFactoryRoundtrip : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void backendManagerLoadsAndUnloadsDummy();
    void actionManagerLoadsAndExecutesDummy();

private:
    QString m_libraryPathAdded;
};

void TestPluginFactoryRoundtrip::initTestCase()
{
    // The dummy plugins install under ${CMAKE_BINARY_DIR}/bin/wildpalms_test/plugins/.
    // Qt looks up plugins relative to library paths. Adding the bin/ dir
    // makes KPluginMetaData::findPlugins("wildpalms_test/plugins") work.
    const QString binDir = QCoreApplication::applicationDirPath();
    // Walk up until we find a directory containing "wildpalms_test/plugins".
    QDir d(binDir);
    QString candidate;
    for (int i = 0; i < 6; ++i) {
        if (d.exists(QStringLiteral("wildpalms_test/plugins"))) {
            candidate = d.absolutePath();
            break;
        }
        if (d.exists(QStringLiteral("bin/wildpalms_test/plugins"))) {
            candidate = d.absoluteFilePath(QStringLiteral("bin"));
            break;
        }
        if (!d.cdUp()) break;
    }
    QVERIFY2(!candidate.isEmpty(),
             "Could not locate wildpalms_test/plugins under the build tree. "
             "Run cmake --build . --target dummy_backend_plugin dummy_action_plugin first.");

    m_libraryPathAdded = candidate;
    QCoreApplication::addLibraryPath(m_libraryPathAdded);
}

void TestPluginFactoryRoundtrip::backendManagerLoadsAndUnloadsDummy()
{
    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms_test/plugins"));
    mgr.discoverPlugins();

    const auto cat = mgr.catalogue();
    QVERIFY2(std::any_of(cat.begin(), cat.end(),
                         [](const auto &info){
                             return info.metaData.pluginId() == QStringLiteral("dummy_backend");
                         }),
             "dummy_backend not found in catalogue");

    QSignalSpy loadSpy(&mgr, &WildPalms::BackendPluginManager::pluginLoaded);
    QVERIFY(mgr.loadPlugin(QStringLiteral("dummy_backend")));
    QCOMPARE(loadSpy.count(), 1);

    WildPalms::IBackendPlugin *plug = mgr.plugin(QStringLiteral("dummy_backend"));
    QVERIFY(plug != nullptr);
    QCOMPARE(plug->pluginId(), QStringLiteral("dummy_backend"));
    QCOMPARE(plug->claimedDatabases(), (QStringList{QStringLiteral("DummyDB")}));
    QCOMPARE(mgr.pluginForDatabase(QStringLiteral("DummyDB")), plug);

    QSignalSpy unloadSpy(&mgr, &WildPalms::BackendPluginManager::pluginUnloading);
    mgr.unloadPlugin(QStringLiteral("dummy_backend"));
    QCOMPARE(unloadSpy.count(), 1);
    QCOMPARE(mgr.plugin(QStringLiteral("dummy_backend")), nullptr);
}

void TestPluginFactoryRoundtrip::actionManagerLoadsAndExecutesDummy()
{
    WildPalms::PluginActionManager mgr(nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms_test/plugins"));
    mgr.discoverActions();

    QVERIFY(mgr.loadAction(QStringLiteral("dummy_action")));

    WildPalms::IPluginAction *action = mgr.action(QStringLiteral("dummy_action"));
    QVERIFY(action != nullptr);
    QCOMPARE(action->pluginId(), QStringLiteral("dummy_action"));
    QVERIFY(!action->preconditions().requiresDeviceConnection);

    WildPalms::SimpleActionContext ctx;
    QSignalSpy messageSpy(&ctx, &WildPalms::SimpleActionContext::message);
    QVERIFY(action->execute(&ctx, nullptr, QJsonObject()));
    QCOMPARE(ctx.total(), 1);
    QCOMPARE(ctx.current(), 1);
    QCOMPARE(messageSpy.count(), 1);

    mgr.unloadAction(QStringLiteral("dummy_action"));
    QCOMPARE(mgr.action(QStringLiteral("dummy_action")), nullptr);
}

QTEST_MAIN(TestPluginFactoryRoundtrip)
#include "tst_plugin_factory_roundtrip.moc"
```

- [ ] **Step 10.2: Register the test in `tests/runtime/CMakeLists.txt`**

Append:

```cmake
add_wildpalms_runtime_test(tst_plugin_factory_roundtrip tst_plugin_factory_roundtrip.cpp)
# Force the dummy .so files to be built before running this test.
add_dependencies(tst_plugin_factory_roundtrip
    dummy_backend_plugin
    dummy_action_plugin
)
```

- [ ] **Step 10.3: Build + run the round-trip test**

Run: `cd build-dev && cmake --build . --target tst_plugin_factory_roundtrip 2>&1 | tail -10 && ctest -R '^tst_plugin_factory_roundtrip$' --output-on-failure`

Expected: 2/2 cases pass — both managers locate their dummy .so, instantiate it via `KPluginFactory`, query basic identity, and unload cleanly.

- [ ] **Step 10.4: Run the **entire** test suite and confirm no regressions**

Run: `cd build-dev && ctest --output-on-failure 2>&1 | tail -30`

Expected: no previously-green test goes red. New runtime tests contribute ~23 new green cases.

- [ ] **Step 10.5: Commit**

```bash
git add tests/runtime/tst_plugin_factory_roundtrip.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(runtime): end-to-end plugin factory round-trip (Phase E.8)

Real-.so discovery + load + query + unload through BackendPluginManager
and PluginActionManager. Uses QCoreApplication::addLibraryPath to point
KPluginMetaData::findPlugins at tests/plugins/'s build output, then
asserts identity, database claim routing, and execute()-driven context
side effects for the dummy action.

This is the explicit E.8 exit gate per spec line 586
("manager loads/unloads dummy plugin").

EOF
)"
```

---

## Task 11: Flip spec row E.8, update integration plan, update memory

Goal: finish the paperwork. Flip the E.8 row in the design doc, append a one-liner to the integration plan's Phase E table, and drop a fresh memory file noting the pinned "fullsync → runtime relocation deferred to E.15" decision so future conversations remember it.

**Files:**

- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (flip row E.8)
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md` (Phase E sub-phases table)
- Create: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_plugin_abi_e8.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`

- [ ] **Step 11.1: Flip the spec's E.8 row to landed**

Open `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. Locate row E.8 (currently begins `| **E.8** | New plugin ABI interfaces...`). Replace it with:

```markdown
| ✅ **E.8** | New plugin ABI interfaces at `src/core/` (`iplugin.h`, `ibackendplugin.h`, `ipluginaction.h`). `BackendPluginManager` + `PluginActionManager` at `src/runtime/` in new static lib `WildPalmsRuntime`. Shared metadata readers (`metaString`/`metaBool`/`metaInt`/`metaStringList`) factored out of `ConduitManager` for reuse. `SimpleActionContext`: concrete `IPluginAction::ActionContext`. Dummy backend + action plugins at `tests/plugins/`. `src/fullsync/` relocation to `src/runtime/` **deferred to E.15** per E.8 plan decision (not correctness-blocking). Landed 2026-04-23. Plan: `docs/superpowers/plans/2026-04-23-phase-e8-plugin-abi.md`. | WP | E.6 | WP ctest passes; `tst_plugin_factory_roundtrip` exercises both managers against real `.so` fixtures under `tests/plugins/`. |
```

- [ ] **Step 11.2: Note E.8 in the integration plan's Phase E table**

Open `docs/plans/2026-04-20-libkalburator-integration.md`. Find the line (~line 26) for the Phase E row and append `; E.8 landed 2026-04-23` to the status cell — matching the pattern commit `4ae8099` used for E.7.

- [ ] **Step 11.3: Create memory file `project_plugin_abi_e8.md`**

```markdown
---
name: Phase E.8 plugin ABI decisions
description: New WP plugin ABI (IPlugin/IBackendPlugin/IPluginAction + BackendPluginManager/PluginActionManager) landed 2026-04-23; coexists with old IConduit surface until E.16. Pinned deferral of fullsync → runtime relocation to E.15.
type: project
---

Phase E.8 landed 2026-04-23. New plugin ABI is live:
- `src/core/iplugin.h`, `ibackendplugin.h`, `ipluginaction.h`.
- `src/runtime/` = new static lib `WildPalmsRuntime` holding
  `BackendPluginManager`, `PluginActionManager`, `SimpleActionContext`,
  and `pluginmetadatahelpers` (factored out of `ConduitManager`).
- Dummy plugins at `tests/plugins/dummy_{backend,action}/` with
  INSTALL_NAMESPACE `wildpalms_test/plugins`.

**Why (non-obvious):** E.8 does NOT relocate `src/fullsync/` → `src/runtime/`,
even though the Phase-E spec's §Directory layout and E.16 row say
"relocated in E.8". E.8's exit gate (spec line 586) only requires the
new managers + dummy load/unload. Relocating `fullsync/` is mechanical
source movement that (a) has no E.8 correctness benefit and (b) fits
more naturally in E.15 (unified-runtime sub-phase) since those files
are unified-runtime code. Pinned in E.8 plan header.

**How to apply:** Through E.15, `src/runtime/` holds ONLY the new plugin
managers. `src/fullsync/` contents stay where they are. When a future
sub-phase needs to add runtime code that logically sits with
`SyncHost_WP` / `CalendarCollection_WP`, put it in `src/fullsync/` for
now; E.15 moves it all together.

**Coexistence rule:** `IConduit` + `ConduitManager` + `wildpalms/conduits/`
stay alive until E.16. New plugins (E.9+) land under
`wildpalms/plugins/`. Both subdirs scanned independently. A single .so
cannot be both an IConduit and an IBackendPlugin.
```

- [ ] **Step 11.4: Register the memory in `MEMORY.md`**

Append to the memory index at `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`:

```markdown
- [project_plugin_abi_e8.md](project_plugin_abi_e8.md) — E.8 landed 2026-04-23; new plugin ABI + managers live alongside old IConduit until E.16; fullsync→runtime move deferred to E.15
```

- [ ] **Step 11.5: Commit the docs + memory update**

```bash
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md \
        docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "$(cat <<'EOF'
docs(phase-e): flip E.8 to landed; note fullsync-relocation deferral

E.8 delivered the new plugin ABI (IPlugin/IBackendPlugin/IPluginAction)
plus BackendPluginManager + PluginActionManager under the new
WildPalmsRuntime static lib. Integration-plan table updated to match.

The spec's "src/fullsync/ relocated in E.8" aside is re-scoped to E.15
per the E.8 plan; rationale captured inline in the row.

EOF
)"
```

(Memory files are not tracked by the project repo; they sit under `~/.claude/`. Write them with the Write tool; no git commit is needed there.)

- [ ] **Step 11.6: Final verification — full ctest still green**

Run: `cd build-dev && ctest --output-on-failure 2>&1 | tail -5`

Expected: all tests pass.

---

## Completion checklist

- [ ] Three new interface headers installed in `include/wildpalms/core`.
- [ ] `WildPalmsRuntime` static lib with two managers, one concrete `ActionContext`, one metadata-helper TU.
- [ ] Four runtime unit-test binaries (`tst_pluginmetadatahelpers`, `tst_backendpluginmanager`, `tst_simpleactioncontext`, `tst_pluginactionmanager`) + one end-to-end test (`tst_plugin_factory_roundtrip`).
- [ ] Two dummy plugins under `tests/plugins/` building as real `.so` files.
- [ ] `ctest` green; no regressions in the pre-existing 50+ test baseline.
- [ ] Spec row E.8 flipped to `✅`; integration plan E row updated; memory note recorded.
- [ ] Commits landed in logical order (one per task, ~11 commits total).

On completion, hand off to `superpowers:finishing-a-development-branch`.
