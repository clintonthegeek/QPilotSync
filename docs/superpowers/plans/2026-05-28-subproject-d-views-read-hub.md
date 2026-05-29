# Sub-project D — Views read the hub — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the four PIM views (Calendar, Contacts, Memo, ToDo) to read records from the canonical hub (`"wp-hub"`) via per-domain `HubFooReader` facades instead of scanning `<sync>/rawfiles/<domain>/<col>/*.<ext>`, and surface Contacts in the V2 sidebar for the first time.

**Architecture:** Per-domain `HubFooReader` facades live inside each PIM plugin and wrap the borrowed `wp-hub` `Kalburator::Sync::SyncBackend*` for that plugin's collection id. Each plugin inherits a new WP-local `WildPalms::Plugins::PimPlugin` base (the seam for `setHub` / `setRuntime`, which are not editable on `Kalburator::Plugin`). `PalmRuntime` dispatches `setHub` to every `PimPlugin` after creating the hub, and emits `syncCompleted` after every sync run to drive view refresh. Views ship read-only in D; New/Save/Delete affordances are hidden until E.

**Tech Stack:** Qt6, KF6, libkalburator v0.60 (FetchContent-pinned), C++17, CMake.

**Spec:** `docs/superpowers/specs/2026-05-28-subproject-d-views-read-hub-design.md`

---

## File map

### New files (15)

| Path | Purpose |
|---|---|
| `src/plugins/pimplugin.h` | `WildPalms::Plugins::PimPlugin` intermediate base |
| `src/plugins/calendar/hubcalendarreader.{h,cpp}` | Hub-record facade for `palm:calendar` |
| `src/plugins/contacts/hubcontactsreader.{h,cpp}` | Hub-record facade for `palm:contacts` |
| `src/plugins/memo/hubmemoreader.{h,cpp}` | Hub-record facade for `palm:memo` |
| `src/plugins/todos/hubtodoreader.{h,cpp}` | Hub-record facade for `palm:todo` |
| `tests/plugins/calendar/tst_hubcalendarreader.cpp` | Reader unit test |
| `tests/plugins/contacts/tst_hubcontactsreader.cpp` | Reader unit test |
| `tests/plugins/memo/tst_hubmemoreader.cpp` | Reader unit test |
| `tests/plugins/todos/tst_hubtodoreader.cpp` | Reader unit test |
| `tests/plugins/calendar/tst_calendar_view_reads_hub.cpp` | View integration test |
| `tests/plugins/contacts/tst_contact_view_reads_hub.cpp` | View integration test |
| `tests/plugins/memo/tst_memo_view_reads_hub.cpp` | View integration test |
| `tests/plugins/todos/tst_task_view_reads_hub.cpp` | View integration test |
| `tests/runtime/tst_palm_runtime_emits_sync_completed.cpp` | Runtime signal test |
| `tests/plugins/contacts/tst_contacts_in_sidebar.cpp` | Sidebar registration test |

### Modified files

| Path | Change |
|---|---|
| `src/runtime/palmruntime.{h,cpp}` | Add `syncCompleted` signal + emission + PimPlugin dispatch |
| `src/plugins/calendar/calendarbackendplugin.{h,cpp}` | Base → `PimPlugin`; `setHub`/`setRuntime` overrides; `createMainView` wiring |
| `src/plugins/contacts/contactsbackendplugin.{h,cpp}` | Same + flip `hasMainView()` to `true`; add `createMainView`/`mainViewName`/`mainViewIcon` |
| `src/plugins/memo/memobackendplugin.{h,cpp}` | Base → `PimPlugin`; `setHub`/`setRuntime`; `createMainView` wiring |
| `src/plugins/todos/todobackendplugin.{h,cpp}` | Same |
| `src/plugins/calendar/calendarview.{h,cpp}` | Add `setHubReader` + reader-driven `loadEvents`; hide edit affordances (none currently; keep) |
| `src/plugins/contacts/contactview.{h,cpp}` | Add `setHubReader` + reader-driven `loadContacts` |
| `src/plugins/memo/memoview.{h,cpp}` | Add `setHubReader` + reader-driven `loadMemos`; hide New/Save/Delete + memo-category combo |
| `src/plugins/todos/taskview.{h,cpp}` | Add `setHubReader` + reader-driven `loadTasks`; hide New/Delete/ToggleComplete |
| `src/plugins/CMakeLists.txt` | Add `pimplugin.h` to install set (header-only) |
| `src/plugins/<domain>/CMakeLists.txt` (×4) | Add `hubfooreader.cpp` to plugin lib |
| `tests/plugins/<domain>/CMakeLists.txt` (×4) | Register new test targets |
| `tests/runtime/CMakeLists.txt` | Register `tst_palm_runtime_emits_sync_completed` |

---

## Task 1 — WP `PimPlugin` base class

**Files:**
- Create: `src/plugins/pimplugin.h`
- Modify: `src/plugins/CMakeLists.txt` (add header install)

- [ ] **Step 1: Create the header**

```cpp
// src/plugins/pimplugin.h
#ifndef WILDPALMS_PLUGINS_PIMPLUGIN_H
#define WILDPALMS_PLUGINS_PIMPLUGIN_H

#include "plugin.h"   // Kalburator::Plugin (libkalburator)

namespace Kalburator::Sync { class SyncBackend; }
namespace WildPalms::Runtime { class PalmRuntime; }

namespace WildPalms::Plugins {

/**
 * @brief WP-local intermediate base for the four PIM-view plugins
 *        (Calendar, Contacts, Memo, ToDo).
 *
 * Adds two non-pure-virtual lifecycle hooks that PalmRuntime calls
 * after constructing the canonical hub. PalmRuntime dispatches via
 * dynamic_cast<PimPlugin*>; non-PIM plugins (e.g. plucker) inherit
 * Kalburator::Plugin directly and are skipped by the cast.
 *
 * The defaults are no-ops so a PIM plugin can override only what it
 * needs.
 */
class PimPlugin : public Kalburator::Plugin {
public:
    /// Called once per PalmRuntime instance, after m_hub is constructed
    /// and registered. The plugin builds any HubFooReader it needs.
    /// hub is borrowed; lifetime is the PalmRuntime's.
    virtual void setHub(Kalburator::Sync::SyncBackend *hub) { Q_UNUSED(hub); }

    /// Called once per PalmRuntime instance, alongside setHub. The
    /// plugin caches the pointer so createMainView can connect
    /// runtime->syncCompleted to view->refresh.
    virtual void setRuntime(WildPalms::Runtime::PalmRuntime *runtime) {
        Q_UNUSED(runtime);
    }
};

} // namespace WildPalms::Plugins

#endif // WILDPALMS_PLUGINS_PIMPLUGIN_H
```

- [ ] **Step 2: Verify the header compiles**

Run: `cmake --build build-dev --target WildPalmsCore -- -j$(nproc) 2>&1 | tail -20`
Expected: existing target still builds (header isn't linked anywhere yet).

- [ ] **Step 3: Commit**

```bash
git add src/plugins/pimplugin.h
git commit -m "feat(plugins): add WP-local PimPlugin base for PIM-view plugins

Intermediate base between Kalburator::Plugin (libkalburator,
not editable from WP) and the four PIM-view plugins. Provides
two non-pure-virtual lifecycle hooks (setHub, setRuntime) that
PalmRuntime will call after constructing the canonical hub.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2 — Four `HubFooReader` facades + unit tests

**Files:**
- Create: `src/plugins/calendar/hubcalendarreader.{h,cpp}`
- Create: `src/plugins/contacts/hubcontactsreader.{h,cpp}`
- Create: `src/plugins/memo/hubmemoreader.{h,cpp}`
- Create: `src/plugins/todos/hubtodoreader.{h,cpp}`
- Create: `tests/plugins/calendar/tst_hubcalendarreader.cpp`
- Create: `tests/plugins/contacts/tst_hubcontactsreader.cpp`
- Create: `tests/plugins/memo/tst_hubmemoreader.cpp`
- Create: `tests/plugins/todos/tst_hubtodoreader.cpp`
- Modify: `src/plugins/calendar/CMakeLists.txt` / `contacts/.../CMakeLists.txt` etc.
- Modify: `tests/plugins/<domain>/CMakeLists.txt` (×4)

The four readers are mechanically identical; only the namespace, class name, and default
collection id differ. Each step shows Calendar; repeat verbatim for the other three
domains substituting `Contacts/Memo/Todo` and the matching collection id.

- [ ] **Step 1: Write the failing reader test (calendar)**

```cpp
// tests/plugins/calendar/tst_hubcalendarreader.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "plugins/calendar/hubcalendarreader.h"
#include <genericsqlitebackend.h>
#include <backendrecord.h>

using WildPalms::CalendarPlugin::HubCalendarReader;

class TstHubCalendarReader : public QObject
{
    Q_OBJECT
private slots:
    void listsSeededIds();
    void returnsSeededBytesVerbatim();
    void missingIdYieldsEmptyBytes();
    void emptyCollectionYieldsEmptyList();
};

namespace {
std::unique_ptr<Kalburator::Sinks::GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<Kalburator::Sinks::GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("palm:calendar");
    info.displayName = QStringLiteral("Calendar");
    info.type = QStringLiteral("calendar");
    hub->createCollection(info);
    return hub;
}

Kalburator::Sync::BackendRecord makeRecord(const QString &id, const QByteArray &bytes)
{
    Kalburator::Sync::BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("event");
    r.data = bytes;
    return r;
}
}

void TstHubCalendarReader::listsSeededIds()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    hub->createRecord(QStringLiteral("palm:calendar"),
                      makeRecord(QStringLiteral("evt-1"), "BEGIN:VEVENT\nEND:VEVENT\n"));
    hub->createRecord(QStringLiteral("palm:calendar"),
                      makeRecord(QStringLiteral("evt-2"), "BEGIN:VEVENT\nEND:VEVENT\n"));

    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    const QStringList ids = reader.listRecordIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("evt-1")));
    QVERIFY(ids.contains(QStringLiteral("evt-2")));
}

void TstHubCalendarReader::returnsSeededBytesVerbatim()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    const QByteArray payload = "BEGIN:VEVENT\nSUMMARY:Hello\nEND:VEVENT\n";
    hub->createRecord(QStringLiteral("palm:calendar"),
                      makeRecord(QStringLiteral("evt-1"), payload));

    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    QCOMPARE(reader.recordBytes(QStringLiteral("evt-1")), payload);
}

void TstHubCalendarReader::missingIdYieldsEmptyBytes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    QVERIFY(reader.recordBytes(QStringLiteral("no-such-id")).isEmpty());
}

void TstHubCalendarReader::emptyCollectionYieldsEmptyList()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    QVERIFY(reader.listRecordIds().isEmpty());
}

QTEST_MAIN(TstHubCalendarReader)
#include "tst_hubcalendarreader.moc"
```

- [ ] **Step 2: Verify the test fails to compile (reader doesn't exist)**

Add the test to `tests/plugins/calendar/CMakeLists.txt`:

```cmake
add_executable(tst_hubcalendarreader
    tst_hubcalendarreader.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/calendar/hubcalendarreader.cpp
)
target_include_directories(tst_hubcalendarreader
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_hubcalendarreader
    PRIVATE Qt::Test Qt::Core Kalburator::Kalburator)
add_test(NAME tst_hubcalendarreader COMMAND tst_hubcalendarreader)
set_tests_properties(tst_hubcalendarreader PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Run: `cmake --build build-dev --target tst_hubcalendarreader 2>&1 | tail -10`
Expected: compile error — `hubcalendarreader.h` not found.

- [ ] **Step 3: Write the reader header**

```cpp
// src/plugins/calendar/hubcalendarreader.h
#ifndef WILDPALMS_CALENDAR_HUBCALENDARREADER_H
#define WILDPALMS_CALENDAR_HUBCALENDARREADER_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync { class SyncBackend; }

namespace WildPalms::CalendarPlugin {

/**
 * @brief Thin per-domain facade over the canonical hub
 *        ("wp-hub" GenericSqliteBackend, collection "palm:calendar").
 *
 * Reads-only. The view (CalendarView) talks to this facade — never
 * to Kalburator::Sync::SyncBackend directly. The reader owns no Qt
 * signals; refresh is driven by PalmRuntime::syncCompleted.
 *
 * Lifetime: the hub pointer is borrowed and outlives the reader.
 * The reader is owned by CalendarBackendPlugin; the plugin outlives
 * the view that borrows the reader pointer.
 */
class HubCalendarReader {
public:
    HubCalendarReader(Kalburator::Sync::SyncBackend *hub,
                      QString collectionId);

    QStringList listRecordIds() const;
    QByteArray  recordBytes(const QString &id) const;
    QString     collectionId() const { return m_collectionId; }

private:
    Kalburator::Sync::SyncBackend *m_hub;
    QString m_collectionId;
};

} // namespace WildPalms::CalendarPlugin

#endif
```

- [ ] **Step 4: Write the reader implementation**

```cpp
// src/plugins/calendar/hubcalendarreader.cpp
#include "hubcalendarreader.h"

#include <syncbackendbase.h>
#include <backendrecord.h>

#include <QDebug>

namespace WildPalms::CalendarPlugin {

HubCalendarReader::HubCalendarReader(Kalburator::Sync::SyncBackend *hub,
                                     QString collectionId)
    : m_hub(hub)
    , m_collectionId(std::move(collectionId))
{
    Q_ASSERT(m_hub);
}

QStringList HubCalendarReader::listRecordIds() const
{
    if (!m_hub) return {};
    QStringList ids;
    const auto records = m_hub->loadRecords(m_collectionId);
    ids.reserve(records.size());
    for (const auto &r : records) {
        if (!r.isDeleted) ids.append(r.id);
    }
    return ids;
}

QByteArray HubCalendarReader::recordBytes(const QString &id) const
{
    if (!m_hub) return {};
    const auto records = m_hub->loadRecords(m_collectionId);
    for (const auto &r : records) {
        if (r.id == id && !r.isDeleted) return r.data;
    }
    return {};
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 5: Run the reader test, verify it passes**

Run: `cmake --build build-dev --target tst_hubcalendarreader && ctest --test-dir build-dev -R tst_hubcalendarreader --output-on-failure`
Expected: 4/4 sub-tests pass.

- [ ] **Step 6: Repeat steps 1-5 for Contacts, Memo, ToDo**

Per-domain substitutions:

| Field | Calendar | Contacts | Memo | ToDo |
|---|---|---|---|---|
| Namespace | `WildPalms::CalendarPlugin` | `WildPalms::ContactsPlugin` | `WildPalms::MemoPlugin` | `WildPalms::TodoPlugin` |
| Class | `HubCalendarReader` | `HubContactsReader` | `HubMemoReader` | `HubTodoReader` |
| Collection id | `palm:calendar` | `palm:contacts` | `palm:memo` | `palm:todo` |
| Header guard | `..._HUBCALENDARREADER_H` | `..._HUBCONTACTSREADER_H` | `..._HUBMEMOREADER_H` | `..._HUBTODOREADER_H` |
| CollectionInfo.type | `calendar` | `contacts` | `note` | `todo` |
| Test record `type` field | `event` | `contact` | `memo` | `todo` |
| Test seed bytes | `BEGIN:VEVENT\nEND:VEVENT\n` | `BEGIN:VCARD\nVERSION:4.0\nFN:Test\nEND:VCARD\n` | `# title\n\nbody\n` | `BEGIN:VTODO\nEND:VTODO\n` |

For each domain:
- Test file under `tests/plugins/<domain>/tst_hub<foo>reader.cpp`
- Reader files under `src/plugins/<domain>/hub<foo>reader.{h,cpp}`
- CMake registration matching the calendar pattern in `tests/plugins/<domain>/CMakeLists.txt`

- [ ] **Step 7: Run all four reader tests**

Run: `ctest --test-dir build-dev -R "tst_hub.*reader" --output-on-failure`
Expected: 4 test binaries, 16 sub-tests total, all pass.

- [ ] **Step 8: Commit**

```bash
git add src/plugins/calendar/hubcalendarreader.{h,cpp} \
        src/plugins/contacts/hubcontactsreader.{h,cpp} \
        src/plugins/memo/hubmemoreader.{h,cpp} \
        src/plugins/todos/hubtodoreader.{h,cpp} \
        tests/plugins/calendar/tst_hubcalendarreader.cpp \
        tests/plugins/contacts/tst_hubcontactsreader.cpp \
        tests/plugins/memo/tst_hubmemoreader.cpp \
        tests/plugins/todos/tst_hubtodoreader.cpp \
        src/plugins/*/CMakeLists.txt \
        tests/plugins/*/CMakeLists.txt
git commit -m "feat(plugins): per-domain HubFooReader facades + unit tests

Four read-only facades (HubCalendarReader / HubContactsReader /
HubMemoReader / HubTodoReader) wrap the borrowed wp-hub
SyncBackend* and expose listRecordIds() + recordBytes(id) to the
view layer. Each is ~50 LOC. 16 sub-tests pin record-id
enumeration, byte fidelity, missing-id, and empty-collection
behavior.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3 — `PalmRuntime::syncCompleted` signal + emission

**Files:**
- Modify: `src/runtime/palmruntime.h` (add signal declaration)
- Modify: `src/runtime/palmruntime.cpp` (emit alongside `runFinished`)
- Create: `tests/runtime/tst_palm_runtime_emits_sync_completed.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/runtime/tst_palm_runtime_emits_sync_completed.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

class TstPalmRuntimeEmitsSyncCompleted : public QObject
{
    Q_OBJECT
private slots:
    void emitsOnceAfterHotSync();
};

void TstPalmRuntimeEmitsSyncCompleted::emitsOnceAfterHotSync()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    auto device = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    rt.setDeviceAccessForTest(std::move(device));

    QSignalSpy spy(&rt, &WildPalms::Runtime::PalmRuntime::syncCompleted);

    auto fut = rt.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 10000);

    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TstPalmRuntimeEmitsSyncCompleted)
#include "tst_palm_runtime_emits_sync_completed.moc"
```

- [ ] **Step 2: Register in `tests/runtime/CMakeLists.txt`**

Append after the `add_wildpalms_runtime_test(tst_pluginactionmanager …)` line:

```cmake
add_wildpalms_runtime_test(tst_palm_runtime_emits_sync_completed
    tst_palm_runtime_emits_sync_completed.cpp)
target_link_libraries(tst_palm_runtime_emits_sync_completed PRIVATE
    WildPalmsPalmDevice
    pisock)
```

(The extra link is required because the test pulls in `PalmDeviceAccess` —
matches `tst_palm_runtime_modes`'s link set.)

- [ ] **Step 3: Run the test, verify it fails**

Run: `cmake --build build-dev --target tst_palm_runtime_emits_sync_completed 2>&1 | tail -10`
Expected: compile error — `syncCompleted` is not a member of `PalmRuntime`.

- [ ] **Step 4: Add the signal declaration**

In `src/runtime/palmruntime.h`, in the `signals:` section (after the existing `conflictDetected` signal at line 214):

```cpp
    /// Emitted after every sync run (hotSync / fullSync /
    /// copyPalmToPC / copyPCToPalm / backup / restore). Sub-project D
    /// views connect to this in createMainView to drive
    /// view->refresh(). Fires once per QFuture returned by the public
    /// sync methods.
    void syncCompleted();
```

- [ ] **Step 5: Emit the signal at sync-finish**

In `src/runtime/palmruntime.cpp`, find every spot where `runFinished(...)` is emitted (there is one central path through `QFutureWatcher::finished` in `runAllMappings()` / `runMirror()` — search for `emit runFinished`). For each `emit runFinished(...)` line, add immediately after:

```cpp
    emit syncCompleted();
```

If `runFinished` is emitted from a single helper (likely the `runAllMappings()` finished-lambda), one addition suffices. The signal is sender-thread; views on the GUI thread receive via `Qt::AutoConnection`.

- [ ] **Step 6: Run the test, verify it passes**

Run: `cmake --build build-dev --target tst_palm_runtime_emits_sync_completed && ctest --test-dir build-dev -R tst_palm_runtime_emits_sync_completed --output-on-failure`
Expected: 1/1 passes.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        tests/runtime/tst_palm_runtime_emits_sync_completed.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "feat(runtime): PalmRuntime::syncCompleted signal

Fires once per sync run alongside runFinished. PIM-view plugins
will connect this to view->refresh() in createMainView (sub-project
D Task 5).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4 — `PalmRuntime` dispatches `setHub` / `setRuntime` to PIM plugins

**Files:**
- Modify: `src/runtime/palmruntime.cpp` (add dispatch loop after hub creation)
- Modify: `src/runtime/palmruntime.h` (include for `PimPlugin`)

Because no plugin inherits `PimPlugin` until Task 5, this task lands the
dispatch logic but its observable effect is exercised by Task 5's tests.
The dispatch is a small, safe no-op until consumers exist.

- [ ] **Step 1: Add the dispatch loop**

In `src/runtime/palmruntime.cpp`, after the `ensureHubCollections();` call (currently line 243) and immediately before the `QObject::connect(this, &PalmRuntime::runStarted, ...)` block (currently line 245), insert:

```cpp
    // Sub-project D: dispatch setHub/setRuntime to every PimPlugin.
    // Non-PIM plugins (plucker) inherit Kalburator::Plugin directly
    // and dynamic_cast to nullptr — they are skipped naturally.
    for (auto &p : m_palmPlugins) {
        if (auto *pim = dynamic_cast<WildPalms::Plugins::PimPlugin*>(p.get())) {
            pim->setHub(m_hub.get());
            pim->setRuntime(this);
        }
    }
```

- [ ] **Step 2: Add the include**

In `src/runtime/palmruntime.cpp`, with the other plugin includes (search for `#include "plugins/`), add:

```cpp
#include "plugins/pimplugin.h"
```

- [ ] **Step 3: Verify the runtime still builds**

Run: `cmake --build build-dev --target WildPalmsRuntime 2>&1 | tail -10`
Expected: builds clean.

- [ ] **Step 4: Run the full runtime test suite**

Run: `ctest --test-dir build-dev -R "tst_palm_runtime" --output-on-failure`
Expected: all existing palm_runtime tests still pass (Task 4 is a no-op until plugins inherit PimPlugin).

- [ ] **Step 5: Commit**

```bash
git add src/runtime/palmruntime.cpp
git commit -m "feat(runtime): dispatch setHub/setRuntime to PimPlugins

Iterates m_palmPlugins after hub creation, dynamic_casts each to
WildPalms::Plugins::PimPlugin, and calls setHub + setRuntime on
the cast. Non-PIM plugins (plucker) are skipped naturally.

Currently a no-op — Task 5 switches the four PIM plugins onto
PimPlugin, which gives this dispatch its observable consumers.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5 — PIM plugins inherit `PimPlugin` + `createMainView` wiring + Contacts sidebar flip

**Files:**
- Modify: `src/plugins/calendar/calendarbackendplugin.{h,cpp}`
- Modify: `src/plugins/contacts/contactsbackendplugin.{h,cpp}` (also adds `createMainView`/`mainViewName`/`mainViewIcon` + flips `hasMainView` to `true`)
- Modify: `src/plugins/memo/memobackendplugin.{h,cpp}`
- Modify: `src/plugins/todos/todobackendplugin.{h,cpp}`
- Create: `tests/plugins/contacts/tst_contacts_in_sidebar.cpp`
- Modify: `tests/plugins/contacts/CMakeLists.txt`

The four plugins receive the same five-part change. Calendar is shown
fully; the table at the end of Step 4 lists per-plugin substitutions.

- [ ] **Step 1: Write the failing Contacts sidebar test**

```cpp
// tests/plugins/contacts/tst_contacts_in_sidebar.cpp
#include <QtTest/QtTest>
#include "plugins/contacts/contactsbackendplugin.h"

class TstContactsInSidebar : public QObject
{
    Q_OBJECT
private slots:
    void hasMainViewIsTrue();
};

void TstContactsInSidebar::hasMainViewIsTrue()
{
    WildPalms::ContactsPlugin::ContactsBackendPlugin plugin;
    QVERIFY(plugin.hasMainView());
}

QTEST_MAIN(TstContactsInSidebar)
#include "tst_contacts_in_sidebar.moc"
```

Register in `tests/plugins/contacts/CMakeLists.txt`:

```cmake
add_executable(tst_contacts_in_sidebar
    tst_contacts_in_sidebar.cpp
)
target_include_directories(tst_contacts_in_sidebar
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_contacts_in_sidebar PRIVATE
    Qt::Test Qt::Core
    WildPalmsContactsPlugin)   # the plugin static lib
add_test(NAME tst_contacts_in_sidebar COMMAND tst_contacts_in_sidebar)
set_tests_properties(tst_contacts_in_sidebar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

(Match the actual static-lib target name used elsewhere in
`tests/plugins/contacts/CMakeLists.txt` — verify by reading that file first.)

Run: `cmake --build build-dev --target tst_contacts_in_sidebar && ctest --test-dir build-dev -R tst_contacts_in_sidebar --output-on-failure`
Expected: FAIL (`hasMainView()` returns `false`).

- [ ] **Step 2: Switch Calendar plugin to `PimPlugin` base + add overrides**

In `src/plugins/calendar/calendarbackendplugin.h`:

Replace `#include "plugin.h"` with:

```cpp
#include "plugins/pimplugin.h"
```

Change the class declaration:

```cpp
class CalendarBackendPlugin : public WildPalms::Plugins::PimPlugin
```

In the `public:` section, add the override declarations and a private reader field. Add after the `categoryStore()` declaration:

```cpp
    // Sub-project D: PimPlugin lifecycle hooks.
    void setHub(Kalburator::Sync::SyncBackend *hub) override;
    void setRuntime(WildPalms::Runtime::PalmRuntime *runtime) override;
```

In `private:`, after the existing `m_device` field:

```cpp
    std::unique_ptr<WildPalms::CalendarPlugin::HubCalendarReader> m_hubReader;
    WildPalms::Runtime::PalmRuntime *m_runtime = nullptr;  // borrowed
```

Add the forward declaration near the top with the other `namespace ... { class ... }` lines:

```cpp
namespace WildPalms::CalendarPlugin { class HubCalendarReader; }
namespace WildPalms::Runtime { class PalmRuntime; }
```

- [ ] **Step 3: Implement the overrides + wire `createMainView`**

In `src/plugins/calendar/calendarbackendplugin.cpp`:

Add includes at the top:

```cpp
#include "hubcalendarreader.h"
#include "runtime/palmruntime.h"
```

Add the override implementations (anywhere in the class body — e.g. after `createConflictHandler`):

```cpp
void CalendarBackendPlugin::setHub(Kalburator::Sync::SyncBackend *hub)
{
    Q_ASSERT(hub);
    m_hubReader = std::make_unique<WildPalms::CalendarPlugin::HubCalendarReader>(
        hub, QStringLiteral("palm:calendar"));
}

void CalendarBackendPlugin::setRuntime(WildPalms::Runtime::PalmRuntime *runtime)
{
    m_runtime = runtime;
}
```

Modify `createMainView` (currently `return new CalendarView(parent);`):

```cpp
QWidget *CalendarBackendPlugin::createMainView(QWidget *parent) const
{
    auto *v = new CalendarView(parent);
    v->setHubReader(m_hubReader.get());
    if (m_runtime) {
        QObject::connect(m_runtime,
                         &WildPalms::Runtime::PalmRuntime::syncCompleted,
                         v, &CalendarView::refresh);
    }
    return v;
}
```

(`setHubReader` lands in Task 6; this code references it before that
task to keep all plugin-side changes coherent in Task 5. The plugin's
`.cpp` won't compile until Task 6 lands the view-side declaration —
move Step 8 [build verification] to after Task 6, or land Step 2-8
as a single squashed commit at the end of Task 5/6. The plan
sequences this as a fence: Tasks 5 and 6 must commit together, see
Task 6 Step 8.)

- [ ] **Step 4: Repeat Steps 2-3 for the other three plugins**

Per-plugin substitutions:

| Field | Calendar | Contacts | Memo | ToDo |
|---|---|---|---|---|
| Plugin file stem | `calendarbackendplugin` | `contactsbackendplugin` | `memobackendplugin` | `todobackendplugin` |
| Plugin class | `CalendarBackendPlugin` | `ContactsBackendPlugin` | `MemoPlugin` | `TodoBackendPlugin` |
| Reader class | `HubCalendarReader` | `HubContactsReader` | `HubMemoReader` | `HubTodoReader` |
| Reader header | `hubcalendarreader.h` | `hubcontactsreader.h` | `hubmemoreader.h` | `hubtodoreader.h` |
| Reader ns | `WildPalms::CalendarPlugin` | `WildPalms::ContactsPlugin` | `WildPalms::MemoPlugin` | `WildPalms::TodoPlugin` |
| Hub collection id | `palm:calendar` | `palm:contacts` | `palm:memo` | `palm:todo` |
| View class | `CalendarView` | `ContactView` | `MemoView` | `TaskView` |

- [ ] **Step 5: Contacts — additional changes**

The contacts plugin lacks `createMainView` / `mainViewName` / `mainViewIcon` (today its `hasMainView()` returns `false`). Add to `contactsbackendplugin.h`:

```cpp
    // Sub-project D: contacts now has a main view.
    bool     hasMainView()   const { return true; }
    QWidget *createMainView(QWidget *parent) const;
    QString  mainViewName()  const { return QStringLiteral("Contacts"); }
    QIcon    mainViewIcon()  const;
```

Remove the existing `bool hasMainView() const { return false; }`.

In `contactsbackendplugin.cpp`, add:

```cpp
#include "contactview.h"
#include "hubcontactsreader.h"
#include "runtime/palmruntime.h"
#include <QIcon>

QWidget *ContactsBackendPlugin::createMainView(QWidget *parent) const
{
    auto *v = new ContactView(parent);
    v->setHubReader(m_hubReader.get());
    if (m_runtime) {
        QObject::connect(m_runtime,
                         &WildPalms::Runtime::PalmRuntime::syncCompleted,
                         v, &ContactView::refresh);
    }
    return v;
}

QIcon ContactsBackendPlugin::mainViewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("view-pim-contacts"));
}
```

- [ ] **Step 6: Add per-plugin CMake source registrations**

Each plugin's `src/plugins/<domain>/CMakeLists.txt` already lists the plugin sources; ensure `hub<foo>reader.cpp` is in that source list (it was added in Task 2 already — verify it lands in the plugin's static library, not just the test executable).

- [ ] **Step 7: Build sanity check (deferred — Task 6 also needed for compile)**

The plugin code now references `view->setHubReader(...)`, which is added in Task 6. Skip the build sanity check; both tasks commit together at the end of Task 6.

- [ ] **Step 8: Commit (DEFERRED — bundled with Task 6 commit)**

Do not commit yet; Task 6 lands the view-side declarations that make this code compile. The combined commit happens at Task 6 Step 8.

---

## Task 6 — Rewrite the four views: `setHubReader`, reader-driven `loadFoos`, hide edit affordances

**Files:**
- Modify: `src/plugins/calendar/calendarview.{h,cpp}`
- Modify: `src/plugins/contacts/contactview.{h,cpp}`
- Modify: `src/plugins/memo/memoview.{h,cpp}`
- Modify: `src/plugins/todos/taskview.{h,cpp}`
- Create: `tests/plugins/calendar/tst_calendar_view_reads_hub.cpp`
- Create: `tests/plugins/contacts/tst_contact_view_reads_hub.cpp`
- Create: `tests/plugins/memo/tst_memo_view_reads_hub.cpp`
- Create: `tests/plugins/todos/tst_task_view_reads_hub.cpp`
- Modify: `tests/plugins/<domain>/CMakeLists.txt` (×4)

The four views receive structurally identical changes:
1. Add forward decl + `setHubReader(HubFooReader *)` slot
2. Add `HubFooReader *m_hubReader = nullptr;` field
3. Rewrite `loadFoos()` body to iterate the reader instead of the filesystem
4. Hide New / Save / Delete `QAction`s and (for Memo) the per-record category combo at construction
5. In `loadFromPath`, remove the filesystem-walk fallback; keep the category-manager init

Step 1 is shown for Memo; replicate for the other three.

- [ ] **Step 1: Write the failing view test (memo)**

```cpp
// tests/plugins/memo/tst_memo_view_reads_hub.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QListWidget>
#include <memory>

#include "plugins/memo/memoview.h"
#include "plugins/memo/hubmemoreader.h"
#include <genericsqlitebackend.h>
#include <backendrecord.h>

class TstMemoViewReadsHub : public QObject
{
    Q_OBJECT
private slots:
    void populatesListFromReader();
    void emptyReaderRendersEmptyState();
    void hidesEditAffordances();
};

namespace {
std::unique_ptr<Kalburator::Sinks::GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<Kalburator::Sinks::GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("palm:memo");
    info.displayName = QStringLiteral("Memo");
    info.type = QStringLiteral("note");
    hub->createCollection(info);
    return hub;
}

void seedMemo(Kalburator::Sinks::GenericSqliteBackend *hub,
              const QString &id, const QString &title)
{
    const QByteArray bytes = QStringLiteral(
        "---\nid: 1\ncategory: Unfiled\n---\n\n%1\n").arg(title).toUtf8();
    Kalburator::Sync::BackendRecord r;
    r.id = id; r.type = QStringLiteral("memo"); r.data = bytes;
    hub->createRecord(QStringLiteral("palm:memo"), r);
}
}

void TstMemoViewReadsHub::populatesListFromReader()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    seedMemo(hub.get(), QStringLiteral("m-1"), QStringLiteral("First"));
    seedMemo(hub.get(), QStringLiteral("m-2"), QStringLiteral("Second"));

    WildPalms::MemoPlugin::HubMemoReader reader(hub.get(),
                                                QStringLiteral("palm:memo"));
    MemoView view;
    view.setHubReader(&reader);
    view.loadFromPath(tmp.path());      // initializes CategoryManager + calls refresh

    auto *list = view.findChild<QListWidget*>();
    QVERIFY(list);
    // Two real items (placeholder filtered out by the parser).
    QCOMPARE(list->count(), 2);
}

void TstMemoViewReadsHub::emptyReaderRendersEmptyState()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    WildPalms::MemoPlugin::HubMemoReader reader(hub.get(),
                                                QStringLiteral("palm:memo"));
    MemoView view;
    view.setHubReader(&reader);
    view.loadFromPath(tmp.path());

    auto *list = view.findChild<QListWidget*>();
    QVERIFY(list);
    // The view inserts an "No memos found" placeholder.
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), QStringLiteral("No memos found"));
}

void TstMemoViewReadsHub::hidesEditAffordances()
{
    MemoView view;
    const auto actions = view.findChildren<QAction*>();
    bool anyVisible = false;
    for (auto *a : actions) {
        const QString t = a->text();
        if (t.contains(QStringLiteral("New"))
            || t.contains(QStringLiteral("Delete"))
            || t.contains(QStringLiteral("Save"))) {
            if (a->isVisible()) anyVisible = true;
        }
    }
    QVERIFY(!anyVisible);
}

QTEST_MAIN(TstMemoViewReadsHub)
#include "tst_memo_view_reads_hub.moc"
```

Register in `tests/plugins/memo/CMakeLists.txt` (linking against the
existing `WildPalmsMemoPluginCore` static lib that already includes
`memoview.cpp` and now `hubmemoreader.cpp` too):

```cmake
add_executable(tst_memo_view_reads_hub tst_memo_view_reads_hub.cpp)
target_include_directories(tst_memo_view_reads_hub PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_memo_view_reads_hub PRIVATE
    Qt::Test Qt::Core Qt::Widgets KF6::I18n
    WildPalmsMemoPluginCore
    Kalburator::Kalburator)
add_test(NAME tst_memo_view_reads_hub COMMAND tst_memo_view_reads_hub)
set_tests_properties(tst_memo_view_reads_hub PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

(Match actual link names by reading `tests/plugins/memo/CMakeLists.txt`
before adding.)

Run: `cmake --build build-dev --target tst_memo_view_reads_hub 2>&1 | tail -10`
Expected: compile error — `setHubReader` not declared.

- [ ] **Step 2: Add `setHubReader` + reader field to `MemoView`**

In `src/plugins/memo/memoview.h`:

Add forward decl near other forwards:

```cpp
namespace WildPalms::MemoPlugin { class HubMemoReader; }
```

In `public Q_SLOTS:`, after `void refresh();`:

```cpp
    void setHubReader(WildPalms::MemoPlugin::HubMemoReader *reader);
```

In `private:`, after `m_currentMemoIndex`:

```cpp
    WildPalms::MemoPlugin::HubMemoReader *m_hubReader = nullptr;  // borrowed
```

- [ ] **Step 3: Implement `setHubReader` and rewrite `loadMemos`**

In `src/plugins/memo/memoview.cpp`, add the include:

```cpp
#include "hubmemoreader.h"
```

Add the slot implementation:

```cpp
void MemoView::setHubReader(WildPalms::MemoPlugin::HubMemoReader *reader)
{
    m_hubReader = reader;
}
```

Rewrite `loadMemos()`. Replace the body (current lines 145-239 of
`memoview.cpp`) — the **regex parsers and the for-loop that fills
`m_memos`** stay; only the *source of bytes* changes. The new body:

```cpp
void MemoView::loadMemos()
{
    m_ignoreContentChanges = true;
    m_memoList->clear();
    m_memos.clear();
    m_itemToIndex.clear();
    m_contentView->clear();
    m_currentMemoIndex = -1;
    m_deleteAction->setEnabled(false);
    m_memoCategoryCombo->setEnabled(false);

    if (!m_hubReader) {
        m_memoList->addItem(i18n("No memos found"));
        m_ignoreContentChanges = false;
        return;
    }

    static QRegularExpression frontmatterRe(
        QStringLiteral("^---\\n(.+?)\\n---\\n"),
        QRegularExpression::DotMatchesEverythingOption);
    static QRegularExpression idRe(QStringLiteral("^id:\\s*(\\d+)$"),
                                    QRegularExpression::MultilineOption);
    static QRegularExpression categoryRe(QStringLiteral("^category:\\s*(.+)$"),
                                          QRegularExpression::MultilineOption);
    static QRegularExpression privateRe(QStringLiteral("^private:\\s*true$"),
                                         QRegularExpression::MultilineOption);

    const QStringList ids = m_hubReader->listRecordIds();
    for (const QString &recordId : ids) {
        const QByteArray bytes = m_hubReader->recordBytes(recordId);
        if (bytes.isEmpty()) continue;

        const QString fullContent = QString::fromUtf8(bytes);

        MemoItem memo;
        memo.filePath = QString();           // no file on disk
        memo.recordId = 0;
        memo.category = CategoryManager::unfiledCategoryName();
        memo.isPrivate = false;
        memo.isDirty = false;

        QString content = fullContent;
        QRegularExpressionMatch fmMatch = frontmatterRe.match(fullContent);
        if (fmMatch.hasMatch()) {
            QString frontmatter = fmMatch.captured(1);
            content = fullContent.mid(fmMatch.capturedEnd()).trimmed();

            QRegularExpressionMatch idMatch = idRe.match(frontmatter);
            if (idMatch.hasMatch()) memo.recordId = idMatch.captured(1).toInt();

            QRegularExpressionMatch catMatch = categoryRe.match(frontmatter);
            if (catMatch.hasMatch()) memo.category = catMatch.captured(1).trimmed();

            if (privateRe.match(frontmatter).hasMatch()) memo.isPrivate = true;
        }

        memo.content = content;
        memo.title = extractTitle(content);
        m_memos.append(memo);
    }

    applyFilter();
    m_ignoreContentChanges = false;
}
```

- [ ] **Step 4: Hide edit affordances in `setupUI`**

In `src/plugins/memo/memoview.cpp` inside `setupUI()`, immediately after each
`addAction(...)` call (lines 53-65) for `m_newAction`, `m_deleteAction`,
`m_saveAction`, add:

```cpp
    m_newAction->setVisible(false);
    m_deleteAction->setVisible(false);
    m_saveAction->setVisible(false);
```

And hide the per-record memo-category combo's enclosing header layout. The
simplest path: hide both the label and the combo. After the
`headerLayout->addLayout(...)` call (~line 104), add:

```cpp
    // Sub-project D: per-record category edit is hidden; re-enabled by E.
    m_memoCategoryCombo->setVisible(false);
    // The "Memo Category:" label is owned by headerLayout; find and hide.
    for (QLabel *lbl : this->findChildren<QLabel*>()) {
        if (lbl->text() == i18n("Memo Category:")) lbl->setVisible(false);
    }
```

- [ ] **Step 5: Strip the filesystem walk from `loadFromPath`**

`loadFromPath` already calls `loadMemos()` at the end, which is now reader-
driven. The body of `loadFromPath` itself (`m_categoryManager->setBasePath` /
`m_categoryManager->load` / `m_categoryModel->reload` / `m_memoCategoryCombo->
setModel` / `loadMemos()`) is unchanged because `loadMemos` no longer touches
the filesystem. **No edit required** to `loadFromPath`.

- [ ] **Step 6: Run the Memo view test, verify it passes**

Run: `cmake --build build-dev --target tst_memo_view_reads_hub && ctest --test-dir build-dev -R tst_memo_view_reads_hub --output-on-failure`
Expected: 3/3 sub-tests pass.

- [ ] **Step 7: Repeat Steps 1-6 for Calendar, Contacts, ToDo**

Per-view differences:

**Calendar** (`CalendarView::loadEvents`, `src/plugins/calendar/calendarview.cpp:130-…`):
- No `New`/`Save`/`Delete` actions in current toolbar (read-only by design today). No hide step needed.
- Replace the `QDir(rawfiles…)` walk with the reader-driven loop.
- The existing `KCalendarCore::ICalFormat::fromString` parser consumes the bytes; substitute `m_hubReader->recordBytes(id)` for the file read.

**Contacts** (`ContactView::loadContacts`, `src/plugins/contacts/contactview.cpp:108-152`):
- No edit affordances today. No hide step needed.
- The existing `parseVCard` takes a *file path*; refactor it to take bytes:
  rename to `parseVCardBytes(const QByteArray &bytes, const QString &recordId)`,
  remove the `QFile::open` block, feed `bytes` to the existing parser body.
  Caller passes `m_hubReader->recordBytes(id)` and uses `recordId` for
  the `contact.filePath` field (now an opaque id, not a path).

**ToDo** (`TaskView::loadTasks`, `src/plugins/todos/taskview.cpp:…`):
- Hide `m_newAction`, `m_deleteAction`, `m_toggleCompleteAction` (all in `setupUI`).
- Replace the filesystem walk with the reader loop; `KCalendarCore::ICalFormat`
  parses the bytes.

Each view gets a `tst_<foo>_view_reads_hub.cpp` matching the memo test's shape:
seed 2-3 records into a `GenericSqliteBackend`, instantiate the view, call
`setHubReader` + `loadFromPath`, assert the view's list-widget (or table-model
for ToDo) row count.

- [ ] **Step 8: Run the full four-view + plugin combined build + test**

This is where Task 5's plugin-side code becomes compilable (view
declarations now exist).

Run: `cmake --build build-dev 2>&1 | tail -30`
Expected: clean build.

Run: `ctest --test-dir build-dev -R "tst_(hub.*reader|.*_view_reads_hub|contacts_in_sidebar|palm_runtime_emits)" --output-on-failure`
Expected: all new tests pass (4 reader + 4 view + 1 sidebar + 1 runtime = 10 binaries, ~25 sub-tests).

- [ ] **Step 9: Commit Tasks 5 + 6 together**

```bash
git add src/plugins/calendar/calendarbackendplugin.{h,cpp} \
        src/plugins/calendar/calendarview.{h,cpp} \
        src/plugins/contacts/contactsbackendplugin.{h,cpp} \
        src/plugins/contacts/contactview.{h,cpp} \
        src/plugins/memo/memobackendplugin.{h,cpp} \
        src/plugins/memo/memoview.{h,cpp} \
        src/plugins/todos/todobackendplugin.{h,cpp} \
        src/plugins/todos/taskview.{h,cpp} \
        tests/plugins/calendar/tst_calendar_view_reads_hub.cpp \
        tests/plugins/contacts/tst_contact_view_reads_hub.cpp \
        tests/plugins/contacts/tst_contacts_in_sidebar.cpp \
        tests/plugins/memo/tst_memo_view_reads_hub.cpp \
        tests/plugins/todos/tst_task_view_reads_hub.cpp \
        tests/plugins/*/CMakeLists.txt
git commit -m "feat(plugins): four PIM views read from wp-hub

Each PIM plugin inherits WildPalms::Plugins::PimPlugin and overrides
setHub (constructs HubFooReader) + setRuntime. createMainView
injects the reader pointer and connects runtime->syncCompleted to
view->refresh. Views replace their rawfiles/* QDir walk with a
m_hubReader->listRecordIds() + recordBytes(id) loop; parser bodies
are unchanged. Memo + ToDo hide their New/Save/Delete actions
(re-enabled by sub-project E).

Contacts joins the V2 sidebar — hasMainView() now returns true,
and createMainView / mainViewName / mainViewIcon are added to
match the other three plugins.

Five new test binaries pin: contacts-in-sidebar, four
view-reads-from-hub end-to-end tests.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 7 — Final integration sweep

**Files:**
- No file changes by default; this task is verification + any cleanups surfaced by the full-suite run.

- [ ] **Step 1: Full clean build against FetchContent v0.60**

Run from a scratch build dir to prove there's no incremental-cache contamination:

```bash
cmake -S . -B build-rcheck \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-rcheck -j$(nproc) 2>&1 | tail -40
```

Expected: 0 errors.

- [ ] **Step 2: Run the full ctest suite**

```bash
ctest --test-dir build-rcheck --output-on-failure -j$(nproc)
```

Expected: all tests pass. Count should be the prior baseline (107 from the
hub-routing landing) **plus 10 new test binaries** = ~117 test binaries,
with ~25 new sub-tests.

- [ ] **Step 3: Spot-check launching the app**

If a display is available:

```bash
build-rcheck/src/app/WildPalms &
```

Expected behaviour on a fresh profile, before any sync:
- Sidebar shows **four** icons: Calendar, **Contacts**, Memo, ToDo.
- Each view renders its empty-state placeholder ("No memos found", etc.).
- No New / Save / Delete buttons visible in any toolbar.

(If no display, skip this step and rely on the test suite. The view tests
already pin the GUI behaviour offscreen.)

- [ ] **Step 4: Clean up the scratch build dir**

```bash
rm -rf build-rcheck
```

- [ ] **Step 5: Push the branch**

```bash
git push origin feature/three-tier-sync
```

- [ ] **Step 6: No commit required** — Task 7 is verification only. Sub-project D is complete.

---

## Spec coverage map

| Spec requirement | Task(s) |
|---|---|
| §3 Per-domain HubFooReader facade | Task 2 |
| §4 Architecture: plugin owns reader, pre-injects at createMainView | Tasks 1, 5 |
| §4 Read-only affordances (hide New/Save/Delete + memo combo) | Task 6 |
| §5 New files (15) | Tasks 1, 2, 3, 5, 6 |
| §5 WP-local PimPlugin base | Task 1 |
| §5 Plugin modifications (×4) + Contacts hasMainView flip | Task 5 |
| §5 View modifications (×4) | Task 6 |
| §5 PalmRuntime: syncCompleted + dispatch | Tasks 3, 4 |
| §6 Data flow at app start (setHub at hub-creation time) | Task 4 |
| §6 Data flow after HotSync (syncCompleted → view refresh) | Tasks 3, 5 |
| §7 Error handling: empty list, missing id, null reader, parser-fail | Tasks 2, 6 |
| §8 Reader unit tests (×4) | Task 2 |
| §8 View integration tests (×4) | Task 6 |
| §8 Runtime signal test | Task 3 |
| §8 Contacts sidebar registration test | Task 5 |
| §9 User UX (4 sidebar icons; hidden edit; auto-refresh) | Tasks 5, 6, validated Task 7 |
| §10 Success criteria | Task 7 |
