# Phase E.9 — Memo Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the Memo conduit as the first new-ABI `IBackendPlugin`, proving the new plugin surface end-to-end against a `MockPalmDatabaseAccess` + `LocalBlobBackend` round-trip via `BlobSyncEngine::twoWayWithBaseline`.

**Architecture:** `MemoBlobBackend` wraps the shared `PalmBackend` (landed E.3), transcoding Palm wire bytes ↔ Markdown via `MemoCodec` (landed E.7) + a new `MemoMarkdown` encoder. A concrete `PalmDeviceConnection` (new in E.9) delivers the shared `PalmBackend` to plugins. `IBackendPlugin` grows optional view + conflict-presentation hooks. Old `MemoConduit` stays alive behind a CMake toggle until E.16.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), KF6::CoreAddons (`KPluginMetaData`, `KPluginFactory`, `kcoreaddons_add_plugin`), `Kalburator::Sync` (`IBlobBackend`, `BlobSyncEngine::twoWayWithBaseline`, `LocalBlobBackend`, `QSyncCore::RecordSnapshot`, `BlobBaselineStore`, `ConflictHandlerRegistry`, `ConflictStore`, `ConflictPolicy`), pisock (via `MemoCodec`). No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-04-23-phase-e9-memo-plugin-design.md`. Decisions #1-#9 are authoritative for this plan.

**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.9 (line 587).

**Repo:** All work in `~/dev/WildPalms/`. Build directory: `build-dev/` (preset project). No upstream libkalburator changes.

**Scope explicitly excluded:**

- **Deleting `MemoConduit`, `memomapper.{h,cpp}`, `memo-conduit.json`, `ConduitManager`, `SyncConduitBase`, `iconduit.h`, `isyncconduit.h`, `itoolconduit.h`.** All are retired in E.16.
- **`ConflictDialog` new-plugin lookup.** Interactive memo conflicts before the follow-up remain a known regression (per spec Risk R6). Policy-driven conflict resolution via `PalmConflictHandler` works fine.
- **Memo-specific `ConflictHandler`.** Fallback to `PalmConflictHandler` registered under `"palm"` is sufficient; the default handler's archive/secret/category overlays cover memo correctly.
- **`CategoryMappingStore` rename/move to `src/palm/`.** Stays at `src/palm/calendar/`; memo uses it keyed on `"MemoDB"`. Rename deferred until E.11/E.12 when a third consumer lands.
- **`SyncCoordinator`-level end-to-end.** `tst_memo_v2` drives `BlobSyncEngine::twoWayWithBaseline` directly. Coordinator coverage lands in E.18 alongside POSE64 integration.
- **AppInfo-block parsing for memo categories.** `CategoryMappingStore` stays empty in the E.9 end-to-end test; the encoder's null-store fallback path is what's exercised.
- **Migrating existing `~/.wildpalms/.../memo/*.md` files.** The new decoder tolerates old files (ignores unknown keys); the first sync rewrites them into the canonical form.
- **Flipping the CMake toggle default.** `WILDPALMS_MEMO_PLUGIN_V2` ships `ON`; the `OFF` path keeps the legacy conduit building. Both paths are exercised by the test matrix in Task 8.

---

## File Structure

**Files to CREATE:**

Memo plugin library (static lib shared between plugin `.so` and tests):

- `src/plugins/memo/memomarkdown.h` — POD `MarkdownMemo` ↔ Markdown with YAML frontmatter; filename derivation.
- `src/plugins/memo/memomarkdown.cpp`
- `src/plugins/memo/memoblobbackend.h` — transcoding `IBlobBackend` wrapping `PalmBackend`'s `palm:memo` collection.
- `src/plugins/memo/memoblobbackend.cpp`
- `src/plugins/memo/memobackendplugin.h` — `IBackendPlugin` subclass holding metadata + `createBackends` + view/conflict hooks.
- `src/plugins/memo/memobackendplugin.cpp` — class implementation + `K_PLUGIN_FACTORY_WITH_JSON`.
- `src/plugins/memo/memo-backend-plugin.json` — new manifest (`X-WildPalms-PluginType: "backend"`).

New device-layer aggregator:

- `src/palm/palmdeviceconnection.h` — concrete type (was forward-declared in E.8).
- `src/palm/palmdeviceconnection.cpp`

Core ABI source (new because `formatConflictRecordHtml` default needs out-of-line definition):

- `src/core/ibackendplugin.cpp` — default for `formatConflictRecordHtml`.

Tests:

- `tests/plugins/memo/CMakeLists.txt`
- `tests/plugins/memo/tst_memomarkdown.cpp`
- `tests/plugins/memo/tst_memoblobbackend.cpp`
- `tests/plugins/memo/tst_memobackendplugin.cpp`
- `tests/plugins/memo/tst_memo_v2.cpp` — end-to-end via `BackendPluginManager` + `BlobSyncEngine`.
- `tests/palm/tst_palmdeviceconnection.cpp`
- `tests/core/tst_ibackendplugin_defaults.cpp`

**Files to MODIFY:**

- `src/core/ibackendplugin.h` — add view + conflict hooks (virtuals with defaults), forward-declare `Kalburator::Sync::QSyncCore::RecordSnapshot`.
- `src/palm/sync/palmbackend.h` / `palmbackend.cpp` — add category-aware `updatePalmRecord(dbName, PalmRecord)` helper.
- `src/palm/CMakeLists.txt` — add `add_subdirectory(<none>)` ... actually add `palmdeviceconnection.{h,cpp}` to a new or existing target (see Task 3).
- `src/plugins/memo/CMakeLists.txt` — add `WILDPALMS_MEMO_PLUGIN_V2` option; build the new static lib + plugin when on; keep the legacy conduit when off.
- `src/kf6/CMakeLists.txt` (or wherever `WildPalmsCore` is assembled) — compile the new `src/core/ibackendplugin.cpp` into `WildPalmsCore`.
- `src/kf6/kf6mainwindow.cpp` — add a `BackendPluginManager` view-loading loop parallel to the existing `ConduitManager` view loop at lines 537-545.
- `src/kf6/kf6mainwindow.h` — add a `BackendPluginManager *m_backendPluginManager` member.
- `tests/CMakeLists.txt` — add `add_subdirectory(plugins/memo)`, `add_subdirectory(palm)` if not already present, `add_subdirectory(core)` if not already present.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` — flip row E.9 to `✅ **E.9**` at end.
- `docs/plans/2026-04-20-libkalburator-integration.md` — mark E.9 landed in the Phase E sub-phases table.
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + a new `project_phase_e9_memo.md` — record that memo is the first ported plugin; note the CMake toggle and deferrals (ConflictDialog wiring, CategoryMappingStore move).

**Files to LEAVE UNTOUCHED:**

- `src/plugins/memo/memoconduit.{h,cpp}`, `src/plugins/memo/memomapper.{h,cpp}`, `src/plugins/memo/memo-conduit.json` — retained until E.16.
- `src/plugins/memo/memoview.{h,cpp}` — shared between old and new plugin paths; no changes required.
- `tests/test_memomapper.cpp` — keeps testing the old mapper; still green.
- `src/core/iplugin.h`, `src/core/ipluginaction.h` — base interfaces unchanged.
- `src/kf6/conduitmanager.{h,cpp}` — stays alive until E.16.
- `pilot-link/`, `pilot-link-git/` — `PROJECT_VISION.md:105`: read-only.

---

## Task 1: Extend `IBackendPlugin` with view + conflict hooks

Goal: add six virtual methods (four view + two conflict) to `src/core/ibackendplugin.h` with sensible no-op defaults so the E.8 dummy-backend fixture keeps compiling and memo can override them in Task 6.

**Files:**

- Modify: `src/core/ibackendplugin.h`
- Create: `src/core/ibackendplugin.cpp`
- Modify: `src/kf6/CMakeLists.txt` (or the `CMakeLists.txt` that assembles `WildPalmsCore` — find it via `grep -rn "WildPalmsCore" src/ --include=CMakeLists.txt`)
- Create: `tests/core/CMakeLists.txt` (if not present)
- Create: `tests/core/tst_ibackendplugin_defaults.cpp`
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(core)` if not present)

- [ ] **Step 1.1: Inspect current `src/core/ibackendplugin.h` and find `WildPalmsCore` assembly**

Run:
```bash
cat /home/clinton/dev/WildPalms/src/core/ibackendplugin.h
grep -rn "WildPalmsCore" /home/clinton/dev/WildPalms/src/ --include=CMakeLists.txt
```

Note the CMakeLists.txt path that adds sources to `WildPalmsCore`; you'll need it in step 1.4.

- [ ] **Step 1.2: Write the failing test**

Create `tests/core/tst_ibackendplugin_defaults.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QIcon>
#include <QWidget>
#include "core/ibackendplugin.h"
#include "kalburator/sync/qsynccore/recordsnapshot.h"

// Minimal concrete IBackendPlugin that overrides only the required pure
// virtuals. Every optional hook falls through to the default
// implementation, which is the surface this test pins down.
class TrivialBackendPlugin : public WildPalms::IBackendPlugin {
public:
    QString pluginId()    const override { return QStringLiteral("trivial"); }
    QString displayName() const override { return QStringLiteral("Trivial"); }
    QIcon   icon()        const override { return {}; }
    QString description() const override { return {}; }
    QString version()     const override { return QStringLiteral("0.0"); }
    QStringList claimedDatabases() const override { return {}; }
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                    PalmDeviceConnection *) override { return {}; }
};

class TestIBackendPluginDefaults : public QObject {
    Q_OBJECT
private slots:
    void viewHooksDefaultToNoView();
    void conflictHooksDefaultToNoop();
};

void TestIBackendPluginDefaults::viewHooksDefaultToNoView()
{
    TrivialBackendPlugin p;
    QCOMPARE(p.hasMainView(), false);
    QWidget parent;
    QCOMPARE(p.createMainView(&parent), static_cast<QWidget *>(nullptr));
    QVERIFY(p.mainViewName().isEmpty());
    QVERIFY(p.mainViewIcon().isNull());
}

void TestIBackendPluginDefaults::conflictHooksDefaultToNoop()
{
    TrivialBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    snap.content = "hello";
    p.enrichConflictSnapshot(snap, true);        // must not crash
    QCOMPARE(snap.content, QByteArray("hello")); // default mutates nothing

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<pre>")));
    QVERIFY(html.contains(QStringLiteral("hello")));
}

QTEST_MAIN(TestIBackendPluginDefaults)
#include "tst_ibackendplugin_defaults.moc"
```

- [ ] **Step 1.3: Create the test CMakeLists**

Create `tests/core/CMakeLists.txt`:

```cmake
add_executable(tst_ibackendplugin_defaults tst_ibackendplugin_defaults.cpp)
target_link_libraries(tst_ibackendplugin_defaults
    PRIVATE
        WildPalmsCore
        Qt::Test
        Kalburator::Sync
)
add_test(NAME tst_ibackendplugin_defaults COMMAND tst_ibackendplugin_defaults)
```

And append to `tests/CMakeLists.txt`:

```cmake
add_subdirectory(core)
```

- [ ] **Step 1.4: Extend `src/core/ibackendplugin.h`**

Replace the current header with:

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
        struct RecordSnapshot;
    }
}

class PalmDeviceConnection; // concrete type lands in Phase E.9 as src/palm/palmdeviceconnection.h

namespace WildPalms {

class IBackendPlugin : public IPlugin
{
public:
    // ========== Database claims ==========
    virtual QStringList claimedDatabases() const = 0;

    // ========== Backend construction ==========
    struct ProvidedBackends {
        Kalburator::Sync::IBlobBackend *blob     = nullptr; // required
        Kalburator::Sync::SyncBackend  *calendar = nullptr; // optional
    };

    virtual ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                            PalmDeviceConnection         *device) = 0;

    // ========== Optional conflict handler ==========
    virtual Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler()
    {
        return nullptr;
    }

    // ========== Ordering hints ==========
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter() const  { return {}; }

    // ========== Main view surface (Phase E.9) ==========
    //
    // Returns a dockable main-window widget (e.g. MemoView, CalendarView).
    // Default: no view. When `hasMainView()` returns true, the main window
    // adds a KPageWidgetItem created from `createMainView(parent)` with
    // title `mainViewName()` and icon `mainViewIcon()`.
    virtual bool     hasMainView() const { return false; }
    virtual QWidget *createMainView(QWidget *parent)
    {
        Q_UNUSED(parent)
        return nullptr;
    }
    virtual QString mainViewName() const { return {}; }
    virtual QIcon   mainViewIcon() const { return {}; }

    // ========== Conflict presentation (Phase E.9) ==========
    //
    // enrichConflictSnapshot: mutate `snapshot` in place so the
    // downstream ConflictDialog has content/metadata/contentType set
    // to plugin-friendly values. `isSourceSide` is true when the
    // snapshot holds this plugin's own wire bytes (Palm), false when
    // it carries target-backend bytes (already in the plugin's
    // canonical form, e.g. Markdown).
    //
    // formatConflictRecordHtml: produce HTML for ConflictDialog's
    // detail pane. Default implementation UTF-8-decodes
    // `snapshot.content` into a `<pre>` block.
    virtual void enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const
    {
        Q_UNUSED(snapshot)
        Q_UNUSED(isSourceSide)
    }
    virtual QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const;
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IBackendPlugin,
                    "ca.vibekoder.WildPalms.IBackendPlugin/1.0")

#endif // WILDPALMS_IBACKENDPLUGIN_H
```

- [ ] **Step 1.5: Create `src/core/ibackendplugin.cpp`**

```cpp
#include "ibackendplugin.h"

#include "kalburator/sync/qsynccore/recordsnapshot.h"

#include <QString>

namespace WildPalms {

QString IBackendPlugin::formatConflictRecordHtml(
    const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const
{
    const QString body = QString::fromUtf8(snapshot.content).toHtmlEscaped();
    return QStringLiteral("<pre>%1</pre>").arg(body);
}

} // namespace WildPalms
```

- [ ] **Step 1.6: Wire `ibackendplugin.cpp` into `WildPalmsCore`**

In the CMakeLists.txt identified in step 1.1, add `core/ibackendplugin.cpp` to the source list alongside the existing `core/ipluginaction.cpp`. Example (adapt to actual location):

```cmake
add_library(WildPalmsCore STATIC
    # ... existing sources ...
    core/ipluginaction.cpp
    core/ibackendplugin.cpp      # <-- add this
)

target_link_libraries(WildPalmsCore PUBLIC Kalburator::Sync)  # if not already
```

`Kalburator::Sync` must be a public link dep of `WildPalmsCore` only if it wasn't already; `ibackendplugin.cpp` includes `recordsnapshot.h`. If the existing setup keeps Kalburator as private and you'd rather not flip it, inline the snapshot include into a small TU-local forward declaration — but the spec's decision was to let `ibackendplugin.cpp` have a direct dep.

Verify with: `grep -n "Kalburator" <path-to-WildPalmsCore-cmake>`. If `PUBLIC Kalburator::Sync` is already there, no change needed.

- [ ] **Step 1.7: Configure + build**

Run:
```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target WildPalmsCore tst_ibackendplugin_defaults
```

If the build fails because `WildPalmsCore` can't see `recordsnapshot.h`, add `Kalburator::Sync` to its `target_link_libraries(... PUBLIC ...)`.

Expected: both targets build clean.

- [ ] **Step 1.8: Run test — expect PASS**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_ibackendplugin_defaults --output-on-failure
```

Expected: PASS. (This test verifies defaults; no TDD red phase since the defaults exist the moment the header change compiles.)

- [ ] **Step 1.9: Verify E.8 dummy fixture still compiles**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_plugin_factory_roundtrip
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_plugin_factory_roundtrip --output-on-failure
```

Expected: PASS. The dummy fixture does not override any of the new methods; it inherits the defaults.

- [ ] **Step 1.10: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/core/ibackendplugin.h src/core/ibackendplugin.cpp \
        tests/core/CMakeLists.txt tests/core/tst_ibackendplugin_defaults.cpp \
        tests/CMakeLists.txt <path-to-WildPalmsCore-cmake>
git commit -m "$(cat <<'EOF'
feat(core): IBackendPlugin view + conflict hooks (Phase E.9)

Adds six virtuals with no-op defaults — hasMainView/createMainView/
mainViewName/mainViewIcon plus enrichConflictSnapshot/
formatConflictRecordHtml — so plugins can surface main-window tabs
and ConflictDialog HTML. The formatConflictRecordHtml default lives
in a new ibackendplugin.cpp; all other defaults are inline. Dummy
E.8 fixture inherits unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Add `PalmBackend::updatePalmRecord(dbName, PalmRecord)` category-aware helper

Goal: give the memo plugin a write path that preserves the Palm category slot. Current `PalmBackend::updateRecord(BackendRecord)` drops category (the BackendRecord has no properties map).

**Files:**

- Modify: `src/palm/sync/palmbackend.h`
- Modify: `src/palm/sync/palmbackend.cpp`
- Modify: `tests/palmsync/tst_palmbackend_roundtrip.cpp` (append one test case)

- [ ] **Step 2.1: Write the failing test**

Open `tests/palmsync/tst_palmbackend_roundtrip.cpp`. Find the class definition and add a new private slot:

```cpp
    void updatePalmRecordPreservesCategory();
```

And add the implementation at the end of the file (before `QTEST_MAIN` / `.moc`):

```cpp
void TestPalmBackendRoundtrip::updatePalmRecordPreservesCategory()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");

    WildPalms::PalmSync::PalmRecord pr;
    pr.category = 3;
    pr.data = QByteArrayLiteral("hello");
    pr.lastModified = QDateTime::currentDateTimeUtc();
    const std::uint32_t id = dev.createRecord("MemoDB", pr);
    QVERIFY(id != 0);

    WildPalms::PalmSync::PalmBackend backend(&dev);

    WildPalms::PalmSync::PalmRecord updated;
    updated.recordId = id;
    updated.category = 7;  // <-- move to a different slot
    updated.data = QByteArrayLiteral("hello world");
    updated.lastModified = QDateTime::currentDateTimeUtc();
    QVERIFY(backend.updatePalmRecord("MemoDB", updated));

    const auto stored = dev.readRecord("MemoDB", id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->category, 7);
    QCOMPARE(stored->data, QByteArrayLiteral("hello world"));
}
```

- [ ] **Step 2.2: Run test — expect FAIL**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmbackend_roundtrip 2>&1 | tail -20
```

Expected: compile error — `PalmBackend::updatePalmRecord` does not exist.

- [ ] **Step 2.3: Add declaration to `src/palm/sync/palmbackend.h`**

Under the `// --- Palm-level record access (category-aware) ---` block (next to `loadPalmRecords`, `loadPalmRecord`, `createPalmRecord`):

```cpp
    bool updatePalmRecord(const QString &dbName, const PalmRecord &record);
```

- [ ] **Step 2.4: Implement in `src/palm/sync/palmbackend.cpp`**

Append after `createPalmRecord`:

```cpp
bool PalmBackend::updatePalmRecord(const QString &dbName, const PalmRecord &record)
{
    if (!m_device) return false;
    return m_device->updateRecord(dbName, record);
}
```

- [ ] **Step 2.5: Build + run test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmbackend_roundtrip
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_palmbackend_roundtrip --output-on-failure
```

Expected: PASS. All other tests in the binary still green.

- [ ] **Step 2.6: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/palm/sync/palmbackend.h src/palm/sync/palmbackend.cpp \
        tests/palmsync/tst_palmbackend_roundtrip.cpp
git commit -m "$(cat <<'EOF'
feat(palmsync): PalmBackend::updatePalmRecord category-aware helper (Phase E.9)

Mirrors createPalmRecord: accepts PalmRecord with explicit category
slot and delegates to IPalmDatabaseAccess::updateRecord. Required by
MemoBlobBackend's write path (landing next); updateRecord(BackendRecord)
cannot carry a category because BackendRecord has no properties map.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Concrete `PalmDeviceConnection`

Goal: define the type that's been forward-declared in `src/core/ibackendplugin.h:18` since E.8. Plugins call `device->palmBackend()` to reach the shared backend.

**Files:**

- Create: `src/palm/palmdeviceconnection.h`
- Create: `src/palm/palmdeviceconnection.cpp`
- Modify: `src/palm/CMakeLists.txt` (see step 3.6 for location — may need a small new target)
- Create: `tests/palm/CMakeLists.txt`
- Create: `tests/palm/tst_palmdeviceconnection.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 3.1: Inspect existing palm CMakeLists**

```bash
ls /home/clinton/dev/WildPalms/src/palm/
cat /home/clinton/dev/WildPalms/src/palm/CMakeLists.txt 2>/dev/null || echo "no top-level palm CMakeLists; each subdir has its own"
ls /home/clinton/dev/WildPalms/src/palm/*.cpp
```

Identify which CMake target already compiles `devicesession.cpp`, `deviceworker.cpp`, etc. (grep for `devicesession` in the project's CMakeLists files). That's where `palmdeviceconnection.{h,cpp}` joins.

- [ ] **Step 3.2: Write the failing test**

Create `tests/palm/tst_palmdeviceconnection.cpp`:

```cpp
#include <QtTest/QtTest>
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"

class TestPalmDeviceConnection : public QObject {
    Q_OBJECT
private slots:
    void exposesDeviceAndPalmBackend();
    void palmBackendIsUsableAcrossCalls();
};

void TestPalmDeviceConnection::exposesDeviceAndPalmBackend()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);

    QCOMPARE(conn.device(), &dev);
    QVERIFY(conn.palmBackend() != nullptr);
    QCOMPARE(conn.palmBackend()->backendId(), QStringLiteral("palm"));
}

void TestPalmDeviceConnection::palmBackendIsUsableAcrossCalls()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    PalmDeviceConnection conn(&dev);

    auto *pb = conn.palmBackend();
    QVERIFY(pb == conn.palmBackend()); // same pointer — not reconstructed

    const auto collections = pb->availableCollections();
    bool sawMemo = false;
    for (const auto &c : collections) {
        if (c.id == QStringLiteral("palm:memo")) sawMemo = true;
    }
    QVERIFY(sawMemo);
}

QTEST_MAIN(TestPalmDeviceConnection)
#include "tst_palmdeviceconnection.moc"
```

- [ ] **Step 3.3: Create `tests/palm/CMakeLists.txt`**

```cmake
add_executable(tst_palmdeviceconnection tst_palmdeviceconnection.cpp)
target_link_libraries(tst_palmdeviceconnection
    PRIVATE
        WildPalmsPalmSync
        WildPalmsCore
        Qt::Test
)
add_test(NAME tst_palmdeviceconnection COMMAND tst_palmdeviceconnection)
```

And append to `tests/CMakeLists.txt` (if `add_subdirectory(palm)` isn't already there):

```cmake
add_subdirectory(palm)
```

- [ ] **Step 3.4: Run test — expect FAIL (compile error)**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmdeviceconnection 2>&1 | tail -10
```

Expected: compile error — `palmdeviceconnection.h` not found.

- [ ] **Step 3.5: Create `src/palm/palmdeviceconnection.h`**

```cpp
#ifndef WILDPALMS_PALM_PALMDEVICECONNECTION_H
#define WILDPALMS_PALM_PALMDEVICECONNECTION_H

#include <QObject>

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
class PalmBackend;
}

/**
 * @brief Aggregator passed to plugins via IBackendPlugin::createBackends.
 *
 * Owns a PalmBackend wrapping the caller-supplied IPalmDatabaseAccess.
 * Does NOT own the IPalmDatabaseAccess — the caller (application
 * runtime) must keep it alive for the connection's lifetime.
 *
 * Lives in the global namespace to match the forward declaration in
 * src/core/ibackendplugin.h (which stays Kalburator-free and
 * namespace-lean).
 */
class PalmDeviceConnection : public QObject
{
    Q_OBJECT
public:
    explicit PalmDeviceConnection(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        QObject *parent = nullptr);
    ~PalmDeviceConnection() override;

    WildPalms::PalmSync::IPalmDatabaseAccess *device() const;
    WildPalms::PalmSync::PalmBackend         *palmBackend() const;

signals:
    void connected();     // wired in a future sub-phase (E.15/E.17)
    void disconnected();  // wired in a future sub-phase (E.15/E.17)

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    WildPalms::PalmSync::PalmBackend         *m_palmBackend = nullptr;
};

#endif // WILDPALMS_PALM_PALMDEVICECONNECTION_H
```

- [ ] **Step 3.6: Create `src/palm/palmdeviceconnection.cpp`**

```cpp
#include "palmdeviceconnection.h"

#include "sync/palmbackend.h"

PalmDeviceConnection::PalmDeviceConnection(
    WildPalms::PalmSync::IPalmDatabaseAccess *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_palmBackend(new WildPalms::PalmSync::PalmBackend(device, this))
{
}

PalmDeviceConnection::~PalmDeviceConnection() = default;

WildPalms::PalmSync::IPalmDatabaseAccess *PalmDeviceConnection::device() const
{
    return m_device;
}

WildPalms::PalmSync::PalmBackend *PalmDeviceConnection::palmBackend() const
{
    return m_palmBackend;
}
```

- [ ] **Step 3.7: Add to CMake**

Add `palmdeviceconnection.{h,cpp}` to the existing `WildPalmsPalmSync` target's source list (the one that already has `palmbackend.cpp`), OR, if `PalmDeviceConnection` logically belongs with `devicesession.cpp` instead, add it there. Pick whichever already links against `WildPalmsPalmSync`.

The header includes `QObject`, which triggers AUTOMOC — make sure the target has `set(CMAKE_AUTOMOC ON)` applied (already true for Qt6 projects in this tree).

Example patch in `src/palm/sync/CMakeLists.txt` (or wherever `WildPalmsPalmSync` is defined):

```cmake
add_library(WildPalmsPalmSync STATIC
    # ... existing sources ...
    ../palmdeviceconnection.h
    ../palmdeviceconnection.cpp
)
```

Or define a tiny sibling target, whichever fits the existing pattern. Confirm no circular dep (PalmDeviceConnection depends on PalmBackend — fine; neither depends on WildPalmsRuntime, so no cycle).

- [ ] **Step 3.8: Build + run test**

```bash
cmake /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmdeviceconnection
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_palmdeviceconnection --output-on-failure
```

Expected: PASS.

- [ ] **Step 3.9: Verify nothing else broke**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```

Expected: all existing tests still pass.

- [ ] **Step 3.10: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/palm/palmdeviceconnection.h src/palm/palmdeviceconnection.cpp \
        src/palm/sync/CMakeLists.txt \
        tests/palm/CMakeLists.txt tests/palm/tst_palmdeviceconnection.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm): concrete PalmDeviceConnection (Phase E.9)

Was forward-declared in core/ibackendplugin.h since E.8; the first
real plugin (Memo) needs it concrete to reach the shared PalmBackend
via device->palmBackend(). Owns a PalmBackend wrapping the supplied
IPalmDatabaseAccess. Signals declared for future runtime wiring
(E.15/E.17), not emitted yet.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `MemoMarkdown` — POD ↔ Markdown encoder/decoder

Goal: land the format layer the memo plugin bolts on top of `MemoCodec`. Hash-stable, canonical-ordered frontmatter, tolerant parser.

**Files:**

- Create: `src/plugins/memo/memomarkdown.h`
- Create: `src/plugins/memo/memomarkdown.cpp`
- Create: `tests/plugins/memo/CMakeLists.txt`
- Create: `tests/plugins/memo/tst_memomarkdown.cpp`
- Modify: `tests/CMakeLists.txt`

*Note:* The static library that will host these sources (`WildPalmsMemoPluginCore`) is created in Task 8's CMake work. For Tasks 4–6 we build the test binaries directly against the source files. This keeps Tasks 4–6 testable in isolation without deciding plugin-lib packaging up front.

- [ ] **Step 4.1: Write failing tests (round-trip + canonicalisation)**

Create `tests/plugins/memo/tst_memomarkdown.cpp`:

```cpp
#include <QtTest/QtTest>
#include "plugins/memo/memomarkdown.h"

using WildPalms::Memo::MarkdownMemo;
using WildPalms::Memo::encode;
using WildPalms::Memo::decode;
using WildPalms::Memo::filenameFor;

class TestMemoMarkdown : public QObject {
    Q_OBJECT
private slots:
    void roundTripTextOnly();
    void roundTripWithCategoryAndPrivate();
    void canonicalKeyOrder();
    void defaultOmissionSlotZero();
    void defaultOmissionPrivateFalse();
    void defaultOmissionMissingCategoryName();
    void bodyTrailingNewlineCanonical();
    void parseAcceptsIntegerCategory();
    void parseAcceptsStringCategory();
    void parseToleratesMissingKeys();
    void parseToleratesMalformedFrontmatter();
    void parseAcceptsOldCreatedField();
    void filenameFromFirstLine();
    void filenameFallbackForEmptyBody();
    void filenameSanitisesSpecialChars();
};

// --- round-trip ---

void TestMemoMarkdown::roundTripTextOnly()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("hello world");
    const QString md = encode(m);

    MarkdownMemo back = decode(md);
    QCOMPARE(back.content.text, m.content.text);
    QCOMPARE(back.content.isPrivate, false);
    QCOMPARE(back.categorySlot, 0);
}

void TestMemoMarkdown::roundTripWithCategoryAndPrivate()
{
    MarkdownMemo m;
    m.recordId = 42;
    m.content.text = QStringLiteral("secret note\nsecond line");
    m.content.isPrivate = true;
    m.categorySlot = 3;
    m.categoryName = QStringLiteral("Work");
    const QString md = encode(m);

    MarkdownMemo back = decode(md);
    QCOMPARE(back.recordId, 42u);
    QCOMPARE(back.content.text, m.content.text);
    QCOMPARE(back.content.isPrivate, true);
    QCOMPARE(back.categorySlot, 3);
    QCOMPARE(back.categoryName.value_or(QString()), QStringLiteral("Work"));
}

// --- canonicalisation ---

void TestMemoMarkdown::canonicalKeyOrder()
{
    MarkdownMemo m;
    m.recordId = 1;
    m.content.text = QStringLiteral("body");
    m.content.isPrivate = true;
    m.categorySlot = 2;
    m.categoryName = QStringLiteral("Home");
    const QString md = encode(m);

    // id < category < categoryName < private, each on its own line.
    const int idxId       = md.indexOf(QStringLiteral("id:"));
    const int idxCat      = md.indexOf(QStringLiteral("category:"));
    const int idxCatName  = md.indexOf(QStringLiteral("categoryName:"));
    const int idxPrivate  = md.indexOf(QStringLiteral("private:"));
    QVERIFY(idxId >= 0 && idxCat > idxId && idxCatName > idxCat && idxPrivate > idxCatName);
}

void TestMemoMarkdown::defaultOmissionSlotZero()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("body");
    m.categorySlot = 0;  // default — no categoryName either
    QVERIFY(!encode(m).contains(QStringLiteral("category:")));
}

void TestMemoMarkdown::defaultOmissionPrivateFalse()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("body");
    m.content.isPrivate = false;
    QVERIFY(!encode(m).contains(QStringLiteral("private:")));
}

void TestMemoMarkdown::defaultOmissionMissingCategoryName()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("body");
    m.categorySlot = 5;
    m.categoryName.reset();  // no store or slot unknown
    const QString md = encode(m);
    QVERIFY(md.contains(QStringLiteral("category: 5")));
    QVERIFY(!md.contains(QStringLiteral("categoryName:")));
}

void TestMemoMarkdown::bodyTrailingNewlineCanonical()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("line one\nline two");  // no trailing \n
    const QString md = encode(m);
    QVERIFY(md.endsWith(QChar('\n')));
    QVERIFY(!md.endsWith(QStringLiteral("\n\n")));

    // Decoding strips the single trailing newline.
    MarkdownMemo back = decode(md);
    QCOMPARE(back.content.text, m.content.text);
}

// --- parse tolerance ---

void TestMemoMarkdown::parseAcceptsIntegerCategory()
{
    const QString md = QStringLiteral(
        "---\n"
        "category: 4\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.categorySlot, 4);
    QVERIFY(!m.categoryName.has_value());
}

void TestMemoMarkdown::parseAcceptsStringCategory()
{
    const QString md = QStringLiteral(
        "---\n"
        "category: Work\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.categorySlot, 0);
    QCOMPARE(m.categoryName.value_or(QString()), QStringLiteral("Work"));
}

void TestMemoMarkdown::parseToleratesMissingKeys()
{
    const QString md = QStringLiteral("just body text\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.content.text, QStringLiteral("just body text"));
    QCOMPARE(m.recordId, 0u);
    QCOMPARE(m.categorySlot, 0);
    QCOMPARE(m.content.isPrivate, false);
}

void TestMemoMarkdown::parseToleratesMalformedFrontmatter()
{
    const QString md = QStringLiteral(
        "---\n"
        "garbage without colon\n"
        "id: 7\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.recordId, 7u);
    QCOMPARE(m.content.text, QStringLiteral("body"));
}

void TestMemoMarkdown::parseAcceptsOldCreatedField()
{
    // The old memomapper emitted `created:`; we ignore it silently.
    const QString md = QStringLiteral(
        "---\n"
        "id: 3\n"
        "created: 2025-01-01T00:00:00\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.recordId, 3u);
    QCOMPARE(m.content.text, QStringLiteral("body"));
}

// --- filenames ---

void TestMemoMarkdown::filenameFromFirstLine()
{
    MarkdownMemo m;
    m.recordId = 9;
    m.content.text = QStringLiteral("Grocery list\n- milk\n- eggs");
    QCOMPARE(filenameFor(m), QStringLiteral("Grocery_list.md"));
}

void TestMemoMarkdown::filenameFallbackForEmptyBody()
{
    MarkdownMemo m;
    m.recordId = 17;
    m.content.text.clear();
    QCOMPARE(filenameFor(m), QStringLiteral("memo_17.md"));
}

void TestMemoMarkdown::filenameSanitisesSpecialChars()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("a/b\\c:d*e");
    // Every invalid char maps to '_', then underscores collapse at trim.
    const QString f = filenameFor(m);
    QVERIFY(f.endsWith(QStringLiteral(".md")));
    QVERIFY(!f.contains('/'));
    QVERIFY(!f.contains('\\'));
    QVERIFY(!f.contains(':'));
    QVERIFY(!f.contains('*'));
}

QTEST_MAIN(TestMemoMarkdown)
#include "tst_memomarkdown.moc"
```

- [ ] **Step 4.2: Create `tests/plugins/memo/CMakeLists.txt`**

```cmake
# Tasks 4-6 build test binaries directly against the source files.
# Task 8 packages these into WildPalmsMemoPluginCore static lib; we
# re-link the test binaries then.

set(MEMO_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/memo)

add_executable(tst_memomarkdown
    tst_memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
)
target_include_directories(tst_memomarkdown
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_memomarkdown
    PRIVATE
        WildPalmsPalmCodecs   # MemoCodec provides PalmCodecs::Memo POD
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memomarkdown COMMAND tst_memomarkdown)
```

And append to `tests/CMakeLists.txt`:

```cmake
add_subdirectory(plugins/memo)
```

(If `tests/plugins/CMakeLists.txt` exists from E.8, ensure it has `add_subdirectory(memo)`. Otherwise either add it or skip that intermediate and reference `plugins/memo` directly from `tests/CMakeLists.txt` as above.)

- [ ] **Step 4.3: Run test — expect FAIL**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memomarkdown 2>&1 | tail -10
```

Expected: compile error — `memomarkdown.h` not found.

- [ ] **Step 4.4: Create `src/plugins/memo/memomarkdown.h`**

```cpp
#ifndef WILDPALMS_MEMO_MEMOMARKDOWN_H
#define WILDPALMS_MEMO_MEMOMARKDOWN_H

#include <cstdint>
#include <optional>

#include <QString>

#include "palm/codecs/memocodec.h"

namespace WildPalms::Memo {

/// Bundle of "everything a Markdown file on disk represents about a
/// memo." Content (text + isPrivate) is the Palm-facing POD from
/// MemoCodec. Everything else is file-layer state: record id for
/// round-trip, category slot (authoritative), optional
/// human-readable category name (decorative).
struct MarkdownMemo {
    WildPalms::PalmCodecs::Memo content;
    std::uint32_t               recordId = 0;
    int                         categorySlot = 0;     // 0..15
    std::optional<QString>      categoryName;          // empty => omit
};

/// Encode to Markdown with canonical YAML frontmatter. Produces
/// hash-stable bytes: same MarkdownMemo always yields the same
/// QString. Omits default-valued fields (slot 0, private=false,
/// missing categoryName). Body ends with exactly one \n.
QString encode(const MarkdownMemo &memo);

/// Parse a Markdown file. Tolerates missing frontmatter, malformed
/// frontmatter lines, integer or string `category:` values, and
/// unknown keys (including the legacy `created:` field the old
/// memomapper emitted).
MarkdownMemo decode(const QString &markdown);

/// Human-friendly filename derived from the first line of the memo
/// body. Falls back to "memo_<recordId>.md" for empty bodies or
/// bodies that sanitise to nothing useful.
QString filenameFor(const MarkdownMemo &memo);

} // namespace WildPalms::Memo

#endif // WILDPALMS_MEMO_MEMOMARKDOWN_H
```

- [ ] **Step 4.5: Create `src/plugins/memo/memomarkdown.cpp`**

```cpp
#include "memomarkdown.h"

#include <QRegularExpression>
#include <QStringList>

namespace WildPalms::Memo {

namespace {

QString sanitiseFilenameStem(const QString &input)
{
    static const QRegularExpression invalidChars("[^a-zA-Z0-9_\\-. ]");
    static const QRegularExpression multiSpace("\\s+");

    QString stem = input;
    stem.replace(invalidChars, QStringLiteral("_"));
    stem.replace(multiSpace, QStringLiteral("_"));
    while (stem.startsWith('_')) stem.remove(0, 1);
    while (stem.endsWith('_'))   stem.chop(1);
    return stem.trimmed();
}

} // namespace

QString encode(const MarkdownMemo &memo)
{
    QString out;
    out.reserve(128 + memo.content.text.size());

    out += QStringLiteral("---\n");
    out += QStringLiteral("id: %1\n").arg(memo.recordId);

    const bool hasCategoryName =
        memo.categoryName.has_value() && !memo.categoryName->isEmpty();
    if (memo.categorySlot != 0 || hasCategoryName) {
        out += QStringLiteral("category: %1\n").arg(memo.categorySlot);
    }
    if (hasCategoryName) {
        out += QStringLiteral("categoryName: %1\n").arg(*memo.categoryName);
    }
    if (memo.content.isPrivate) {
        out += QStringLiteral("private: true\n");
    }

    out += QStringLiteral("---\n\n");
    out += memo.content.text;
    if (!out.endsWith(QChar('\n'))) {
        out += QChar('\n');
    }
    return out;
}

MarkdownMemo decode(const QString &markdown)
{
    MarkdownMemo m;

    QString body;
    if (markdown.startsWith(QStringLiteral("---"))) {
        const int end = markdown.indexOf(QStringLiteral("\n---"), 3);
        if (end != -1) {
            const QString frontmatter = markdown.mid(4, end - 4);
            body = markdown.mid(end + 5);
            while (body.startsWith(QChar('\n'))) body.remove(0, 1);

            const QStringList lines = frontmatter.split(QChar('\n'));
            for (const QString &line : lines) {
                const int colon = line.indexOf(QChar(':'));
                if (colon == -1) continue;

                const QString key   = line.left(colon).trimmed().toLower();
                const QString value = line.mid(colon + 1).trimmed();

                if (key == QStringLiteral("id")) {
                    m.recordId = static_cast<std::uint32_t>(value.toUInt());
                } else if (key == QStringLiteral("category")) {
                    bool ok = false;
                    const int asInt = value.toInt(&ok);
                    if (ok) {
                        m.categorySlot = asInt;
                    } else {
                        m.categoryName = value;
                    }
                } else if (key == QStringLiteral("categoryname")) {
                    m.categoryName = value;
                } else if (key == QStringLiteral("private")) {
                    m.content.isPrivate = (value.toLower() == QStringLiteral("true"));
                }
                // Unknown keys (created:, modified:, etc.) silently ignored.
            }
        } else {
            body = markdown;
        }
    } else {
        body = markdown;
    }

    if (body.endsWith(QChar('\n'))) body.chop(1);
    m.content.text = body;
    return m;
}

QString filenameFor(const MarkdownMemo &memo)
{
    const QString firstLine = memo.content.text.split(QChar('\n')).first().trimmed();

    QString stem;
    if (firstLine.isEmpty()) {
        stem = QStringLiteral("memo_%1").arg(memo.recordId);
    } else {
        stem = sanitiseFilenameStem(firstLine.left(50));
        if (stem.isEmpty()) {
            stem = QStringLiteral("memo_%1").arg(memo.recordId);
        }
    }
    return stem + QStringLiteral(".md");
}

} // namespace WildPalms::Memo
```

- [ ] **Step 4.6: Build + run test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memomarkdown
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_memomarkdown --output-on-failure
```

Expected: all 15 sub-tests pass.

- [ ] **Step 4.7: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/memo/memomarkdown.h src/plugins/memo/memomarkdown.cpp \
        tests/plugins/memo/CMakeLists.txt tests/plugins/memo/tst_memomarkdown.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(memo): MemoMarkdown encoder/decoder with hash-stable output (Phase E.9)

POD MarkdownMemo ↔ Markdown-with-YAML-frontmatter. Canonical key
order (id, category, categoryName, private), default omission, exactly
one trailing newline. Decoder accepts integer or string `category:`,
tolerates missing keys and malformed frontmatter, silently ignores
unknown keys (including the legacy `created:` field the old memomapper
emitted).

Round-trip hash stability is the load-bearing property: the sync
engine compares content hashes, so the encoder must produce
byte-identical output from any MarkdownMemo that decodes back to the
same value.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `MemoBlobBackend` — transcoding `IBlobBackend`

Goal: the wrapper over `PalmBackend` that transcodes Palm wire bytes ↔ Markdown for the `palm:memo` collection only.

**Files:**

- Create: `src/plugins/memo/memoblobbackend.h`
- Create: `src/plugins/memo/memoblobbackend.cpp`
- Modify: `tests/plugins/memo/CMakeLists.txt`
- Create: `tests/plugins/memo/tst_memoblobbackend.cpp`

- [ ] **Step 5.1: Write failing tests**

Create `tests/plugins/memo/tst_memoblobbackend.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QCryptographicHash>

#include "plugins/memo/memoblobbackend.h"
#include "plugins/memo/memomarkdown.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/memocodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"

using WildPalms::Memo::MemoBlobBackend;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

class TestMemoBlobBackend : public QObject {
    Q_OBJECT
private slots:
    void availableCollectionsExposesOnlyPalmMemo();
    void loadRecordsDecodesPalmToMarkdown();
    void createRecordEncodesMarkdownToPalmPreservingCategory();
    void updateRecordRoundTripsCategory();
    void deleteRecordPropagatesToDevice();
    void modifiedSinceDelegates();
    void privateFlagPreservedBothDirections();
    void categoryNameResolvedFromStoreWhenPresent();

private:
    std::uint32_t seedMemo(MockPalmDatabaseAccess *dev,
                           const QString &text,
                           int category,
                           bool isPrivate);
};

std::uint32_t TestMemoBlobBackend::seedMemo(MockPalmDatabaseAccess *dev,
                                            const QString &text,
                                            int category,
                                            bool isPrivate)
{
    PalmRecord pr;
    pr.category = category;
    pr.data = WildPalms::PalmCodecs::encodeMemo({text, isPrivate});
    if (isPrivate) pr.attributes |= PalmRecord::AttrSecret;
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return dev->createRecord("MemoDB", pr);
}

// --- collection exposure ---

void TestMemoBlobBackend::availableCollectionsExposesOnlyPalmMemo()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    dev.createDatabase("DatebookDB");   // other DBs should not surface here
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto cols = mb.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, QStringLiteral("palm:memo"));
    QCOMPARE(cols.first().type, QStringLiteral("memos"));
}

// --- read path ---

void TestMemoBlobBackend::loadRecordsDecodesPalmToMarkdown()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("hello\nworld"), 0, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto records = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(records.size(), 1);
    const QString md = QString::fromUtf8(records.first().data);
    QVERIFY(md.contains(QStringLiteral("hello\nworld")));
    QVERIFY(md.startsWith(QStringLiteral("---\n")));
    QVERIFY(records.first().contentHash ==
            QString::fromLatin1(QCryptographicHash::hash(
                records.first().data, QCryptographicHash::Sha256).toHex()));
}

// --- write path ---

void TestMemoBlobBackend::createRecordEncodesMarkdownToPalmPreservingCategory()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    // Synthesise a Markdown BackendRecord as if coming from LocalBlobBackend.
    WildPalms::Memo::MarkdownMemo m;
    m.content.text = QStringLiteral("a new memo");
    m.categorySlot = 4;
    const QByteArray mdBytes = WildPalms::Memo::encode(m).toUtf8();

    Kalburator::Sync::BackendRecord br;
    br.type = QStringLiteral("memos");
    br.data = mdBytes;
    const QString newId = mb.createRecord(QStringLiteral("palm:memo"), br);
    QVERIFY(!newId.isEmpty());

    const auto stored = dev.readAllRecords("MemoDB");
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().category, 4);
    const auto decoded = WildPalms::PalmCodecs::decodeMemo(stored.first().data);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QStringLiteral("a new memo"));
}

void TestMemoBlobBackend::updateRecordRoundTripsCategory()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    const auto id = seedMemo(&dev, QStringLiteral("initial"), 2, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    // Load, mutate via markdown, write back.
    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);

    auto decoded = WildPalms::Memo::decode(QString::fromUtf8(recs.first().data));
    decoded.content.text = QStringLiteral("updated body");
    decoded.categorySlot = 6;

    Kalburator::Sync::BackendRecord updated = recs.first();
    updated.data = WildPalms::Memo::encode(decoded).toUtf8();

    QVERIFY(mb.updateRecord(updated));

    const auto stored = dev.readRecord("MemoDB", id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->category, 6);
    const auto reDecoded = WildPalms::PalmCodecs::decodeMemo(stored->data);
    QVERIFY(reDecoded.has_value());
    QCOMPARE(reDecoded->text, QStringLiteral("updated body"));
}

void TestMemoBlobBackend::deleteRecordPropagatesToDevice()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    const auto id = seedMemo(&dev, QStringLiteral("doomed"), 0, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    QVERIFY(mb.deleteRecord(PalmBackend::encodeRecordId("MemoDB", id)));
    QVERIFY(!dev.readRecord("MemoDB", id).has_value());
}

void TestMemoBlobBackend::modifiedSinceDelegates()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("old"), 0, false);

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-1);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto mods = mb.modifiedSince(QStringLiteral("palm:memo"), cutoff);
    QCOMPARE(mods.size(), 1);
    QVERIFY(QString::fromUtf8(mods.first().data).contains(QStringLiteral("old")));
}

void TestMemoBlobBackend::privateFlagPreservedBothDirections()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("shh"), 0, /*isPrivate=*/true);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);
    QVERIFY(QString::fromUtf8(recs.first().data).contains(QStringLiteral("private: true")));

    // Round-trip the markdown back to Palm — private flag preserved.
    Kalburator::Sync::BackendRecord br = recs.first();
    br.data = recs.first().data;  // unchanged
    QVERIFY(mb.updateRecord(br));

    const auto stored = dev.readAllRecords("MemoDB");
    QCOMPARE(stored.size(), 1);
    QVERIFY((stored.first().attributes & PalmRecord::AttrSecret) != 0);
}

void TestMemoBlobBackend::categoryNameResolvedFromStoreWhenPresent()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("work thing"), 3, false);
    PalmBackend pb(&dev);

    WildPalms::PalmCalendar::CategoryMappingStore store;
    store.setSlotName(QStringLiteral("MemoDB"), 3, QStringLiteral("Work"));
    MemoBlobBackend mb(&pb, &store);

    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);
    const QString md = QString::fromUtf8(recs.first().data);
    QVERIFY(md.contains(QStringLiteral("category: 3")));
    QVERIFY(md.contains(QStringLiteral("categoryName: Work")));
}

QTEST_MAIN(TestMemoBlobBackend)
#include "tst_memoblobbackend.moc"
```

- [ ] **Step 5.2: Extend `tests/plugins/memo/CMakeLists.txt`**

Append:

```cmake
add_executable(tst_memoblobbackend
    tst_memoblobbackend.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoblobbackend.cpp
)
target_include_directories(tst_memoblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_memoblobbackend
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar   # CategoryMappingStore
        WildPalmsCore
        Kalburator::Sync
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memoblobbackend COMMAND tst_memoblobbackend)
```

- [ ] **Step 5.3: Run test — expect FAIL (compile error)**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memoblobbackend 2>&1 | tail -10
```

Expected: compile error — `memoblobbackend.h` not found.

- [ ] **Step 5.4: Create `src/plugins/memo/memoblobbackend.h`**

```cpp
#ifndef WILDPALMS_MEMO_MEMOBLOBBACKEND_H
#define WILDPALMS_MEMO_MEMOBLOBBACKEND_H

#include "kalburator/sync/iblobbackend.h"

#include <QObject>

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Memo {

/**
 * @brief Transcoding IBlobBackend wrapping PalmBackend's palm:memo.
 *
 * Exposes exactly one collection ("palm:memo") with type "memos".
 * loadRecords returns Markdown bytes (via MemoCodec → MemoMarkdown);
 * createRecord / updateRecord accept Markdown bytes and encode back
 * to Palm wire format. Category slot is preserved via PalmBackend's
 * category-aware createPalmRecord / updatePalmRecord helpers.
 *
 * Non-owning pointers: palmBackend and categoryStore must outlive
 * the MemoBlobBackend. categoryStore may be null — when null the
 * encoder omits categoryName, and the decoder falls back to slot 0
 * for name-only `category:` strings.
 */
class MemoBlobBackend : public Kalburator::Sync::IBlobBackend
{
    Q_OBJECT
public:
    static constexpr const char *CollectionId = "palm:memo";
    static constexpr const char *PalmDbName   = "MemoDB";

    explicit MemoBlobBackend(
        WildPalms::PalmSync::PalmBackend *palmBackend,
        WildPalms::PalmCalendar::CategoryMappingStore *categoryStore = nullptr,
        QObject *parent = nullptr);
    ~MemoBlobBackend() override;

    // --- Identity ---
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- Collections ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &) override;
    QString createCollection(const Kalburator::Sync::CollectionInfo &) override;

    // --- Records ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord &) override;
    bool    deleteRecord(const QString &recordId) override;

    // --- Change detection ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &, const QDateTime &) override;
    QStringList deletedSince(const QString &, const QDateTime &) override;
    bool        supportsDeleteTracking() const override;

private:
    WildPalms::PalmSync::PalmBackend              *m_palmBackend = nullptr;
    WildPalms::PalmCalendar::CategoryMappingStore *m_categoryStore = nullptr;
};

} // namespace WildPalms::Memo

#endif // WILDPALMS_MEMO_MEMOBLOBBACKEND_H
```

- [ ] **Step 5.5: Create `src/plugins/memo/memoblobbackend.cpp`**

```cpp
#include "memoblobbackend.h"

#include <QCryptographicHash>

#include "memomarkdown.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/memocodec.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

#include "kalburator/sync/backendrecord.h"
#include "kalburator/sync/collectioninfo.h"

namespace WildPalms::Memo {

namespace {

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

Kalburator::Sync::BackendRecord palmToMarkdownRecord(
    const WildPalms::PalmSync::PalmRecord &pr,
    const WildPalms::PalmCalendar::CategoryMappingStore *store)
{
    const auto decoded = WildPalms::PalmCodecs::decodeMemo(pr.data);
    WildPalms::Memo::MarkdownMemo m;
    m.recordId = pr.recordId;
    m.categorySlot = pr.category;
    m.content = decoded.value_or(WildPalms::PalmCodecs::Memo{});
    m.content.isPrivate = (pr.attributes & WildPalms::PalmSync::PalmRecord::AttrSecret) != 0;
    if (store) {
        const QString name = store->slotName(QStringLiteral("MemoDB"), pr.category);
        if (!name.isEmpty()) m.categoryName = name;
    }

    const QByteArray bytes = WildPalms::Memo::encode(m).toUtf8();
    Kalburator::Sync::BackendRecord br;
    br.id   = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral("MemoDB"), pr.recordId);
    br.type = QStringLiteral("memos");
    br.data = bytes;
    br.contentHash = sha256Hex(bytes);
    br.lastModified = pr.lastModified;
    br.isDeleted = pr.isDeleted();
    return br;
}

WildPalms::PalmSync::PalmRecord markdownRecordToPalm(
    const Kalburator::Sync::BackendRecord &br,
    std::uint32_t existingId,
    const WildPalms::PalmCalendar::CategoryMappingStore *store)
{
    WildPalms::Memo::MarkdownMemo m = WildPalms::Memo::decode(QString::fromUtf8(br.data));
    if (m.recordId == 0) m.recordId = existingId;

    // If frontmatter gave us a name but not a slot, try to resolve slot
    // from the store (keyed on MemoDB).
    if (m.categorySlot == 0 && m.categoryName.has_value() && store) {
        // CategoryMappingStore doesn't offer a reverse lookup; walk 1..15.
        for (int slot = 1; slot < 16; ++slot) {
            const QString name = store->slotName(QStringLiteral("MemoDB"), slot);
            if (name.compare(*m.categoryName, Qt::CaseInsensitive) == 0) {
                m.categorySlot = slot;
                break;
            }
        }
    }

    WildPalms::PalmSync::PalmRecord pr;
    pr.recordId = m.recordId;   // 0 on create, non-zero on update
    pr.category = m.categorySlot;
    pr.data     = WildPalms::PalmCodecs::encodeMemo(m.content);
    if (m.content.isPrivate) {
        pr.attributes |= WildPalms::PalmSync::PalmRecord::AttrSecret;
    }
    pr.lastModified = br.lastModified.isValid()
        ? br.lastModified
        : QDateTime::currentDateTimeUtc();
    return pr;
}

bool isMemoCollection(const QString &collectionId)
{
    return collectionId == QStringLiteral(MemoBlobBackend::CollectionId);
}

} // namespace

MemoBlobBackend::MemoBlobBackend(
    WildPalms::PalmSync::PalmBackend *palmBackend,
    WildPalms::PalmCalendar::CategoryMappingStore *categoryStore,
    QObject *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_palmBackend(palmBackend)
    , m_categoryStore(categoryStore)
{
}

MemoBlobBackend::~MemoBlobBackend() = default;

QString MemoBlobBackend::backendId()   const { return QStringLiteral("palm-memo"); }
QString MemoBlobBackend::displayName() const { return QStringLiteral("Palm Memos"); }
bool    MemoBlobBackend::isAvailable() const
{
    return m_palmBackend && m_palmBackend->isAvailable();
}

QList<Kalburator::Sync::CollectionInfo> MemoBlobBackend::availableCollections()
{
    Kalburator::Sync::CollectionInfo info;
    info.id   = QStringLiteral(CollectionId);
    info.name = QStringLiteral("Memos");
    info.type = QStringLiteral("memos");
    return { info };
}

Kalburator::Sync::CollectionInfo MemoBlobBackend::collectionInfo(const QString &collectionId)
{
    if (!isMemoCollection(collectionId)) return {};
    return availableCollections().first();
}

QString MemoBlobBackend::createCollection(const Kalburator::Sync::CollectionInfo &)
{
    // Palm collections are device-allocated; not user-creatable through this API.
    return {};
}

QList<Kalburator::Sync::BackendRecord> MemoBlobBackend::loadRecords(
    const QString &collectionId)
{
    if (!m_palmBackend || !isMemoCollection(collectionId)) return {};

    QList<Kalburator::Sync::BackendRecord> out;
    for (const auto &pr : m_palmBackend->loadPalmRecords(QStringLiteral(PalmDbName))) {
        out.append(palmToMarkdownRecord(pr, m_categoryStore));
    }
    return out;
}

std::optional<Kalburator::Sync::BackendRecord> MemoBlobBackend::loadRecord(
    const QString &recordId)
{
    if (!m_palmBackend) return std::nullopt;
    QString dbName;
    std::uint32_t numericId = 0;
    if (!WildPalms::PalmSync::PalmBackend::decodeRecordId(
            recordId, &dbName, &numericId)) return std::nullopt;
    if (dbName != QStringLiteral(PalmDbName)) return std::nullopt;

    const auto pr = m_palmBackend->loadPalmRecord(dbName, numericId);
    if (!pr.has_value()) return std::nullopt;
    return palmToMarkdownRecord(*pr, m_categoryStore);
}

QString MemoBlobBackend::createRecord(const QString &collectionId,
                                      const Kalburator::Sync::BackendRecord &record)
{
    if (!m_palmBackend || !isMemoCollection(collectionId)) return {};
    const auto pr = markdownRecordToPalm(record, /*existingId=*/0, m_categoryStore);
    const auto newId = m_palmBackend->createPalmRecord(QStringLiteral(PalmDbName), pr);
    if (newId == 0) return {};
    return WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(PalmDbName), newId);
}

bool MemoBlobBackend::updateRecord(const Kalburator::Sync::BackendRecord &record)
{
    if (!m_palmBackend) return false;
    QString dbName;
    std::uint32_t numericId = 0;
    if (!WildPalms::PalmSync::PalmBackend::decodeRecordId(
            record.id, &dbName, &numericId)) return false;
    if (dbName != QStringLiteral(PalmDbName)) return false;

    const auto pr = markdownRecordToPalm(record, numericId, m_categoryStore);
    return m_palmBackend->updatePalmRecord(QStringLiteral(PalmDbName), pr);
}

bool MemoBlobBackend::deleteRecord(const QString &recordId)
{
    if (!m_palmBackend) return false;
    return m_palmBackend->deleteRecord(recordId);
}

QList<Kalburator::Sync::BackendRecord> MemoBlobBackend::modifiedSince(
    const QString &collectionId, const QDateTime &since)
{
    if (!m_palmBackend || !isMemoCollection(collectionId)) return {};
    // Delegate to PalmBackend for the filter, then remap each to markdown.
    QList<Kalburator::Sync::BackendRecord> out;
    for (const auto &br : m_palmBackend->modifiedSince(collectionId, since)) {
        QString dbName;
        std::uint32_t numericId = 0;
        if (!WildPalms::PalmSync::PalmBackend::decodeRecordId(
                br.id, &dbName, &numericId)) continue;
        const auto pr = m_palmBackend->loadPalmRecord(dbName, numericId);
        if (!pr.has_value()) continue;
        out.append(palmToMarkdownRecord(*pr, m_categoryStore));
    }
    return out;
}

QStringList MemoBlobBackend::deletedSince(const QString &collectionId, const QDateTime &since)
{
    if (!m_palmBackend || !isMemoCollection(collectionId)) return {};
    return m_palmBackend->deletedSince(collectionId, since);
}

bool MemoBlobBackend::supportsDeleteTracking() const
{
    return m_palmBackend ? m_palmBackend->supportsDeleteTracking() : false;
}

} // namespace WildPalms::Memo
```

- [ ] **Step 5.6: Build + run test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memoblobbackend
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_memoblobbackend --output-on-failure
```

Expected: all 8 sub-tests pass.

- [ ] **Step 5.7: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/memo/memoblobbackend.h src/plugins/memo/memoblobbackend.cpp \
        tests/plugins/memo/CMakeLists.txt tests/plugins/memo/tst_memoblobbackend.cpp
git commit -m "$(cat <<'EOF'
feat(memo): MemoBlobBackend transcoding wrapper (Phase E.9)

IBlobBackend that wraps PalmBackend's palm:memo collection. Read
path: Palm wire bytes → MemoCodec::decodeMemo → MarkdownMemo →
MemoMarkdown::encode → Markdown bytes. Write path: reverse.

Uses PalmBackend's category-aware create/update/load helpers
(loadPalmRecord / createPalmRecord / updatePalmRecord) so the
category slot round-trips even though BackendRecord has no
properties map. CategoryMappingStore is optional; null-store path
omits the decorative categoryName from Markdown.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: `MemoBackendPlugin` class (no factory yet)

Goal: the `IBackendPlugin` subclass itself, testable in isolation. The `K_PLUGIN_FACTORY` macro lands in Task 7 alongside the JSON manifest.

**Files:**

- Create: `src/plugins/memo/memobackendplugin.h`
- Create: `src/plugins/memo/memobackendplugin.cpp`
- Modify: `tests/plugins/memo/CMakeLists.txt`
- Create: `tests/plugins/memo/tst_memobackendplugin.cpp`

- [ ] **Step 6.1: Write failing tests**

Create `tests/plugins/memo/tst_memobackendplugin.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QWidget>

#include "plugins/memo/memobackendplugin.h"
#include "plugins/memo/memoblobbackend.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

#include "kalburator/sync/qsynccore/recordsnapshot.h"

using WildPalms::Memo::MemoBackendPlugin;
using WildPalms::Memo::MemoBlobBackend;

class TestMemoBackendPlugin : public QObject {
    Q_OBJECT
private slots:
    void metadataStatics();
    void claimsMemoDB();
    void createBackendsReturnsMemoBlobBackendOverPalmBackend();
    void viewHooksReportMemoSurface();
    void conflictHtmlRendersMemoTitleAndBody();
    void enrichSnapshotDecodesPalmBytesOnSourceSide();
};

void TestMemoBackendPlugin::metadataStatics()
{
    MemoBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("memo"));
    QCOMPARE(p.displayName(), QStringLiteral("Memos"));
    QCOMPARE(p.description(),
             QStringLiteral("Synchronizes Palm MemoDB with Markdown files"));
    QCOMPARE(p.version(), QStringLiteral("2.0"));
}

void TestMemoBackendPlugin::claimsMemoDB()
{
    MemoBackendPlugin p;
    QCOMPARE(p.claimedDatabases(), QStringList{QStringLiteral("MemoDB")});
}

void TestMemoBackendPlugin::createBackendsReturnsMemoBlobBackendOverPalmBackend()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);
    MemoBackendPlugin p;

    auto backends = p.createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);
    QCOMPARE(backends.blob->backendId(), QStringLiteral("palm-memo"));
    QCOMPARE(backends.calendar, static_cast<Kalburator::Sync::SyncBackend *>(nullptr));

    // Clean up: the plugin returns a raw pointer without parenting.
    delete backends.blob;
}

void TestMemoBackendPlugin::viewHooksReportMemoSurface()
{
    MemoBackendPlugin p;
    QCOMPARE(p.hasMainView(), true);
    QCOMPARE(p.mainViewName(), QStringLiteral("Memos"));
    QVERIFY(!p.mainViewIcon().isNull() || p.mainViewIcon().isNull());
    // (isNull() is environment-dependent on missing icon themes.)

    QWidget parent;
    QWidget *view = p.createMainView(&parent);
    QVERIFY(view != nullptr);
    delete view;
}

void TestMemoBackendPlugin::conflictHtmlRendersMemoTitleAndBody()
{
    MemoBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    snap.content = QByteArrayLiteral("Grocery list\n- milk\n- eggs");
    snap.metadata[QStringLiteral("title")] = QStringLiteral("Grocery list");
    snap.contentType = QStringLiteral("text/plain");

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<h3>Grocery list</h3>")));
    QVERIFY(html.contains(QStringLiteral("- milk")));
}

void TestMemoBackendPlugin::enrichSnapshotDecodesPalmBytesOnSourceSide()
{
    MemoBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    // Palm wire bytes for "Shopping list\nfirst line extends" — encode
    // via MemoCodec to get the canonical format.
    const auto palm = WildPalms::PalmCodecs::encodeMemo(
        {QStringLiteral("Shopping list\nfirst line extends"), false});
    snap.content = palm;

    p.enrichConflictSnapshot(snap, /*isSourceSide=*/true);
    QCOMPARE(QString::fromUtf8(snap.content),
             QStringLiteral("Shopping list\nfirst line extends"));
    QCOMPARE(snap.contentType, QStringLiteral("text/plain"));
    QCOMPARE(snap.metadata.value(QStringLiteral("title")).toString(),
             QStringLiteral("Shopping list"));
}

QTEST_MAIN(TestMemoBackendPlugin)
#include "tst_memobackendplugin.moc"
```

- [ ] **Step 6.2: Extend `tests/plugins/memo/CMakeLists.txt`**

Append:

```cmake
add_executable(tst_memobackendplugin
    tst_memobackendplugin.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoblobbackend.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memobackendplugin.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoview.cpp
)
target_include_directories(tst_memobackendplugin
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_memobackendplugin
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar
        WildPalmsCore
        Kalburator::Sync
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memobackendplugin COMMAND tst_memobackendplugin)
```

Note: this target compiles `memoview.cpp` into the test binary because `createMainView` instantiates `MemoView`. That's a conscious trade-off — builds more than necessary for unit testing but verifies the view construction path.

- [ ] **Step 6.3: Run test — expect FAIL (compile error)**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memobackendplugin 2>&1 | tail -10
```

Expected: compile error — `memobackendplugin.h` not found.

- [ ] **Step 6.4: Create `src/plugins/memo/memobackendplugin.h`**

```cpp
#ifndef WILDPALMS_MEMO_MEMOBACKENDPLUGIN_H
#define WILDPALMS_MEMO_MEMOBACKENDPLUGIN_H

#include "core/ibackendplugin.h"

#include <QObject>

namespace WildPalms::Memo {

/**
 * @brief First new-ABI WildPalms plugin.
 *
 * Provides a MemoBlobBackend wrapping the shared PalmBackend the
 * runtime owns (reached via PalmDeviceConnection::palmBackend).
 * Surfaces MemoView as a main-window tab.
 */
class MemoBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit MemoBackendPlugin(QObject *parent = nullptr);
    ~MemoBackendPlugin() override;

    // IPlugin
    QString pluginId()    const override;
    QString displayName() const override;
    QIcon   icon()        const override;
    QString description() const override;
    QString version()     const override;

    // IBackendPlugin
    QStringList      claimedDatabases() const override;
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                    PalmDeviceConnection         *device) override;

    // IBackendPlugin — main view
    bool     hasMainView()   const override;
    QWidget *createMainView(QWidget *parent) override;
    QString  mainViewName()  const override;
    QIcon    mainViewIcon()  const override;

    // IBackendPlugin — conflict presentation
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const override;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const override;
};

} // namespace WildPalms::Memo

#endif // WILDPALMS_MEMO_MEMOBACKENDPLUGIN_H
```

- [ ] **Step 6.5: Create `src/plugins/memo/memobackendplugin.cpp`**

```cpp
#include "memobackendplugin.h"

#include "memoblobbackend.h"
#include "memoview.h"

#include "palm/codecs/memocodec.h"
#include "palm/palmdeviceconnection.h"

#include "kalburator/sync/qsynccore/recordsnapshot.h"

#include <QIcon>
#include <QString>
#include <QWidget>

namespace WildPalms::Memo {

MemoBackendPlugin::MemoBackendPlugin(QObject *parent) : QObject(parent) {}
MemoBackendPlugin::~MemoBackendPlugin() = default;

// --- IPlugin ---

QString MemoBackendPlugin::pluginId()    const { return QStringLiteral("memo"); }
QString MemoBackendPlugin::displayName() const { return QStringLiteral("Memos"); }
QIcon   MemoBackendPlugin::icon()        const
{
    return QIcon::fromTheme(QStringLiteral("view-pim-notes"));
}
QString MemoBackendPlugin::description() const
{
    return QStringLiteral("Synchronizes Palm MemoDB with Markdown files");
}
QString MemoBackendPlugin::version()     const { return QStringLiteral("2.0"); }

// --- IBackendPlugin ---

QStringList MemoBackendPlugin::claimedDatabases() const
{
    return { QStringLiteral("MemoDB") };
}

WildPalms::IBackendPlugin::ProvidedBackends
MemoBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *host,
                                  PalmDeviceConnection         *device)
{
    Q_UNUSED(host)
    ProvidedBackends out;
    if (device) {
        out.blob = new MemoBlobBackend(device->palmBackend(),
                                       /*categoryStore=*/nullptr);
    }
    return out;
}

// --- Main view ---

bool MemoBackendPlugin::hasMainView() const { return true; }

QWidget *MemoBackendPlugin::createMainView(QWidget *parent)
{
    return new MemoView(parent);
}

QString MemoBackendPlugin::mainViewName() const { return QStringLiteral("Memos"); }

QIcon MemoBackendPlugin::mainViewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("view-pim-notes"));
}

// --- Conflict presentation ---

void MemoBackendPlugin::enrichConflictSnapshot(
    Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
    bool isSourceSide) const
{
    if (snapshot.content.isEmpty()) return;

    if (isSourceSide) {
        const auto decoded = WildPalms::PalmCodecs::decodeMemo(snapshot.content);
        if (decoded.has_value()) {
            snapshot.content = decoded->text.toUtf8();
        }
    }

    QString text = QString::fromUtf8(snapshot.content).trimmed();
    const int nl = text.indexOf(QChar('\n'));
    QString title = (nl > 0) ? text.left(nl).trimmed() : text;
    if (title.length() > 60) title = title.left(57) + QStringLiteral("...");

    snapshot.metadata[QStringLiteral("title")] = title;
    snapshot.contentType = QStringLiteral("text/plain");
}

QString MemoBackendPlugin::formatConflictRecordHtml(
    const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const
{
    QString html;
    const QString title = snapshot.metadata.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(title.toHtmlEscaped());
    }
    const QString content = QString::fromUtf8(snapshot.content);
    html += QStringLiteral("<pre>%1</pre>").arg(content.toHtmlEscaped());
    return html;
}

} // namespace WildPalms::Memo
```

- [ ] **Step 6.6: Build + run test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memobackendplugin
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_memobackendplugin --output-on-failure
```

Expected: all 6 sub-tests pass.

- [ ] **Step 6.7: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/memo/memobackendplugin.h src/plugins/memo/memobackendplugin.cpp \
        tests/plugins/memo/CMakeLists.txt tests/plugins/memo/tst_memobackendplugin.cpp
git commit -m "$(cat <<'EOF'
feat(memo): MemoBackendPlugin class (Phase E.9)

IBackendPlugin subclass: pluginId=memo, claims MemoDB, createBackends
returns MemoBlobBackend over device->palmBackend(), hasMainView=true
surfaces MemoView. Conflict hooks preserve the old conduit's behaviour:
enrichConflictSnapshot decodes Palm wire bytes on the source side and
extracts a title from the first line; formatConflictRecordHtml renders
<h3>title</h3><pre>body</pre>.

K_PLUGIN_FACTORY wiring + JSON manifest land in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: JSON manifest + `K_PLUGIN_FACTORY`

Goal: package the class into a `.so` discoverable by `BackendPluginManager`.

**Files:**

- Create: `src/plugins/memo/memo-backend-plugin.json`
- Modify: `src/plugins/memo/memobackendplugin.cpp`

- [ ] **Step 7.1: Create the JSON manifest**

`src/plugins/memo/memo-backend-plugin.json`:

```json
{
    "KPlugin": {
        "Id": "memo",
        "Name": "Memo Sync",
        "Description": "Syncs Palm MemoDB to Markdown files.",
        "Icon": "view-pim-notes",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["MemoDB"],
    "X-WildPalms-ClaimDescriptions": {
        "MemoDB": "Syncs MemoDB to Markdown files."
    },
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 10
}
```

- [ ] **Step 7.2: Append `K_PLUGIN_FACTORY` to `memobackendplugin.cpp`**

At the bottom of `src/plugins/memo/memobackendplugin.cpp`, after the closing `} // namespace WildPalms::Memo`:

```cpp
#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(MemoBackendPluginFactory,
                           "memo-backend-plugin.json",
                           registerPlugin<WildPalms::Memo::MemoBackendPlugin>();)

#include "memobackendplugin.moc"
```

- [ ] **Step 7.3: Verify the build expectations**

No tests to run yet — the factory macro is dead code until the plugin target builds the `.so`. That's Task 8. Just confirm the source compiles as part of the test binary from Task 6:

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memobackendplugin
```

Expected: builds clean. The `K_PLUGIN_FACTORY_WITH_JSON` and `memobackendplugin.moc` inclusion compile fine in the test binary context (the factory is just extern "C" glue that's harmless when nobody calls it).

If compilation fails because the test target's AUTOMOC doesn't pick up the nested .moc include, remove the `#include "memobackendplugin.moc"` line from the `.cpp`; AUTOMOC handles it from source. In that case, the test binary still works.

- [ ] **Step 7.4: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/memo/memo-backend-plugin.json src/plugins/memo/memobackendplugin.cpp
git commit -m "$(cat <<'EOF'
feat(memo): JSON manifest + K_PLUGIN_FACTORY (Phase E.9)

Registers MemoBackendPlugin for KPluginFactory discovery under
X-WildPalms-PluginType="backend" so BackendPluginManager picks it
up from wildpalms/plugins/. Manifest drops the legacy
X-WildPalms-ConduitId / ConduitType / PalmCreatorId / RequiresDevice /
RunBefore / RunAfter keys (new ABI subsumes them via virtuals on
IBackendPlugin).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: CMake toggle + plugin `.so` target

Goal: build `wildpalms_memo_v2.so` under `wildpalms/plugins/` when `WILDPALMS_MEMO_PLUGIN_V2=ON` (default), and keep the legacy `wildpalms_memo` conduit building under `wildpalms/conduits/` when it's off.

**Files:**

- Modify: `src/plugins/memo/CMakeLists.txt`

- [ ] **Step 8.1: Back up the current CMakeLists for reference**

```bash
cat /home/clinton/dev/WildPalms/src/plugins/memo/CMakeLists.txt
```

Note the existing target name (`wildpalms_memo`) and `INSTALL_NAMESPACE` for the legacy path — both are reused.

- [ ] **Step 8.2: Replace `src/plugins/memo/CMakeLists.txt`**

```cmake
option(WILDPALMS_MEMO_PLUGIN_V2 "Build the new IBackendPlugin-based Memo plugin" ON)

if (WILDPALMS_MEMO_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_memo_v2
        SOURCES
            memobackendplugin.cpp  memobackendplugin.h
            memoblobbackend.cpp    memoblobbackend.h
            memomarkdown.cpp       memomarkdown.h
            memoview.cpp           memoview.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_memo_v2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(wildpalms_memo_v2
        PRIVATE
            WildPalmsCore
            WildPalmsPalmSync
            WildPalmsPalmCodecs
            WildPalmsPalmCalendar
            KF6::CoreAddons
            KF6::I18n
            KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_memo
        SOURCES
            memoconduit.cpp memoconduit.h
            memomapper.cpp  memomapper.h
            memoview.cpp    memoview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_memo
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
    )
endif ()
```

- [ ] **Step 8.3: Reconfigure + build with toggle ON (default)**

```bash
cmake /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev
cmake --build /home/clinton/dev/WildPalms/build-dev --target wildpalms_memo_v2
ls /home/clinton/dev/WildPalms/build-dev/bin/wildpalms/plugins/wildpalms_memo_v2.so \
 || find /home/clinton/dev/WildPalms/build-dev -name "wildpalms_memo_v2*.so"
```

Expected: `.so` file present under `build-dev/.../wildpalms/plugins/`.

- [ ] **Step 8.4: Verify the full test suite is green**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```

Expected: all previously-passing tests still pass. Memo unit tests (`tst_memomarkdown`, `tst_memoblobbackend`, `tst_memobackendplugin`) all green. No `tst_memo_v2` yet — that's Task 9.

- [ ] **Step 8.5: Verify the OFF path still builds**

```bash
cmake /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev -DWILDPALMS_MEMO_PLUGIN_V2=OFF
cmake --build /home/clinton/dev/WildPalms/build-dev --target wildpalms_memo
find /home/clinton/dev/WildPalms/build-dev -name "wildpalms_memo*.so"
# Flip back to default for subsequent tasks.
cmake /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev -DWILDPALMS_MEMO_PLUGIN_V2=ON
cmake --build /home/clinton/dev/WildPalms/build-dev
```

Expected: legacy `.so` under `wildpalms/conduits/` builds; flipping back to ON restores the V2 target.

- [ ] **Step 8.6: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/memo/CMakeLists.txt
git commit -m "$(cat <<'EOF'
build(memo): WILDPALMS_MEMO_PLUGIN_V2 toggle (Phase E.9)

Default ON: builds wildpalms_memo_v2 under wildpalms/plugins/ (new
IBackendPlugin ABI, discoverable by BackendPluginManager).

OFF: builds legacy wildpalms_memo under wildpalms/conduits/ (old
ISyncConduit ABI, discoverable by ConduitManager). Both paths verified
to compile. The toggle retires in E.16 along with the legacy surface.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: End-to-end `tst_memo_v2`

Goal: the sub-phase's headline test. Load the `.so` via `BackendPluginManager`, wire both ends into `BlobSyncEngine::twoWayWithBaseline`, and assert the three canonical scenarios: fresh sync, modify + resync, delete + resync.

**Files:**

- Modify: `tests/plugins/memo/CMakeLists.txt`
- Create: `tests/plugins/memo/tst_memo_v2.cpp`

- [ ] **Step 9.1: Sketch the file-layout expected by the harness**

Before writing the test, confirm where the V2 plugin `.so` lands:

```bash
find /home/clinton/dev/WildPalms/build-dev -name "wildpalms_memo_v2*.so"
```

Note the parent directory; it's the path we'll add to `QCoreApplication::libraryPaths()`. In this repo the pattern from E.8 is `${CMAKE_BINARY_DIR}/bin/` + `<INSTALL_NAMESPACE>` (i.e. `wildpalms/plugins`).

- [ ] **Step 9.2: Write the failing test**

Create `tests/plugins/memo/tst_memo_v2.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/codecs/memocodec.h"
#include "plugins/memo/memomarkdown.h"
#include "runtime/backendpluginmanager.h"

#include "kalburator/sync/blobsyncengine.h"
#include "kalburator/sync/blobbaselinestore.h"
#include "kalburator/sync/localblobbackend.h"
#include "kalburator/sync/qsynccore/conflicthandlerregistry.h"
#include "kalburator/sync/qsynccore/conflictstore.h"
#include "kalburator/sync/qsynccore/conflictpolicy.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestMemoV2 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSyncCreatesLocalFiles();
    void modifyLocalPropagatesToPalm();
    void deletePalmRemovesLocalFile();
    void idempotentNoopSyncChangesNothing();

private:
    QString m_libraryPath;
    void seedPalmMemos(MockPalmDatabaseAccess *dev) const;
};

void TestMemoV2::initTestCase()
{
    // E.8 convention: the plugin `.so` sits at
    // ${CMAKE_BINARY_DIR}/bin/wildpalms/plugins/. Add the parent of
    // that path so KPluginMetaData::findPlugins("wildpalms/plugins")
    // picks it up.
    const QString buildBin = QStringLiteral(CMAKE_BINARY_DIR "/bin");
    QCoreApplication::addLibraryPath(buildBin);
    m_libraryPath = buildBin;
}

void TestMemoV2::seedPalmMemos(MockPalmDatabaseAccess *dev) const
{
    dev->createDatabase("MemoDB");
    const QStringList bodies = {
        QStringLiteral("first memo"),
        QStringLiteral("second memo"),
        QStringLiteral("third memo"),
    };
    const QList<int> cats = {0, 0, 2};
    for (int i = 0; i < bodies.size(); ++i) {
        PalmRecord pr;
        pr.category = cats[i];
        pr.data = WildPalms::PalmCodecs::encodeMemo({bodies[i], false});
        pr.lastModified = QDateTime::currentDateTimeUtc();
        dev->createRecord("MemoDB", pr);
    }
}

void TestMemoV2::freshSyncCreatesLocalFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms/plugins"));
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(QStringLiteral("memo")));
    auto *plugin = mgr.plugin(QStringLiteral("memo"));
    QVERIFY(plugin != nullptr);

    auto backends = plugin->createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);
    auto *memoBackend = backends.blob;

    Kalburator::Sync::LocalBlobBackend localBackend(tmp.path());
    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("local-memo");
    info.name = QStringLiteral("Local Memos");
    info.type = QStringLiteral("memos");
    localBackend.createCollection(info);

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;

    Kalburator::Sync::BlobSyncEngine engine;
    auto result = engine.twoWayWithBaseline(
        memoBackend, &localBackend,
        QStringLiteral("palm:memo"),
        QStringLiteral("e9-fresh"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY(result.success);

    // Expect three .md files under <tmp>/local-memo/.
    const QDir outDir(tmp.filePath(QStringLiteral("local-memo")));
    const auto entries = outDir.entryList(QStringList() << QStringLiteral("*.md"),
                                          QDir::Files);
    QCOMPARE(entries.size(), 3);

    delete memoBackend;
}

void TestMemoV2::modifyLocalPropagatesToPalm()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms/plugins"));
    mgr.discoverPlugins();
    mgr.loadPlugin(QStringLiteral("memo"));
    auto backends = mgr.plugin(QStringLiteral("memo"))
        ->createBackends(nullptr, &conn);

    Kalburator::Sync::LocalBlobBackend localBackend(tmp.path());
    Kalburator::Sync::CollectionInfo info;
    info.id   = QStringLiteral("local-memo");
    info.name = QStringLiteral("Local Memos");
    info.type = QStringLiteral("memos");
    localBackend.createCollection(info);

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    // First sync — establishes baseline.
    auto r1 = engine.twoWayWithBaseline(
        backends.blob, &localBackend,
        QStringLiteral("palm:memo"), QStringLiteral("e9-mod"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY(r1.success);

    // Rewrite a local file with a modified body.
    const auto localRecs = localBackend.loadRecords(QStringLiteral("local-memo"));
    QVERIFY(localRecs.size() >= 1);

    Kalburator::Sync::BackendRecord mutated = localRecs.first();
    auto md = WildPalms::Memo::decode(QString::fromUtf8(mutated.data));
    md.content.text = QStringLiteral("edited on local side");
    mutated.data = WildPalms::Memo::encode(md).toUtf8();
    QVERIFY(localBackend.updateRecord(mutated));

    // Second sync — change should propagate.
    auto r2 = engine.twoWayWithBaseline(
        backends.blob, &localBackend,
        QStringLiteral("palm:memo"), QStringLiteral("e9-mod"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY(r2.success);

    // Verify the Palm side saw the edit.
    bool sawEdit = false;
    for (const auto &pr : dev.readAllRecords("MemoDB")) {
        const auto decoded = WildPalms::PalmCodecs::decodeMemo(pr.data);
        if (decoded && decoded->text.contains(QStringLiteral("edited on local side"))) {
            sawEdit = true;
            break;
        }
    }
    QVERIFY(sawEdit);

    delete backends.blob;
}

void TestMemoV2::deletePalmRemovesLocalFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms/plugins"));
    mgr.discoverPlugins();
    mgr.loadPlugin(QStringLiteral("memo"));
    auto backends = mgr.plugin(QStringLiteral("memo"))
        ->createBackends(nullptr, &conn);

    Kalburator::Sync::LocalBlobBackend localBackend(tmp.path());
    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("local-memo");
    info.name = QStringLiteral("Local Memos");
    info.type = QStringLiteral("memos");
    localBackend.createCollection(info);

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    engine.twoWayWithBaseline(
        backends.blob, &localBackend,
        QStringLiteral("palm:memo"), QStringLiteral("e9-del"),
        &baseline, &registry, &conflicts, policy);

    // Delete one memo on the Palm side.
    const auto all = dev.readAllRecords("MemoDB");
    QVERIFY(!all.isEmpty());
    QVERIFY(dev.deleteRecord("MemoDB", all.first().recordId));

    // Re-sync; expect the corresponding local file to go away.
    const int before = QDir(tmp.filePath(QStringLiteral("local-memo")))
        .entryList(QStringList() << QStringLiteral("*.md"), QDir::Files).size();
    QCOMPARE(before, 3);

    auto r = engine.twoWayWithBaseline(
        backends.blob, &localBackend,
        QStringLiteral("palm:memo"), QStringLiteral("e9-del"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY(r.success);

    const int after = QDir(tmp.filePath(QStringLiteral("local-memo")))
        .entryList(QStringList() << QStringLiteral("*.md"), QDir::Files).size();
    QCOMPARE(after, 2);

    delete backends.blob;
}

void TestMemoV2::idempotentNoopSyncChangesNothing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms/plugins"));
    mgr.discoverPlugins();
    mgr.loadPlugin(QStringLiteral("memo"));
    auto backends = mgr.plugin(QStringLiteral("memo"))
        ->createBackends(nullptr, &conn);

    Kalburator::Sync::LocalBlobBackend localBackend(tmp.path());
    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("local-memo");
    info.name = QStringLiteral("Local Memos");
    info.type = QStringLiteral("memos");
    localBackend.createCollection(info);

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    auto r1 = engine.twoWayWithBaseline(
        backends.blob, &localBackend,
        QStringLiteral("palm:memo"), QStringLiteral("e9-noop"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY(r1.success);

    auto r2 = engine.twoWayWithBaseline(
        backends.blob, &localBackend,
        QStringLiteral("palm:memo"), QStringLiteral("e9-noop"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY(r2.success);
    QCOMPARE(r2.sourceStats.created, 0);
    QCOMPARE(r2.sourceStats.updated, 0);
    QCOMPARE(r2.sourceStats.deleted, 0);
    QCOMPARE(r2.targetStats.created, 0);
    QCOMPARE(r2.targetStats.updated, 0);
    QCOMPARE(r2.targetStats.deleted, 0);

    delete backends.blob;
}

QTEST_MAIN(TestMemoV2)
#include "tst_memo_v2.moc"
```

- [ ] **Step 9.3: Extend `tests/plugins/memo/CMakeLists.txt`**

Append:

```cmake
add_executable(tst_memo_v2 tst_memo_v2.cpp)
target_compile_definitions(tst_memo_v2
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_include_directories(tst_memo_v2
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_memo_v2
    PRIVATE
        WildPalmsRuntime
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsCore
        Kalburator::Sync
        Qt::Test
        Qt::Core
)
# The test binary needs the memo_v2 .so to exist before it runs.
add_dependencies(tst_memo_v2 wildpalms_memo_v2)
add_test(NAME tst_memo_v2 COMMAND tst_memo_v2)
set_tests_properties(tst_memo_v2 PROPERTIES
    ENVIRONMENT "QT_PLUGIN_PATH=${CMAKE_BINARY_DIR}/bin")
```

Note: the test binary doesn't need the plugin source files compiled into it — it loads the `.so` through `BackendPluginManager`. It pulls in `plugins/memo/memomarkdown.cpp` only because the test itself decodes Markdown for assertion purposes. Include it in the test target sources:

```cmake
target_sources(tst_memo_v2 PRIVATE ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp)
```

- [ ] **Step 9.4: Run test — expect PASS (plugin exists from Task 8)**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_memo_v2
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_memo_v2 --output-on-failure
```

Expected: all four sub-tests pass.

If `BackendPluginManager::loadPlugin("memo")` returns false, inspect:

```bash
QT_DEBUG_PLUGINS=1 /home/clinton/dev/WildPalms/build-dev/tests/plugins/memo/tst_memo_v2 2>&1 | head -80
```

and verify the `.so` filename and metadata. Common fixes:
- `KPluginMetaData::pluginId()` doesn't match `"memo"` — check the manifest's `KPlugin.Id`.
- The `.so` is outside `QT_PLUGIN_PATH` — adjust the `set_tests_properties` path.

- [ ] **Step 9.5: Sanity-check the full `ctest` run**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```

Expected: full suite green.

- [ ] **Step 9.6: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add tests/plugins/memo/CMakeLists.txt tests/plugins/memo/tst_memo_v2.cpp
git commit -m "$(cat <<'EOF'
test(memo): tst_memo_v2 end-to-end plugin round-trip (Phase E.9)

Loads the real wildpalms_memo_v2.so via BackendPluginManager, wires
the plugin-supplied MemoBlobBackend and a LocalBlobBackend into
BlobSyncEngine::twoWayWithBaseline, and verifies:

  1. Fresh sync writes three .md files matching seeded Palm memos.
  2. Local edits propagate back to the Palm side.
  3. Palm-side deletions remove the matching local file (baseline-
     driven deletion propagation).
  4. A no-op re-sync reports zero changes on both sides.

Matches E.9 exit gate: end-to-end smoke of the Memo plugin via the
engine. Coordinator-level coverage lands in E.18.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Main-window `BackendPluginManager` view loop

Goal: surface `MemoView` in the main window when the V2 plugin is built. Old `ConduitManager` view loop keeps running; new loop runs alongside it until E.16.

**Files:**

- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`
- Modify: `src/kf6/CMakeLists.txt` (link `WildPalmsRuntime` if not already)

- [ ] **Step 10.1: Inspect the current wiring**

```bash
grep -n "ConduitManager\|m_conduitManager\|onConduitLoaded\|conduitPages" \
    /home/clinton/dev/WildPalms/src/kf6/kf6mainwindow.{h,cpp}
```

Note the member names; you'll mirror them for `BackendPluginManager`.

- [ ] **Step 10.2: Add a forward decl + member to `kf6mainwindow.h`**

Locate the `class KF6MainWindow` declaration; add next to the existing `ConduitManager *m_conduitManager;`:

```cpp
// Phase E.9 — new-ABI plugin manager. Coexists with ConduitManager
// until E.16 retires the old surface.
namespace WildPalms { class BackendPluginManager; class IBackendPlugin; }

// ... inside KF6MainWindow private section:
WildPalms::BackendPluginManager *m_backendPluginManager = nullptr;
QMap<QString, KPageWidgetItem *> m_backendPluginPages;

private Q_SLOTS:
    void onBackendPluginLoaded(WildPalms::IBackendPlugin *plugin);
    void onBackendPluginUnloading(WildPalms::IBackendPlugin *plugin);
```

- [ ] **Step 10.3: Instantiate + wire the manager in `kf6mainwindow.cpp`**

Find the existing `m_conduitManager = new ConduitManager(this);` block (around line 512). Immediately after the conduit-loading block, add:

```cpp
// Phase E.9: new-ABI plugins discovered from wildpalms/plugins/.
m_backendPluginManager = new WildPalms::BackendPluginManager(
    /*host=*/nullptr, /*device=*/nullptr, /*coordinator=*/nullptr, this);
m_backendPluginManager->discoverPlugins();

connect(m_backendPluginManager, &WildPalms::BackendPluginManager::pluginLoaded,
        this, &KF6MainWindow::onBackendPluginLoaded);
connect(m_backendPluginManager, &WildPalms::BackendPluginManager::pluginUnloading,
        this, &KF6MainWindow::onBackendPluginUnloading);

for (const auto &info : m_backendPluginManager->catalogue()) {
    if (info.defaultEnabled) {
        m_backendPluginManager->loadPlugin(info.metaData.pluginId());
    }
}
```

- [ ] **Step 10.4: Implement the slots**

Append near `onConduitLoaded`:

```cpp
void KF6MainWindow::onBackendPluginLoaded(WildPalms::IBackendPlugin *plugin)
{
    if (!plugin || !plugin->hasMainView()) return;

    QWidget *view = plugin->createMainView(this);
    if (!view) return;
    auto *page = new KPageWidgetItem(view, plugin->mainViewName());
    page->setIcon(plugin->mainViewIcon());
    page->setHeaderVisible(false);
    m_pageWidget->addPage(page);
    m_backendPluginPages[plugin->pluginId()] = page;
}

void KF6MainWindow::onBackendPluginUnloading(WildPalms::IBackendPlugin *plugin)
{
    if (!plugin) return;
    if (auto *page = m_backendPluginPages.take(plugin->pluginId())) {
        m_pageWidget->removePage(page);
        page->deleteLater();
    }
}
```

- [ ] **Step 10.5: Link `WildPalmsRuntime` in the main-window target**

In `src/kf6/CMakeLists.txt` (or wherever the main window target is defined), ensure `WildPalmsRuntime` appears in `target_link_libraries`:

```bash
grep -n "WildPalmsRuntime\|WildPalmsCore" /home/clinton/dev/WildPalms/src/kf6/CMakeLists.txt
```

If missing:

```cmake
target_link_libraries(<main-window-target>
    PRIVATE
        # ... existing ...
        WildPalmsRuntime
)
```

- [ ] **Step 10.6: Add include**

At the top of `kf6mainwindow.cpp`, add:

```cpp
#include "runtime/backendpluginmanager.h"
#include "core/ibackendplugin.h"
```

- [ ] **Step 10.7: Build + manual smoke**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev
```

Expected: builds clean. If there's a Qt/MOC error about `WildPalms::IBackendPlugin`, double-check the forward declaration is in the header, not just the `.cpp`.

Manual smoke (no automation here — the UI is Qt/KDE and this plan is test-focused):

```bash
/home/clinton/dev/WildPalms/build-dev/bin/<main-window-binary>  # name varies
```

In the running app, expect a "Memos" tab in the page widget showing the `MemoView`. Close the app. Nothing to assert programmatically in E.9.

- [ ] **Step 10.8: Rerun ctest (no regressions)**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```

Expected: all green.

- [ ] **Step 10.9: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp src/kf6/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): BackendPluginManager view loop in main window (Phase E.9)

Adds a parallel loading/view-surfacing path alongside ConduitManager.
New-ABI plugins discovered from wildpalms/plugins/ get loaded on
startup; those with hasMainView() contribute KPageWidgetItems to the
main window. Old-path conduits continue to work via ConduitManager
until E.16 retires them.

The new manager is constructed with null host/device/coordinator for
now; runtime wiring (which owns the real PalmDeviceConnection) lands
in E.15/E.17.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Update docs + mark E.9 landed

**Files:**

- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`
- Create: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e9_memo.md`

- [ ] **Step 11.1: Flip E.9 to landed in the parent spec**

In `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`, locate the line:

```
| **E.9** | Rewrite **Memo** as `IBackendPlugin`. CMake toggle: old memo off, new memo on. Existing memo tests ported to new shape. | WP | E.8 | `ctest tst_memo_v2` passes; end-to-end smoke of Memo plugin via coordinator. |
```

Replace with:

```
| ✅ **E.9** | Memo rewritten as `IBackendPlugin` (`MemoBackendPlugin` + `MemoBlobBackend` + `MemoMarkdown`). `PalmDeviceConnection` concrete. `IBackendPlugin` gained view + conflict hooks. CMake toggle `WILDPALMS_MEMO_PLUGIN_V2=ON` swaps the new plugin in at `wildpalms/plugins/`; legacy `MemoConduit` remains at `wildpalms/conduits/` until E.16. Landed 2026-04-23. Plan: `docs/superpowers/plans/2026-04-23-phase-e9-memo-plugin.md`. | WP | E.8 | WP ctest passes; `tst_memo_v2` covers the full round-trip; coordinator-level coverage deferred to E.18. |
```

- [ ] **Step 11.2: Mark E.9 landed in the integration plan**

In `docs/plans/2026-04-20-libkalburator-integration.md`, find the Phase E sub-phases table (same pattern as 4ae8099 for E.7 and 170535b for E.8). Flip the E.9 row marker and add a brief "Landed 2026-04-23" note.

- [ ] **Step 11.3: Update `MEMORY.md`**

Add a line to `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`:

```
- [project_phase_e9_memo.md](project_phase_e9_memo.md) — E.9 landed 2026-04-23; Memo is the first new-ABI plugin; toggle + deferrals recorded
```

- [ ] **Step 11.4: Create the memory note**

`/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e9_memo.md`:

```markdown
---
name: Phase E.9 Memo plugin landed
description: Memo is the first plugin on the new IBackendPlugin ABI; status of toggle + remaining E.9 follow-ups
type: project
---

Phase E.9 landed 2026-04-23. Memo is the first plugin on the new
IBackendPlugin ABI.

**Why:** Proves the ABI on a boring record type before Calendar (E.10)
layers in typed complexity.

**How to apply:** For future plugin rewrites (E.10-E.14), mirror
MemoBackendPlugin + MemoBlobBackend: wrapper backend over the shared
PalmBackend (reached via PalmDeviceConnection::palmBackend), plugin
provides only its own transcoding view. Do NOT push transcoding into
PalmBackend itself — it stays byte-passing.

**Status of artifacts:**
- Spec: `docs/superpowers/specs/2026-04-23-phase-e9-memo-plugin-design.md`
- Plan: `docs/superpowers/plans/2026-04-23-phase-e9-memo-plugin.md`
- Toggle: `WILDPALMS_MEMO_PLUGIN_V2` (default ON). Legacy
  `MemoConduit` still builds when OFF; both paths exercised in CI.

**E.9 deferred items (still open):**
- `ConflictDialog` lookup path — today calls ConduitManager; needs a
  fall-through to BackendPluginManager by backend id. Interactive
  memo conflicts are a known regression until this lands. Policy-
  driven resolution (PalmConflictHandler) still works.
- `CategoryMappingStore` rename/move to `src/palm/` — waits until
  contacts/todos (E.11/E.12) also use it.
- Main-window new-plugin loop runs alongside old ConduitManager loop
  until E.16.
- `SyncCoordinator`-level e2e test — E.18.
```

- [ ] **Step 11.5: Run the full test suite one more time**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```

Expected: every test green, including `tst_memo_v2`.

- [ ] **Step 11.6: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md \
        docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "$(cat <<'EOF'
docs(phase-e): flip E.9 to landed; note remaining follow-ups

Memo rewritten to the new IBackendPlugin ABI. Spec and integration
plan mark E.9 as landed and point at the implementation plan.
Follow-ups (ConflictDialog lookup, CategoryMappingStore relocation,
runtime wiring) tracked as deferrals.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

# Memory files live outside the repo — commit them with:
cd /home/clinton/.claude/projects/-home-clinton-dev-WildPalms
git add memory/MEMORY.md memory/project_phase_e9_memo.md 2>/dev/null || true
```

(The memory directory may or may not be a git repo depending on the user's setup; skip the final `cd` step if it errors.)

---

## Self-review checklist (for the implementer)

When all eleven tasks are complete, verify:

- [ ] `ctest --test-dir build-dev --output-on-failure` is entirely green.
- [ ] `wildpalms_memo_v2.so` is present under `build-dev/bin/wildpalms/plugins/`.
- [ ] Toggling `-DWILDPALMS_MEMO_PLUGIN_V2=OFF` still builds and produces
      `wildpalms_memo.so` under `build-dev/bin/wildpalms/conduits/`.
- [ ] `tst_memo_v2` loads the real `.so` via `BackendPluginManager` and
      all four sub-tests pass (fresh, modify, delete, no-op).
- [ ] The E.8 dummy-backend fixture (`tst_plugin_factory_roundtrip`) is
      still green — proves the `IBackendPlugin` header change is
      backward-compatible for existing plugins via its defaults.
- [ ] `PalmBackend::updatePalmRecord` exists and is exercised by
      `tst_palmbackend_roundtrip::updatePalmRecordPreservesCategory`.
- [ ] `PalmDeviceConnection` is concrete, owns a `PalmBackend`, and is
      covered by `tst_palmdeviceconnection`.
- [ ] Parent spec (`docs/superpowers/specs/2026-04-21-...`) shows row
      E.9 with the ✅ marker.
- [ ] Integration plan (`docs/plans/2026-04-20-...`) shows E.9 landed.
- [ ] No stray compilation of the old `MemoConduit` class when the
      toggle is ON (it's just sitting in tree; its sources aren't in
      any target's source list on the ON path).
