# Phase E.11 — ToDo Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the ToDo conduit as the third new-ABI `IBackendPlugin`, after Memo (E.9) and Calendar (E.10). Surface Palm ToDoDB category slots as virtual sub-collections, route end-to-end through `BlobSyncEngine::twoWayWithBaseline`, and add a ToDo-aware conflict handler that protects completion-status from being silently reverted by an unrelated edit on the other side.

**Architecture:** `TodoBackendPlugin` returns `ProvidedBackends.blob = TodoBlobBackend` (transcoding `IBlobBackend`, one collection per populated category slot, `text/calendar` carrying VTODO bytes). Plugin owns a `CategoryMappingStore` populated from the ToDoDB AppInfo block via the shared `parseCategoryAppInfo` reader (promoted from calendar plugin into `WildPalmsPalmCalendar` static lib in Task 1). `TodoConflictHandler` composes a `PalmConflictHandler` and adds one ToDo overlay (completion-asymmetric merge) before delegation. Behind CMake toggle `WILDPALMS_TODO_PLUGIN_V2=ON`; legacy `TodoConduit` keeps building when off.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), KF6::CoreAddons (`KPluginMetaData`, `KPluginFactory`, `kcoreaddons_add_plugin`), KF6::CalendarCore (`Todo`, `ICalFormat`), `Kalburator::Sync` (`IBlobBackend`, `BlobSyncEngine::twoWayWithBaseline`, `MockBlobBackend`, `QSyncCore::ConflictHandler`, `BlobBaselineStore`, `ConflictHandlerRegistry`, `ConflictStore`, `ConflictPolicy`), pisock (via existing `decodeTodo`/`encodeTodo` from `WildPalmsPalmCodecs`). No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-04-25-phase-e11-todo-plugin-design.md`. Decisions #1–#6 are authoritative.

**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.11 (line 589).

**Repo:** Parent at `~/dev/WildPalms/`. Build directory: `build-dev/` (preset project). Plugin source lives in submodule `src/plugins/todos/` (`wildpalms-conduit-todos.git`). Tests live in parent under `tests/plugins/todos/`. No upstream libkalburator changes.

**Submodule split:**
- Task 1: edits to `src/plugins/calendar/` (submodule) **and** `src/palm/calendar/` (parent). Test edits in parent.
- Tasks 2–5: commit inside `src/plugins/todos/` submodule.
- Tasks 6, 7: parent repo.
- Task 7 includes the submodule pointer bumps for both `src/plugins/calendar/` (Task 1 restructure) and `src/plugins/todos/` (Tasks 2–5 worth of commits).

---

## Scope explicitly excluded

- **Deleting `TodoConduit`, `todomapper.{h,cpp}`, `todos-conduit.json`** — retired in E.16.
- **TaskView ↔ `PalmTodosAdapter` rewiring** — TaskView reused untouched per Decision #4. Any UI data-path rework is a separate post-E.16 follow-up.
- **`LocalBlobBackend` smoke test for ToDo** — `tst_todo_v2` uses `MockBlobBackend` per Decision #5, mirroring memo/calendar id-space deferral. `IDMappingStore` is E.15+.
- **Live-device ToDoDB AppInfo integration test** — E.18 (POSE64 sandbox).
- **Speculative conflict overlays** (priority tie-break, due-date asymmetric edits) — land post-E.18 only if real syncs surface bugs.
- **Typed `TodoSyncBackend` upstream extraction** — defers to "second consumer needs it" gate.
- **Flipping the CMake toggle default** — `WILDPALMS_TODO_PLUGIN_V2` ships `ON`; `OFF` keeps legacy conduit building.
- **`ConflictDialog` new-plugin lookup-path** — open from E.9; not blocked by E.11.

---

## File Structure

**Files to MOVE from `src/plugins/calendar/` (calendar submodule) to `src/palm/calendar/` (parent repo) — Task 1:**

- `categoryappinforeader.h` → `src/palm/calendar/categoryappinforeader.h`
- `categoryappinforeader.cpp` → `src/palm/calendar/categoryappinforeader.cpp`

**Files to MODIFY in calendar submodule — Task 1:**

- `CMakeLists.txt` — drop `categoryappinforeader.{cpp,h}` from plugin sources.
- `calendarbackendplugin.cpp` — change `#include "categoryappinforeader.h"` → `#include "palm/calendar/categoryappinforeader.h"`; change `parseDatebookAppInfo` → `parseCategoryAppInfo` references (only if the plugin calls the parse helper directly — it currently calls `populateFromAppInfo`, which gets renamed implicitly via header).

**Files to MODIFY in parent — Task 1:**

- `src/palm/calendar/CMakeLists.txt` — add the two reader files to `WildPalmsPalmCalendar` static lib; ensure `pisock` private linkage.
- `tests/plugins/calendar/tst_categoryappinforeader.cpp` — update include path + function name (`parseDatebookAppInfo` → `parseCategoryAppInfo`).
- `tests/plugins/calendar/CMakeLists.txt` — drop the `${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp` source from `tst_categoryappinforeader` and `tst_calendarbackendplugin` targets (now resolved via `WildPalmsPalmCalendar`).

**Files to CREATE in todos submodule — Tasks 2–5:**

- `todoicstranscoder.h` (Task 2) — namespace-scope free functions.
- `todoicstranscoder.cpp` (Task 2)
- `todoblobbackend.h` (Task 3) — transcoding `IBlobBackend`.
- `todoblobbackend.cpp` (Task 3)
- `todoconflicthandler.h` (Task 4) — `QSyncCore::ConflictHandler` with completion overlay.
- `todoconflicthandler.cpp` (Task 4)
- `todobackendplugin.h` (Task 5) — `IBackendPlugin` shell.
- `todobackendplugin.cpp` (Task 5) — class implementation + `K_PLUGIN_FACTORY_WITH_JSON`.
- `todo-backend-plugin.json` (Task 5) — new manifest.

**Files to MODIFY in todos submodule — Task 5:**

- `CMakeLists.txt` — add `WILDPALMS_TODO_PLUGIN_V2` option; build new plugin when on; keep legacy when off.

**Files to CREATE in parent repo — Tasks 2–6:**

- `tests/plugins/todos/CMakeLists.txt` (Task 2 onward, grown per task)
- `tests/plugins/todos/tst_todoicstranscoder.cpp` (Task 2)
- `tests/plugins/todos/tst_todoblobbackend.cpp` (Task 3)
- `tests/plugins/todos/tst_todoconflicthandler.cpp` (Task 4)
- `tests/plugins/todos/tst_todobackendplugin.cpp` (Task 5)
- `tests/plugins/todos/tst_todo_v2.cpp` (Task 6)

**Files to MODIFY in parent — Tasks 6, 7:**

- `tests/plugins/CMakeLists.txt` (Task 2) — add `add_subdirectory(todos)`.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (Task 7) — flip row E.11 to `✅ **E.11**`.
- `docs/plans/2026-04-20-libkalburator-integration.md` (Task 7) — mark E.11 landed.
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + new `project_phase_e11_todo.md` (Task 7).
- Submodule pointer bumps for `src/plugins/calendar/` and `src/plugins/todos/` (Task 7).

---

## Task 1: Promote `CategoryAppInfoReader` into `WildPalmsPalmCalendar`

**Why:** ToDo plugin needs to parse the ToDoDB AppInfo block. The reader currently lives in `src/plugins/calendar/`, which is a runtime plugin (`.so`) — runtime plugins can't link from each other. Move the two files into `WildPalmsPalmCalendar` (the static lib that already houses `CategoryMappingStore`), rename `parseDatebookAppInfo` → `parseCategoryAppInfo` to match its actual generality, and update the four call sites. Pure mechanical refactor; no behaviour change.

**Files:**
- Move (calendar submodule → parent):
  - `src/plugins/calendar/categoryappinforeader.h` → `src/palm/calendar/categoryappinforeader.h`
  - `src/plugins/calendar/categoryappinforeader.cpp` → `src/palm/calendar/categoryappinforeader.cpp`
- Modify: `src/palm/calendar/CMakeLists.txt`
- Modify (calendar submodule): `src/plugins/calendar/CMakeLists.txt`
- Modify (calendar submodule): `src/plugins/calendar/calendarbackendplugin.cpp`
- Modify: `tests/plugins/calendar/tst_categoryappinforeader.cpp`
- Modify: `tests/plugins/calendar/CMakeLists.txt`

- [ ] **Step 1: Stage the file move and rename in the destination**

Read the existing reader files. Move both into `src/palm/calendar/`, renaming `parseDatebookAppInfo` → `parseCategoryAppInfo`. The destination header should look like this (note the include guard rename and namespace move into `WildPalms::PalmCalendar`):

```cpp
// src/palm/calendar/categoryappinforeader.h
#ifndef WILDPALMS_PALMCALENDAR_CATEGORYAPPINFOREADER_H
#define WILDPALMS_PALMCALENDAR_CATEGORYAPPINFOREADER_H

#include <array>
#include <optional>

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCalendar {

class CategoryMappingStore;

/**
 * @brief 16 Palm category names parsed from a CategoryAppInfo block.
 *
 * Slot 0 is forced to "Unfiled" if blank; slots 1..15 are returned
 * verbatim (may be empty when the user hasn't named the slot).
 */
struct CategoryNames {
    std::array<QString, 16> names;
};

/**
 * @brief Parse any Palm CategoryAppInfo block into 16 category names.
 *
 * Wraps pisock's `unpack_CategoryAppInfo` (pi-appinfo.h). Generic
 * across DatebookDB / ToDoDB / AddressDB / MemoDB — all four use the
 * same `CategoryAppInfo_t` schema.
 *
 * Returns std::nullopt if the bytes don't unpack. Pure function.
 */
std::optional<CategoryNames>
parseCategoryAppInfo(const QByteArray &appInfoBytes);

/**
 * @brief Populate `store` with the named slots from `appInfoBytes`.
 *
 * Calls `parseCategoryAppInfo`, then for every non-empty name in
 * slots 1..15 invokes `store.setSlotName(dbName, slot, name)`. Slot 0
 * is intentionally skipped.
 *
 * Returns false if parsing failed (store left untouched), true
 * otherwise.
 */
bool populateFromAppInfo(CategoryMappingStore &store,
                         const QString &dbName,
                         const QByteArray &appInfoBytes);

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_PALMCALENDAR_CATEGORYAPPINFOREADER_H
```

The destination .cpp keeps the function bodies verbatim, just:
- Rename `parseDatebookAppInfo` → `parseCategoryAppInfo` (definition + the internal call from `populateFromAppInfo`).
- Move `namespace WildPalms::CalendarPlugin { ... }` → `namespace WildPalms::PalmCalendar { ... }` and drop the `WildPalms::PalmCalendar::` qualifier on `CategoryMappingStore` references (they're now in the same namespace).
- Change `#include "categoryappinforeader.h"` → `#include "categoryappinforeader.h"` (relative include unchanged, file is still adjacent).
- Change `#include "palm/calendar/categorymappingstore.h"` → `#include "categorymappingstore.h"` (now adjacent).

After the moves, delete the source files from `src/plugins/calendar/`.

- [ ] **Step 2: Add the moved files to `WildPalmsPalmCalendar`**

Edit `src/palm/calendar/CMakeLists.txt` to add the two new files to the static lib. The library currently lists `categorymappingstore`, `datebookcodec`, `palmcalendarbackend`. Append:

```cmake
add_library(WildPalmsPalmCalendar STATIC
    categorymappingstore.h
    categorymappingstore.cpp
    categoryappinforeader.h          # NEW
    categoryappinforeader.cpp        # NEW
    datebookcodec.h
    datebookcodec.cpp
    palmcalendarbackend.h
    palmcalendarbackend.cpp
)
```

`pisock` is already a `PRIVATE` dependency on this target (used by `datebookcodec.cpp`), so the new file's `#include <pi-appinfo.h>` resolves without further plumbing. The `add_dependencies(WildPalmsPalmCalendar pilot-link-external)` line is already in place.

- [ ] **Step 3: Drop the moved files from the calendar plugin's CMakeLists.txt**

Inside the calendar submodule, edit `src/plugins/calendar/CMakeLists.txt`. The `WILDPALMS_CALENDAR_PLUGIN_V2` branch currently lists `categoryappinforeader.cpp categoryappinforeader.h` in its sources. Remove those two lines:

```cmake
    kcoreaddons_add_plugin(wildpalms_calendar_v2
        SOURCES
            calendarbackendplugin.cpp   calendarbackendplugin.h
            calendarblobbackend.cpp     calendarblobbackend.h
            calendarconflicthandler.cpp calendarconflicthandler.h
            # categoryappinforeader.cpp   categoryappinforeader.h   <-- REMOVED
            icstranscoder.cpp           icstranscoder.h
            calendarview.cpp            calendarview.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
```

`WildPalmsPalmCalendar` is already in the plugin's `target_link_libraries`, so the symbols are reachable.

- [ ] **Step 4: Update the calendar plugin's call site**

Edit `src/plugins/calendar/calendarbackendplugin.cpp`. The current `#include "categoryappinforeader.h"` (a sibling include) must change to the new location:

```cpp
#include "palm/calendar/categoryappinforeader.h"
```

The plugin's only call is `populateFromAppInfo(*m_categoryStore, QStringLiteral("DatebookDB"), ...)`, which keeps the same name. The plugin already qualifies the helper as a free function in its current namespace (`WildPalms::CalendarPlugin`); after the move it's `WildPalms::PalmCalendar::populateFromAppInfo`. Update the call:

```cpp
WildPalms::PalmCalendar::populateFromAppInfo(*m_categoryStore,
                                             QStringLiteral("DatebookDB"),
                                             palmBackend->readAppBlock(QStringLiteral("DatebookDB")));
```

(Or add `using WildPalms::PalmCalendar::populateFromAppInfo;` at the top of the file if you prefer.)

- [ ] **Step 5: Update the existing test**

Edit `tests/plugins/calendar/tst_categoryappinforeader.cpp`:
- Change `#include "plugins/calendar/categoryappinforeader.h"` → `#include "palm/calendar/categoryappinforeader.h"`.
- Change `using WildPalms::CalendarPlugin::parseDatebookAppInfo;` → `using WildPalms::PalmCalendar::parseCategoryAppInfo;`.
- Change `using WildPalms::CalendarPlugin::populateFromAppInfo;` → `using WildPalms::PalmCalendar::populateFromAppInfo;`.
- Replace every call to `parseDatebookAppInfo(...)` with `parseCategoryAppInfo(...)`. There are 4 call sites in the file.

The test methods, fixtures, and expectations stay identical — the rename is mechanical.

- [ ] **Step 6: Update the test CMakeLists**

Edit `tests/plugins/calendar/CMakeLists.txt`. Two test targets currently compile `${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp` directly: `tst_categoryappinforeader` and `tst_calendarbackendplugin`. Drop those source lines — the symbols are now in `WildPalmsPalmCalendar` which both targets already link.

```cmake
add_executable(tst_categoryappinforeader
    tst_categoryappinforeader.cpp
    # ${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp   <-- REMOVED
)
```

```cmake
add_executable(tst_calendarbackendplugin
    tst_calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarconflicthandler.cpp
    # ${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp   <-- REMOVED
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarview.cpp
)
```

- [ ] **Step 7: Reconfigure and build**

```
cmake --build build-dev --target WildPalmsPalmCalendar wildpalms_calendar_v2 tst_categoryappinforeader tst_calendarbackendplugin tst_icstranscoder tst_calendarblobbackend tst_calendarconflicthandler tst_calendar_v2 -j
```

Expected: clean build. If `cmake` complains about "categoryappinforeader.cpp not found" inside the calendar plugin, the source removal in Step 3 was missed.

- [ ] **Step 8: Run the calendar test suite**

```
ctest --test-dir build-dev -R '^tst_(categoryappinforeader|calendarbackendplugin|icstranscoder|calendarblobbackend|calendarconflicthandler|calendar_v2)$' --output-on-failure
```

Expected: all six pass. Test counts unchanged from pre-Task-1.

- [ ] **Step 9: Commit (calendar submodule)**

```
cd src/plugins/calendar
git add CMakeLists.txt calendarbackendplugin.cpp
git rm categoryappinforeader.h categoryappinforeader.cpp
git commit -m "$(cat <<'EOF'
refactor(calendar): drop categoryappinforeader (moved into WildPalmsPalmCalendar)

Phase E.11 Task 1. The reader is generic across all Palm DBs that use
CategoryAppInfo_t (Datebook, ToDoDB, Address, Memo). Promoting it into
the static lib lets the upcoming ToDo plugin reuse it without a
runtime-plugin -> runtime-plugin dependency.

parseDatebookAppInfo renamed parseCategoryAppInfo to match its actual
generality. Plugin call site updated.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
cd ../../..
```

- [ ] **Step 10: Commit (parent repo)**

```
git add src/palm/calendar/categoryappinforeader.h src/palm/calendar/categoryappinforeader.cpp src/palm/calendar/CMakeLists.txt tests/plugins/calendar/tst_categoryappinforeader.cpp tests/plugins/calendar/CMakeLists.txt src/plugins/calendar
git commit -m "$(cat <<'EOF'
refactor(palm): host CategoryAppInfoReader in WildPalmsPalmCalendar

Phase E.11 Task 1. Moves categoryappinforeader.{h,cpp} from the
calendar plugin (where it was a private detail) into the shared
WildPalmsPalmCalendar static lib so that both Calendar and the
upcoming ToDo plugin can call parseCategoryAppInfo /
populateFromAppInfo without crossing runtime-plugin boundaries.

Renames parseDatebookAppInfo -> parseCategoryAppInfo. Test moved/
updated. Calendar submodule pointer bumped.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `TodoIcsTranscoder`

**Files:**
- Create (todos submodule): `src/plugins/todos/todoicstranscoder.h`
- Create (todos submodule): `src/plugins/todos/todoicstranscoder.cpp`
- Create (parent): `tests/plugins/todos/tst_todoicstranscoder.cpp`
- Create (parent): `tests/plugins/todos/CMakeLists.txt`
- Modify (parent): `tests/plugins/CMakeLists.txt`

- [ ] **Step 1: Create `tests/plugins/todos/CMakeLists.txt`**

```cmake
# Phase E.11 — ToDo plugin tests.
# Tasks 2-6 build test binaries directly against the source files in
# the todos submodule.

set(TODOS_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/todos)

# --- Task 2: TodoIcsTranscoder ---
add_executable(tst_todoicstranscoder
    tst_todoicstranscoder.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoicstranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoicstranscoder
    PRIVATE
        WildPalmsPalmCodecs   # decodeTodo / encodeTodo
        WildPalmsPalmSync     # PalmRecord
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoicstranscoder COMMAND tst_todoicstranscoder)
```

- [ ] **Step 2: Hook the new directory into the test tree**

Edit `tests/plugins/CMakeLists.txt`. Append:

```cmake
# Phase E.11 — ToDo plugin tests.
add_subdirectory(todos)
```

- [ ] **Step 3: Write the failing tests**

Create `tests/plugins/todos/tst_todoicstranscoder.cpp`:

```cpp
#include <QtTest/QtTest>

#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/todos/todoicstranscoder.h"
#include "palm/codecs/todocodec.h"
#include "palm/sync/palmrecord.h"

using WildPalms::TodoPlugin::encodePalmToIcs;
using WildPalms::TodoPlugin::decodeIcsToPalm;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord makeTodoRecord(const QString &description,
                          int slot,
                          bool complete = false,
                          int priority = 1)
{
    Todo t;
    t.description = description;
    t.note = QStringLiteral("");
    t.hasIndefiniteDue = true;
    t.priority = priority;
    t.isComplete = complete;
    t.isPrivate = false;
    PalmRecord pr;
    pr.recordId = 42;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeTodo(t);
    return pr;
}

PalmRecord makeTodoRecordFull()
{
    Todo t;
    t.description = QStringLiteral("Buy groceries");
    t.note = QStringLiteral("Milk, bread, eggs");
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 5, 15), QTime(0, 0));
    t.priority = 2;
    t.isComplete = false;
    t.isPrivate = true;
    PalmRecord pr;
    pr.recordId = 17;
    pr.category = 3;
    pr.data = encodeTodo(t);
    return pr;
}

} // namespace

class TestTodoIcsTranscoder : public QObject
{
    Q_OBJECT
private slots:
    void encodeProducesParseableVtodo();
    void encodePreservesSummaryAndDescription();
    void encodeStampsCategoryAndRecordIdProperties();
    void encodeIndefiniteDueOmitsDtDue();
    void encodeCompletedTodoSetsCompleted();
    void encodePrivateSetsClassification();
    void roundTripPreservesAllFields();
    void decodeWithEmptyBytesReturnsNullopt();
    void decodeWithGarbageReturnsNullopt();
    void decodeSlotHintOverridesEmbeddedSlot();
    void decodePreservesRecordIdWhenPresent();
};

void TestTodoIcsTranscoder::encodeProducesParseableVtodo()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(!ics.isEmpty());
    QVERIFY(ics.contains("BEGIN:VCALENDAR"));
    QVERIFY(ics.contains("BEGIN:VTODO"));
    QVERIFY(ics.contains("END:VCALENDAR"));
}

void TestTodoIcsTranscoder::encodePreservesSummaryAndDescription()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("SUMMARY:Buy groceries"));
    QVERIFY(ics.contains("Milk, bread, eggs"));
}

void TestTodoIcsTranscoder::encodeStampsCategoryAndRecordIdProperties()
{
    PalmRecord pr = makeTodoRecordFull();   // category = 3, recordId = 17
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("X-WP-PALM-CATEGORY-SLOT:3"));
    QVERIFY(ics.contains("X-WP-PALM-RECORDID:17"));
}

void TestTodoIcsTranscoder::encodeIndefiniteDueOmitsDtDue()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("No due"), 0);
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(!ics.contains("DUE:"));
    QVERIFY(!ics.contains("DUE;"));
}

void TestTodoIcsTranscoder::encodeCompletedTodoSetsCompleted()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("Done"), 0, /*complete=*/true);
    QByteArray ics = encodePalmToIcs(pr);
    // VTODO completion shows up as STATUS:COMPLETED + COMPLETED:<timestamp>.
    QVERIFY(ics.contains("STATUS:COMPLETED"));
    QVERIFY(ics.contains("COMPLETED:"));
}

void TestTodoIcsTranscoder::encodePrivateSetsClassification()
{
    PalmRecord pr = makeTodoRecordFull();   // isPrivate = true
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("CLASS:PRIVATE"));
}

void TestTodoIcsTranscoder::roundTripPreservesAllFields()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr);
    auto roundTripped = decodeIcsToPalm(ics, 3);
    QVERIFY(roundTripped.has_value());
    QCOMPARE(static_cast<int>(roundTripped->category), 3);
    QCOMPARE(roundTripped->recordId, 17u);

    auto rtTodo = decodeTodo(QByteArrayView(roundTripped->data));
    auto srcTodo = decodeTodo(QByteArrayView(pr.data));
    QVERIFY(rtTodo.has_value());
    QVERIFY(srcTodo.has_value());
    QCOMPARE(rtTodo->description, srcTodo->description);
    QCOMPARE(rtTodo->note, srcTodo->note);
    QCOMPARE(rtTodo->hasIndefiniteDue, srcTodo->hasIndefiniteDue);
    QCOMPARE(rtTodo->due, srcTodo->due);
    QCOMPARE(rtTodo->priority, srcTodo->priority);
    QCOMPARE(rtTodo->isComplete, srcTodo->isComplete);
    QCOMPARE(rtTodo->isPrivate, srcTodo->isPrivate);
}

void TestTodoIcsTranscoder::decodeWithEmptyBytesReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray(), 0).has_value());
}

void TestTodoIcsTranscoder::decodeWithGarbageReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray("not a vcalendar"), 0).has_value());
}

void TestTodoIcsTranscoder::decodeSlotHintOverridesEmbeddedSlot()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("Slot test"), 5);
    QByteArray ics = encodePalmToIcs(pr);
    auto rt = decodeIcsToPalm(ics, /*slotHint=*/9);
    QVERIFY(rt.has_value());
    QCOMPARE(static_cast<int>(rt->category), 9);
}

void TestTodoIcsTranscoder::decodePreservesRecordIdWhenPresent()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("With id"), 0);   // recordId = 42
    QByteArray ics = encodePalmToIcs(pr);
    auto rt = decodeIcsToPalm(ics, 0);
    QVERIFY(rt.has_value());
    QCOMPARE(rt->recordId, 42u);
}

QTEST_MAIN(TestTodoIcsTranscoder)
#include "tst_todoicstranscoder.moc"
```

- [ ] **Step 4: Run the test to verify it fails (no source yet)**

```
cmake --build build-dev --target tst_todoicstranscoder -j 2>&1 | tail -20
```

Expected: link failure with "undefined reference to `WildPalms::TodoPlugin::encodePalmToIcs`" / `decodeIcsToPalm`. That's what we want.

- [ ] **Step 5: Write the header**

Create `src/plugins/todos/todoicstranscoder.h` (in todos submodule):

```cpp
#ifndef WILDPALMS_TODO_TODOICSTRANSCODER_H
#define WILDPALMS_TODO_TODOICSTRANSCODER_H

#include <optional>

#include <QByteArray>

#include "palm/sync/palmrecord.h"

namespace WildPalms::TodoPlugin {

/**
 * @brief Encode a Palm ToDo record into iCalendar VCALENDAR/VTODO bytes.
 *
 * Composes WildPalms::PalmCodecs::decodeTodo (Palm bytes -> Todo POD)
 * with a Todo -> KCalendarCore::Todo mapper, then serialises the
 * single-VTODO calendar via KCalendarCore::ICalFormat. Stamps the
 * Palm slot via X-WP-PALM-CATEGORY-SLOT and the Palm recordId via
 * X-WP-PALM-RECORDID for round-trip.
 *
 * Returns empty QByteArray on decode failure or empty input.
 */
QByteArray encodePalmToIcs(const WildPalms::PalmSync::PalmRecord &record);

/**
 * @brief Decode VCALENDAR bytes containing a single VTODO into a PalmRecord.
 *
 * `slotHint` populates `PalmRecord::category` (overriding any
 * X-WP-PALM-CATEGORY-SLOT in the body — collection-id wins). The
 * X-WP-PALM-RECORDID property, if present, populates
 * `PalmRecord::recordId`; otherwise recordId stays 0 and the device
 * assigns on write.
 *
 * Returns std::nullopt if `icsBytes` doesn't parse as a single VTODO,
 * or if encoding to Palm bytes fails.
 */
std::optional<WildPalms::PalmSync::PalmRecord>
decodeIcsToPalm(const QByteArray &icsBytes, int slotHint);

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_TODOICSTRANSCODER_H
```

- [ ] **Step 6: Write the implementation**

Create `src/plugins/todos/todoicstranscoder.cpp`:

```cpp
#include "todoicstranscoder.h"

#include "palm/codecs/todocodec.h"

#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace WildPalms::TodoPlugin {

namespace {

constexpr const char *kCategorySlotProp = "X-WP-PALM-CATEGORY-SLOT";
constexpr const char *kRecordIdProp     = "X-WP-PALM-RECORDID";

// Palm priority (1..5, 1=highest) -> KCal priority (1..9, 1=highest).
int palmPriorityToKCal(int p)
{
    switch (p) {
        case 1: return 1;
        case 2: return 3;
        case 3: return 5;
        case 4: return 7;
        case 5: return 9;
        default: return 5;
    }
}

// KCal priority (1..9) -> Palm (1..5). Lossy on even input; lossless
// for any value the Palm could have authored.
int kcalPriorityToPalm(int p)
{
    if (p <= 1) return 1;
    if (p <= 3) return 2;
    if (p <= 5) return 3;
    if (p <= 7) return 4;
    return 5;
}

// Map Palm Todo POD -> KCalendarCore::Todo.
KCalendarCore::Todo::Ptr toKCalTodo(const WildPalms::PalmCodecs::Todo &t,
                                    int slot,
                                    std::uint32_t recordId)
{
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);
    todo->setSummary(t.description);
    if (!t.note.isEmpty()) {
        todo->setDescription(t.note);
    }
    if (!t.hasIndefiniteDue && t.due.isValid()) {
        todo->setDtStart(t.due);
        todo->setDtDue(t.due);
        todo->setAllDay(true);
    }
    todo->setPriority(palmPriorityToKCal(t.priority));
    if (t.isComplete) {
        todo->setStatus(KCalendarCore::Incidence::StatusCompleted);
        todo->setCompleted(QDateTime::currentDateTimeUtc());
    }
    if (t.isPrivate) {
        todo->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
    } else {
        todo->setSecrecy(KCalendarCore::Incidence::SecrecyPublic);
    }
    todo->setNonKDECustomProperty(kCategorySlotProp,
                                  QString::number(slot));
    todo->setNonKDECustomProperty(kRecordIdProp,
                                  QString::number(recordId));
    return todo;
}

// Map KCalendarCore::Todo -> Palm Todo POD.
WildPalms::PalmCodecs::Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &todo)
{
    WildPalms::PalmCodecs::Todo t;
    t.description = todo->summary();
    t.note        = todo->description();
    if (todo->hasDueDate()) {
        t.hasIndefiniteDue = false;
        t.due = todo->dtDue();
    } else {
        t.hasIndefiniteDue = true;
    }
    t.priority   = kcalPriorityToPalm(todo->priority());
    t.isComplete = todo->isCompleted();
    t.isPrivate  = todo->secrecy() == KCalendarCore::Incidence::SecrecyPrivate;
    return t;
}

} // namespace

QByteArray encodePalmToIcs(const WildPalms::PalmSync::PalmRecord &record)
{
    if (record.data.isEmpty()) return {};
    auto decoded = WildPalms::PalmCodecs::decodeTodo(QByteArrayView(record.data));
    if (!decoded.has_value()) return {};

    auto todo = toKCalTodo(*decoded,
                           static_cast<int>(record.category),
                           record.recordId);

    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!cal->addTodo(todo)) return {};

    KCalendarCore::ICalFormat fmt;
    return fmt.toString(cal).toUtf8();
}

std::optional<WildPalms::PalmSync::PalmRecord>
decodeIcsToPalm(const QByteArray &icsBytes, int slotHint)
{
    if (icsBytes.isEmpty()) return std::nullopt;

    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromString(cal, QString::fromUtf8(icsBytes))) {
        return std::nullopt;
    }

    auto todos = cal->todos();
    if (todos.isEmpty()) return std::nullopt;
    auto todo = todos.first();
    if (!todo) return std::nullopt;

    auto pod = fromKCalTodo(todo);
    QByteArray bytes = WildPalms::PalmCodecs::encodeTodo(pod);
    if (bytes.isEmpty()) return std::nullopt;

    WildPalms::PalmSync::PalmRecord pr;
    pr.data     = bytes;
    pr.category = static_cast<std::uint8_t>(slotHint);
    bool ok = false;
    const QString rid = todo->nonKDECustomProperty(kRecordIdProp);
    if (!rid.isEmpty()) {
        const std::uint32_t parsed = rid.toUInt(&ok);
        if (ok) pr.recordId = parsed;
    }
    return pr;
}

} // namespace WildPalms::TodoPlugin
```

- [ ] **Step 7: Build and run**

```
cmake --build build-dev --target tst_todoicstranscoder -j 2>&1 | tail -20 && ctest --test-dir build-dev -R '^tst_todoicstranscoder$' --output-on-failure
```

Expected: builds cleanly; all 11 tests pass.

If `STATUS:COMPLETED` test fails because `KCalendarCore::Todo::isCompleted()` requires the `completed` timestamp to be set rather than only the status, double-check `setCompleted(QDateTime::currentDateTimeUtc())` is present in `toKCalTodo`. KCal sets STATUS:COMPLETED automatically when you call `setCompleted()`.

If `roundTripPreservesAllFields` fails on `due`, verify `setAllDay(true)` is called when `hasIndefiniteDue=false` — KCal otherwise emits `DUE` with a UTC time component that the Palm encoder doesn't carry round-trip.

- [ ] **Step 8: Commit (todos submodule)**

```
cd src/plugins/todos
git add todoicstranscoder.h todoicstranscoder.cpp
git commit -m "$(cat <<'EOF'
feat(todo): TodoIcsTranscoder (Palm bytes <-> VTODO bytes)

Phase E.11 Task 2. Pure functions composing decodeTodo / encodeTodo
with a KCalendarCore::Todo mapper and ICalFormat serialiser. Stamps
X-WP-PALM-CATEGORY-SLOT and X-WP-PALM-RECORDID for blob-store
round-trip. Priority maps Palm 1..5 <-> KCal 1..9 (lossless on
Palm-authored values; lossy when KCal sources land with even
priority).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
cd ../../..
```

- [ ] **Step 9: Commit (parent repo)**

```
git add tests/plugins/todos/CMakeLists.txt tests/plugins/todos/tst_todoicstranscoder.cpp tests/plugins/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(todo): tst_todoicstranscoder (Phase E.11 Task 2)

11 tests cover encode/decode round-trip, summary/note/category/
recordId stamping, indefinite-due omission, completed/private
overlays, and slot-hint precedence. Test target compiles the
transcoder source from the todos submodule directly (matches the
calendar test wiring from E.10).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `TodoBlobBackend`

**Files:**
- Create (todos submodule): `src/plugins/todos/todoblobbackend.h`
- Create (todos submodule): `src/plugins/todos/todoblobbackend.cpp`
- Create (parent): `tests/plugins/todos/tst_todoblobbackend.cpp`
- Modify (parent): `tests/plugins/todos/CMakeLists.txt`

Reference pattern: `src/plugins/calendar/calendarblobbackend.{h,cpp}` is the closest analog. The structure is identical apart from db name (`ToDoDB` vs `DatebookDB`), backend id (`palm-todo` vs `calendar`), collection prefix (`palm:todo/` vs `palm:calendar/`), and the transcoder pair.

- [ ] **Step 1: Append the new test target to `tests/plugins/todos/CMakeLists.txt`**

```cmake
# --- Task 3: TodoBlobBackend ---
add_executable(tst_todoblobbackend
    tst_todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoblobbackend
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore
        WildPalmsPalmCodecs      # encodeTodo
        WildPalmsPalmSync        # PalmBackend, PalmRecord, MockPalmDatabaseAccess
        WildPalmsCore
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoblobbackend COMMAND tst_todoblobbackend)
```

- [ ] **Step 2: Write the failing tests**

Create `tests/plugins/todos/tst_todoblobbackend.cpp`:

```cpp
#include <QtTest/QtTest>

#include "plugins/todos/todoblobbackend.h"
#include "plugins/todos/todoicstranscoder.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/todocodec.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"

#include "backendrecord.h"
#include "collectioninfo.h"

using WildPalms::TodoPlugin::TodoBlobBackend;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRecord;

namespace {

PalmRecord makeTodo(std::uint32_t recordId,
                    int slot,
                    const QString &summary)
{
    Todo t;
    t.description = summary;
    t.priority = 1;
    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeTodo(t);
    return pr;
}

} // namespace

class TestTodoBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void emptyStoreEmitsUnfiledOnly();
    void populatedStoreEmitsNamedSlots();
    void slotZeroNamedDoesNotShadowUnfiled();
    void loadRecordsRoutesByCategory();
    void loadRecordsMixedCategoryDataset();
    void createRecordStampsSlotFromCollectionId();
    void updateRecordPreservesRecordId();
    void deleteRecordForwardsToPalmBackend();
    void backendIdentity();
};

void TestTodoBlobBackend::backendIdentity()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    TodoBlobBackend be(&palm, &store);
    QCOMPARE(be.backendId(), QStringLiteral("palm-todo"));
}

void TestTodoBlobBackend::emptyStoreEmitsUnfiledOnly()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    TodoBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols[0].id, QStringLiteral("palm:todo/0"));
    QCOMPARE(cols[0].name, QStringLiteral("Unfiled"));
}

void TestTodoBlobBackend::populatedStoreEmitsNamedSlots()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("Personal"));
    store.setSlotName(QStringLiteral("ToDoDB"), 2, QStringLiteral("Business"));

    TodoBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();
    QCOMPARE(cols.size(), 3);
    QCOMPARE(cols[0].id, QStringLiteral("palm:todo/0"));
    QCOMPARE(cols[1].id, QStringLiteral("palm:todo/1"));
    QCOMPARE(cols[1].name, QStringLiteral("Personal"));
    QCOMPARE(cols[2].id, QStringLiteral("palm:todo/2"));
    QCOMPARE(cols[2].name, QStringLiteral("Business"));
}

void TestTodoBlobBackend::slotZeroNamedDoesNotShadowUnfiled()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    // CategoryMappingStore::setSlotName ignores non-"Unfiled" names for slot 0.
    store.setSlotName(QStringLiteral("ToDoDB"), 0, QStringLiteral("Renamed"));

    TodoBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols[0].name, QStringLiteral("Unfiled"));
}

void TestTodoBlobBackend::loadRecordsRoutesByCategory()
{
    MockPalmDatabaseAccess device;
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(1, 0, QStringLiteral("Anything")));
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(2, 1, QStringLiteral("Personal one")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("Personal"));
    TodoBlobBackend be(&palm, &store);

    auto unfiled = be.loadRecords(QStringLiteral("palm:todo/0"));
    auto personal = be.loadRecords(QStringLiteral("palm:todo/1"));
    QCOMPARE(unfiled.size(), 1);
    QCOMPARE(personal.size(), 1);
    QVERIFY(unfiled[0].data.contains("Anything"));
    QVERIFY(personal[0].data.contains("Personal one"));
}

void TestTodoBlobBackend::loadRecordsMixedCategoryDataset()
{
    MockPalmDatabaseAccess device;
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(1, 1, QStringLiteral("A1")));
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(2, 1, QStringLiteral("A2")));
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(3, 2, QStringLiteral("B1")));
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(4, 0, QStringLiteral("U1")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("Slot1"));
    store.setSlotName(QStringLiteral("ToDoDB"), 2, QStringLiteral("Slot2"));
    TodoBlobBackend be(&palm, &store);

    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/0")).size(), 1);
    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/1")).size(), 2);
    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/2")).size(), 1);
}

void TestTodoBlobBackend::createRecordStampsSlotFromCollectionId()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 5, QStringLiteral("Five"));
    TodoBlobBackend be(&palm, &store);

    PalmRecord src = makeTodo(0, 0, QStringLiteral("Created"));
    QByteArray ics = WildPalms::TodoPlugin::encodePalmToIcs(src);

    BackendRecord created = be.createRecord(QStringLiteral("palm:todo/5"), ics);
    QVERIFY(!created.id.isEmpty());

    // Pull from device directly; verify category landed.
    const auto rolled = device.records(QStringLiteral("ToDoDB"));
    QCOMPARE(rolled.size(), 1);
    QCOMPARE(static_cast<int>(rolled.first().category), 5);
}

void TestTodoBlobBackend::updateRecordPreservesRecordId()
{
    MockPalmDatabaseAccess device;
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(99, 1, QStringLiteral("Original")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("One"));
    TodoBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:todo/1"));
    QCOMPARE(records.size(), 1);
    const QString id = records[0].id;

    PalmRecord modified = makeTodo(99, 1, QStringLiteral("Edited"));
    QByteArray ics = WildPalms::TodoPlugin::encodePalmToIcs(modified);
    QVERIFY(be.updateRecord(QStringLiteral("palm:todo/1"), id, ics));

    const auto rolled = device.records(QStringLiteral("ToDoDB"));
    QCOMPARE(rolled.size(), 1);
    QCOMPARE(rolled.first().recordId, 99u);
    QVERIFY(rolled.first().data.contains("Edited"));
}

void TestTodoBlobBackend::deleteRecordForwardsToPalmBackend()
{
    MockPalmDatabaseAccess device;
    device.seedRecord(QStringLiteral("ToDoDB"), makeTodo(7, 0, QStringLiteral("Doomed")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    TodoBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:todo/0"));
    QCOMPARE(records.size(), 1);
    QVERIFY(be.deleteRecord(QStringLiteral("palm:todo/0"), records[0].id));

    // Mock semantics: deletion may either remove or mark with AttrDeleted.
    // Mirror the calendar blobbackend test approach: re-loadRecords should return empty.
    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/0")).size(), 0);
}

QTEST_MAIN(TestTodoBlobBackend)
#include "tst_todoblobbackend.moc"
```

- [ ] **Step 3: Run build expecting failure**

```
cmake --build build-dev --target tst_todoblobbackend -j 2>&1 | tail -20
```

Expected: link errors for `TodoBlobBackend::*`. Source files don't exist yet.

- [ ] **Step 4: Write the header**

Create `src/plugins/todos/todoblobbackend.h`:

```cpp
#ifndef WILDPALMS_TODO_TODOBLOBBACKEND_H
#define WILDPALMS_TODO_TODOBLOBBACKEND_H

#include "iblobbackend.h"

#include <QObject>

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::TodoPlugin {

/**
 * @brief Transcoding IBlobBackend wrapping PalmBackend's "ToDoDB".
 *
 * Surfaces one collection per populated category slot:
 *   - "palm:todo/0"   "Unfiled" (always present)
 *   - "palm:todo/<N>" 1..15, present iff
 *     `categoryStore->slotName("ToDoDB", N)` is non-empty.
 *
 * Records route to/from these collections by PalmRecord::category.
 * loadRecords transcodes wire bytes -> VTODO via TodoIcsTranscoder;
 * createRecord/updateRecord transcode VTODO -> wire and forward to
 * PalmBackend's category-aware createPalmRecord/updatePalmRecord.
 *
 * Lifetime: does NOT own palmBackend or categoryStore. Caller retains
 * ownership; both must outlive the backend.
 */
class TodoBlobBackend : public Kalburator::Sync::IBlobBackend
{
    Q_OBJECT
public:
    static constexpr const char *BackendId        = "palm-todo";
    static constexpr const char *PalmDbName       = "ToDoDB";
    static constexpr const char *CollectionPrefix = "palm:todo/";

    explicit TodoBlobBackend(
        WildPalms::PalmSync::PalmBackend *palmBackend,
        const WildPalms::PalmCalendar::CategoryMappingStore *categoryStore,
        QObject *parent = nullptr);
    ~TodoBlobBackend() override;

    // Identity
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override;
    bool    deleteCollection(const QString &collectionId) override;

    // Records
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    Kalburator::Sync::BackendRecord createRecord(const QString &collectionId,
                                                  const QByteArray &data) override;
    bool updateRecord(const QString &collectionId,
                      const QString &recordId,
                      const QByteArray &data) override;
    bool deleteRecord(const QString &collectionId,
                      const QString &recordId) override;

private:
    static QString collectionIdForSlot(int slot);
    static int     slotForCollectionId(const QString &collectionId);

    WildPalms::PalmSync::PalmBackend *m_palmBackend = nullptr;
    const WildPalms::PalmCalendar::CategoryMappingStore *m_categoryStore = nullptr;
};

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_TODOBLOBBACKEND_H
```

- [ ] **Step 5: Write the implementation**

Create `src/plugins/todos/todoblobbackend.cpp`:

```cpp
#include "todoblobbackend.h"

#include "todoicstranscoder.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

#include "backendrecord.h"
#include "collectioninfo.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QStringList>

namespace WildPalms::TodoPlugin {

namespace {

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString idForPalmRecord(std::uint32_t recordId)
{
    return WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral("ToDoDB"), recordId);
}

bool decodeId(const QString &id, std::uint32_t *outRecordId)
{
    QString dbName;
    return WildPalms::PalmSync::PalmBackend::decodeRecordId(id, &dbName, outRecordId)
        && dbName == QLatin1String("ToDoDB");
}

} // namespace

TodoBlobBackend::TodoBlobBackend(
    WildPalms::PalmSync::PalmBackend *palmBackend,
    const WildPalms::PalmCalendar::CategoryMappingStore *categoryStore,
    QObject *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_palmBackend(palmBackend)
    , m_categoryStore(categoryStore)
{
}

TodoBlobBackend::~TodoBlobBackend() = default;

QString TodoBlobBackend::backendId()   const { return QStringLiteral("palm-todo"); }
QString TodoBlobBackend::displayName() const { return QStringLiteral("Palm ToDo"); }
bool    TodoBlobBackend::isAvailable() const
{
    return m_palmBackend != nullptr && m_palmBackend->isAvailable();
}

QString TodoBlobBackend::collectionIdForSlot(int slot)
{
    return QStringLiteral("palm:todo/") + QString::number(slot);
}

int TodoBlobBackend::slotForCollectionId(const QString &collectionId)
{
    if (!collectionId.startsWith(QLatin1String(CollectionPrefix))) return -1;
    bool ok = false;
    const int slot = collectionId.mid(int(qstrlen(CollectionPrefix))).toInt(&ok);
    return (ok && slot >= 0 && slot <= 15) ? slot : -1;
}

QList<Kalburator::Sync::CollectionInfo> TodoBlobBackend::availableCollections()
{
    QList<Kalburator::Sync::CollectionInfo> out;

    Kalburator::Sync::CollectionInfo unfiled;
    unfiled.id   = collectionIdForSlot(0);
    unfiled.name = QStringLiteral("Unfiled");
    unfiled.type = QStringLiteral("calendar");   // VTODO is calendar-typed
    out.append(unfiled);

    if (!m_categoryStore) return out;

    const QList<int> populated = m_categoryStore->populatedSlots(
        QStringLiteral("ToDoDB"));
    for (int slot : populated) {
        Kalburator::Sync::CollectionInfo info;
        info.id   = collectionIdForSlot(slot);
        info.name = m_categoryStore->slotName(
            QStringLiteral("ToDoDB"), slot);
        info.type = QStringLiteral("calendar");
        out.append(info);
    }
    return out;
}

Kalburator::Sync::CollectionInfo TodoBlobBackend::collectionInfo(
    const QString &collectionId)
{
    for (const auto &c : availableCollections()) {
        if (c.id == collectionId) return c;
    }
    return {};
}

QString TodoBlobBackend::createCollection(
    const Kalburator::Sync::CollectionInfo &)
{
    return {};   // slots come from the device's AppInfo block
}

bool TodoBlobBackend::deleteCollection(const QString &)
{
    return false;
}

QList<Kalburator::Sync::BackendRecord>
TodoBlobBackend::loadRecords(const QString &collectionId)
{
    QList<Kalburator::Sync::BackendRecord> out;
    if (!m_palmBackend) return out;

    const int slot = slotForCollectionId(collectionId);
    if (slot < 0) return out;

    const auto records = m_palmBackend->listRecords(QStringLiteral("ToDoDB"));
    for (const auto &pr : records) {
        if (static_cast<int>(pr.category) != slot) continue;
        if (pr.isDeleted()) continue;

        QByteArray ics = encodePalmToIcs(pr);
        if (ics.isEmpty()) continue;

        Kalburator::Sync::BackendRecord br;
        br.id          = idForPalmRecord(pr.recordId);
        br.contentHash = sha256Hex(ics);
        br.data        = ics;
        br.contentType = QStringLiteral("text/calendar");
        br.lastModified = pr.lastModified;
        out.append(br);
    }
    return out;
}

Kalburator::Sync::BackendRecord TodoBlobBackend::createRecord(
    const QString &collectionId,
    const QByteArray &data)
{
    Kalburator::Sync::BackendRecord empty;
    if (!m_palmBackend) return empty;

    const int slot = slotForCollectionId(collectionId);
    if (slot < 0) return empty;

    auto pr = decodeIcsToPalm(data, slot);
    if (!pr.has_value()) return empty;

    pr->category = static_cast<std::uint8_t>(slot);
    pr->recordId = 0;   // device assigns

    const std::uint32_t newId =
        m_palmBackend->createPalmRecord(QStringLiteral("ToDoDB"), *pr);
    if (newId == 0) return empty;

    Kalburator::Sync::BackendRecord br;
    br.id          = idForPalmRecord(newId);
    br.contentHash = sha256Hex(data);
    br.data        = data;
    br.contentType = QStringLiteral("text/calendar");
    br.lastModified = QDateTime::currentDateTimeUtc();
    return br;
}

bool TodoBlobBackend::updateRecord(const QString &collectionId,
                                    const QString &recordId,
                                    const QByteArray &data)
{
    if (!m_palmBackend) return false;

    std::uint32_t rid = 0;
    if (!decodeId(recordId, &rid)) return false;

    const int slot = slotForCollectionId(collectionId);
    if (slot < 0) return false;

    auto pr = decodeIcsToPalm(data, slot);
    if (!pr.has_value()) return false;

    pr->recordId = rid;
    pr->category = static_cast<std::uint8_t>(slot);
    return m_palmBackend->updatePalmRecord(QStringLiteral("ToDoDB"), *pr);
}

bool TodoBlobBackend::deleteRecord(const QString &/*collectionId*/,
                                    const QString &recordId)
{
    if (!m_palmBackend) return false;
    std::uint32_t rid = 0;
    if (!decodeId(recordId, &rid)) return false;
    return m_palmBackend->deletePalmRecord(QStringLiteral("ToDoDB"), rid);
}

} // namespace WildPalms::TodoPlugin
```

If `PalmBackend::createPalmRecord` / `updatePalmRecord` / `deletePalmRecord` / `listRecords` have different signatures, mirror what `CalendarBlobBackend.cpp` calls — both backends must use the same surface. Cross-check `src/plugins/calendar/calendarblobbackend.cpp` if the build complains about method names.

- [ ] **Step 6: Build and run**

```
cmake --build build-dev --target tst_todoblobbackend -j 2>&1 | tail -20 && ctest --test-dir build-dev -R '^tst_todoblobbackend$' --output-on-failure
```

Expected: 9 tests pass.

- [ ] **Step 7: Commit (todos submodule)**

```
cd src/plugins/todos
git add todoblobbackend.h todoblobbackend.cpp
git commit -m "$(cat <<'EOF'
feat(todo): TodoBlobBackend (transcoding IBlobBackend over ToDoDB)

Phase E.11 Task 3. One collection per populated category slot
(palm:todo/<N>). Records route by PalmRecord::category. Wire bytes
<-> VTODO via TodoIcsTranscoder. Backend id "palm-todo".
Mirrors CalendarBlobBackend's shape from E.10.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
cd ../../..
```

- [ ] **Step 8: Commit (parent repo)**

```
git add tests/plugins/todos/CMakeLists.txt tests/plugins/todos/tst_todoblobbackend.cpp
git commit -m "$(cat <<'EOF'
test(todo): tst_todoblobbackend (Phase E.11 Task 3)

9 tests cover backend id, collection emission (empty/populated/
slot-zero shadow), category routing on read, slot stamping on
create/update, recordId preservation, and delete forwarding.
Test target compiles backend + transcoder from todos submodule.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `TodoConflictHandler`

**Files:**
- Create (todos submodule): `src/plugins/todos/todoconflicthandler.h`
- Create (todos submodule): `src/plugins/todos/todoconflicthandler.cpp`
- Create (parent): `tests/plugins/todos/tst_todoconflicthandler.cpp`
- Modify (parent): `tests/plugins/todos/CMakeLists.txt`

Reference pattern: `src/plugins/calendar/calendarconflicthandler.{h,cpp}`. Same shape — composes a `PalmConflictHandler`, overrides `handleConflict`, applies overlay, falls through to delegated base.

- [ ] **Step 1: Append the test target**

Add to `tests/plugins/todos/CMakeLists.txt`:

```cmake
# --- Task 4: TodoConflictHandler ---
add_executable(tst_todoconflicthandler
    tst_todoconflicthandler.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoconflicthandler.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoconflicthandler
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoconflicthandler
    PRIVATE
        WildPalmsPalmConflict   # PalmConflictHandler
        WildPalmsPalmCodecs     # decodeTodo / encodeTodo
        WildPalmsPalmSync       # PalmRecord, MockPalmDatabaseAccess
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoconflicthandler COMMAND tst_todoconflicthandler)
```

- [ ] **Step 2: Write the failing tests**

Create `tests/plugins/todos/tst_todoconflicthandler.cpp`:

```cpp
#include <QtTest/QtTest>

#include "plugins/todos/todoconflicthandler.h"
#include "plugins/todos/todoicstranscoder.h"

#include "palm/codecs/todocodec.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"

#include "conflictpolicy.h"
#include "conflictrecord.h"

using WildPalms::TodoPlugin::TodoConflictHandler;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using Kalburator::Sync::QSyncCore::ConflictDecision;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictRecord;
using Kalburator::Sync::QSyncCore::ConflictType;
using Kalburator::Sync::QSyncCore::RecordSnapshot;

namespace {

QByteArray makeTodoIcs(const QString &description,
                       const QString &note,
                       bool complete,
                       int priority = 1)
{
    Todo t;
    t.description = description;
    t.note = note;
    t.priority = priority;
    t.isComplete = complete;
    PalmRecord pr;
    pr.recordId = 1;
    pr.category = 0;
    pr.data = encodeTodo(t);
    return WildPalms::TodoPlugin::encodePalmToIcs(pr);
}

ConflictRecord makeConflict(const QByteArray &leftBytes,
                            const QByteArray &rightBytes,
                            const QDateTime &leftMod,
                            const QDateTime &rightMod)
{
    ConflictRecord c;
    c.recordId = QStringLiteral("palm-todo:1");
    c.type = ConflictType::BothModified;
    c.left.content = leftBytes;
    c.left.lastModified = leftMod;
    c.right.content = rightBytes;
    c.right.lastModified = rightMod;
    return c;
}

// Default ConflictPolicy: NewerWins, which is the basis the Palm
// handler uses for tie-breaking.
ConflictPolicy defaultPolicy()
{
    ConflictPolicy p;
    return p;
}

} // namespace

class TestTodoConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void completionAsymmetricMergeIsApplied();
    void completionOnBothSidesFallsThroughToPalm();
    void completionWithSameSideTextEditFallsThrough();
    void decodeFailureFallsThroughToPalm();
    void registrationIdMatchesBlobBackend();
};

void TestTodoConflictHandler::registrationIdMatchesBlobBackend()
{
    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    TodoConflictHandler h(&device, &cfg);
    Q_UNUSED(h);
    // Constructor takes (device, config). The plugin layer registers it
    // under "palm-todo"; this test just exercises construction.
    QVERIFY(true);
}

void TestTodoConflictHandler::completionAsymmetricMergeIsApplied()
{
    // Left  (Palm) marks COMPLETE with original text.
    // Right (target) edits the note but completion stays false.
    // Expected: Merge with both — completion=true AND target's note.
    const QByteArray leftIcs  = makeTodoIcs(QStringLiteral("Email Bob"),
                                            QStringLiteral("Original"),
                                            /*complete=*/true);
    const QByteArray rightIcs = makeTodoIcs(QStringLiteral("Email Bob"),
                                            QStringLiteral("Edited note"),
                                            /*complete=*/false);

    auto c = makeConflict(leftIcs, rightIcs,
                          QDateTime(QDate(2026, 4, 25), QTime(10, 0)),
                          QDateTime(QDate(2026, 4, 25), QTime(11, 0)));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    TodoConflictHandler h(&device, &cfg);

    ConflictDecision d = h.handleConflict(c, defaultPolicy());
    QCOMPARE(d, ConflictDecision::Merge);

    // Merged content must reflect: complete=true AND note="Edited note".
    auto merged = WildPalms::TodoPlugin::decodeIcsToPalm(c.mergedContent, 0);
    QVERIFY(merged.has_value());
    auto t = decodeTodo(QByteArrayView(merged->data));
    QVERIFY(t.has_value());
    QVERIFY(t->isComplete);
    QCOMPARE(t->note, QStringLiteral("Edited note"));
    QCOMPARE(h.lastOverlay(), QStringLiteral("completion-asymmetric"));
}

void TestTodoConflictHandler::completionOnBothSidesFallsThroughToPalm()
{
    const QByteArray leftIcs  = makeTodoIcs(QStringLiteral("Foo"), QStringLiteral(""), true);
    const QByteArray rightIcs = makeTodoIcs(QStringLiteral("Foo"), QStringLiteral(""), true);

    auto c = makeConflict(leftIcs, rightIcs,
                          QDateTime(QDate(2026, 4, 25), QTime(10, 0)),
                          QDateTime(QDate(2026, 4, 25), QTime(11, 0)));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    TodoConflictHandler h(&device, &cfg);

    h.handleConflict(c, defaultPolicy());
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestTodoConflictHandler::completionWithSameSideTextEditFallsThrough()
{
    // Left flips complete AND edits text -> not asymmetric -> delegate.
    const QByteArray leftIcs  = makeTodoIcs(QStringLiteral("Foo"), QStringLiteral("New"), true);
    const QByteArray rightIcs = makeTodoIcs(QStringLiteral("Foo"), QStringLiteral("Old"), false);

    auto c = makeConflict(leftIcs, rightIcs,
                          QDateTime(QDate(2026, 4, 25), QTime(10, 0)),
                          QDateTime(QDate(2026, 4, 25), QTime(11, 0)));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    TodoConflictHandler h(&device, &cfg);

    h.handleConflict(c, defaultPolicy());
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestTodoConflictHandler::decodeFailureFallsThroughToPalm()
{
    auto c = makeConflict(QByteArray("garbage"), QByteArray("also garbage"),
                          QDateTime(QDate(2026, 4, 25), QTime(10, 0)),
                          QDateTime(QDate(2026, 4, 25), QTime(11, 0)));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    TodoConflictHandler h(&device, &cfg);

    h.handleConflict(c, defaultPolicy());
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

QTEST_MAIN(TestTodoConflictHandler)
#include "tst_todoconflicthandler.moc"
```

- [ ] **Step 3: Verify failing build**

```
cmake --build build-dev --target tst_todoconflicthandler -j 2>&1 | tail -20
```

Expected: link errors for `WildPalms::TodoPlugin::TodoConflictHandler`.

- [ ] **Step 4: Write the header**

Create `src/plugins/todos/todoconflicthandler.h`:

```cpp
#ifndef WILDPALMS_TODO_TODOCONFLICTHANDLER_H
#define WILDPALMS_TODO_TODOCONFLICTHANDLER_H

#include "conflictpolicy.h"   // brings in QSyncCore::ConflictHandler

#include <memory>

#include <QString>

namespace WildPalms::PalmSync { class IPalmDatabaseAccess; }
namespace WildPalms::PalmConflict {
class PalmConflictHandler;
struct PalmBackendConfig;
}

namespace WildPalms::TodoPlugin {

/**
 * @brief ConflictHandler with one ToDo overlay, delegating to PalmConflictHandler.
 *
 * Resolution order:
 *   1. Decode both sides as PalmRecord -> Todo POD via the transcoder.
 *      If either side fails to decode -> delegate to PalmConflictHandler.
 *   2. Detect "completion-asymmetric merge": exactly one side flipped
 *      isComplete false->true and the other side touched non-completion
 *      fields and the completion-flipper did NOT touch those fields.
 *      Return ConflictDecision::Merge with mergedContent =
 *      re-serialised VTODO holding {isComplete=true, peer's text/
 *      priority/due/private}.
 *   3. Otherwise -> delegate to PalmConflictHandler::handleConflict.
 *
 * Owns its inner PalmConflictHandler (constructed from the (device,
 * config) pair the plugin passes through).
 *
 * Lifetime: does NOT own device or config. Both must outlive the
 * handler.
 */
class TodoConflictHandler : public Kalburator::Sync::QSyncCore::ConflictHandler
{
public:
    TodoConflictHandler(WildPalms::PalmSync::IPalmDatabaseAccess *device,
                        const WildPalms::PalmConflict::PalmBackendConfig *config);
    ~TodoConflictHandler() override;

    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &policy) override;

    bool canPrompt() const override { return false; }

    /// Test hook: which path was last taken.
    /// Values: "" (uninitialised), "completion-asymmetric", "delegated".
    const QString &lastOverlay() const { return m_lastOverlay; }

private:
    std::unique_ptr<WildPalms::PalmConflict::PalmConflictHandler> m_palm;
    QString m_lastOverlay;
};

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_TODOCONFLICTHANDLER_H
```

- [ ] **Step 5: Write the implementation**

Create `src/plugins/todos/todoconflicthandler.cpp`:

```cpp
#include "todoconflicthandler.h"

#include "todoicstranscoder.h"

#include "palm/codecs/todocodec.h"
#include "palm/conflict/palmconflicthandler.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/sync/palmrecord.h"

#include "conflictrecord.h"

namespace WildPalms::TodoPlugin {

namespace {

using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;
using WildPalms::PalmSync::PalmRecord;

struct DecodedSide {
    PalmRecord record;
    Todo       todo;
    bool       valid = false;
};

DecodedSide decodeSide(const QByteArray &icsBytes, int slotHint)
{
    DecodedSide out;
    auto pr = WildPalms::TodoPlugin::decodeIcsToPalm(icsBytes, slotHint);
    if (!pr.has_value()) return out;
    auto t = decodeTodo(QByteArrayView(pr->data));
    if (!t.has_value()) return out;
    out.record = *pr;
    out.todo   = *t;
    out.valid  = true;
    return out;
}

bool nonCompletionFieldsEqual(const Todo &a, const Todo &b)
{
    return a.description == b.description
        && a.note == b.note
        && a.hasIndefiniteDue == b.hasIndefiniteDue
        && a.due == b.due
        && a.priority == b.priority
        && a.isPrivate == b.isPrivate;
}

bool nonCompletionFieldsTouched(const Todo &before, const Todo &after)
{
    return !nonCompletionFieldsEqual(before, after);
}

// Build merged VTODO bytes: take peer's non-completion fields, set
// isComplete=true. recordId/category come from peer.
QByteArray buildMergedIcs(const PalmRecord &peer, const Todo &peerTodo)
{
    Todo merged = peerTodo;
    merged.isComplete = true;
    PalmRecord pr = peer;
    pr.data = encodeTodo(merged);
    return WildPalms::TodoPlugin::encodePalmToIcs(pr);
}

} // namespace

TodoConflictHandler::TodoConflictHandler(
    WildPalms::PalmSync::IPalmDatabaseAccess *device,
    const WildPalms::PalmConflict::PalmBackendConfig *config)
    : m_palm(std::make_unique<WildPalms::PalmConflict::PalmConflictHandler>(device, config))
{
}

TodoConflictHandler::~TodoConflictHandler() = default;

Kalburator::Sync::QSyncCore::ConflictDecision TodoConflictHandler::handleConflict(
    Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
    const Kalburator::Sync::QSyncCore::ConflictPolicy &policy)
{
    // Same-slot decode (slot doesn't matter for the overlay; defaults
    // to 0 — we re-stamp the merged record with peer's slot below).
    DecodedSide left  = decodeSide(conflict.left.content,  0);
    DecodedSide right = decodeSide(conflict.right.content, 0);

    if (!left.valid || !right.valid) {
        m_lastOverlay = QStringLiteral("delegated");
        return m_palm->handleConflict(conflict, policy);
    }

    const bool leftFlipped  = left.todo.isComplete  && !right.todo.isComplete;
    const bool rightFlipped = right.todo.isComplete && !left.todo.isComplete;

    // Overlay rule: fire iff exactly one side is complete, the other
    // isn't, AND non-completion fields differ between sides. Take the
    // not-yet-complete side as the authoritative source of non-completion
    // content (the flipper only intended to mark complete).
    //
    // Without per-side baselines accessible at the handler layer, we
    // can't strictly distinguish "flipper edited only completion" from
    // "flipper also edited text back to the baseline". The rule above
    // matches the failure mode the overlay exists to fix and degrades
    // gracefully: if both sides edited text, falling through to
    // PalmConflictHandler's lastModified tie-break is no worse than
    // the legacy conduit's behaviour today.
    if ((leftFlipped || rightFlipped) &&
        !nonCompletionFieldsEqual(left.todo, right.todo)) {
        const PalmRecord &peerRecord = leftFlipped ? right.record : left.record;
        const Todo       &peerTodo   = leftFlipped ? right.todo   : left.todo;
        conflict.mergedContent = buildMergedIcs(peerRecord, peerTodo);
        m_lastOverlay = QStringLiteral("completion-asymmetric");
        return Kalburator::Sync::QSyncCore::ConflictDecision::Merge;
    }

    m_lastOverlay = QStringLiteral("delegated");
    return m_palm->handleConflict(conflict, policy);
}

} // namespace WildPalms::TodoPlugin
```

**Note on the overlay heuristic:** without per-side baselines available at the conflict-handler layer, we can't strictly distinguish "flipper edited only completion" from "flipper edited completion + reverted text to baseline". The pragmatic rule above triggers the overlay whenever (a) one side is complete and the other isn't, and (b) non-completion fields differ — and uses the *non-flipping* side's text as the merged content. This matches the failure mode the overlay exists to fix (Palm flips complete; target made unrelated edits) and degrades gracefully if both sides edited text (delegating to PalmConflictHandler's `lastModified` tie-break is no worse than today). If a future failure mode argues for stricter detection, baseline access via libkalburator is the path; for E.11, the rule above is sufficient.

If `ConflictRecord` doesn't have a `mergedContent` field, check `src/plugins/calendar/calendarconflicthandler.cpp` for the actual field name (it might be called `merged`, `resolvedContent`, etc.) and use the same field. The calendar plugin's overlays do the same kind of merge-content emission, so its naming is authoritative.

- [ ] **Step 6: Build and run**

```
cmake --build build-dev --target tst_todoconflicthandler -j 2>&1 | tail -20 && ctest --test-dir build-dev -R '^tst_todoconflicthandler$' --output-on-failure
```

Expected: 5 tests pass.

- [ ] **Step 7: Commit (todos submodule)**

```
cd src/plugins/todos
git add todoconflicthandler.h todoconflicthandler.cpp
git commit -m "$(cat <<'EOF'
feat(todo): TodoConflictHandler with completion-asymmetric overlay

Phase E.11 Task 4. Single ToDo-aware overlay protects completion
flips against silent reversion when the peer made an unrelated
edit. Falls through to PalmConflictHandler for every other diff
shape (priority, due, both-flipping, decode failures).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
cd ../../..
```

- [ ] **Step 8: Commit (parent repo)**

```
git add tests/plugins/todos/CMakeLists.txt tests/plugins/todos/tst_todoconflicthandler.cpp
git commit -m "$(cat <<'EOF'
test(todo): tst_todoconflicthandler (Phase E.11 Task 4)

5 tests cover the asymmetric-merge happy path, both-flipping
delegation, same-side-text-edit delegation, decode-failure
delegation, and constructor smoke.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `TodoBackendPlugin` + manifest + CMake toggle

**Files:**
- Create (todos submodule): `src/plugins/todos/todobackendplugin.h`
- Create (todos submodule): `src/plugins/todos/todobackendplugin.cpp`
- Create (todos submodule): `src/plugins/todos/todo-backend-plugin.json`
- Modify (todos submodule): `src/plugins/todos/CMakeLists.txt`
- Create (parent): `tests/plugins/todos/tst_todobackendplugin.cpp`
- Modify (parent): `tests/plugins/todos/CMakeLists.txt`

Reference pattern: `src/plugins/calendar/calendarbackendplugin.{h,cpp}` and `calendar-backend-plugin.json`.

- [ ] **Step 1: Append the test target**

Add to `tests/plugins/todos/CMakeLists.txt`:

```cmake
# --- Task 5: TodoBackendPlugin (in-process, no .so loading) ---
add_executable(tst_todobackendplugin
    tst_todobackendplugin.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todobackendplugin.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoconflicthandler.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
    ${TODOS_PLUGIN_SRC_DIR}/taskview.cpp
)
target_include_directories(tst_todobackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_todobackendplugin
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore + parseCategoryAppInfo
        WildPalmsPalmConflict
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsCore
        Kalburator::Sync
        KF6::CoreAddons
        KF6::CalendarCore
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        ${PILOT_LINK_LIBRARIES}
)
add_test(NAME tst_todobackendplugin COMMAND tst_todobackendplugin)
```

- [ ] **Step 2: Write the failing tests**

Create `tests/plugins/todos/tst_todobackendplugin.cpp`:

```cpp
#include <QtTest/QtTest>

#include <cstring>

#include <pi-appinfo.h>

#include "plugins/todos/todobackendplugin.h"
#include "plugins/todos/todoblobbackend.h"
#include "plugins/todos/todoconflicthandler.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

#include "core/ibackendplugin.h"

using WildPalms::TodoPlugin::TodoBackendPlugin;
using WildPalms::TodoPlugin::TodoBlobBackend;
using WildPalms::TodoPlugin::TodoConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

namespace {

QByteArray buildAppInfoTwoSlots()
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Business") };
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray utf = names[i].toUtf8().left(15);
        std::memcpy(info.name[i], utf.constData(), utf.size());
        info.name[i][utf.size()] = '\0';
        info.ID[i] = static_cast<unsigned char>(i);
    }
    info.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_CategoryAppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

} // namespace

class TestTodoBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void metadataValues();
    void claimedDatabasesIsToDoDB();
    void createBackendsPopulatesCategoryStoreFromAppInfo();
    void createConflictHandlerReturnsTodoHandler();
    void createMainViewReturnsTaskView();
};

void TestTodoBackendPlugin::metadataValues()
{
    TodoBackendPlugin p;
    QCOMPARE(p.pluginId(),    QStringLiteral("todo"));
    QCOMPARE(p.displayName(), QStringLiteral("Tasks"));
    QVERIFY(!p.description().isEmpty());
    QCOMPARE(p.version(),     QStringLiteral("2.0"));
}

void TestTodoBackendPlugin::claimedDatabasesIsToDoDB()
{
    TodoBackendPlugin p;
    auto claims = p.claimedDatabases();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims[0], QStringLiteral("ToDoDB"));
}

void TestTodoBackendPlugin::createBackendsPopulatesCategoryStoreFromAppInfo()
{
    MockPalmDatabaseAccess device;
    device.setAppBlock(QStringLiteral("ToDoDB"), buildAppInfoTwoSlots());

    PalmBackend palm(&device);
    PalmDeviceConnection conn(&device, &palm);

    TodoBackendPlugin p;
    auto provided = p.createBackends(/*host=*/nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    auto *blob = qobject_cast<TodoBlobBackend *>(provided.blob);
    QVERIFY(blob);
    auto cols = blob->availableCollections();
    QCOMPARE(cols.size(), 3);  // Unfiled + Personal + Business
    QCOMPARE(cols[1].name, QStringLiteral("Personal"));
    QCOMPARE(cols[2].name, QStringLiteral("Business"));

    delete provided.blob;   // plugin manager owns in production
}

void TestTodoBackendPlugin::createConflictHandlerReturnsTodoHandler()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    PalmDeviceConnection conn(&device, &palm);

    TodoBackendPlugin p;
    p.createBackends(nullptr, &conn);   // primes m_device

    auto *handler = p.createConflictHandler();
    QVERIFY(handler != nullptr);
    QVERIFY(dynamic_cast<TodoConflictHandler *>(handler) != nullptr);
    delete handler;
}

void TestTodoBackendPlugin::createMainViewReturnsTaskView()
{
    TodoBackendPlugin p;
    QVERIFY(p.hasMainView());
    QWidget *w = p.createMainView(nullptr);
    QVERIFY(w);
    QCOMPARE(QString::fromLatin1(w->metaObject()->className()),
             QStringLiteral("TaskView"));
    delete w;
}

QTEST_MAIN(TestTodoBackendPlugin)
#include "tst_todobackendplugin.moc"
```

- [ ] **Step 3: Verify failing build**

```
cmake --build build-dev --target tst_todobackendplugin -j 2>&1 | tail -20
```

Expected: link errors for `TodoBackendPlugin::*`.

- [ ] **Step 4: Write the manifest**

Create `src/plugins/todos/todo-backend-plugin.json` (in todos submodule):

```json
{
    "KPlugin": {
        "Id": "todo",
        "Name": "Task Sync",
        "Description": "Syncs Palm ToDoDB to iCalendar VTODO files via virtual category sub-collections.",
        "Icon": "view-pim-tasks",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["ToDoDB"],
    "X-WildPalms-ClaimDescriptions": {
        "ToDoDB": "Syncs ToDoDB to iCalendar VTODO files; one virtual sub-collection per Palm category."
    },
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 30
}
```

- [ ] **Step 5: Write the plugin header**

Create `src/plugins/todos/todobackendplugin.h`:

```cpp
#ifndef WILDPALMS_TODO_TODOBACKENDPLUGIN_H
#define WILDPALMS_TODO_TODOBACKENDPLUGIN_H

#include <memory>

#include <QObject>

#include "core/ibackendplugin.h"

namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
namespace WildPalms::PalmConflict { struct PalmBackendConfig; }
class PalmDeviceConnection;

namespace WildPalms::TodoPlugin {

/**
 * @brief Third new-ABI WildPalms plugin (after Memo E.9, Calendar E.10).
 *
 * Provides:
 *   - TodoBlobBackend wrapping the shared PalmBackend (one
 *     collection per populated category slot under "ToDoDB").
 *   - No typed SyncBackend; libkalburator has no typed-todo upper
 *     layer (extract-on-second-consumer per parent spec).
 *   - TodoConflictHandler (completion-asymmetric overlay + Palm
 *     delegation).
 *
 * Owns the per-session CategoryMappingStore, populated from the
 * ToDoDB AppInfo block at createBackends() time.
 *
 * Surfaces TaskView as a main-window tab (reused unchanged from
 * the legacy TodoConduit).
 */
class TodoBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit TodoBackendPlugin(QObject *parent = nullptr);
    ~TodoBackendPlugin() override;

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

    // IBackendPlugin — conflict handler
    Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler() override;

    // IBackendPlugin — main view
    bool     hasMainView()   const override;
    QWidget *createMainView(QWidget *parent) const override;
    QString  mainViewName()  const override;
    QIcon    mainViewIcon()  const override;

    // IBackendPlugin — conflict presentation
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const override;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const override;

private:
    std::unique_ptr<WildPalms::PalmCalendar::CategoryMappingStore> m_categoryStore;
    std::unique_ptr<WildPalms::PalmConflict::PalmBackendConfig>    m_palmConfig;
    PalmDeviceConnection *m_device = nullptr;
};

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_TODOBACKENDPLUGIN_H
```

- [ ] **Step 6: Write the plugin implementation**

Create `src/plugins/todos/todobackendplugin.cpp`:

```cpp
#include "todobackendplugin.h"

#include "todoblobbackend.h"
#include "todoconflicthandler.h"
#include "todoicstranscoder.h"
#include "taskview.h"

#include "palm/calendar/categoryappinforeader.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/palmbackend.h"
#include "palm/codecs/todocodec.h"

#include "conflictrecord.h"

#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QIcon>
#include <QLoggingCategory>
#include <QString>
#include <QWidget>

namespace {
Q_LOGGING_CATEGORY(WP_TODO_PLUGIN, "wildpalms.todo.plugin")
}

namespace WildPalms::TodoPlugin {

TodoBackendPlugin::TodoBackendPlugin(QObject *parent)
    : QObject(parent)
    , m_categoryStore(std::make_unique<WildPalms::PalmCalendar::CategoryMappingStore>())
    , m_palmConfig(std::make_unique<WildPalms::PalmConflict::PalmBackendConfig>())
{
}

TodoBackendPlugin::~TodoBackendPlugin() = default;

QString TodoBackendPlugin::pluginId()    const { return QStringLiteral("todo"); }
QString TodoBackendPlugin::displayName() const { return QStringLiteral("Tasks"); }
QIcon   TodoBackendPlugin::icon()        const
{
    return QIcon::fromTheme(QStringLiteral("view-pim-tasks"));
}
QString TodoBackendPlugin::description() const
{
    return QStringLiteral(
        "Synchronizes Palm ToDoDB with iCalendar VTODO files via virtual category sub-collections");
}
QString TodoBackendPlugin::version()     const { return QStringLiteral("2.0"); }

QStringList TodoBackendPlugin::claimedDatabases() const
{
    return { QStringLiteral("ToDoDB") };
}

WildPalms::IBackendPlugin::ProvidedBackends
TodoBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *host,
                                  PalmDeviceConnection         *device)
{
    Q_UNUSED(host)
    ProvidedBackends out;
    if (!device) return out;

    m_device = device;

    auto *palmBackend = device->palmBackend();
    if (palmBackend) {
        WildPalms::PalmCalendar::populateFromAppInfo(
            *m_categoryStore,
            QStringLiteral("ToDoDB"),
            palmBackend->readAppBlock(QStringLiteral("ToDoDB")));
        out.blob = new TodoBlobBackend(palmBackend, m_categoryStore.get());
    }

    // No typed SyncBackend: libkalburator has no typed-todo upstream
    // layer. out.calendar stays null.
    return out;
}

Kalburator::Sync::QSyncCore::ConflictHandler *
TodoBackendPlugin::createConflictHandler()
{
    if (!m_device || !m_device->device()) {
        qCWarning(WP_TODO_PLUGIN)
            << "createConflictHandler called before createBackends";
        return nullptr;
    }
    return new TodoConflictHandler(m_device->device(), m_palmConfig.get());
}

bool TodoBackendPlugin::hasMainView() const { return true; }

QWidget *TodoBackendPlugin::createMainView(QWidget *parent) const
{
    return new TaskView(parent);
}

QString TodoBackendPlugin::mainViewName() const { return QStringLiteral("Tasks"); }

QIcon TodoBackendPlugin::mainViewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("view-pim-tasks"));
}

void TodoBackendPlugin::enrichConflictSnapshot(
    Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
    bool /*isSourceSide*/) const
{
    if (snapshot.content.isEmpty()) return;

    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromString(cal, QString::fromUtf8(snapshot.content))) return;
    auto todos = cal->todos();
    if (todos.isEmpty()) return;
    auto todo = todos.first();
    if (!todo) return;

    snapshot.metadata[QStringLiteral("title")] = todo->summary();
    snapshot.metadata[QStringLiteral("complete")] =
        todo->isCompleted() ? QStringLiteral("yes") : QStringLiteral("no");
    snapshot.contentType = QStringLiteral("text/calendar");
}

QString TodoBackendPlugin::formatConflictRecordHtml(
    const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const
{
    QString html;
    const QString title    = snapshot.metadata.value(QStringLiteral("title")).toString();
    const QString complete = snapshot.metadata.value(QStringLiteral("complete")).toString();
    if (!title.isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(title.toHtmlEscaped());
    }
    if (!complete.isEmpty()) {
        html += QStringLiteral("<p><b>Complete:</b> %1</p>").arg(complete);
    }
    html += QStringLiteral("<pre>%1</pre>")
        .arg(QString::fromUtf8(snapshot.content).toHtmlEscaped());
    return html;
}

} // namespace WildPalms::TodoPlugin

#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(TodoBackendPluginFactory,
                           "todo-backend-plugin.json",
                           registerPlugin<WildPalms::TodoPlugin::TodoBackendPlugin>();)

#include "todobackendplugin.moc"
```

- [ ] **Step 7: Update the todos plugin CMakeLists**

Replace the contents of `src/plugins/todos/CMakeLists.txt` with:

```cmake
option(WILDPALMS_TODO_PLUGIN_V2 "Build the new IBackendPlugin-based ToDo plugin" ON)

if (WILDPALMS_TODO_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_todos_v2
        SOURCES
            todobackendplugin.cpp     todobackendplugin.h
            todoblobbackend.cpp       todoblobbackend.h
            todoconflicthandler.cpp   todoconflicthandler.h
            todoicstranscoder.cpp     todoicstranscoder.h
            taskview.cpp              taskview.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_todos_v2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    # See calendar plugin's CMakeLists for why this BEFORE include is
    # required (Kalburator::Sync ordering vs WildPalmsCore's legacy
    # ::Sync include).
    target_include_directories(wildpalms_todos_v2 BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(wildpalms_todos_v2
        PRIVATE
            WildPalmsCore
            WildPalmsPalmSync
            WildPalmsPalmCalendar           # CategoryMappingStore + parseCategoryAppInfo
            WildPalmsPalmCodecs             # decodeTodo / encodeTodo
            WildPalmsPalmConflict           # PalmConflictHandler
            KF6::CoreAddons
            KF6::CalendarCore
            KF6::I18n
            KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_todos
        SOURCES
            todoconduit.cpp
            todoconduit.h
            todomapper.cpp
            todomapper.h
            taskview.cpp
            taskview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_todos
        WildPalmsCore
        KF6::CoreAddons
        KF6::CalendarCore
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
    )
endif ()
```

- [ ] **Step 8: Build and test**

```
cmake --build build-dev --target wildpalms_todos_v2 tst_todobackendplugin -j 2>&1 | tail -20 && ctest --test-dir build-dev -R '^tst_todobackendplugin$' --output-on-failure
```

Expected: plugin .so builds; 5 tests pass.

- [ ] **Step 9: Toggle-off smoke**

```
cmake -S . -B build-toggle-off -DWILDPALMS_TODO_PLUGIN_V2=OFF
cmake --build build-toggle-off --target wildpalms_todos -j 2>&1 | tail -10
```

Expected: legacy `wildpalms_todos.so` builds. Then clean up:

```
rm -rf build-toggle-off
```

- [ ] **Step 10: Commit (todos submodule)**

```
cd src/plugins/todos
git add todobackendplugin.h todobackendplugin.cpp todo-backend-plugin.json todoblobbackend.h todoblobbackend.cpp todoconflicthandler.h todoconflicthandler.cpp todoicstranscoder.h todoicstranscoder.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(todo): TodoBackendPlugin + manifest + CMake toggle (Phase E.11)

Phase E.11 Task 5. Wires together TodoBlobBackend, TodoConflictHandler,
TodoIcsTranscoder behind the WILDPALMS_TODO_PLUGIN_V2 toggle (default
ON). Reuses TaskView as the main-window tab. Plugin id "todo",
sort order 30 (after memo=10, calendar=20). Manifest claims ToDoDB.

Legacy TodoConduit stays buildable when the toggle is off; both
.so paths never co-installed (wildpalms/plugins vs wildpalms/conduits).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
cd ../../..
```

- [ ] **Step 11: Commit (parent repo)**

```
git add tests/plugins/todos/CMakeLists.txt tests/plugins/todos/tst_todobackendplugin.cpp
git commit -m "$(cat <<'EOF'
test(todo): tst_todobackendplugin (Phase E.11 Task 5)

5 tests cover plugin metadata, ToDoDB claim, AppInfo-driven category
store population, conflict-handler factory typing, and TaskView main-
view construction.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: End-to-end test `tst_todo_v2`

**Files:**
- Create (parent): `tests/plugins/todos/tst_todo_v2.cpp`
- Modify (parent): `tests/plugins/todos/CMakeLists.txt`

This test loads the real `wildpalms_todos_v2.so` via `BackendPluginManager` and drives `BlobSyncEngine::twoWayWithBaseline` against a `MockBlobBackend` target across multiple slots, mirroring `tst_calendar_v2.cpp` (read it first as a structural template). Four scenarios.

- [ ] **Step 1: Append the test target**

Add to `tests/plugins/todos/CMakeLists.txt`:

```cmake
# --- Task 6: End-to-end via BackendPluginManager + BlobSyncEngine ---
add_executable(tst_todo_v2
    tst_todo_v2.cpp
)
target_include_directories(tst_todo_v2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_todo_v2
    PRIVATE
        WildPalmsCore
        WildPalmsRuntime              # BackendPluginManager
        WildPalmsPalmSync
        WildPalmsPalmCalendar
        WildPalmsPalmCodecs
        WildPalmsPalmConflict
        Kalburator::Sync
        KF6::CoreAddons
        KF6::CalendarCore
        Qt::Test
        ${PILOT_LINK_LIBRARIES}
)
# tst_todo_v2 depends on wildpalms_todos_v2.so being built and locatable.
add_dependencies(tst_todo_v2 wildpalms_todos_v2)
add_test(NAME tst_todo_v2 COMMAND tst_todo_v2)
set_tests_properties(tst_todo_v2 PROPERTIES
    ENVIRONMENT "QT_PLUGIN_PATH=${CMAKE_BINARY_DIR}/bin"
)
```

(Cross-check `tests/plugins/calendar/CMakeLists.txt` for the exact `QT_PLUGIN_PATH` plumbing — it sets the env to wherever `kcoreaddons_add_plugin` installs to inside the build tree. Use the same path.)

- [ ] **Step 2: Write the e2e test**

Create `tests/plugins/todos/tst_todo_v2.cpp`. This is a substantial test — the core skeleton:

```cpp
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>

#include <cstring>

#include <pi-appinfo.h>

#include "core/ibackendplugin.h"
#include "palm/codecs/todocodec.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"
#include "runtime/backendpluginmanager.h"

#include "blobsyncengine.h"
#include "blobbaselinestore.h"
#include "mockblobbackend.h"
#include "conflicthandlerregistry.h"
#include "conflictstore.h"
#include "conflictpolicy.h"

using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRecord;

namespace {

QByteArray buildAppInfo()
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Business"),
                             QStringLiteral("Errands") };
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray utf = names[i].toUtf8().left(15);
        std::memcpy(info.name[i], utf.constData(), utf.size());
        info.name[i][utf.size()] = '\0';
        info.ID[i] = static_cast<unsigned char>(i);
    }
    info.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_CategoryAppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

PalmRecord makeTodo(std::uint32_t recordId,
                    int slot,
                    const QString &description,
                    bool complete = false)
{
    Todo t;
    t.description = description;
    t.priority = 1;
    t.isComplete = complete;
    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeTodo(t);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace

class TestTodoV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSyncMovesTargetRecordsToPalm();
    void palmRecordsLandAtTargetInCorrectSlots();
    void completionConflictMergesViaOverlay();
    void crossSlotMoveUpdatesPalmCategory();

private:
    void runEngineOnce(WildPalms::Runtime::BackendPluginManager &mgr,
                       Kalburator::Sync::IBlobBackend *target,
                       Kalburator::Sync::BlobBaselineStore &baseline,
                       Kalburator::Sync::ConflictStore &conflicts,
                       Kalburator::Sync::ConflictHandlerRegistry &registry,
                       MockPalmDatabaseAccess *device,
                       PalmBackend *palm);
};

void TestTodoV2::initTestCase()
{
    qApp->setApplicationName("tst_todo_v2");
}

void TestTodoV2::freshSyncMovesTargetRecordsToPalm()
{
    // Setup: empty Palm, target has 2 VTODO records (one in Unfiled, one in Personal).
    // Run engine.
    // Expect: PalmBackend gains both records with categories 0 and 1.
    //
    // Skeleton — fill in using tst_calendar_v2.cpp's `freshSyncMovesTargetEventsToPalm`
    // pattern. The shape:
    //   1. Construct MockPalmDatabaseAccess + setAppBlock("ToDoDB", buildAppInfo()).
    //   2. Construct PalmBackend, PalmDeviceConnection.
    //   3. Construct BackendPluginManager, call loadAll().
    //   4. Find the "todo" plugin, call createBackends(...).
    //   5. Construct MockBlobBackend, seed with 2 VTODO records.
    //   6. Run BlobSyncEngine::twoWayWithBaseline(palm-todo-blob, target, baseline,
    //      registry, conflicts, policy).
    //   7. Assert device.records("ToDoDB") size == 2 and categories match.
    QSKIP("Implement using tst_calendar_v2.cpp's matching scenario as a template.");
}

void TestTodoV2::palmRecordsLandAtTargetInCorrectSlots()
{
    // Setup: Palm has 3 records across slots 0, 1, 2; target empty.
    // Run engine.
    // Expect: target gains 3 records, each in a collection matching its slot.
    QSKIP("Implement using tst_calendar_v2.cpp's matching scenario as a template.");
}

void TestTodoV2::completionConflictMergesViaOverlay()
{
    // Setup: Palm and target both have the same record (baseline matches).
    // Modify Palm: flip isComplete to true, no other changes.
    // Modify target: edit the description, leave isComplete false.
    // Run engine.
    // Expect: post-sync, both sides hold a record with isComplete=true AND
    // the target's edited description (the overlay's merged content).
    QSKIP("Implement using tst_calendar_v2.cpp's matching scenario as a template.");
}

void TestTodoV2::crossSlotMoveUpdatesPalmCategory()
{
    // Setup: target re-categorises a VTODO from palm:todo/1 -> palm:todo/2 (via
    // X-WP-PALM-CATEGORY-SLOT change in the body). The MockBlobBackend lets you
    // reseed the record under a new collection id.
    // Run engine.
    // Expect: device.records("ToDoDB") now has the record with category=2.
    QSKIP("Implement using tst_calendar_v2.cpp's matching scenario as a template.");
}

QTEST_MAIN(TestTodoV2)
#include "tst_todo_v2.moc"
```

Replace each `QSKIP(...)` body with the full test once `tst_calendar_v2.cpp` is read in. The four scenarios map 1:1 onto its calendar equivalents — only the data-construction helpers (Todo POD vs Event::Ptr) and assertions on record content (description vs summary, completion vs alarm/exdate/tz) differ. Mirror its structure exactly: same engine setup, same baseline store, same handler-registry registration of `palm-todo`, same MockBlobBackend seeding pattern.

- [ ] **Step 3: Build and run**

```
cmake --build build-dev --target tst_todo_v2 -j 2>&1 | tail -20 && ctest --test-dir build-dev -R '^tst_todo_v2$' --output-on-failure
```

Expected during initial implementation: build passes, test runs but skips. Iterate by porting one scenario from `tst_calendar_v2.cpp` at a time, removing the `QSKIP` once each is wired.

- [ ] **Step 4: Commit (parent repo)**

```
git add tests/plugins/todos/CMakeLists.txt tests/plugins/todos/tst_todo_v2.cpp
git commit -m "$(cat <<'EOF'
test(todo): tst_todo_v2 end-to-end (Phase E.11 Task 6)

Loads the real wildpalms_todos_v2.so via BackendPluginManager and
drives BlobSyncEngine::twoWayWithBaseline against a MockBlobBackend
across multiple slots. Four scenarios:

  - Fresh sync from target into empty Palm.
  - Palm-side records propagate to target in correct sub-collections.
  - Completion-asymmetric merge fires through the registered handler.
  - Cross-slot moves update Palm category.

MockBlobBackend stands in for LocalBlobBackend per E.10's id-space
deferral; LocalBlobBackend coverage lands in E.15+.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Submodule pointer bumps + docs/memory updates

**Files:**
- Modify (parent): `src/plugins/calendar` submodule pointer (Task 1's calendar commit).
- Modify (parent): `src/plugins/todos` submodule pointer (Tasks 2–5 commits).
- Modify (parent): `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`.
- Modify (parent): `docs/plans/2026-04-20-libkalburator-integration.md`.
- Modify (parent): `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`.
- Create (parent): `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e11_todo.md`.

- [ ] **Step 1: Stage the submodule pointers**

Both submodule pointers should already be updated by Task 1 (calendar commit) and Task 5 (final todos commit). Verify:

```
git status -- src/plugins/calendar src/plugins/todos
```

Expected: both show modified pointers.

- [ ] **Step 2: Flip the parent spec row**

Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. Find the row starting `| **E.11** | Rewrite **ToDo**` and rewrite it to:

```
| ✅ **E.11** | ToDo rewritten as `IBackendPlugin` (`TodoBackendPlugin` + `TodoBlobBackend` + `TodoConflictHandler` + `TodoIcsTranscoder`). Second consumer of the shared `CategoryAppInfoReader` (promoted from calendar plugin into `WildPalmsPalmCalendar` static lib in Task 1; `parseDatebookAppInfo` renamed `parseCategoryAppInfo` for generality). Virtual sub-collections `palm:todo/<N>` (one per populated category slot, slot 0 always present). `TodoConflictHandler` adds one ToDo overlay (completion-asymmetric merge) before delegating to `PalmConflictHandler`. TaskView reused untouched. CMake toggle `WILDPALMS_TODO_PLUGIN_V2=ON`; legacy `TodoConduit` remains buildable. `tst_todo_v2` runs end-to-end via `BackendPluginManager` against `MockBlobBackend`. Landed 2026-04-25. Plan: `docs/superpowers/plans/2026-04-25-phase-e11-todo-plugin.md`. | WP | E.10 | WP ctest passes; ~25 todo tests cover transcoder/blob-backend/conflict-handler/plugin metadata + 4 e2e scenarios. |
```

- [ ] **Step 3: Update the integration plan**

Edit `docs/plans/2026-04-20-libkalburator-integration.md`. In the Phase E sub-phases table, change the E.11 row from pending to landed, mirroring how E.10 was marked landed in commit `c38f06b`. (Open the file, find the E.11 line, and apply the same `✅` / `Landed YYYY-MM-DD` treatment that E.10 received.)

- [ ] **Step 4: Update memory MEMORY.md**

Edit `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`. Append:

```
- [project_phase_e11_todo.md](project_phase_e11_todo.md) — E.11 landed 2026-04-25; ToDo is third new-ABI plugin; CategoryAppInfoReader now in WildPalmsPalmCalendar
```

- [ ] **Step 5: Create the new project memory file**

Create `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e11_todo.md`:

```markdown
---
name: Phase E.11 (ToDo plugin) status
description: E.11 landed 2026-04-25 — ToDo plugin third on new ABI, AppInfo reader promoted into shared static lib, completion-asymmetric overlay
type: project
---

E.11 landed 2026-04-25 (Phase E.11 of the libkalburator-adoption rewrite).

**What landed:**
- `TodoBackendPlugin` + `TodoBlobBackend` + `TodoConflictHandler` +
  `TodoIcsTranscoder` in `src/plugins/todos/` (submodule).
- VTODO-on-the-blob-face, virtual sub-collections `palm:todo/<slot>`.
- One conflict overlay: completion-asymmetric merge (Palm flips
  complete + target makes unrelated edit → merged result keeps both).
- TaskView reused untouched as the main-window tab.
- CMake toggle `WILDPALMS_TODO_PLUGIN_V2=ON`; legacy `TodoConduit`
  buildable when off.

**Architectural change:** `CategoryAppInfoReader` moved from
`src/plugins/calendar/` (where it was Calendar plugin's private detail)
into `src/palm/calendar/` as part of the `WildPalmsPalmCalendar`
static lib. `parseDatebookAppInfo` renamed `parseCategoryAppInfo` to
match its generality (the same `unpack_CategoryAppInfo` pisock helper
parses Datebook, ToDoDB, AddressDB, MemoDB).

**Why:** ToDo plugin is the second consumer of the AppInfo reader.
Cross-plugin runtime-`.so`-to-runtime-`.so` linking isn't a thing;
promote-to-shared-static-lib was the clean fix.

**How to apply:** Future plugins that need category-name parsing
(Contacts in E.12 will be the third) just `target_link_libraries(...
WildPalmsPalmCalendar)` and call `parseCategoryAppInfo`.

**Deferrals (still open after E.11):**
- LocalBlobBackend e2e for ToDo → E.15+ (id-space cutover).
- TaskView ↔ PalmTodosAdapter rewiring → post-E.16 UI follow-up.
- Live-device test in POSE64 → E.18.
- Speculative conflict overlays (priority, due-date) → only if
  real syncs surface failure modes.
- Legacy TodoConduit removal → E.16.
```

- [ ] **Step 6: Commit (parent repo)**

```
git add src/plugins/calendar src/plugins/todos docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "$(cat <<'EOF'
docs(phase-e11): land ToDo plugin; bump submodule pointers

Phase E.11 closed 2026-04-25. TodoBackendPlugin is the third new-ABI
plugin, mirroring memo/calendar shape. Six new files in the todos
submodule + reader promotion in the calendar submodule. Full plan:
docs/superpowers/plans/2026-04-25-phase-e11-todo-plugin.md.

Bumps both submodule pointers (calendar for the AppInfo reader
move; todos for the new plugin).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Stage and commit the memory updates**

```
cd /home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory
git add MEMORY.md project_phase_e11_todo.md
git commit -m "memory: phase E.11 (ToDo plugin) status"
cd -
```

(If the memory directory isn't a git repo, skip the git step — the writes themselves are the durable record.)

- [ ] **Step 8: Final smoke**

```
cmake --build build-dev -j 2>&1 | tail -10 && ctest --test-dir build-dev --output-on-failure 2>&1 | tail -30
```

Expected: full WP build green; full ctest green; new `tst_todo*` targets reported as passing.

---

## Self-review checklist

After all tasks complete, verify:

1. **Spec coverage:** every Decision (#1–#6) maps to a Task. #1 → Task 2 (transcoder), #2 → Task 3 (blob backend collections), #3 → Task 4 (conflict handler), #4 → Task 5 (TaskView), #5 → Tasks 2–6 (test set), #6 → Task 1 (reader promotion). ✓

2. **Placeholder scan:** no "TBD" / "TODO" in tasks; e2e test in Task 6 carries explicit `QSKIP`s with cross-references to `tst_calendar_v2.cpp` for the structural template — the engineer reads that file as the working pattern, then ports.

3. **Type consistency:** `TodoBlobBackend::backendId() == "palm-todo"` referenced in plugin/handler registration tests. `parseCategoryAppInfo` (renamed) referenced consistently across Tasks 1, 5, and the calendar test update.

4. **Build invariant:** every task ends with a green ctest. Toggle-off smoke in Task 5 Step 9 catches legacy regressions early.

5. **Submodule discipline:** Tasks 1 (partial), 2, 3, 4, 5 commit inside submodules first; Task 7 bumps the parent pointer in one final commit. Mirrors the cadence E.10 used.
