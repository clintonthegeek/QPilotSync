# Phase E.6 — PalmCalendarBackend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land `PalmCalendarBackend : Kalburator::Sync::SyncBackend`, a
calendar-typed adapter that wraps `PalmBackend`'s datebook collection
and exposes virtual sub-calendars per Palm category slot. Ship a new
`DatebookCodec` (Palm `PalmRecord` bytes ↔ `KCalendarCore::Event::Ptr`)
and an in-memory `CategoryMappingStore` that feeds slot display names
into the backend.

**Architecture:** New static lib `WildPalmsPalmCalendar` at
`src/palm/calendar/`. Sibling to `WildPalmsPalmSync` (E.3),
`WildPalmsPalmDevice` (E.4), `WildPalmsPalmConflict` (E.5). Links
`WildPalmsPalmSync` (for `PalmBackend`, `PalmRecord`,
`IPalmDatabaseAccess`), `KF6::CalendarCore` (for
`Incidence`/`Event`/`Alarm`/`Recurrence`), `Kalburator::Sync` (for
`SyncBackend` + operation types), and `pisock` (for `pi-datebook.h`
which does the bit-level Datebook record pack/unpack).

The backend:

1. Borrows an `IPalmDatabaseAccess *` to read/write Palm records.
2. Borrows a `CategoryMappingStore *` to name virtual sub-calendars.
3. Exposes `palm:calendar/<slot>` IDs for slot ∈ 0..15. Slot 0 is
   always "Unfiled"; slots 1..15 surface only when named in the store.
4. Routes records to/from virtual calendars by `PalmRecord.category`.

**Why link pisock:** Implementing the Palm Datebook bit-layout from
scratch would duplicate ~800 lines of byte-parsing logic for no
isolation gain. `pisock`'s `unpack_Appointment` / `pack_Appointment`
are the proven reference implementations. The isolation boundary we've
held since E.3 is "no `WildPalmsCore` / `Qt::Widgets` / `KF6::XmlGui`
dep", not "no pisock". The spec's §"Directory layout" explicitly
locates codecs at `src/palm/codecs/` which use pisock.

**Why a fresh codec (not a bridge to the plugin's `CalendarMapper`):**
The existing `src/plugins/calendar/calendarmapper.{h,cpp}` takes the
legacy `PilotRecord*` type and lives inside a runtime plugin. Bridging
to it would drag `WildPalmsCore` + `Qt::Widgets` + `KF6::XmlGui` into
`WildPalmsPalmCalendar`. The fresh codec takes `PalmRecord` directly,
has no WP-runtime dep, and replaces the plugin mapper when E.10
rewrites the Calendar plugin. The temporary duplication is ~5
sub-phases, worth it to hold the boundary.

**Tech Stack:** C++20, Qt6 (Core, Test), Kalburator::Sync (for
`SyncBackend` + `FetchOperation`/`PushOperation`/`DeleteOperation`),
KF6::CalendarCore (for `KCalendarCore::Event`/`Incidence`/`Alarm`/
`Recurrence`), pisock (for `pi-datebook.h`). No new runtime dependency
beyond pisock, which the wider WP tree already links.

**Repo:** All work in `~/dev/WildPalms/`. No upstream changes.

**Scope not in E.6:**

- **AppInfo block parsing.** The `CategoryMappingStore` is populated
  by callers (tests + future app-layer wiring); real AppInfo block
  parsing from pisock's `dlp_ReadAppBlock` + `unpack_CategoryAppInfo`
  lands when live-device integration tests arrive (E.17/E.18 territory).
- **Operation async.** Operations complete synchronously — the device
  abstraction is fast enough and a worker thread is a runtime concern
  (E.16). `FetchOperation`/`PushOperation`/`DeleteOperation` still fire
  their `finished()` signal correctly.
- **Categorical-conflict remap in the handler.** E.5's handler has a
  simple "prefer non-zero category" rule that doesn't need the store.
  Giving the handler a `CategoryMappingStore *` for richer remap
  (swapping slots across resolution) is an E.10+ concern when a real
  multi-category sync runs.
- **Category 0 vs. "Unfiled" localisation.** Display name is the
  literal English string "Unfiled". i18n of category names is a
  Phase-F concern.
- **Bit-preservation of attributes.** `fetchItems` only reads the
  category field from `PalmRecord`. Writing back preserves the
  category the user chose via the virtual calendar; `AttrArchived` /
  `AttrSecret` preservation on apply-path is still E.7's territory
  (typed adapters extending `backendToPalm`).
- **Calendar CRUD at the sub-calendar level.** Slots are implicit on
  the Palm — there's no "create slot 7" operation; DateBk6+ users
  create categories via the device UI. `createCalendar` /
  `deleteCalendar` / `renameCalendar` return false (default). A future
  phase may hook these to AppInfo-block mutation; YAGNI for E.6.
- **Recurrence edge cases.** Palm supports daily/weekly/monthly-by-day/
  monthly-by-date/yearly with the same flags the existing mapper
  handles. BYSETPOS / BYMONTHDAY / hourly / minutely are not supported
  — `capabilities()` will reflect this but we don't block on
  lossy-recurrence UI (that's Phase-F work).
- **Palm text encoding edge cases.** Palm stores text as Windows-1252.
  The codec calls `QString::fromLatin1` on description/note for now —
  the full cp1252 transliteration the existing mapper does is a
  faithful behaviour to carry over but orthogonal to E.6's exit gate.
  Logged as a follow-up for E.7 or a dedicated text-encoding cleanup.
- **App-layer construction / plugin registration.** Nothing
  constructs a `PalmCalendarBackend` at app startup yet — that's E.10
  (Calendar plugin rewrite) + E.16 (unified runtime).

**Spec reference:**
`docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
§"WP-side class layout" (`PalmCalendarBackend` row), §"Category
routing (virtual sub-calendars)", §"Directory layout"
(`src/palm/codecs/` / `src/palm/calendar/`), and the sub-phases table
(E.6 row).

---

## File Structure

| Path | Role | Created / Modified |
|---|---|---|
| `src/palm/calendar/categorymappingstore.h` | `CategoryMappingStore` header | Create |
| `src/palm/calendar/categorymappingstore.cpp` | Store impl | Create |
| `src/palm/calendar/datebookcodec.h` | `DatebookCodec` header | Create |
| `src/palm/calendar/datebookcodec.cpp` | Codec impl (uses `pi-datebook.h`) | Create |
| `src/palm/calendar/palmcalendarbackend.h` | Backend header | Create |
| `src/palm/calendar/palmcalendarbackend.cpp` | Backend impl | Create |
| `src/palm/calendar/CMakeLists.txt` | New static lib `WildPalmsPalmCalendar` | Create |
| `src/CMakeLists.txt` | `add_subdirectory(palm/calendar)` | Modify |
| `tests/palmcalendar/CMakeLists.txt` | Test target wiring | Create |
| `tests/palmcalendar/tst_categorymappingstore.cpp` | Store tests | Create |
| `tests/palmcalendar/tst_datebookcodec.cpp` | Codec round-trip tests | Create |
| `tests/palmcalendar/tst_palmcalendarbackend.cpp` | Backend tests (identity + discovery + CRUD) | Create |
| `tests/CMakeLists.txt` | `add_subdirectory(palmcalendar)` | Modify |
| `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` | Mark E.6 ✅ | Modify |

---

## Task 1: CategoryMappingStore + lib scaffold

**Files:**
- Create: `src/palm/calendar/categorymappingstore.h`
- Create: `src/palm/calendar/categorymappingstore.cpp`
- Create: `src/palm/calendar/datebookcodec.h` (placeholder)
- Create: `src/palm/calendar/datebookcodec.cpp` (placeholder)
- Create: `src/palm/calendar/palmcalendarbackend.h` (placeholder)
- Create: `src/palm/calendar/palmcalendarbackend.cpp` (placeholder)
- Create: `src/palm/calendar/CMakeLists.txt`
- Create: `tests/palmcalendar/CMakeLists.txt`
- Create: `tests/palmcalendar/tst_categorymappingstore.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the store header.**

File `src/palm/calendar/categorymappingstore.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_CATEGORYMAPPINGSTORE_H
#define WILDPALMS_CALENDAR_CATEGORYMAPPINGSTORE_H

#include <QHash>
#include <QList>
#include <QString>

namespace WildPalms::PalmCalendar {

/**
 * @brief In-memory slot → display-name store for Palm categories.
 *
 * Keyed by Palm database name (e.g. "DatebookDB") because each Palm
 * database carries its own `CategoryAppInfo_t` with independent slot
 * assignments. Slot 0 is reserved for "Unfiled" across all databases.
 *
 * Callers populate the store from the AppInfo block at session start
 * (real parsing lands later; tests use setSlotName directly). The
 * `PalmCalendarBackend` borrows a pointer for display-name lookups
 * during `loadCalendars`. Non-owning — must outlive the backend.
 */
class CategoryMappingStore {
public:
    static constexpr const char *UnfiledName = "Unfiled";
    static constexpr int UnfiledSlot = 0;
    static constexpr int MaxSlots = 16;  // Palm supports 0..15

    CategoryMappingStore() = default;

    /// Set display name for (dbName, slot). Empty name removes the
    /// entry. Slot 0 only accepts "Unfiled" — any other name is ignored
    /// (returns false). Valid slot range is 1..15 for user-defined names.
    bool setSlotName(const QString &dbName, int slot, const QString &name);

    /// Display name for (dbName, slot). Slot 0 always returns "Unfiled".
    /// Slots 1..15 return the stored name, or empty if unset.
    QString slotName(const QString &dbName, int slot) const;

    /// All slots in [1..15] with non-empty names for dbName, sorted
    /// ascending. Slot 0 is NOT included (callers add it unconditionally).
    QList<int> populatedSlots(const QString &dbName) const;

    /// Remove all entries for dbName.
    void clear(const QString &dbName);

private:
    // dbName → (slot → name). slot keys only ever in 1..15.
    QHash<QString, QHash<int, QString>> m_slots;
};

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_CALENDAR_CATEGORYMAPPINGSTORE_H
```

- [ ] **Step 2: Write the store impl.**

File `src/palm/calendar/categorymappingstore.cpp`:

```cpp
#include "categorymappingstore.h"

#include <algorithm>

namespace WildPalms::PalmCalendar {

bool CategoryMappingStore::setSlotName(const QString &dbName, int slot,
                                       const QString &name)
{
    if (slot == UnfiledSlot) {
        return name == QStringLiteral("Unfiled");  // No-op accept for parity
    }
    if (slot < 1 || slot >= MaxSlots) {
        return false;
    }
    auto &dbSlots = m_slots[dbName];
    if (name.isEmpty()) {
        dbSlots.remove(slot);
        if (dbSlots.isEmpty()) {
            m_slots.remove(dbName);
        }
    } else {
        dbSlots.insert(slot, name);
    }
    return true;
}

QString CategoryMappingStore::slotName(const QString &dbName, int slot) const
{
    if (slot == UnfiledSlot) {
        return QStringLiteral("Unfiled");
    }
    const auto it = m_slots.constFind(dbName);
    if (it == m_slots.constEnd()) {
        return {};
    }
    return it.value().value(slot);
}

QList<int> CategoryMappingStore::populatedSlots(const QString &dbName) const
{
    const auto it = m_slots.constFind(dbName);
    if (it == m_slots.constEnd()) {
        return {};
    }
    QList<int> result = it.value().keys();
    std::sort(result.begin(), result.end());
    return result;
}

void CategoryMappingStore::clear(const QString &dbName)
{
    m_slots.remove(dbName);
}

} // namespace WildPalms::PalmCalendar
```

- [ ] **Step 3: Write placeholder codec files.**

Land placeholder `datebookcodec.{h,cpp}` and `palmcalendarbackend.{h,cpp}`
so the CMake target builds. These are filled in by Tasks 2/3/4/5.

File `src/palm/calendar/datebookcodec.h`:

```cpp
// Placeholder — filled in by Phase E.6 task 2.
#ifndef WILDPALMS_CALENDAR_DATEBOOKCODEC_H
#define WILDPALMS_CALENDAR_DATEBOOKCODEC_H
#endif
```

File `src/palm/calendar/datebookcodec.cpp`:

```cpp
// Placeholder — filled in by Phase E.6 task 2.
#include "datebookcodec.h"
namespace { [[maybe_unused]] int wp_datebookcodec_placeholder() { return 0; } }
```

File `src/palm/calendar/palmcalendarbackend.h`:

```cpp
// Placeholder — filled in by Phase E.6 task 4.
#ifndef WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H
#define WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H
#endif
```

File `src/palm/calendar/palmcalendarbackend.cpp`:

```cpp
// Placeholder — filled in by Phase E.6 task 4.
#include "palmcalendarbackend.h"
namespace { [[maybe_unused]] int wp_palmcalendarbackend_placeholder() { return 0; } }
```

- [ ] **Step 4: Write the CMake target.**

File `src/palm/calendar/CMakeLists.txt`:

```cmake
# WildPalmsPalmCalendar — Calendar-typed SyncBackend for Palm Datebook.
#
# Phase E.6 of the libkalburator integration. Provides:
#   - PalmCalendarBackend : Kalburator::Sync::SyncBackend
#   - DatebookCodec: Palm Datebook record bytes <-> KCalendarCore::Event
#   - CategoryMappingStore: slot -> display-name map for virtual calendars
#
# Links WildPalmsPalmSync (PalmBackend / PalmRecord / IPalmDatabaseAccess),
# Kalburator::Sync (SyncBackend + operation types), KF6::CalendarCore
# (Event/Alarm/Recurrence), and pisock (pi-datebook.h for the Datebook
# record pack/unpack). Deliberately does NOT link WildPalmsCore —
# keeps the Qt::Widgets / KF6::XmlGui dep out of the calendar codec.

find_package(KF6 REQUIRED COMPONENTS CalendarCore)

add_library(WildPalmsPalmCalendar STATIC
    categorymappingstore.h
    categorymappingstore.cpp
    datebookcodec.h
    datebookcodec.cpp
    palmcalendarbackend.h
    palmcalendarbackend.cpp
)

target_include_directories(WildPalmsPalmCalendar
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmCalendar
    PUBLIC
        Qt::Core
        KF6::CalendarCore
        WildPalmsPalmSync
    PRIVATE
        pisock
)

# Ensure pilot-link is built before the codec tries to include pi-datebook.h
add_dependencies(WildPalmsPalmCalendar pilot-link-external)

set_target_properties(WildPalmsPalmCalendar PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

- [ ] **Step 5: Hook into the src tree.**

Edit `src/CMakeLists.txt`. After the existing `add_subdirectory(palm/conflict)`
line (landed in E.5), add:

```cmake
# Calendar-typed SyncBackend adapter for Palm Datebook (Phase E.6 of
# libkalburator integration). Exposes virtual sub-calendars per Palm
# category slot via PalmCalendarBackend : SyncBackend.
add_subdirectory(palm/calendar)
```

- [ ] **Step 6: Write the store unit tests.**

File `tests/palmcalendar/tst_categorymappingstore.cpp`:

```cpp
#include <QtTest/QtTest>

#include "categorymappingstore.h"

using WildPalms::PalmCalendar::CategoryMappingStore;

class TestCategoryMappingStore : public QObject
{
    Q_OBJECT
private slots:
    void slotZeroAlwaysReturnsUnfiled();
    void setAndGetRoundTripForUserSlots();
    void emptyNameRemovesSlot();
    void dbIsolationBetweenDatabases();
    void slotZeroRejectsArbitraryNames();
    void populatedSlotsIsSortedAndExcludesZero();
    void outOfRangeSlotRejected();
};

void TestCategoryMappingStore::slotZeroAlwaysReturnsUnfiled()
{
    CategoryMappingStore store;
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 0),
             QStringLiteral("Unfiled"));

    // Even for a DB we've never touched.
    QCOMPARE(store.slotName(QStringLiteral("MemoDB"), 0),
             QStringLiteral("Unfiled"));
}

void TestCategoryMappingStore::setAndGetRoundTripForUserSlots()
{
    CategoryMappingStore store;
    QVERIFY(store.setSlotName(QStringLiteral("DatebookDB"), 3,
                              QStringLiteral("Work")));
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 3),
             QStringLiteral("Work"));
}

void TestCategoryMappingStore::emptyNameRemovesSlot()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 5,
                      QStringLiteral("Personal"));
    QCOMPARE(store.populatedSlots(QStringLiteral("DatebookDB")).size(), 1);

    QVERIFY(store.setSlotName(QStringLiteral("DatebookDB"), 5, QString{}));
    QVERIFY(store.slotName(QStringLiteral("DatebookDB"), 5).isEmpty());
    QVERIFY(store.populatedSlots(QStringLiteral("DatebookDB")).isEmpty());
}

void TestCategoryMappingStore::dbIsolationBetweenDatabases()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("MemoDB"),     1, QStringLiteral("Ideas"));

    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 1),
             QStringLiteral("Work"));
    QCOMPARE(store.slotName(QStringLiteral("MemoDB"), 1),
             QStringLiteral("Ideas"));
}

void TestCategoryMappingStore::slotZeroRejectsArbitraryNames()
{
    CategoryMappingStore store;
    // setSlotName for slot 0 is a no-op accept only for "Unfiled".
    QVERIFY(!store.setSlotName(QStringLiteral("DatebookDB"), 0,
                               QStringLiteral("NotUnfiled")));
    // slot 0 still reads as "Unfiled"
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 0),
             QStringLiteral("Unfiled"));
}

void TestCategoryMappingStore::populatedSlotsIsSortedAndExcludesZero()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("E"));
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("A"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("C"));

    const auto slots = store.populatedSlots(QStringLiteral("DatebookDB"));
    QCOMPARE(slots, (QList<int>{1, 3, 5}));
}

void TestCategoryMappingStore::outOfRangeSlotRejected()
{
    CategoryMappingStore store;
    QVERIFY(!store.setSlotName(QStringLiteral("DatebookDB"), -1,
                               QStringLiteral("X")));
    QVERIFY(!store.setSlotName(QStringLiteral("DatebookDB"), 16,
                               QStringLiteral("X")));
    QVERIFY(store.populatedSlots(QStringLiteral("DatebookDB")).isEmpty());
}

QTEST_MAIN(TestCategoryMappingStore)
#include "tst_categorymappingstore.moc"
```

- [ ] **Step 7: Write the tests CMakeLists.**

File `tests/palmcalendar/CMakeLists.txt`:

```cmake
# Phase E.6 — PalmCalendarBackend + DatebookCodec + CategoryMappingStore tests.
# Each test links WildPalmsPalmCalendar (transitively WildPalmsPalmSync +
# Kalburator::Sync + KF6::CalendarCore + pisock). Deliberately does NOT
# link WildPalmsCore.

function(add_palm_calendar_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            KF6::CalendarCore
            WildPalmsPalmCalendar
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_calendar_test(tst_categorymappingstore tst_categorymappingstore.cpp)
```

- [ ] **Step 8: Hook into the tests tree.**

Edit `tests/CMakeLists.txt`. After the existing `add_subdirectory(palmconflict)`
line (landed in E.5), add:

```cmake
# ============================================================
# Phase E.6 — PalmCalendarBackend + DatebookCodec
# ============================================================

add_subdirectory(palmcalendar)
```

- [ ] **Step 9: Configure + build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)" --target tst_categorymappingstore
ctest --test-dir build --output-on-failure -R tst_categorymappingstore
```

Expected: 7 tests PASS.

- [ ] **Step 10: Full WP ctest.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Expected: 27/27 pass (26 pre-E.6 + new store test).

- [ ] **Step 11: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/calendar/ src/CMakeLists.txt \
        tests/palmcalendar/ tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-calendar): scaffold WildPalmsPalmCalendar + CategoryMappingStore

Phase E.6 task 1: new static library WildPalmsPalmCalendar at
src/palm/calendar/. Houses the Palm-calendar-typed SyncBackend
adapter (landed in task 4-5), the DatebookCodec (tasks 2-3), and
CategoryMappingStore.

CategoryMappingStore is an in-memory (dbName, slot) -> displayName
map. Slot 0 always reads "Unfiled". Slots 1..15 accept user-defined
names. Out-of-range slots and non-"Unfiled" slot-0 writes are
rejected.

Seven unit tests: slot-0 invariant, round-trip for user slots,
empty-name removal, per-database isolation, slot-0 rejection
semantics, populatedSlots sort order, out-of-range slot rejection.
Full WP ctest at 27/27.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: DatebookCodec core scaffold + identity fields

**Files:**
- Replace: `src/palm/calendar/datebookcodec.h`
- Replace: `src/palm/calendar/datebookcodec.cpp`
- Create: `tests/palmcalendar/tst_datebookcodec.cpp`
- Modify: `tests/palmcalendar/CMakeLists.txt`

Sets up the codec surface (`DecodeResult`, static `decode` / `encode`)
and implements the identity-round-trip parts: recordId in/out via
`X-WP-PALM-RECORDID` custom property, slot in/out via
`PalmRecord::category`, and the flags-byte (AttrDeleted handling).
Repeat/alarm/date fields land in tasks 3.

- [ ] **Step 1: Write the codec header.**

File `src/palm/calendar/datebookcodec.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_DATEBOOKCODEC_H
#define WILDPALMS_CALENDAR_DATEBOOKCODEC_H

#include <KCalendarCore/Event>

#include "palmrecord.h"

namespace WildPalms::PalmCalendar {

/**
 * @brief Pure byte-level codec: Palm Datebook record <-> KCalendarCore::Event.
 *
 * Uses pisock's `pi-datebook.h` (pack_Appointment / unpack_Appointment)
 * for the bit-level Datebook record layout. Stateless; caller owns
 * the `PalmRecord` and the resulting `Event::Ptr`.
 *
 * Palm record ID round-trip: stashed as
 * X-WP-PALM-RECORDID on the Event; if present on encode, preserved;
 * if absent on encode, the PalmRecord's recordId is zero and the
 * device assigns on write.
 *
 * Category slot round-trip: decoded from `PalmRecord::category` and
 * stashed as X-WP-PALM-CATEGORY-SLOT (so downstream callers that
 * lose the calendar-ID context can still recover it). On encode, the
 * `slot` parameter wins over any property.
 */
class DatebookCodec {
public:
    static constexpr const char *RecordIdProperty    = "X-WP-PALM-RECORDID";
    static constexpr const char *CategorySlotProperty = "X-WP-PALM-CATEGORY-SLOT";

    struct DecodeResult {
        KCalendarCore::Event::Ptr event;  ///< null on failure
        int  slot = 0;                    ///< PalmRecord::category
        QString failureReason;            ///< empty on success
        bool isValid() const { return !event.isNull(); }
    };

    /// Decode Palm Datebook record bytes into an Event. Records with
    /// `AttrDeleted` set return a null-event DecodeResult with
    /// failureReason="deleted" — callers typically skip these rather
    /// than surface as tombstones.
    static DecodeResult decode(const WildPalms::PalmSync::PalmRecord &record);

    /// Encode an Event into a PalmRecord with the given category slot.
    /// `slot` is clamped to [0..15]. `recordId` is copied from the
    /// Event's X-WP-PALM-RECORDID property if present (and parseable),
    /// else 0. Other attributes (Deleted/Dirty/Secret/Archived) are
    /// left as 0 — preserving on write-back is an E.7 concern.
    static WildPalms::PalmSync::PalmRecord encode(
        const KCalendarCore::Event::Ptr &event, int slot);
};

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_CALENDAR_DATEBOOKCODEC_H
```

- [ ] **Step 2: Write the codec impl — identity fields only.**

File `src/palm/calendar/datebookcodec.cpp`:

```cpp
#include "datebookcodec.h"

#include <cstring>

#include <QByteArray>
#include <QDateTime>
#include <QDebug>

extern "C" {
#include <pi-buffer.h>
#include <pi-datebook.h>
}

namespace WildPalms::PalmCalendar {

using WildPalms::PalmSync::PalmRecord;

namespace {

/// Scoped wrapper so we free the pisock Appointment_t's allocations
/// even on early return.
struct ScopedAppointment {
    Appointment_t a{};
    ~ScopedAppointment() { free_Appointment(&a); }
    ScopedAppointment() = default;
    ScopedAppointment(const ScopedAppointment &) = delete;
    ScopedAppointment &operator=(const ScopedAppointment &) = delete;
};

/// Scoped wrapper for pi_buffer_t.
struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 256) {
        buf = pi_buffer_new(initial);
    }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

} // namespace

DatebookCodec::DecodeResult
DatebookCodec::decode(const PalmRecord &record)
{
    DecodeResult result;
    result.slot = static_cast<int>(record.category);

    if (record.isDeleted()) {
        result.failureReason = QStringLiteral("deleted");
        return result;
    }

    if (record.data.isEmpty()) {
        result.failureReason = QStringLiteral("empty-record");
        return result;
    }

    // Unpack via pisock.
    ScopedAppointment appt;
    ScopedBuffer buf(record.data.size());
    if (!buf.buf) {
        result.failureReason = QStringLiteral("pi-buffer-alloc-failed");
        return result;
    }
    pi_buffer_append(buf.buf, record.data.constData(),
                     static_cast<std::size_t>(record.data.size()));

    const int rc = unpack_Appointment(&appt.a, buf.buf, datebook_v1);
    if (rc < 0) {
        result.failureReason = QStringLiteral("unpack-failed:rc=%1").arg(rc);
        return result;
    }

    // Build the Event scaffold — content-carrying fields land in task 3.
    auto event = KCalendarCore::Event::Ptr::create();
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::RecordIdProperty),
                             QString::number(record.recordId));
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::CategorySlotProperty),
                             QString::number(record.category));

    // Generate a stable UID derived from recordId so repeated decodes
    // of the same record produce the same UID.
    event->setUid(QStringLiteral("palm-datebook-%1").arg(record.recordId));

    result.event = event;
    return result;
}

PalmRecord DatebookCodec::encode(const KCalendarCore::Event::Ptr &event,
                                 int slot)
{
    PalmRecord rec;
    if (!event) {
        return rec;  // Empty rec signals failure upstream.
    }

    rec.category = static_cast<std::uint8_t>(std::clamp(slot, 0, 15));

    // Record ID from X-WP-PALM-RECORDID if present.
    const auto idStr = event->customProperty(
        "KCalendarCore", QByteArray(DatebookCodec::RecordIdProperty));
    if (!idStr.isEmpty()) {
        bool ok = false;
        const auto id = idStr.toUInt(&ok);
        if (ok) {
            rec.recordId = id;
        }
    }

    // Pack via pisock. In this task we pack a minimal Appointment_t
    // (just the flag stubs zeroed) — content fields land in task 3.
    ScopedAppointment appt;
    // unpack-then-pack of a minimal appointment needs sensible defaults.
    appt.a.event = 1;           // untimed (all-day) placeholder
    appt.a.alarm = 0;
    appt.a.repeatType = repeatNone;
    appt.a.repeatFrequency = 0;
    appt.a.exceptions = 0;
    appt.a.description = nullptr;
    appt.a.note = nullptr;
    const auto now = QDateTime::currentDateTime();
    std::tm tm{};
    tm.tm_year = now.date().year() - 1900;
    tm.tm_mon  = now.date().month() - 1;
    tm.tm_mday = now.date().day();
    appt.a.begin = tm;
    appt.a.end   = tm;

    ScopedBuffer buf(256);
    if (!buf.buf) {
        return {};
    }
    const int rc = pack_Appointment(&appt.a, buf.buf, datebook_v1);
    if (rc < 0) {
        return {};
    }

    rec.data = QByteArray(reinterpret_cast<const char *>(buf.buf->data),
                          static_cast<int>(buf.buf->used));
    rec.lastModified = QDateTime::currentDateTimeUtc();
    return rec;
}

} // namespace WildPalms::PalmCalendar
```

- [ ] **Step 3: Write identity-level codec tests.**

File `tests/palmcalendar/tst_datebookcodec.cpp`:

```cpp
#include <QtTest/QtTest>

#include <KCalendarCore/Event>

#include "datebookcodec.h"
#include "palmrecord.h"

using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::PalmRecord;

class TestDatebookCodec : public QObject
{
    Q_OBJECT
private slots:
    void deletedRecordDecodesToFailure();
    void emptyBytesDecodeToFailure();
    void recordIdPreservedThroughDecode();
    void categorySlotPreservedThroughDecode();
    void encodeRespectsSlotParameter();
    void encodeClampsOutOfRangeSlot();
    void encodeReadsRecordIdFromProperty();
    void roundTripMinimalEventPreservesRecordId();
};

namespace {

/// Build a minimal PalmRecord with known bytes by encoding a trivial event.
/// This gives us a starting byte sequence for decode tests.
PalmRecord makeMinimalRecord(std::uint32_t recordId,
                             std::uint8_t category = 0,
                             std::uint8_t attributes = 0)
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("minimal"));
    auto rec = DatebookCodec::encode(ev, category);
    rec.recordId   = recordId;
    rec.attributes = attributes;
    return rec;
}

} // namespace

void TestDatebookCodec::deletedRecordDecodesToFailure()
{
    PalmRecord rec = makeMinimalRecord(1, 0, PalmRecord::AttrDeleted);
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(!result.isValid());
    QCOMPARE(result.failureReason, QStringLiteral("deleted"));
}

void TestDatebookCodec::emptyBytesDecodeToFailure()
{
    PalmRecord rec;
    rec.recordId = 1;
    rec.data.clear();
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(!result.isValid());
    QCOMPARE(result.failureReason, QStringLiteral("empty-record"));
}

void TestDatebookCodec::recordIdPreservedThroughDecode()
{
    PalmRecord rec = makeMinimalRecord(42);
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(result.isValid());
    QCOMPARE(result.event->customProperty(
                 "KCalendarCore",
                 QByteArray(DatebookCodec::RecordIdProperty)),
             QStringLiteral("42"));
}

void TestDatebookCodec::categorySlotPreservedThroughDecode()
{
    PalmRecord rec = makeMinimalRecord(1, /*category=*/7);
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(result.isValid());
    QCOMPARE(result.slot, 7);
    QCOMPARE(result.event->customProperty(
                 "KCalendarCore",
                 QByteArray(DatebookCodec::CategorySlotProperty)),
             QStringLiteral("7"));
}

void TestDatebookCodec::encodeRespectsSlotParameter()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    const auto rec = DatebookCodec::encode(ev, /*slot=*/11);
    QCOMPARE(static_cast<int>(rec.category), 11);
}

void TestDatebookCodec::encodeClampsOutOfRangeSlot()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    QCOMPARE(static_cast<int>(DatebookCodec::encode(ev, -5).category), 0);
    QCOMPARE(static_cast<int>(DatebookCodec::encode(ev, 99).category), 15);
}

void TestDatebookCodec::encodeReadsRecordIdFromProperty()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setCustomProperty("KCalendarCore",
                          QByteArray(DatebookCodec::RecordIdProperty),
                          QStringLiteral("99"));
    const auto rec = DatebookCodec::encode(ev, 0);
    QCOMPARE(rec.recordId, 99u);
}

void TestDatebookCodec::roundTripMinimalEventPreservesRecordId()
{
    PalmRecord original = makeMinimalRecord(123, /*category=*/5);
    const auto decoded = DatebookCodec::decode(original);
    QVERIFY(decoded.isValid());

    const auto re = DatebookCodec::encode(decoded.event, decoded.slot);
    QCOMPARE(re.recordId, 123u);
    QCOMPARE(static_cast<int>(re.category), 5);
}

QTEST_MAIN(TestDatebookCodec)
#include "tst_datebookcodec.moc"
```

- [ ] **Step 4: Register the test.**

Append to `tests/palmcalendar/CMakeLists.txt`:

```cmake
add_palm_calendar_test(tst_datebookcodec tst_datebookcodec.cpp)
```

- [ ] **Step 5: Build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_datebookcodec
ctest --test-dir build --output-on-failure -R tst_datebookcodec
```

Expected: 8 tests PASS.

- [ ] **Step 6: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/calendar/datebookcodec.h \
        src/palm/calendar/datebookcodec.cpp \
        tests/palmcalendar/tst_datebookcodec.cpp \
        tests/palmcalendar/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-calendar): DatebookCodec scaffold + identity round-trip

Phase E.6 task 2: DatebookCodec surface (decode/encode static
methods, DecodeResult struct) plus the identity fields that
round-trip cleanly before content lands:

- Palm recordId <-> Event X-WP-PALM-RECORDID custom property
- Palm category slot <-> Event X-WP-PALM-CATEGORY-SLOT property
  plus the DecodeResult.slot field
- AttrDeleted records decode to failureReason="deleted" (callers
  skip rather than surface as tombstones)
- Empty record bytes fail cleanly

Codec uses pisock's unpack_Appointment / pack_Appointment for the
bit-level Datebook format. ScopedAppointment / ScopedBuffer RAII
wrappers handle pi_buffer_t and free_Appointment() regardless of
failure path.

Content fields (begin/end times, description, note, repeats,
alarms, exceptions, private flag) land in task 3.

Eight codec tests cover the identity paths.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: DatebookCodec content — times / text / repeats / alarms / exceptions

**Files:**
- Modify: `src/palm/calendar/datebookcodec.cpp`
- Modify: `tests/palmcalendar/tst_datebookcodec.cpp`

Fills in the content-carrying portion of the codec. Three helper
internal functions:

- `palmTmToQDateTime` / `qDateTimeToPalmTm` — `std::tm` ↔ `QDateTime`
- `palmRepeatToRRule` / `rruleToPalmRepeat` — Palm repeat → iCal RRULE
- `palmAlarmToVAlarm` / `vAlarmToPalmAlarm` — alarm-advance ↔ VALARM

Full round-trip property under test: for each event shape, `encode
(decode(rec).event, slot) == rec` byte-equal (modulo canonical
normalisation of optional fields — see §"Round-trip canonicalisation"
below).

**Round-trip canonicalisation:** pisock's `pack_Appointment` can emit
different byte sequences for equivalent Appointment_t structs (e.g.,
trailing zero bytes in optional fields). To keep tests deterministic,
we compare via the decoded `Appointment_t` fields, not raw bytes. The
helper `appointmentsEqual(a1, a2)` in the test file checks field
equivalence.

- [ ] **Step 1: Extend the codec impl — begin/end times + description/note.**

In `src/palm/calendar/datebookcodec.cpp`, replace the `decode` function
body (just the part after the unpack call) to populate the event:

Find this block:

```cpp
    // Build the Event scaffold — content-carrying fields land in task 3.
    auto event = KCalendarCore::Event::Ptr::create();
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::RecordIdProperty),
                             QString::number(record.recordId));
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::CategorySlotProperty),
                             QString::number(record.category));

    // Generate a stable UID derived from recordId so repeated decodes
    // of the same record produce the same UID.
    event->setUid(QStringLiteral("palm-datebook-%1").arg(record.recordId));

    result.event = event;
    return result;
```

Replace with:

```cpp
    // Build the Event and populate content.
    auto event = KCalendarCore::Event::Ptr::create();
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::RecordIdProperty),
                             QString::number(record.recordId));
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::CategorySlotProperty),
                             QString::number(record.category));
    event->setUid(QStringLiteral("palm-datebook-%1").arg(record.recordId));

    // Description -> SUMMARY. Palm uses Windows-1252; QString::fromLatin1
    // accepts the subset below 0x80 correctly, high-bit chars may need
    // cp1252 transliteration (deferred — see plan "Scope not in E.6").
    if (appt.a.description) {
        event->setSummary(QString::fromLatin1(appt.a.description));
    }
    if (appt.a.note) {
        event->setDescription(QString::fromLatin1(appt.a.note));
    }

    // Begin/end times. `event` flag non-zero => untimed (all-day).
    const bool untimed = appt.a.event != 0;
    const auto beginDt = palmTmToQDateTime(appt.a.begin, untimed);
    if (beginDt.isValid()) {
        event->setDtStart(beginDt);
        event->setAllDay(untimed);
    }
    if (!untimed) {
        const auto endDt = palmTmToQDateTime(appt.a.end, /*untimed=*/false);
        if (endDt.isValid()) {
            event->setDtEnd(endDt);
        }
    }

    // Alarm.
    if (appt.a.alarm) {
        palmAlarmToVAlarm(appt.a.advance, appt.a.advanceUnits, event);
    }

    // Repeat.
    if (appt.a.repeatType != repeatNone) {
        palmRepeatToRRule(appt.a, event);
    }

    // Exceptions (EXDATE). Present only when exceptions > 0 and
    // appt.a.exception is non-null.
    for (int i = 0; i < appt.a.exceptions; ++i) {
        const auto exDt = palmTmToQDateTime(appt.a.exception[i], untimed);
        if (exDt.isValid() && event->recurrence()) {
            event->recurrence()->addExDateTime(exDt);
        }
    }

    result.event = event;
    return result;
```

And at the top of the anonymous namespace (after `ScopedBuffer`), add
the helper forward decls and implementations:

```cpp
/// Palm `struct tm` -> QDateTime. For untimed (all-day) records,
/// only the date portion is meaningful; return a date-only QDateTime
/// (time 00:00 local).
inline QDateTime palmTmToQDateTime(const std::tm &tm, bool untimed)
{
    QDate date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    if (!date.isValid()) {
        return {};
    }
    if (untimed) {
        return QDateTime(date, QTime(0, 0), Qt::LocalTime);
    }
    return QDateTime(date, QTime(tm.tm_hour, tm.tm_min), Qt::LocalTime);
}

inline std::tm qDateTimeToPalmTm(const QDateTime &dt, bool untimed)
{
    std::tm tm{};
    tm.tm_year = dt.date().year() - 1900;
    tm.tm_mon  = dt.date().month() - 1;
    tm.tm_mday = dt.date().day();
    if (!untimed) {
        tm.tm_hour = dt.time().hour();
        tm.tm_min  = dt.time().minute();
    } else {
        tm.tm_hour = 0;
        tm.tm_min  = 0;
    }
    tm.tm_sec  = 0;
    return tm;
}

/// alarm-advance + units -> VALARM on the event.
inline void palmAlarmToVAlarm(int advance, int units,
                              const KCalendarCore::Event::Ptr &event)
{
    auto alarm = event->newAlarm();
    alarm->setType(KCalendarCore::Alarm::Display);
    alarm->setEnabled(true);
    // Palm advance is "how long BEFORE the event"; iCal TRIGGER uses
    // negative duration (Duration ctor takes seconds).
    int seconds = 0;
    switch (units) {
        case 0: seconds = advance * 60;          break;  // minutes
        case 1: seconds = advance * 60 * 60;     break;  // hours
        case 2: seconds = advance * 60 * 60 * 24; break; // days
        default: seconds = advance * 60;         break;
    }
    alarm->setStartOffset(KCalendarCore::Duration(-seconds));
}

inline bool vAlarmToPalmAlarm(const KCalendarCore::Event::Ptr &event,
                              int *advanceOut, int *unitsOut)
{
    if (event->alarms().isEmpty()) {
        return false;
    }
    const auto alarm = event->alarms().first();
    if (!alarm->enabled()) {
        return false;
    }
    const int secondsBefore = -alarm->startOffset().asSeconds();
    if (secondsBefore <= 0) {
        return false;
    }
    // Pick the largest unit that divides cleanly; prefer days > hours > minutes.
    if (secondsBefore % (60 * 60 * 24) == 0) {
        *unitsOut = 2;
        *advanceOut = secondsBefore / (60 * 60 * 24);
    } else if (secondsBefore % (60 * 60) == 0) {
        *unitsOut = 1;
        *advanceOut = secondsBefore / (60 * 60);
    } else {
        *unitsOut = 0;
        *advanceOut = secondsBefore / 60;
    }
    return true;
}

/// Palm repeat block -> KCalendarCore::RecurrenceRule.
inline void palmRepeatToRRule(const Appointment_t &a,
                              const KCalendarCore::Event::Ptr &event)
{
    using namespace KCalendarCore;

    auto *recurrence = event->recurrence();
    if (!recurrence) return;

    switch (a.repeatType) {
        case repeatDaily:
            recurrence->setDaily(a.repeatFrequency ? a.repeatFrequency : 1);
            break;
        case repeatWeekly: {
            recurrence->setWeekly(a.repeatFrequency ? a.repeatFrequency : 1);
            // Palm repeatDays[0..6] is Sun..Sat; KCal uses Mon=1..Sun=7.
            QBitArray days(7);
            const int palmToKCalMon[7] = { 6, 0, 1, 2, 3, 4, 5 };
            for (int i = 0; i < 7; ++i) {
                if (a.repeatDays[i]) {
                    days.setBit(palmToKCalMon[i]);
                }
            }
            if (days.count(true) > 0) {
                recurrence->addWeeklyDays(days);
            }
            break;
        }
        case repeatMonthlyByDay:
            recurrence->setMonthly(a.repeatFrequency ? a.repeatFrequency : 1);
            // repeatDay encodes week-of-month*7 + day-of-week (Palm convention).
            // Translate to a single BYDAY position; leave fine-grained
            // handling to E.10's plugin-rewrite if needed.
            break;
        case repeatMonthlyByDate:
            recurrence->setMonthly(a.repeatFrequency ? a.repeatFrequency : 1);
            recurrence->addMonthlyDate(a.begin.tm_mday);
            break;
        case repeatYearly:
            recurrence->setYearly(a.repeatFrequency ? a.repeatFrequency : 1);
            break;
        case repeatNone:
        default:
            return;
    }

    if (!a.repeatForever) {
        const auto endDt = palmTmToQDateTime(a.repeatEnd, a.event != 0);
        if (endDt.isValid()) {
            recurrence->setEndDateTime(endDt);
        }
    }
}

inline void rruleToPalmRepeat(const KCalendarCore::Event::Ptr &event,
                              Appointment_t &a)
{
    using namespace KCalendarCore;
    if (!event->recurs()) {
        a.repeatType = repeatNone;
        a.repeatForever = 1;
        a.repeatFrequency = 0;
        return;
    }

    auto *recurrence = event->recurrence();
    const auto freq = recurrence->recurrenceType();
    a.repeatFrequency = recurrence->frequency();
    a.repeatForever = recurrence->duration() == -1 ? 1 : 0;
    if (!a.repeatForever) {
        a.repeatEnd = qDateTimeToPalmTm(recurrence->endDateTime(),
                                        event->allDay());
    }

    switch (freq) {
        case Recurrence::rDaily:
            a.repeatType = repeatDaily;
            break;
        case Recurrence::rWeekly: {
            a.repeatType = repeatWeekly;
            const auto days = recurrence->days();
            // KCal days: Mon=0..Sun=6. Palm days: Sun=0..Sat=6.
            const int kCalToPalm[7] = { 1, 2, 3, 4, 5, 6, 0 };
            for (int i = 0; i < 7; ++i) {
                if (i < days.size() && days.testBit(i)) {
                    a.repeatDays[kCalToPalm[i]] = 1;
                }
            }
            break;
        }
        case Recurrence::rMonthlyDay:
        case Recurrence::rMonthlyPos:
            a.repeatType = repeatMonthlyByDate;
            break;
        case Recurrence::rYearlyMonth:
        case Recurrence::rYearlyDay:
        case Recurrence::rYearlyPos:
            a.repeatType = repeatYearly;
            break;
        default:
            a.repeatType = repeatNone;
            break;
    }
}
```

Also add the includes at the top of the `.cpp`:

```cpp
#include <algorithm>

#include <QBitArray>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Duration>
#include <KCalendarCore/Recurrence>
```

- [ ] **Step 2: Extend the encode function to populate Appointment_t fully.**

Replace the body of `DatebookCodec::encode` after the slot/recordId
parsing (starting at the `ScopedAppointment appt;` line) with:

```cpp
    // Pack via pisock. Build an Appointment_t from the event fields.
    ScopedAppointment appt;

    const bool untimed = event->allDay();
    appt.a.event = untimed ? 1 : 0;

    const auto dtStart = event->dtStart();
    appt.a.begin = qDateTimeToPalmTm(dtStart, untimed);
    if (!untimed) {
        const auto dtEnd = event->dtEnd().isValid() ? event->dtEnd() : dtStart;
        appt.a.end = qDateTimeToPalmTm(dtEnd, false);
    } else {
        appt.a.end = appt.a.begin;
    }

    // Description / note. strdup so pisock can free_Appointment them.
    const auto summary = event->summary().toLatin1();
    if (!summary.isEmpty()) {
        appt.a.description = ::strdup(summary.constData());
    }
    const auto notes = event->description().toLatin1();
    if (!notes.isEmpty()) {
        appt.a.note = ::strdup(notes.constData());
    }

    // Alarm.
    int advance = 0, units = 0;
    if (vAlarmToPalmAlarm(event, &advance, &units)) {
        appt.a.alarm = 1;
        appt.a.advance = advance;
        appt.a.advanceUnits = units;
    }

    // Repeat.
    rruleToPalmRepeat(event, appt.a);

    // Exceptions (EXDATE).
    const auto exDates = event->recurs()
        ? event->recurrence()->exDateTimes()
        : QList<QDateTime>{};
    if (!exDates.isEmpty()) {
        appt.a.exceptions = exDates.size();
        appt.a.exception = static_cast<std::tm *>(
            ::calloc(exDates.size(), sizeof(std::tm)));
        for (int i = 0; i < exDates.size(); ++i) {
            appt.a.exception[i] = qDateTimeToPalmTm(exDates[i], untimed);
        }
    }

    // Private flag is represented in the Palm record's attributes byte
    // (AttrSecret), not in the Appointment_t payload. Round-trip at the
    // PalmRecord level:
    // (we'll set this on the returned PalmRecord below)

    ScopedBuffer buf(1024);
    if (!buf.buf) {
        return {};
    }
    const int rc = pack_Appointment(&appt.a, buf.buf, datebook_v1);
    if (rc < 0) {
        return {};
    }

    rec.data = QByteArray(reinterpret_cast<const char *>(buf.buf->data),
                          static_cast<int>(buf.buf->used));
    rec.lastModified = QDateTime::currentDateTimeUtc();

    // Private flag: CLASS:PRIVATE -> AttrSecret.
    if (event->secrecy() == KCalendarCore::Incidence::SecrecyPrivate) {
        rec.attributes |= PalmRecord::AttrSecret;
    }
    return rec;
```

Also, on `decode`, before `result.event = event;` add:

```cpp
    // Private flag: AttrSecret -> CLASS:PRIVATE.
    if (record.isSecret()) {
        event->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
    }
```

- [ ] **Step 3: Add content-level codec tests.**

Append to `TestDatebookCodec` the new slot decls:

```cpp
    void roundTripAllDayEventSummaryAndDate();
    void roundTripTimedEventDescription();
    void roundTripAlarmMinutes();
    void roundTripAlarmHoursAndDays();
    void roundTripWeeklyRepeatWithDaysOfWeek();
    void roundTripDailyRepeatWithFiniteEnd();
    void roundTripYearlyRepeatForever();
    void roundTripExceptionDates();
    void roundTripPrivateFlag();
```

And the slot bodies, before the `QTEST_MAIN` line:

```cpp
namespace {

/// Encode then decode and return the resulting event. This exercises
/// the full codec pipeline in one call.
KCalendarCore::Event::Ptr roundTripThroughBytes(
    const KCalendarCore::Event::Ptr &input, int slot,
    std::uint8_t *attrsOut = nullptr)
{
    const auto rec = DatebookCodec::encode(input, slot);
    if (attrsOut) *attrsOut = rec.attributes;
    auto withId = rec;
    withId.recordId = 1;  // decode needs a non-zero record ID for a valid UID.
    const auto result = DatebookCodec::decode(withId);
    return result.isValid() ? result.event : KCalendarCore::Event::Ptr{};
}

} // namespace

void TestDatebookCodec::roundTripAllDayEventSummaryAndDate()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Holiday"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QCOMPARE(rt->summary(), QStringLiteral("Holiday"));
    QVERIFY(rt->allDay());
    QCOMPARE(rt->dtStart().date(), QDate(2026, 6, 1));
}

void TestDatebookCodec::roundTripTimedEventDescription()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Standup"));
    ev->setDescription(QStringLiteral("Daily team sync"));
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(9, 30), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(9, 45), Qt::LocalTime));

    const auto rt = roundTripThroughBytes(ev, 3);
    QVERIFY(rt);
    QCOMPARE(rt->summary(),     QStringLiteral("Standup"));
    QCOMPARE(rt->description(), QStringLiteral("Daily team sync"));
    QVERIFY(!rt->allDay());
    QCOMPARE(rt->dtStart().time(), QTime(9, 30));
    QCOMPARE(rt->dtEnd().time(),   QTime(9, 45));
}

void TestDatebookCodec::roundTripAlarmMinutes()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Meeting"));
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(10, 0), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(10, 30), Qt::LocalTime));
    auto alarm = ev->newAlarm();
    alarm->setType(KCalendarCore::Alarm::Display);
    alarm->setEnabled(true);
    alarm->setStartOffset(KCalendarCore::Duration(-15 * 60));

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QCOMPARE(rt->alarms().size(), 1);
    QCOMPARE(rt->alarms().first()->startOffset().asSeconds(), -15 * 60);
}

void TestDatebookCodec::roundTripAlarmHoursAndDays()
{
    auto ev1 = KCalendarCore::Event::Ptr::create();
    ev1->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(12, 0), Qt::LocalTime));
    ev1->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(13, 0), Qt::LocalTime));
    auto a1 = ev1->newAlarm();
    a1->setType(KCalendarCore::Alarm::Display);
    a1->setEnabled(true);
    a1->setStartOffset(KCalendarCore::Duration(-2 * 60 * 60));  // 2 hours
    const auto rt1 = roundTripThroughBytes(ev1, 0);
    QVERIFY(rt1);
    QCOMPARE(rt1->alarms().first()->startOffset().asSeconds(), -2 * 60 * 60);

    auto ev2 = KCalendarCore::Event::Ptr::create();
    ev2->setDtStart(QDateTime(QDate(2026, 6, 2), QTime(12, 0), Qt::LocalTime));
    ev2->setDtEnd  (QDateTime(QDate(2026, 6, 2), QTime(13, 0), Qt::LocalTime));
    auto a2 = ev2->newAlarm();
    a2->setType(KCalendarCore::Alarm::Display);
    a2->setEnabled(true);
    a2->setStartOffset(KCalendarCore::Duration(-3 * 24 * 60 * 60));  // 3 days
    const auto rt2 = roundTripThroughBytes(ev2, 0);
    QVERIFY(rt2);
    QCOMPARE(rt2->alarms().first()->startOffset().asSeconds(),
             -3 * 24 * 60 * 60);
}

void TestDatebookCodec::roundTripWeeklyRepeatWithDaysOfWeek()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("MWF gym"));
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(7, 0), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(8, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setWeekly(1);
    QBitArray days(7);
    days.setBit(0);  // Mon (KCal convention)
    days.setBit(2);  // Wed
    days.setBit(4);  // Fri
    rec->addWeeklyDays(days);

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    QCOMPARE(rt->recurrence()->recurrenceType(),
             KCalendarCore::Recurrence::rWeekly);
    const auto rtDays = rt->recurrence()->days();
    QVERIFY(rtDays.testBit(0));
    QVERIFY(rtDays.testBit(2));
    QVERIFY(rtDays.testBit(4));
    QVERIFY(!rtDays.testBit(1));
}

void TestDatebookCodec::roundTripDailyRepeatWithFiniteEnd()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Vitamin"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setDaily(1);
    rec->setEndDateTime(
        QDateTime(QDate(2026, 6, 30), QTime(0, 0), Qt::LocalTime));

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    QCOMPARE(rt->recurrence()->recurrenceType(),
             KCalendarCore::Recurrence::rDaily);
    QCOMPARE(rt->recurrence()->endDateTime().date(), QDate(2026, 6, 30));
}

void TestDatebookCodec::roundTripYearlyRepeatForever()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Birthday"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 15), QTime(0, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setYearly(1);

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    QCOMPARE(rt->recurrence()->recurrenceType(),
             KCalendarCore::Recurrence::rYearlyMonth);
    QCOMPARE(rt->recurrence()->duration(), -1);  // -1 == forever
}

void TestDatebookCodec::roundTripExceptionDates()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Daily with holidays"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setDaily(1);
    const auto ex1 = QDateTime(QDate(2026, 6, 7),  QTime(0, 0), Qt::LocalTime);
    const auto ex2 = QDateTime(QDate(2026, 6, 14), QTime(0, 0), Qt::LocalTime);
    rec->addExDateTime(ex1);
    rec->addExDateTime(ex2);

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    const auto rtEx = rt->recurrence()->exDateTimes();
    QCOMPARE(rtEx.size(), 2);
    QVERIFY(rtEx.contains(ex1));
    QVERIFY(rtEx.contains(ex2));
}

void TestDatebookCodec::roundTripPrivateFlag()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Private"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    ev->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);

    std::uint8_t attrsOut = 0;
    const auto rt = roundTripThroughBytes(ev, 0, &attrsOut);
    QVERIFY(rt);
    QVERIFY(attrsOut & WildPalms::PalmSync::PalmRecord::AttrSecret);
    // decode path re-applies the secrecy from the PalmRecord attrs —
    // but roundTripThroughBytes sets recordId=1 on the decoded record;
    // we need to re-set AttrSecret on the input to decode to see it.
    WildPalms::PalmSync::PalmRecord rec = DatebookCodec::encode(ev, 0);
    rec.recordId = 1;
    rec.attributes |= WildPalms::PalmSync::PalmRecord::AttrSecret;
    const auto decoded = DatebookCodec::decode(rec);
    QVERIFY(decoded.isValid());
    QCOMPARE(decoded.event->secrecy(),
             KCalendarCore::Incidence::SecrecyPrivate);
}
```

- [ ] **Step 4: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_datebookcodec
ctest --test-dir build --output-on-failure -R tst_datebookcodec
```

Expected: 17 tests PASS (8 identity + 9 content).

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/calendar/datebookcodec.cpp \
        tests/palmcalendar/tst_datebookcodec.cpp
git commit -m "$(cat <<'EOF'
feat(palm-calendar): DatebookCodec content round-trips

Phase E.6 task 3: content-carrying fields in DatebookCodec.

- SUMMARY <-> Appointment_t.description
- DESCRIPTION <-> Appointment_t.note
- DTSTART/DTEND + all-day flag <-> Appointment_t.begin/end + .event
- VALARM (minutes/hours/days granularity) <-> .alarm/.advance/.advanceUnits
- RRULE (daily/weekly with days-of-week/monthly-by-date/yearly) <->
  .repeatType/.repeatFrequency/.repeatDays/.repeatEnd/.repeatForever
- EXDATE <-> .exception[0..exceptions]
- CLASS:PRIVATE <-> PalmRecord::AttrSecret (attributes byte, not the
  Appointment_t payload)

Helper functions: palmTmToQDateTime / qDateTimeToPalmTm for time
conversion; palmAlarmToVAlarm / vAlarmToPalmAlarm (with unit selection
preferring days > hours > minutes); palmRepeatToRRule /
rruleToPalmRepeat for RRULE bridging.

Nine new content round-trip tests bring the codec suite to 17
passing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: PalmCalendarBackend identity + loadCalendars

**Files:**
- Replace: `src/palm/calendar/palmcalendarbackend.h`
- Replace: `src/palm/calendar/palmcalendarbackend.cpp`
- Create: `tests/palmcalendar/tst_palmcalendarbackend.cpp`
- Modify: `tests/palmcalendar/CMakeLists.txt`

Lands the backend class, its identity, and `loadCalendars`. CRUD
operations (fetch/push/delete) are stubbed here; Task 5 fills them in.

- [ ] **Step 1: Write the backend header.**

File `src/palm/calendar/palmcalendarbackend.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H
#define WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H

#include "syncbackend.h"

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PalmCalendar {

class CategoryMappingStore;

/**
 * @brief Calendar-typed SyncBackend wrapping PalmBackend's datebook.
 *
 * Exposes the Palm DatebookDB as a collection `"palm:datebook"` and
 * surfaces virtual sub-calendars per category slot:
 *   - `palm:calendar/0`   "Unfiled" (always present)
 *   - `palm:calendar/<N>` N in 1..15, present iff
 *     `CategoryMappingStore::slotName("DatebookDB", N)` is non-empty.
 *
 * Records are routed to/from virtual calendars by PalmRecord.category.
 *
 * Lifetime: does NOT own the IPalmDatabaseAccess or CategoryMappingStore.
 * Caller retains ownership; both must outlive the backend.
 *
 * Scope (Phase E.6): loadCalendars, fetchItems, pushItems, deleteItems
 * fully implemented. Legacy pure-virtual APIs (loadItems, storeItems,
 * updateItem, removeItem, storeCalendars, startSync) get minimal
 * scaffolding to satisfy the abstract interface; Calendar CRUD at the
 * sub-calendar level returns false (Palm slots are implicit).
 */
class PalmCalendarBackend : public Kalburator::Sync::SyncBackend
{
    Q_OBJECT
public:
    /// Collection ID the backend responds to.
    static constexpr const char *CollectionId = "palm:datebook";
    /// Palm database name this backend wraps.
    static constexpr const char *DatabaseName = "DatebookDB";
    /// Prefix of every virtual sub-calendar ID.
    static constexpr const char *CalendarIdPrefix = "palm:calendar/";

    explicit PalmCalendarBackend(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        CategoryMappingStore *categoryStore,
        QObject *parent = nullptr);
    ~PalmCalendarBackend() override;

    // ========== Identity ==========
    QString backendType() const override;
    Kalburator::Sync::DataDomain dataDomain() const override;

    // ========== Discovery ==========
    void loadCalendars(const QString &collectionId) override;

    // ========== Legacy pure-virtual scaffolding (Task 6) ==========
    void loadItems(KCalendarCore::MemoryCalendar *cal,
                   bool suppressSignals = false) override;
    void storeCalendars(
        const QString &collectionId,
        const QList<KCalendarCore::MemoryCalendar *> &calendars) override;
    void storeItems(KCalendarCore::MemoryCalendar *cal,
                    const QList<KCalendarCore::Incidence::Ptr> &items) override;
    void updateItem(KCalendarCore::MemoryCalendar *cal,
                    const KCalendarCore::Incidence::Ptr &item,
                    const QString &icalData) override;
    void startSync(
        const QString &collectionId,
        KCalendarCore::MemoryCalendar *calendar,
        const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
        const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
        const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    // ========== Operation-based API (Task 5) ==========
    Kalburator::Sync::FetchOperation *fetchItems(
        const QString &calendarId) override;
    Kalburator::Sync::PushOperation *pushItems(
        const QString &calendarId,
        const QList<KCalendarCore::Incidence::Ptr> &items) override;
    Kalburator::Sync::DeleteOperation *deleteItems(
        const QString &calendarId, const QStringList &uids) override;

    // Exposed for testing: parse slot from calendarId "palm:calendar/<N>".
    // Returns -1 on bad ID.
    static int slotFromCalendarId(const QString &calendarId);
    static QString calendarIdForSlot(int slot);

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    CategoryMappingStore *m_categoryStore = nullptr;
};

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H
```

- [ ] **Step 2: Write the backend impl — identity + loadCalendars.**

File `src/palm/calendar/palmcalendarbackend.cpp`:

```cpp
#include "palmcalendarbackend.h"

#include <QRegularExpression>

#include "syncoperation.h"

#include "categorymappingstore.h"
#include "datadomain.h"
#include "ipalmdatabaseaccess.h"

namespace WildPalms::PalmCalendar {

using Kalburator::Sync::DataDomain;
using Kalburator::Sync::DeleteOperation;
using Kalburator::Sync::FetchOperation;
using Kalburator::Sync::PushOperation;
using Kalburator::Sync::SyncOperation;
using WildPalms::PalmSync::IPalmDatabaseAccess;

PalmCalendarBackend::PalmCalendarBackend(IPalmDatabaseAccess *device,
                                         CategoryMappingStore *categoryStore,
                                         QObject *parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_device(device)
    , m_categoryStore(categoryStore)
{
}

PalmCalendarBackend::~PalmCalendarBackend() = default;

QString PalmCalendarBackend::backendType() const
{
    return QStringLiteral("palm-calendar");
}

DataDomain PalmCalendarBackend::dataDomain() const
{
    return DataDomain::Calendar;
}

void PalmCalendarBackend::loadCalendars(const QString &collectionId)
{
    if (collectionId != QLatin1String(CollectionId)) {
        emit loadCalendarsFinished(
            collectionId, false,
            QStringLiteral("not a Palm calendar collection"));
        return;
    }

    // Slot 0 always.
    emit calendarDiscovered(collectionId, calendarIdForSlot(0));

    // Slots 1..15 from the store.
    if (m_categoryStore) {
        for (int slot : m_categoryStore->populatedSlots(
                 QLatin1String(DatabaseName))) {
            emit calendarDiscovered(collectionId, calendarIdForSlot(slot));
        }
    }

    emit loadCalendarsFinished(collectionId, true);
}

int PalmCalendarBackend::slotFromCalendarId(const QString &calendarId)
{
    static const QRegularExpression kRe(QStringLiteral("^palm:calendar/(\\d+)$"));
    const auto m = kRe.match(calendarId);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    if (!ok || n < 0 || n > 15) return -1;
    return n;
}

QString PalmCalendarBackend::calendarIdForSlot(int slot)
{
    return QStringLiteral("%1%2")
        .arg(QLatin1String(CalendarIdPrefix))
        .arg(slot);
}

// ========== Legacy pure-virtual stubs (Task 6 fills these) ==========
void PalmCalendarBackend::loadItems(KCalendarCore::MemoryCalendar *,
                                     bool) {}
void PalmCalendarBackend::storeCalendars(
    const QString &, const QList<KCalendarCore::MemoryCalendar *> &) {}
void PalmCalendarBackend::storeItems(
    KCalendarCore::MemoryCalendar *,
    const QList<KCalendarCore::Incidence::Ptr> &) {}
void PalmCalendarBackend::updateItem(
    KCalendarCore::MemoryCalendar *, const KCalendarCore::Incidence::Ptr &,
    const QString &) {}
void PalmCalendarBackend::startSync(
    const QString &, KCalendarCore::MemoryCalendar *,
    const QList<KCalendarCore::Incidence::Ptr> &,
    const QList<KCalendarCore::Incidence::Ptr> &,
    const QMap<QString, QString> &) {}
void PalmCalendarBackend::removeItem(const QString &, const QString &) {}

// ========== Operation API stubs (Task 5 fills these) ==========
FetchOperation *PalmCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    op->fail(QStringLiteral("fetchItems: not yet implemented (Task 5)"));
    return op;
}

PushOperation *PalmCalendarBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    op->fail(QStringLiteral("pushItems: not yet implemented (Task 5)"));
    return op;
}

DeleteOperation *PalmCalendarBackend::deleteItems(
    const QString &calendarId, const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
    op->fail(QStringLiteral("deleteItems: not yet implemented (Task 5)"));
    return op;
}

} // namespace WildPalms::PalmCalendar
```

- [ ] **Step 3: Write identity + discovery tests.**

File `tests/palmcalendar/tst_palmcalendarbackend.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "datadomain.h"

#include "categorymappingstore.h"
#include "mockpalmdatabaseaccess.h"
#include "palmcalendarbackend.h"

using Kalburator::Sync::DataDomain;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCalendar::PalmCalendarBackend;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestPalmCalendarBackend : public QObject
{
    Q_OBJECT
private slots:
    void identity();
    void slotFromCalendarIdParsing();
    void calendarIdForSlotFormatting();
    void loadCalendarsEmptyStoreYieldsOnlyUnfiled();
    void loadCalendarsWithPopulatedSlots();
    void loadCalendarsUnknownCollectionFails();
};

void TestPalmCalendarBackend::identity()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QCOMPARE(backend.backendType(), QStringLiteral("palm-calendar"));
    QCOMPARE(backend.dataDomain(), DataDomain::Calendar);
}

void TestPalmCalendarBackend::slotFromCalendarIdParsing()
{
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/0")), 0);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/15")), 15);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/16")), -1);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/abc")), -1);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("local:memo:1")), -1);
}

void TestPalmCalendarBackend::calendarIdForSlotFormatting()
{
    QCOMPARE(PalmCalendarBackend::calendarIdForSlot(0),
             QStringLiteral("palm:calendar/0"));
    QCOMPARE(PalmCalendarBackend::calendarIdForSlot(7),
             QStringLiteral("palm:calendar/7"));
}

void TestPalmCalendarBackend::loadCalendarsEmptyStoreYieldsOnlyUnfiled()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);
    QSignalSpy finished (&backend, &PalmCalendarBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("palm:datebook"));

    QCOMPARE(discovered.size(), 1);
    QCOMPARE(discovered[0][0].toString(), QStringLiteral("palm:datebook"));
    QCOMPARE(discovered[0][1].toString(), QStringLiteral("palm:calendar/0"));

    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("palm:datebook"));
    QCOMPARE(finished[0][1].toBool(), true);
}

void TestPalmCalendarBackend::loadCalendarsWithPopulatedSlots()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Kids"));
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("Travel"));

    PalmCalendarBackend backend(&dev, &store);
    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);

    backend.loadCalendars(QStringLiteral("palm:datebook"));

    QCOMPARE(discovered.size(), 4);  // 0 + 1 + 3 + 5
    QStringList ids;
    for (const auto &args : discovered) {
        ids << args[1].toString();
    }
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/3")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/5")));
}

void TestPalmCalendarBackend::loadCalendarsUnknownCollectionFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);
    QSignalSpy finished (&backend, &PalmCalendarBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("palm:memos"));

    QCOMPARE(discovered.size(), 0);
    QCOMPARE(finished.size(),   1);
    QCOMPARE(finished[0][1].toBool(), false);
    QCOMPARE(finished[0][2].toString(),
             QStringLiteral("not a Palm calendar collection"));
}

QTEST_MAIN(TestPalmCalendarBackend)
#include "tst_palmcalendarbackend.moc"
```

- [ ] **Step 4: Register the test.**

Append to `tests/palmcalendar/CMakeLists.txt`:

```cmake
add_palm_calendar_test(tst_palmcalendarbackend tst_palmcalendarbackend.cpp)
```

- [ ] **Step 5: Build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_palmcalendarbackend
ctest --test-dir build --output-on-failure -R tst_palmcalendarbackend
```

Expected: 6 tests PASS.

- [ ] **Step 6: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/calendar/palmcalendarbackend.h \
        src/palm/calendar/palmcalendarbackend.cpp \
        tests/palmcalendar/tst_palmcalendarbackend.cpp \
        tests/palmcalendar/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-calendar): PalmCalendarBackend identity + discovery

Phase E.6 task 4: PalmCalendarBackend class, implements
Kalburator::Sync::SyncBackend. Identity: backendType="palm-calendar",
dataDomain=Calendar.

Wraps PalmBackend's datebook collection (id "palm:datebook", DB
"DatebookDB"). Virtual calendar IDs are "palm:calendar/<slot>" for
slot 0..15. Slot 0 always surfaces as "Unfiled"; slots 1..15 only
when named in the CategoryMappingStore.

loadCalendars emits calendarDiscovered for each populated slot then
loadCalendarsFinished. Unknown collection IDs fail cleanly.

CRUD operations (fetch/push/delete) stubbed; Task 5 fills them in.
Legacy pure-virtual APIs stubbed to satisfy the abstract interface;
Task 6 carries minimal legacy-API scaffolding.

Six backend tests cover identity, calendar-id round-trip, empty-store
discovery (only Unfiled), populated-store discovery, and unknown
collection rejection.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: PalmCalendarBackend CRUD (fetch/push/delete)

**Files:**
- Modify: `src/palm/calendar/palmcalendarbackend.cpp`
- Modify: `tests/palmcalendar/tst_palmcalendarbackend.cpp`

Replaces the stub operation methods with real implementations against
the `IPalmDatabaseAccess` + `DatebookCodec`.

Key semantics:

- `fetchItems(calendarId)`: parse slot, read all DatebookDB records,
  filter by `category == slot`, decode each via `DatebookCodec`, skip
  `AttrDeleted` records, populate `FetchOperation::setFetchedItems`,
  mark complete. Emits `fetchStarted` / `itemFetched` / `fetchFinished`.
- `pushItems(calendarId, items)`: parse slot, for each Incidence:
  cast to Event (skip non-Event), encode via `DatebookCodec`, call
  `createRecord` if `recordId == 0` else `updateRecord`. Track
  succeeded/failed UIDs on the op. Emit `writeFinished`.
- `deleteItems(calendarId, uids)`: for each uid, resolve recordId from
  the uid pattern "palm-datebook-<N>" (or via lookup of Incidences in
  the device), call `deleteRecord`. Track succeeded/failed UIDs.

- [ ] **Step 1: Replace the stub operation methods.**

In `src/palm/calendar/palmcalendarbackend.cpp`, replace the three stub
operation method bodies (`fetchItems`, `pushItems`, `deleteItems`) with
the real implementations. First add the required includes at the top:

```cpp
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "datebookcodec.h"
#include "palmrecord.h"
```

Then the new `fetchItems`:

```cpp
FetchOperation *PalmCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);

    const int slot = slotFromCalendarId(calendarId);
    if (slot < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    op->setState(SyncOperation::Running);

    const auto records = m_device->readAllRecords(QLatin1String(DatabaseName));
    emit fetchStarted(calendarId, records.size());

    QList<KCalendarCore::Incidence::Ptr> items;
    int skipped = 0;
    for (const auto &rec : records) {
        if (static_cast<int>(rec.category) != slot) {
            continue;
        }
        const auto decoded = DatebookCodec::decode(rec);
        if (!decoded.isValid()) {
            ++skipped;
            continue;
        }
        items.append(decoded.event);
        emit itemFetched(calendarId, decoded.event);
    }

    op->setFetchedItems(items);
    op->complete();
    emit fetchFinished(calendarId, true);
    return op;
}
```

The new `pushItems`:

```cpp
PushOperation *PalmCalendarBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);

    const int slot = slotFromCalendarId(calendarId);
    if (slot < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        emit writeFinished(calendarId, false, err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        emit writeFinished(calendarId, false, err);
        return op;
    }

    // Ensure the database exists. A real device may surface a missing
    // DatebookDB as a hard error; the mock creates lazily.
    m_device->createDatabase(QLatin1String(DatabaseName));

    op->setState(SyncOperation::Running);
    emit writeStarted(calendarId, items.size());

    for (const auto &incidence : items) {
        if (!incidence || incidence->type() != KCalendarCore::IncidenceBase::TypeEvent) {
            op->addFailedUid(incidence ? incidence->uid() : QString());
            continue;
        }
        const auto event = incidence.staticCast<KCalendarCore::Event>();
        auto rec = DatebookCodec::encode(event, slot);
        if (rec.data.isEmpty()) {
            op->addFailedUid(event->uid());
            continue;
        }

        if (rec.recordId == 0) {
            // New record.
            const auto newId = m_device->createRecord(
                QLatin1String(DatabaseName), rec);
            if (newId == 0) {
                op->addFailedUid(event->uid());
                continue;
            }
            // Stash the assigned record ID on the event so callers
            // carrying the Incidence onward see the server-side ID.
            event->setCustomProperty(
                "KCalendarCore",
                QByteArray(DatebookCodec::RecordIdProperty),
                QString::number(newId));
            op->addSucceededUid(event->uid());
        } else {
            if (m_device->updateRecord(QLatin1String(DatabaseName), rec)) {
                op->addSucceededUid(event->uid());
            } else {
                op->addFailedUid(event->uid());
            }
        }
    }

    op->complete();
    emit writeFinished(calendarId, true);
    return op;
}
```

The new `deleteItems`:

```cpp
DeleteOperation *PalmCalendarBackend::deleteItems(
    const QString &calendarId, const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);

    if (slotFromCalendarId(calendarId) < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        return op;
    }

    op->setState(SyncOperation::Running);

    // UIDs from DatebookCodec have the form "palm-datebook-<recordId>".
    static const QRegularExpression kUidRe(
        QStringLiteral("^palm-datebook-(\\d+)$"));

    for (const auto &uid : uids) {
        const auto m = kUidRe.match(uid);
        if (!m.hasMatch()) {
            op->addFailedUid(uid);
            continue;
        }
        bool ok = false;
        const auto recordId = m.captured(1).toUInt(&ok);
        if (!ok) {
            op->addFailedUid(uid);
            continue;
        }
        if (m_device->deleteRecord(QLatin1String(DatabaseName), recordId)) {
            op->addSucceededUid(uid);
        } else {
            op->addFailedUid(uid);
        }
    }

    op->complete();
    return op;
}
```

- [ ] **Step 2: Append CRUD tests.**

Append new slot decls to `TestPalmCalendarBackend`:

```cpp
    void fetchItemsReturnsOnlyMatchingSlot();
    void fetchItemsSkipsDeletedRecords();
    void fetchItemsInvalidCalendarIdFails();
    void pushItemsCreatesNewRecordsWithCorrectSlot();
    void pushItemsUpdatesExistingRecord();
    void pushItemsWithNonEventSkipsAndReportsFailed();
    void deleteItemsRemovesFromDevice();
    void deleteItemsMissingRecordReportsFailed();
    void pushThenFetchRoundTripsIncidence();
```

And the slot bodies, before `QTEST_MAIN`:

```cpp
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>

#include "datebookcodec.h"
#include "palmrecord.h"

using KCalendarCore::Event;
using KCalendarCore::Incidence;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::PalmRecord;

namespace {

/// Push a minimal all-day event via the codec directly so we can stage
/// records on the mock device without going through the backend. This
/// keeps fetchItems tests independent of pushItems.
PalmRecord stageDatebookRecord(MockPalmDatabaseAccess &dev, int slot,
                               const QString &summary,
                               std::uint8_t extraAttrs = 0)
{
    auto ev = Event::Ptr::create();
    ev->setSummary(summary);
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    auto rec = DatebookCodec::encode(ev, slot);
    rec.attributes |= extraAttrs;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    const auto id = dev.createRecord(QStringLiteral("DatebookDB"), rec);
    rec.recordId = id;
    return rec;
}

} // namespace

void TestPalmCalendarBackend::fetchItemsReturnsOnlyMatchingSlot()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    stageDatebookRecord(dev, 3, QStringLiteral("Work event"));
    stageDatebookRecord(dev, 3, QStringLiteral("Another work"));
    stageDatebookRecord(dev, 7, QStringLiteral("Personal event"));

    auto *op = backend.fetchItems(QStringLiteral("palm:calendar/3"));
    QVERIFY(op);
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->fetchedItems().size(), 2);
    for (const auto &inc : op->fetchedItems()) {
        QCOMPARE(inc->summary().startsWith(QStringLiteral("Work")) ||
                 inc->summary() == QStringLiteral("Another work"),
                 true);
    }
    op->deleteLater();
}

void TestPalmCalendarBackend::fetchItemsSkipsDeletedRecords()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    stageDatebookRecord(dev, 0, QStringLiteral("Live"));
    stageDatebookRecord(dev, 0, QStringLiteral("Dead"),
                        /*extraAttrs=*/PalmRecord::AttrDeleted);

    auto *op = backend.fetchItems(QStringLiteral("palm:calendar/0"));
    QCOMPARE(op->fetchedItems().size(), 1);
    QCOMPARE(op->fetchedItems().first()->summary(), QStringLiteral("Live"));
    op->deleteLater();
}

void TestPalmCalendarBackend::fetchItemsInvalidCalendarIdFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto *op = backend.fetchItems(QStringLiteral("local:memo:1"));
    QCOMPARE(op->state(), SyncOperation::Failed);
    op->deleteLater();
}

void TestPalmCalendarBackend::pushItemsCreatesNewRecordsWithCorrectSlot()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("New meeting"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 7, 1), QTime(0, 0), Qt::LocalTime));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/9"),
                                 { ev.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 1);
    QCOMPARE(op->failedUids().size(),    0);

    const auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 9);
    op->deleteLater();
}

void TestPalmCalendarBackend::pushItemsUpdatesExistingRecord()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    // Stage an existing record.
    const auto existing = stageDatebookRecord(dev, 2, QStringLiteral("Old"));
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 1);

    // Build an event with the existing record ID.
    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("New"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 7, 2), QTime(0, 0), Qt::LocalTime));
    ev->setCustomProperty("KCalendarCore",
                          QByteArray(DatebookCodec::RecordIdProperty),
                          QString::number(existing.recordId));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/2"),
                                 { ev.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    // Still only one record (update, not create).
    const auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);

    // Decode it and check the summary updated.
    const auto rt = DatebookCodec::decode(stored.first());
    QVERIFY(rt.isValid());
    QCOMPARE(rt.event->summary(), QStringLiteral("New"));
    op->deleteLater();
}

void TestPalmCalendarBackend::pushItemsWithNonEventSkipsAndReportsFailed()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto todo = KCalendarCore::Todo::Ptr::create();
    todo->setSummary(QStringLiteral("Not an event"));
    todo->setUid(QStringLiteral("not-an-event"));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/0"),
                                 { todo.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 0);
    QCOMPARE(op->failedUids().size(),    1);
    QCOMPARE(op->failedUids().first(),   QStringLiteral("not-an-event"));
    op->deleteLater();
}

void TestPalmCalendarBackend::deleteItemsRemovesFromDevice()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    const auto staged = stageDatebookRecord(dev, 0, QStringLiteral("Doomed"));
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 1);

    const auto uid = QStringLiteral("palm-datebook-%1").arg(staged.recordId);
    auto *op = backend.deleteItems(QStringLiteral("palm:calendar/0"),
                                   QStringList{ uid });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids(), QStringList{ uid });
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 0);
    op->deleteLater();
}

void TestPalmCalendarBackend::deleteItemsMissingRecordReportsFailed()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    dev.createDatabase(QStringLiteral("DatebookDB"));

    auto *op = backend.deleteItems(
        QStringLiteral("palm:calendar/0"),
        QStringList{ QStringLiteral("palm-datebook-999") });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 0);
    QCOMPARE(op->failedUids().size(),    1);
    op->deleteLater();
}

void TestPalmCalendarBackend::pushThenFetchRoundTripsIncidence()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 7, QStringLiteral("Trips"));
    PalmCalendarBackend backend(&dev, &store);

    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("Flight"));
    ev->setDtStart(QDateTime(QDate(2026, 8, 15), QTime(10, 0), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 8, 15), QTime(14, 0), Qt::LocalTime));
    ev->setDescription(QStringLiteral("Gate B12"));

    auto *pushOp = backend.pushItems(QStringLiteral("palm:calendar/7"),
                                     { ev.staticCast<Incidence>() });
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    pushOp->deleteLater();

    auto *fetchOp = backend.fetchItems(QStringLiteral("palm:calendar/7"));
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    QCOMPARE(fetchOp->fetchedItems().size(), 1);

    const auto rt = fetchOp->fetchedItems().first();
    QCOMPARE(rt->summary(),     QStringLiteral("Flight"));
    QCOMPARE(rt->description(), QStringLiteral("Gate B12"));
    QCOMPARE(rt.staticCast<Event>()->dtStart().time(), QTime(10, 0));
    fetchOp->deleteLater();
}
```

- [ ] **Step 3: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_palmcalendarbackend
ctest --test-dir build --output-on-failure -R tst_palmcalendarbackend
```

Expected: 15 tests PASS (6 from task 4 + 9 new CRUD).

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/calendar/palmcalendarbackend.cpp \
        tests/palmcalendar/tst_palmcalendarbackend.cpp
git commit -m "$(cat <<'EOF'
feat(palm-calendar): PalmCalendarBackend CRUD (fetch/push/delete)

Phase E.6 task 5: real operation-based API on PalmCalendarBackend.

- fetchItems(calendarId): parses slot, reads DatebookDB via
  IPalmDatabaseAccess, filters by category==slot, decodes each
  matching record via DatebookCodec, skips AttrDeleted. Populates
  FetchOperation.fetchedItems, emits fetchStarted/itemFetched/
  fetchFinished, marks Succeeded.
- pushItems(calendarId, items): for each Incidence: skip non-Events,
  encode via DatebookCodec with slot, createRecord if recordId==0
  else updateRecord. Tracks succeededUids/failedUids on the
  PushOperation. New records' assigned IDs are stashed back on the
  event via X-WP-PALM-RECORDID so the caller can round-trip.
- deleteItems(calendarId, uids): parses recordId from uid pattern
  "palm-datebook-<N>", calls deleteRecord. Tracks succeeded/failed.

Nine new CRUD tests bring the backend suite to 15. Round-trip test
(push -> fetch) verifies that a full incidence (summary + description
+ start/end times) survives the encode/decode/store/load cycle
through the real backend against MockPalmDatabaseAccess.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Legacy pure-virtual implementations

**Files:**
- Modify: `src/palm/calendar/palmcalendarbackend.cpp`

Satisfies the deprecated / legacy `SyncBackend` pure-virtual surface
so downstream consumers that still call through the old API don't
trigger `pure virtual called` crashes. The modern path remains
`fetchItems`/`pushItems`/`deleteItems`.

Scope: minimal behaviour that respects the backend's invariants.

- [ ] **Step 1: Replace the legacy stub bodies.**

In `src/palm/calendar/palmcalendarbackend.cpp`, replace the empty
legacy stub method bodies with:

```cpp
void PalmCalendarBackend::loadItems(KCalendarCore::MemoryCalendar *cal,
                                     bool suppressSignals)
{
    if (!cal || !m_device) {
        return;
    }

    // Legacy API has no calendarId context — we load all DatebookDB
    // records into the given MemoryCalendar regardless of slot.
    // Callers preferring slot routing use fetchItems(calendarId).
    const auto records = m_device->readAllRecords(QLatin1String(DatabaseName));
    for (const auto &rec : records) {
        const auto decoded = DatebookCodec::decode(rec);
        if (!decoded.isValid()) continue;
        cal->addIncidence(decoded.event);
        if (!suppressSignals) {
            emit itemLoaded(cal, decoded.event, QString{});
        }
    }
    if (!suppressSignals) {
        emit calendarLoaded(cal);
    }
}

void PalmCalendarBackend::storeCalendars(
    const QString &, const QList<KCalendarCore::MemoryCalendar *> &)
{
    // Palm calendar slots are implicit (created/renamed via the device's
    // category editor). No storage action at this level.
}

void PalmCalendarBackend::storeItems(
    KCalendarCore::MemoryCalendar *,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    // Legacy API lacks calendarId, so we route to Unfiled (slot 0) —
    // callers needing slot control use pushItems(calendarId, items).
    if (items.isEmpty()) return;
    auto *op = pushItems(QStringLiteral("palm:calendar/0"), items);
    if (op) op->deleteLater();
}

void PalmCalendarBackend::updateItem(
    KCalendarCore::MemoryCalendar *, const KCalendarCore::Incidence::Ptr &item,
    const QString &icalData)
{
    if (!item) return;

    KCalendarCore::Incidence::Ptr effective = item;
    if (!icalData.isEmpty()) {
        // Parse icalData and take the first event if present.
        KCalendarCore::ICalFormat fmt;
        auto tempCal = KCalendarCore::MemoryCalendar::Ptr::create(
            QTimeZone::UTC);
        if (fmt.fromString(tempCal, icalData)) {
            const auto events = tempCal->events();
            if (!events.isEmpty()) {
                effective = events.first().staticCast<KCalendarCore::Incidence>();
            }
        }
    }

    // Route to whichever slot the event carries, or 0.
    int slot = 0;
    const auto slotStr = effective->customProperty(
        "KCalendarCore",
        QByteArray(DatebookCodec::CategorySlotProperty));
    if (!slotStr.isEmpty()) {
        bool ok = false;
        const int parsed = slotStr.toInt(&ok);
        if (ok && parsed >= 0 && parsed <= 15) slot = parsed;
    }
    auto *op = pushItems(calendarIdForSlot(slot), { effective });
    if (op) op->deleteLater();
}

void PalmCalendarBackend::startSync(
    const QString &, KCalendarCore::MemoryCalendar *,
    const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
    const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
    const QMap<QString, QString> &stagedDeletions)
{
    // Route by each incidence's X-WP-PALM-CATEGORY-SLOT property,
    // else 0.
    auto slotForIncidence = [](const KCalendarCore::Incidence::Ptr &inc) {
        if (!inc) return 0;
        const auto s = inc->customProperty(
            "KCalendarCore",
            QByteArray(DatebookCodec::CategorySlotProperty));
        if (s.isEmpty()) return 0;
        bool ok = false;
        const int n = s.toInt(&ok);
        return (ok && n >= 0 && n <= 15) ? n : 0;
    };

    // Creations + updates both go through pushItems (the codec
    // preserves recordId from the property, so pushItems' own
    // "recordId==0 ? create : update" logic handles both).
    QHash<int, QList<KCalendarCore::Incidence::Ptr>> bySlot;
    for (const auto &inc : stagedCreations) bySlot[slotForIncidence(inc)].append(inc);
    for (const auto &inc : stagedUpdates)   bySlot[slotForIncidence(inc)].append(inc);
    for (auto it = bySlot.constBegin(); it != bySlot.constEnd(); ++it) {
        auto *op = pushItems(calendarIdForSlot(it.key()), it.value());
        if (op) op->deleteLater();
    }

    // Deletions: map<uid, calendarId>. Group by calendarId.
    QHash<QString, QStringList> delByCal;
    for (auto it = stagedDeletions.constBegin();
         it != stagedDeletions.constEnd(); ++it) {
        delByCal[it.value()].append(it.key());
    }
    for (auto it = delByCal.constBegin(); it != delByCal.constEnd(); ++it) {
        auto *op = deleteItems(it.key(), it.value());
        if (op) op->deleteLater();
    }
}

void PalmCalendarBackend::removeItem(const QString &calId,
                                      const QString &itemUid)
{
    auto *op = deleteItems(calId, QStringList{ itemUid });
    if (op) op->deleteLater();
    emit itemRemoved(calId, itemUid);
}
```

Also add the include at the top of the file:

```cpp
#include <QTimeZone>
```

- [ ] **Step 2: Build — sanity check.**

No new tests here (legacy APIs are covered by the indirect smoke
paths and their behaviour is explicitly "best-effort scaffolding").
Just ensure compilation holds and existing tests still pass.

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target WildPalmsPalmCalendar
cmake --build build -j"$(nproc)" --target tst_palmcalendarbackend
ctest --test-dir build --output-on-failure -R tst_palmcalendarbackend
```

Expected: 15 tests still PASS.

- [ ] **Step 3: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/calendar/palmcalendarbackend.cpp
git commit -m "$(cat <<'EOF'
feat(palm-calendar): legacy SyncBackend API scaffolding

Phase E.6 task 6: minimal bodies for the legacy pure-virtual
SyncBackend methods so the class is non-abstract and old callers
don't segfault:

- loadItems(cal): load all DatebookDB records into the given
  calendar regardless of slot (slot routing requires the new
  fetchItems(calendarId)).
- storeCalendars: no-op (Palm slots are implicit — device-side
  category editor manages them).
- storeItems(cal, items): forwards to pushItems on slot 0.
- updateItem(cal, inc, icalData): parses icalData if present,
  routes to the slot in X-WP-PALM-CATEGORY-SLOT (or 0), pushes.
- startSync: groups creations/updates by slot, forwards to
  pushItems; groups deletions by calendarId, forwards to
  deleteItems.
- removeItem(calId, uid): forwards to deleteItems, emits itemRemoved.

All operations created via these paths deleteLater() themselves
(callers don't expect to take ownership). The modern path remains
fetchItems/pushItems/deleteItems.

Backend suite still at 15 passing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Full-suite verification + backend ID edge cases

**Files:**
- Modify: `tests/palmcalendar/tst_palmcalendarbackend.cpp`

A few additional backend tests that exercise edge cases flagged in
the design conversation:
- Pushing to a slot whose name isn't in the store still works (store
  is for display only).
- deleteItems with an invalid calendarId fails.
- fetchItems for a slot with zero records returns an empty op.

Then run the full WP ctest.

- [ ] **Step 1: Add edge-case tests.**

Append new slot decls to `TestPalmCalendarBackend`:

```cpp
    void pushToUnnamedSlotStillStores();
    void deleteItemsInvalidCalendarIdFails();
    void fetchItemsEmptySlotReturnsEmpty();
```

And the slot bodies:

```cpp
void TestPalmCalendarBackend::pushToUnnamedSlotStillStores()
{
    // Slot 12 is not in the store, but we can still push to it.
    // loadCalendars won't surface the slot, but the record persists.
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("Unnamed slot"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 9, 1), QTime(0, 0), Qt::LocalTime));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/12"),
                                 { ev.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    op->deleteLater();

    const auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 12);
}

void TestPalmCalendarBackend::deleteItemsInvalidCalendarIdFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto *op = backend.deleteItems(
        QStringLiteral("local:memo:1"),
        QStringList{ QStringLiteral("palm-datebook-1") });
    QCOMPARE(op->state(), SyncOperation::Failed);
    op->deleteLater();
}

void TestPalmCalendarBackend::fetchItemsEmptySlotReturnsEmpty()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    // Stage records for slot 3 only; fetch slot 4.
    stageDatebookRecord(dev, 3, QStringLiteral("foo"));

    auto *op = backend.fetchItems(QStringLiteral("palm:calendar/4"));
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QVERIFY(op->fetchedItems().isEmpty());
    op->deleteLater();
}
```

- [ ] **Step 2: Build + run backend tests.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_palmcalendarbackend
ctest --test-dir build --output-on-failure -R tst_palmcalendarbackend
```

Expected: 18 tests PASS.

- [ ] **Step 3: Full WP ctest.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. 29 total
(26 pre-E.6 + tst_categorymappingstore + tst_datebookcodec +
tst_palmcalendarbackend).

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add tests/palmcalendar/tst_palmcalendarbackend.cpp
git commit -m "$(cat <<'EOF'
test(palm-calendar): backend edge-case coverage

Phase E.6 task 7: three additional backend tests covering edge
cases surfaced during design:

- pushItems to an unnamed slot (not in CategoryMappingStore) still
  stores the record. The store is for display-name lookup during
  loadCalendars only; the backend never uses it to gate writes.
- deleteItems with an invalid calendarId fails cleanly (no device
  calls).
- fetchItems for a slot with zero records returns a Succeeded op
  with empty fetchedItems (no failure, no early return).

Full WP ctest at 29/29 (3 new test executables in E.6).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Mark E.6 done in the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`

- [ ] **Step 1: Update the sub-phases table.**

In the sub-phases table, change the E.6 row from:

```markdown
| **E.6** | `PalmCalendarBackend : SyncBackend`. Exposes virtual sub-calendars per category slot. Unit tests: decode Datebook → `Incidence::Ptr`, re-encode, round-trip equality. | WP | E.5 | WP ctest passes. |
```

To:

```markdown
| ✅ **E.6** | `PalmCalendarBackend : SyncBackend` landed at `src/palm/calendar/` in new static lib `WildPalmsPalmCalendar`. Fresh `DatebookCodec` (Palm record bytes ↔ `KCalendarCore::Event::Ptr` via pisock's `pack_Appointment`/`unpack_Appointment`) with full content round-trip: times/summary/description/alarm/repeat/EXDATE/private flag. `CategoryMappingStore` stores slot → display-name map, injected into backend non-owning. Virtual calendar IDs `palm:calendar/<slot>` for 0..15. Records route to/from virtual calendars by `PalmRecord::category`. Real fetchItems/pushItems/deleteItems against `IPalmDatabaseAccess`. Legacy `loadItems`/`storeItems`/`updateItem`/`removeItem`/`startSync` scaffolded (forward to operation API). AppInfo-block parsing + plugin wiring defer to E.10/E.17. Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e6-palm-calendar-backend.md`. | WP | E.5 | WP ctest passes; 7 store + 17 codec + 18 backend tests cover round-trip and routing. |
```

- [ ] **Step 2: Commit.**

```bash
cd ~/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "$(cat <<'EOF'
docs(phase-e): mark E.6 landed in spec sub-phases table

PalmCalendarBackend + DatebookCodec + CategoryMappingStore landed
2026-04-21 in new static lib WildPalmsPalmCalendar at
src/palm/calendar/. Virtual sub-calendars per category slot
(palm:calendar/<0..15>) expose the Palm DatebookDB to the
Kalburator::Sync calendar-typed backend API. Content round-trip
verified (times / summary / description / alarm / repeats /
exceptions / private flag). AppInfo parsing + plugin wiring
deferred to E.10 / E.17.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

**Spec coverage (Phase-E spec E.6 row):**
- "`PalmCalendarBackend : SyncBackend`" — Task 4 + 5 + 6. ✓
- "Exposes virtual sub-calendars per category slot" — Task 4
  (`loadCalendars`) + Task 5 (routing in `fetchItems`/`pushItems`). ✓
- "Unit tests: decode Datebook → `Incidence::Ptr`, re-encode, round-trip
  equality" — Task 3 (9 content round-trip tests). ✓
- "WP ctest passes" — Task 7 exit gate. ✓

**Section §"Category routing (virtual sub-calendars)" coverage:**
- Virtual calendar naming `palm:calendar/<slot>`: Task 4
  (`calendarIdForSlot` / `slotFromCalendarId`). ✓
- Slot 0 "Unfiled" always present: Task 1 (`CategoryMappingStore::slotName`
  always returns "Unfiled" for slot 0) + Task 4 (`loadCalendars`
  unconditionally emits slot 0). ✓
- Slots 1..15 appear when named: Task 4 (`loadCalendars` uses
  `populatedSlots`). ✓
- On read: route by `PalmRecord.category`: Task 5 (`fetchItems`
  filters by slot). ✓
- On write: pack with slot from calendar ID: Task 5 (`pushItems`
  `DatebookCodec::encode(event, slot)`). ✓
- No target mapping → Unfiled fallback: covered by Task 6's legacy
  `storeItems` forwarding to slot 0.
- Category rename/reorder: out-of-scope per spec; the backend reads
  from the store at enumerate-time, so display-name updates follow.
- Category-ID collision during conflict resolution: E.5 handler
  owns that; we deferred handler-side store injection to E.10+ as
  scope note.

**Deferred explicitly:**
- Full AppInfo-block parsing (pisock `unpack_CategoryAppInfo`) —
  E.17/E.18 when live-device integration lands.
- Handler consuming `CategoryMappingStore` — E.10+ when typed adapters
  and real multi-category sync arrive.
- Windows-1252 text transliteration — E.7 (we use
  `QString::fromLatin1` for now, same as the existing mapper's subset).
- Plugin wiring / app-layer construction — E.10 Calendar plugin
  rewrite + E.16 unified runtime.
- Operation async (worker thread) — E.16.
- Sub-calendar CRUD (`createCalendar` / `renameCalendar` /
  `deleteCalendar`): Palm slots are implicit; defer forever unless a
  future DateBk7+ concept surfaces.

**Placeholder scan:** no TBD / TODO / FIXME in any task body. Each
code block is complete; Task 1 Step 3's placeholder-file creation is
intentional scaffolding superseded by tasks 2 and 4.

**Type consistency:**
- `CategoryMappingStore` (namespace `WildPalms::PalmCalendar`): method
  names `setSlotName` / `slotName` / `populatedSlots` / `clear`
  consistent across Tasks 1 + 4 + 5 + 7.
- `DatebookCodec`: `decode`/`encode` signatures consistent;
  `DecodeResult` with `event`/`slot`/`failureReason`/`isValid()`
  consistent across Tasks 2 + 3 + 4 + 5 + 6. Static property names
  `RecordIdProperty` = `"X-WP-PALM-RECORDID"` and
  `CategorySlotProperty` = `"X-WP-PALM-CATEGORY-SLOT"` consistent
  everywhere they appear.
- `PalmCalendarBackend`: `CollectionId` = `"palm:datebook"`,
  `DatabaseName` = `"DatebookDB"`, `CalendarIdPrefix` =
  `"palm:calendar/"` consistent across Tasks 4 + 5 + 6.
- `slotFromCalendarId` / `calendarIdForSlot` signatures stable.
- `PalmRecord::AttrDeleted` / `AttrSecret` constants referenced in
  tests match `src/palm/sync/palmrecord.h:33-37`.
- `Kalburator::Sync::SyncBackend` virtual overrides match
  `libkalburator/src/calendar/syncbackend.h` (`backendType`,
  `dataDomain`, `loadCalendars`, `loadItems`, `storeCalendars`,
  `storeItems`, `updateItem`, `startSync`, `removeItem`, `fetchItems`,
  `pushItems`, `deleteItems`).
- `Kalburator::Sync::FetchOperation`/`PushOperation`/`DeleteOperation`
  method names (`setFetchedItems`/`addSucceededUid`/`addFailedUid`/
  `start`/`complete`/`fail`/`state`) match
  `libkalburator/src/calendar/syncoperation.h:150-271`.
- `pi-datebook.h` types/functions (`Appointment_t`,
  `unpack_Appointment`, `pack_Appointment`, `free_Appointment`,
  `repeatNone`/`repeatDaily`/`repeatWeekly`/`repeatMonthlyByDay`/
  `repeatMonthlyByDate`/`repeatYearly`, `datebook_v1`) are stable
  pisock API.

No gaps detected.

---

## Follow-up plans

After this plan lands:

- **E.7** — Typed adapters for contacts / memos / todos. Extends
  `PalmBackend`'s `backendToPalm` / `palmToBackend` to preserve
  attributes byte (`AttrArchived` / `AttrSecret` / `AttrDirty`) on
  write-back. At that point E.6's codec-via-secret-flag test becomes
  end-to-end through the engine's apply path.
- **E.10** — Calendar plugin rewrite as `IBackendPlugin`. Constructs
  `PalmBackend` + `PalmCalendarBackend` + `CategoryMappingStore` +
  `PalmConflictHandler` and registers with the coordinator. The E.5
  handler gains a `CategoryMappingStore *` for richer category-remap
  logic. Old `src/plugins/calendar/calendarmapper.{h,cpp}` deletes here.
- **E.17/E.18** — Live-device integration: real
  `dlp_ReadAppBlock` + `unpack_CategoryAppInfo` populate the
  `CategoryMappingStore` from the device at session start. POSE64
  sandbox drives end-to-end Datebook round-trips with real bytes.
