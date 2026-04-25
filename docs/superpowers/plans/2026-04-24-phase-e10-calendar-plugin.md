# Phase E.10 — Calendar Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the Calendar conduit as the second new-ABI `IBackendPlugin`, becoming the first real consumer of `PalmBackend` and `PalmCalendarBackend`. Surface Palm category slots as virtual sub-collections that route end-to-end through `BlobSyncEngine::twoWayWithBaseline`, with a calendar-aware conflict handler.

**Architecture:** New plugin `CalendarBackendPlugin` returns both `ProvidedBackends` slots: `blob = CalendarBlobBackend` (transcoding `IBlobBackend`, one collection per populated category slot, `text/calendar`) and `calendar = PalmCalendarBackend` (existing E.6 typed `SyncBackend`). Plugin owns a `CategoryMappingStore` populated at construction from a new `CategoryAppInfoReader` reading the Datebook AppInfo block via a small `IPalmDatabaseAccess::readAppBlock` addition. `CalendarConflictHandler` composes a `PalmConflictHandler` and adds three calendar-aware overlays (alarm-only, EXDATE-only, DTSTART-tz-only) before delegation. Behind CMake toggle `WILDPALMS_CALENDAR_PLUGIN_V2=ON`; legacy `CalendarConduit` keeps building when off; both `.so`s coexist until E.16.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), KF6::CoreAddons (`KPluginMetaData`, `KPluginFactory`, `kcoreaddons_add_plugin`), KF6::CalendarCore (`Event`, `ICalFormat`, `Alarm`), `Kalburator::Sync` (`IBlobBackend`, `BlobSyncEngine::twoWayWithBaseline`, `MockBlobBackend`, `QSyncCore::ConflictHandler`, `BlobBaselineStore`, `ConflictHandlerRegistry`, `ConflictStore`, `ConflictPolicy`), pisock (via existing `DatebookCodec` + `unpack_CategoryAppInfo`). No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-04-24-phase-e10-calendar-plugin-design.md`. Decisions #1–#5 are authoritative.

**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.10 (line 588).

**Repo:** Parent at `~/dev/WildPalms/`. Build directory: `build-dev/` (preset project). Plugin source lives in submodule `src/plugins/calendar/` (`wildpalms-conduit-calendar.git`). Tests live in parent under `tests/plugins/calendar/`. No upstream libkalburator changes.

**Submodule split (per memo precedent in project memory):**
- Tasks 2–6 commit inside `src/plugins/calendar/` submodule.
- Tasks 1, 7, 8 commit in the parent repo.
- Task 8 includes the submodule pointer bump.

---

## Scope explicitly excluded

- **Deleting `CalendarConduit`, `calendarmapper.{h,cpp}`, `calendar-conduit.json`** — retired in E.16.
- **`ConflictDialog` new-plugin lookup path** — open from E.9 (Risk R5). Policy-driven resolution via `CalendarConflictHandler` works fine; interactive calendar conflicts before the follow-up remain a known regression.
- **`CategoryMappingStore` rename/move to `src/palm/`** — deferred until E.11/E.12 when contacts/todos consume it.
- **`SyncCoordinator`-level end-to-end** — `tst_calendar_v2` drives `BlobSyncEngine::twoWayWithBaseline` directly (matches `tst_memo_v2`). Coordinator coverage in E.18.
- **`LocalBlobBackend` smoke test** — `tst_calendar_v2` uses `MockBlobBackend` per Decision #5. `LocalBlobBackend`'s id-rewrite-to-path semantics break baseline matching; `IDMappingStore` is E.15+.
- **Live-device `PilotLinkPalmDatabaseAccess::readAppBlock` integration test** — E.18 (POSE64 sandbox). Task 1 lands the unit-tested forward only.
- **Flipping the CMake toggle default** — `WILDPALMS_CALENDAR_PLUGIN_V2` ships `ON`; `OFF` path keeps the legacy conduit building. CI exercises both.
- **Main-window typed-calendar-tab consumption of `PalmCalendarBackend`** — E.16/E.17 unified runtime.
- **Registry-side `lookupHandler("palm")` API addition** — plugin instantiates its own `PalmConflictHandler` for delegation (matches Decision #3 R3 mitigation).
- **`CalendarView` rewrite or refactor** — reused untouched per Decision #4.

---

## File Structure

**Files to CREATE in submodule `src/plugins/calendar/`:**

- `categoryappinforeader.h` — pure-function pisock wrapper for Datebook AppInfo parsing.
- `categoryappinforeader.cpp`
- `icstranscoder.h` — namespace-scope free functions wrapping `DatebookCodec` + `KCalendarCore::ICalFormat`.
- `icstranscoder.cpp`
- `calendarblobbackend.h` — transcoding `IBlobBackend` wrapping `PalmBackend`'s `palm:datebook` collection.
- `calendarblobbackend.cpp`
- `calendarconflicthandler.h` — `QSyncCore::ConflictHandler` composing `PalmConflictHandler` with three calendar overlays.
- `calendarconflicthandler.cpp`
- `calendarbackendplugin.h` — `IBackendPlugin` shell.
- `calendarbackendplugin.cpp` — class implementation + `K_PLUGIN_FACTORY_WITH_JSON`.
- `calendar-backend-plugin.json` — new manifest (`X-WildPalms-PluginType: "backend"`).

**Files to MODIFY in submodule `src/plugins/calendar/`:**

- `CMakeLists.txt` — add `WILDPALMS_CALENDAR_PLUGIN_V2` option; build new lib + plugin when on; keep legacy conduit when off.

**Files to CREATE in parent repo:**

- `tests/plugins/calendar/CMakeLists.txt`
- `tests/plugins/calendar/tst_categoryappinforeader.cpp`
- `tests/plugins/calendar/tst_icstranscoder.cpp`
- `tests/plugins/calendar/tst_calendarblobbackend.cpp`
- `tests/plugins/calendar/tst_calendarconflicthandler.cpp`
- `tests/plugins/calendar/tst_calendarbackendplugin.cpp`
- `tests/plugins/calendar/tst_calendar_v2.cpp` — end-to-end via `BackendPluginManager` + `BlobSyncEngine`.

**Files to MODIFY in parent repo:**

- `src/palm/sync/ipalmdatabaseaccess.h` — add `virtual QByteArray readAppBlock(const QString &dbName) const = 0;`
- `src/palm/sync/mockpalmdatabaseaccess.h` / `mockpalmdatabaseaccess.cpp` — implement `readAppBlock` + `setAppBlock(dbName, bytes)` test setter.
- `src/palm/sync/palmbackend.h` / `palmbackend.cpp` — add `QByteArray readAppBlock(const QString &dbName) const;` pass-through.
- `src/palm/device/pilotlinkpalmdatabaseaccess.h` / `pilotlinkpalmdatabaseaccess.cpp` — implement `readAppBlock` forwarding to `KPilotLink::readAppBlock` (Task 1 scope).
- `tests/palm/sync/tst_palmbackend.cpp` (or whichever existing palm-backend test file there is — discover during Task 1) — extend with `readAppBlock` tests.
- `tests/plugins/CMakeLists.txt` — add `add_subdirectory(calendar)`.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` — flip row E.10 to `✅ **E.10**` (Task 8).
- `docs/plans/2026-04-20-libkalburator-integration.md` — mark E.10 landed in Phase E sub-phases table (Task 8).
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + new `project_phase_e10_calendar.md` — record landed status, AppInfo reader added, conflict overlays, deferrals (Task 8).

**Files to LEAVE UNTOUCHED in submodule:**

- `calendarconduit.{h,cpp}`, `calendarmapper.{h,cpp}`, `calendar-conduit.json` — retained until E.16.
- `calendarview.{h,cpp}` — shared between old and new plugin paths; no changes (Decision #4).

**Files to LEAVE UNTOUCHED in parent:**

- `src/palm/calendar/categorymappingstore.{h,cpp}` — used as-is.
- `src/palm/calendar/datebookcodec.{h,cpp}` — used as-is.
- `src/palm/calendar/palmcalendarbackend.{h,cpp}` — returned as-is from plugin's `createBackends`.
- `src/palm/conflict/palmconflicthandler.{h,cpp}` — composed inside `CalendarConflictHandler`, no changes.
- `src/palm/conflict/palmbackendconfig.h` — used as-is.
- `src/core/iplugin.h`, `src/core/ibackendplugin.h`, `src/core/ipluginaction.h` — base interfaces unchanged.
- `src/kf6/conduitmanager.{h,cpp}` — stays alive until E.16.
- `pilot-link/`, `pilot-link-git/` — read-only.

---

## Task 1: Add `readAppBlock` to `IPalmDatabaseAccess` + `PalmBackend` + adapters

Goal: introduce a small surface addition so the new `CategoryAppInfoReader` (Task 2) can read the Datebook AppInfo block via `PalmBackend`. `KPilotDeviceLink::readAppBlock` already exists; this task plumbs it up through the `IPalmDatabaseAccess` abstraction the plugin sees.

**Files:**
- Modify: `src/palm/sync/ipalmdatabaseaccess.h`
- Modify: `src/palm/sync/mockpalmdatabaseaccess.h`, `src/palm/sync/mockpalmdatabaseaccess.cpp`
- Modify: `src/palm/sync/palmbackend.h`, `src/palm/sync/palmbackend.cpp`
- Modify: `src/palm/device/pilotlinkpalmdatabaseaccess.h`, `src/palm/device/pilotlinkpalmdatabaseaccess.cpp`
- Modify or create: `tests/palm/sync/tst_mockpalmdatabaseaccess.cpp` and `tst_palmbackend.cpp` (locate existing files)

- [ ] **Step 1.1: Locate existing palm-sync test files**

Run:
```bash
ls /home/clinton/dev/WildPalms/tests/palm/
find /home/clinton/dev/WildPalms/tests -name "tst_palmbackend*" -o -name "tst_mockpalm*"
```

Note the paths returned; the test additions in Steps 1.4 and 1.7 go into the matching files (or new ones if absent).

- [ ] **Step 1.2: Add `readAppBlock` to `IPalmDatabaseAccess`**

Edit `src/palm/sync/ipalmdatabaseaccess.h`. Find the `supportsDeleteTracking()` declaration near the bottom; insert immediately before it:

```cpp
    /// Read the database's AppInfo block (raw bytes, layout
    /// database-specific). Returns empty QByteArray if the database
    /// has no AppInfo or on read error. PalmBackend forwards calls
    /// straight through; plugins (e.g. CalendarBackendPlugin) parse
    /// the bytes via a database-specific reader (e.g. CategoryAppInfo
    /// for Datebook/Address/Memo/Todo).
    virtual QByteArray readAppBlock(const QString &dbName) const = 0;
```

- [ ] **Step 1.3: Add `readAppBlock` + `setAppBlock` to `MockPalmDatabaseAccess`**

Edit `src/palm/sync/mockpalmdatabaseaccess.h`. After the `supportsDeleteTracking() override` line, add:

```cpp
    QByteArray readAppBlock(const QString &dbName) const override;

    /// Test setter: stores `bytes` under `dbName`. Subsequent
    /// readAppBlock(dbName) returns `bytes` verbatim.
    void setAppBlock(const QString &dbName, const QByteArray &bytes);
```

In the `Database` struct (private), add:

```cpp
        QByteArray appInfo;
```

Edit `src/palm/sync/mockpalmdatabaseaccess.cpp`. At the bottom, before the closing namespace, add:

```cpp
QByteArray MockPalmDatabaseAccess::readAppBlock(const QString &dbName) const
{
    auto it = m_dbs.constFind(dbName);
    return (it == m_dbs.cend()) ? QByteArray() : it->appInfo;
}

void MockPalmDatabaseAccess::setAppBlock(const QString &dbName,
                                         const QByteArray &bytes)
{
    // Auto-create the database if absent so test setup order doesn't
    // matter.
    if (!m_dbs.contains(dbName)) {
        m_dbs.insert(dbName, Database{});
    }
    m_dbs[dbName].appInfo = bytes;
}
```

- [ ] **Step 1.4: Write the failing test for the mock**

Create or extend `tests/palm/sync/tst_mockpalmdatabaseaccess.cpp` (use the existing file if Step 1.1 found one; otherwise create with QtTest harness boilerplate). Add this test method:

```cpp
void TestMockPalmDatabaseAccess::appBlockRoundTrip()
{
    MockPalmDatabaseAccess dev;

    // Empty for unknown database.
    QCOMPARE(dev.readAppBlock(QStringLiteral("DatebookDB")), QByteArray());

    // setAppBlock auto-creates database.
    const QByteArray bytes("\x01\x02\x03appinfo-payload", 19);
    dev.setAppBlock(QStringLiteral("DatebookDB"), bytes);
    QCOMPARE(dev.readAppBlock(QStringLiteral("DatebookDB")), bytes);

    // Overwriting works.
    const QByteArray bytes2("other", 5);
    dev.setAppBlock(QStringLiteral("DatebookDB"), bytes2);
    QCOMPARE(dev.readAppBlock(QStringLiteral("DatebookDB")), bytes2);
}
```

If creating a new file, add it to the appropriate `tests/palm/sync/CMakeLists.txt` and register the test method in the class's `private slots:` block.

- [ ] **Step 1.5: Run failing test, verify, then re-run after implementation**

Run from the project root:
```bash
cd /home/clinton/dev/WildPalms && cmake --build build-dev --target tst_mockpalmdatabaseaccess && ctest --test-dir build-dev -R tst_mockpalmdatabaseaccess --output-on-failure
```

Expected: PASS (the implementation in Step 1.3 already exists). If FAIL with "method not declared," confirm Steps 1.2 and 1.3 saved correctly.

- [ ] **Step 1.6: Add `readAppBlock` to `PalmBackend`**

Edit `src/palm/sync/palmbackend.h`. After the last public method (look for `bool updatePalmRecord(...)`), add:

```cpp
    /// AppInfo-block accessor. Forwards to IPalmDatabaseAccess; returns
    /// empty QByteArray on missing database or read failure. Used by
    /// plugins (CalendarBackendPlugin) to populate per-database
    /// CategoryMappingStore at session start.
    QByteArray readAppBlock(const QString &dbName) const;
```

Edit `src/palm/sync/palmbackend.cpp`. Add at the bottom (before closing namespace):

```cpp
QByteArray PalmBackend::readAppBlock(const QString &dbName) const
{
    return m_device ? m_device->readAppBlock(dbName) : QByteArray();
}
```

- [ ] **Step 1.7: Write the failing test for `PalmBackend::readAppBlock`**

Edit (or create) `tests/palm/sync/tst_palmbackend.cpp`. Add:

```cpp
void TestPalmBackend::readAppBlockForwardsToDevice()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);

    // Empty when unset.
    QCOMPARE(backend.readAppBlock(QStringLiteral("DatebookDB")), QByteArray());

    const QByteArray bytes("\x10\x20mock-appinfo", 14);
    dev.setAppBlock(QStringLiteral("DatebookDB"), bytes);
    QCOMPARE(backend.readAppBlock(QStringLiteral("DatebookDB")), bytes);
}
```

Register the slot in the test class's `private slots:` if it isn't auto-discovered.

- [ ] **Step 1.8: Build and run**

```bash
cmake --build build-dev --target tst_palmbackend && ctest --test-dir build-dev -R tst_palmbackend --output-on-failure
```

Expected: PASS.

- [ ] **Step 1.9: Implement `PilotLinkPalmDatabaseAccess::readAppBlock`**

Edit `src/palm/device/pilotlinkpalmdatabaseaccess.h`. Find the existing override declarations (look for `recordsModifiedSince` etc.). Add:

```cpp
    QByteArray readAppBlock(const QString &dbName) const override;
```

Edit `src/palm/device/pilotlinkpalmdatabaseaccess.cpp`. Add (matching the file's existing style — check for the open-then-call-then-close pattern in `readAllRecords`):

```cpp
QByteArray PilotLinkPalmDatabaseAccess::readAppBlock(const QString &dbName) const
{
    if (!m_link) return {};
    const int handle = openDatabase(dbName);   // helper used by other methods
    if (handle < 0) return {};

    // Pisock convention: try a generous buffer; the actual returned
    // size determines what we keep. AppInfo blocks are typically
    // < 1 KiB; 4 KiB is plenty for any pathological case.
    QByteArray buf(4096, '\0');
    std::size_t actualSize = static_cast<std::size_t>(buf.size());
    const bool ok = m_link->readAppBlock(handle,
        reinterpret_cast<unsigned char *>(buf.data()), &actualSize);

    closeDatabase(handle);

    if (!ok) return {};
    buf.resize(static_cast<int>(actualSize));
    return buf;
}
```

(If the existing file uses different helper names like `openDatabaseHandle`/`closeDatabaseHandle`, substitute the actual names; if it inlines open/close in each method, mirror that pattern instead.)

- [ ] **Step 1.10: Build the parent target to confirm no regressions**

```bash
cmake --build build-dev --target WildPalmsPalmSync WildPalmsPalmDevice
ctest --test-dir build-dev -R "tst_(mockpalm|palmbackend|palmdevice)" --output-on-failure
```

Expected: All matching tests PASS.

- [ ] **Step 1.11: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/palm/sync/ipalmdatabaseaccess.h \
        src/palm/sync/mockpalmdatabaseaccess.h \
        src/palm/sync/mockpalmdatabaseaccess.cpp \
        src/palm/sync/palmbackend.h \
        src/palm/sync/palmbackend.cpp \
        src/palm/device/pilotlinkpalmdatabaseaccess.h \
        src/palm/device/pilotlinkpalmdatabaseaccess.cpp \
        tests/palm/sync/tst_mockpalmdatabaseaccess.cpp \
        tests/palm/sync/tst_palmbackend.cpp
git commit -m "$(cat <<'EOF'
feat(palm): readAppBlock on IPalmDatabaseAccess + PalmBackend (Phase E.10)

Adds the surface CategoryAppInfoReader needs to populate
CategoryMappingStore at plugin construction. MockPalmDatabaseAccess
gains a setAppBlock test setter; PilotLinkPalmDatabaseAccess forwards
to KPilotLink::readAppBlock. Live-device coverage defers to E.18.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `CategoryAppInfoReader` (in submodule)

Goal: pure-function pisock wrapper that parses a Datebook AppInfo block into 16 category names, plus a helper that drives `CategoryMappingStore::setSlotName` for each non-empty slot.

**Files:**
- Create (in submodule `src/plugins/calendar/`): `categoryappinforeader.h`, `categoryappinforeader.cpp`
- Create (in parent): `tests/plugins/calendar/tst_categoryappinforeader.cpp` and minimal `tests/plugins/calendar/CMakeLists.txt`
- Modify (in parent): `tests/plugins/CMakeLists.txt` to add the new subdirectory

- [ ] **Step 2.1: Create the header**

Create `src/plugins/calendar/categoryappinforeader.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_CATEGORYAPPINFOREADER_H
#define WILDPALMS_CALENDAR_CATEGORYAPPINFOREADER_H

#include <array>
#include <optional>

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCalendar {
class CategoryMappingStore;
}

namespace WildPalms::CalendarPlugin {

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
 * @brief Parse a Datebook AppInfo block into 16 category names.
 *
 * Wraps pisock's `unpack_CategoryAppInfo` (pi-appinfo.h). Returns
 * std::nullopt if `appInfoBytes.size()` < the minimum CategoryAppInfo
 * size or if the underlying unpack fails.
 *
 * Slot 0 is normalised to "Unfiled" when the unpacked name is empty.
 *
 * Pure function — no Qt event loop, no I/O, no global state. Safe to
 * call from any thread.
 */
std::optional<CategoryNames>
parseDatebookAppInfo(const QByteArray &appInfoBytes);

/**
 * @brief Populate `store` with the named slots from `appInfoBytes`.
 *
 * Calls `parseDatebookAppInfo`, then for every non-empty name in
 * slots 1..15 invokes `store.setSlotName(dbName, slot, name)`. Slot 0
 * is intentionally skipped — `CategoryMappingStore` treats slot 0 as
 * implicit "Unfiled".
 *
 * Returns false if parsing failed (store left untouched), true
 * otherwise (even if no slots were named — empty AppInfo is valid).
 */
bool populateFromAppInfo(WildPalms::PalmCalendar::CategoryMappingStore &store,
                         const QString &dbName,
                         const QByteArray &appInfoBytes);

} // namespace WildPalms::CalendarPlugin

#endif // WILDPALMS_CALENDAR_CATEGORYAPPINFOREADER_H
```

- [ ] **Step 2.2: Write the failing test**

Create `tests/plugins/calendar/tst_categoryappinforeader.cpp`:

```cpp
#include <QtTest/QtTest>

#include <pi-appinfo.h>
#include <pi-datebook.h>

#include "plugins/calendar/categoryappinforeader.h"
#include "palm/calendar/categorymappingstore.h"

using WildPalms::CalendarPlugin::CategoryNames;
using WildPalms::CalendarPlugin::parseDatebookAppInfo;
using WildPalms::CalendarPlugin::populateFromAppInfo;
using WildPalms::PalmCalendar::CategoryMappingStore;

namespace {

// Build a minimum-valid AppInfo block with the named slots populated.
// Names must fit in pi-appinfo's 16-byte name field.
QByteArray buildAppInfoBytes(const QStringList &slotNames)
{
    AppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    for (int i = 0; i < std::min(slotNames.size(), 16); ++i) {
        const QByteArray utf = slotNames[i].toUtf8().left(15);
        std::memcpy(info.category.name[i], utf.constData(), utf.size());
        info.category.name[i][utf.size()] = '\0';
        info.category.ID[i] = static_cast<unsigned char>(i);
    }
    info.category.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_AppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

} // namespace

class TestCategoryAppInfoReader : public QObject
{
    Q_OBJECT
private slots:
    void parseEmptyReturnsUnfiledOnly();
    void parsePopulatedReturnsNames();
    void parseTruncatedReturnsNullopt();
    void slotZeroForcedToUnfiledWhenBlank();
    void populatePopulatesNonEmptySlotsOnly();
    void populateFailureLeavesStoreUntouched();
};

void TestCategoryAppInfoReader::parseEmptyReturnsUnfiledOnly()
{
    QStringList none;
    QByteArray bytes = buildAppInfoBytes(none);
    QVERIFY(!bytes.isEmpty());

    auto result = parseDatebookAppInfo(bytes);
    QVERIFY(result.has_value());
    QCOMPARE(result->names[0], QStringLiteral("Unfiled"));
    for (int i = 1; i < 16; ++i) {
        QVERIFY2(result->names[i].isEmpty(),
            qPrintable(QStringLiteral("slot %1 expected empty").arg(i)));
    }
}

void TestCategoryAppInfoReader::parsePopulatedReturnsNames()
{
    QStringList names;
    names << QStringLiteral("Unfiled")     // 0
          << QStringLiteral("Work")        // 1
          << QStringLiteral("Personal");   // 2
    auto result = parseDatebookAppInfo(buildAppInfoBytes(names));
    QVERIFY(result.has_value());
    QCOMPARE(result->names[0], QStringLiteral("Unfiled"));
    QCOMPARE(result->names[1], QStringLiteral("Work"));
    QCOMPARE(result->names[2], QStringLiteral("Personal"));
    QVERIFY(result->names[3].isEmpty());
}

void TestCategoryAppInfoReader::parseTruncatedReturnsNullopt()
{
    auto result = parseDatebookAppInfo(QByteArray("\x00\x01", 2));
    QVERIFY(!result.has_value());
}

void TestCategoryAppInfoReader::slotZeroForcedToUnfiledWhenBlank()
{
    // Build with explicitly blank slot 0.
    QStringList names;
    names << QString()                     // 0 — blank, expect "Unfiled"
          << QStringLiteral("Work");
    auto result = parseDatebookAppInfo(buildAppInfoBytes(names));
    QVERIFY(result.has_value());
    QCOMPARE(result->names[0], QStringLiteral("Unfiled"));
    QCOMPARE(result->names[1], QStringLiteral("Work"));
}

void TestCategoryAppInfoReader::populatePopulatesNonEmptySlotsOnly()
{
    CategoryMappingStore store;
    QStringList names;
    names << QStringLiteral("Unfiled")
          << QStringLiteral("Work")
          << QString()                     // slot 2 blank — should NOT populate
          << QStringLiteral("Personal");
    QVERIFY(populateFromAppInfo(store, QStringLiteral("DatebookDB"),
                                buildAppInfoBytes(names)));

    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 1),
             QStringLiteral("Work"));
    QVERIFY(store.slotName(QStringLiteral("DatebookDB"), 2).isEmpty());
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 3),
             QStringLiteral("Personal"));
    // populatedSlots returns sorted ascending list of named slots,
    // skipping slot 0.
    QList<int> populated = store.populatedSlots(QStringLiteral("DatebookDB"));
    QCOMPARE(populated, (QList<int>{1, 3}));
}

void TestCategoryAppInfoReader::populateFailureLeavesStoreUntouched()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Pre"));

    QVERIFY(!populateFromAppInfo(store, QStringLiteral("DatebookDB"),
                                 QByteArray("garbage", 7)));
    // Existing entry untouched.
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 1),
             QStringLiteral("Pre"));
}

QTEST_MAIN(TestCategoryAppInfoReader)
#include "tst_categoryappinforeader.moc"
```

- [ ] **Step 2.3: Create the test CMakeLists**

Create `tests/plugins/calendar/CMakeLists.txt`:

```cmake
# Phase E.10 — Calendar plugin tests.
# Tasks 2-7 build test binaries directly against the source files in
# the calendar submodule.

set(CALENDAR_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/calendar)

# --- Task 2: CategoryAppInfoReader ---
add_executable(tst_categoryappinforeader
    tst_categoryappinforeader.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp
)
target_include_directories(tst_categoryappinforeader
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_categoryappinforeader
    PRIVATE
        WildPalmsPalmCalendar   # CategoryMappingStore
        ${PILOT_LINK_LIBRARIES}
        Qt::Test
        Qt::Core
)
add_test(NAME tst_categoryappinforeader COMMAND tst_categoryappinforeader)
```

(If the project's CMake exposes pisock under different variables — check `src/palm/codecs/CMakeLists.txt` for the exact names — substitute accordingly. Memo's tests link `WildPalmsPalmCodecs` because the codecs link pisock transitively; if `WildPalmsPalmCalendar` does the same, the explicit pisock vars can drop.)

- [ ] **Step 2.4: Wire up the new test directory**

Edit `tests/plugins/CMakeLists.txt`. Add at the bottom:

```cmake
# Phase E.10 — Calendar plugin tests.
add_subdirectory(calendar)
```

- [ ] **Step 2.5: Run failing test, verify**

```bash
cd /home/clinton/dev/WildPalms
cmake --build build-dev --target tst_categoryappinforeader 2>&1 | head -40
```

Expected: FAIL with "categoryappinforeader.cpp: No such file or directory" (we haven't written the implementation).

- [ ] **Step 2.6: Implement `CategoryAppInfoReader`**

Create `src/plugins/calendar/categoryappinforeader.cpp`:

```cpp
#include "categoryappinforeader.h"

#include <cstring>

#include <pi-appinfo.h>

#include "palm/calendar/categorymappingstore.h"

namespace WildPalms::CalendarPlugin {

std::optional<CategoryNames>
parseDatebookAppInfo(const QByteArray &appInfoBytes)
{
    // CategoryAppInfo layout: 16 names of 16 bytes + 16 IDs +
    // lastUniqueID + padding. Empirically ~276 bytes; pisock guards
    // against shorter inputs but we sanity-check up front.
    static constexpr int kMinSize =
        16 * 16 + 16 + 1 + 1;   // 274
    if (appInfoBytes.size() < kMinSize) {
        return std::nullopt;
    }

    AppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const int unpacked = unpack_AppInfo(
        &info,
        reinterpret_cast<unsigned char *>(
            const_cast<char *>(appInfoBytes.constData())),
        appInfoBytes.size());
    if (unpacked < 0) {
        return std::nullopt;
    }

    CategoryNames out;
    for (int i = 0; i < 16; ++i) {
        // Pisock zeros the buffer; trim at NUL.
        const char *raw = info.category.name[i];
        out.names[i] = QString::fromUtf8(raw,
            std::strnlen(raw, sizeof(info.category.name[i])));
    }
    if (out.names[0].isEmpty()) {
        out.names[0] = QStringLiteral("Unfiled");
    }
    return out;
}

bool populateFromAppInfo(WildPalms::PalmCalendar::CategoryMappingStore &store,
                         const QString &dbName,
                         const QByteArray &appInfoBytes)
{
    auto parsed = parseDatebookAppInfo(appInfoBytes);
    if (!parsed) return false;

    for (int slot = 1; slot < 16; ++slot) {
        const QString &name = parsed->names[slot];
        if (!name.isEmpty()) {
            store.setSlotName(dbName, slot, name);
        }
    }
    return true;
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 2.7: Build and run, verify all pass**

```bash
cmake --build build-dev --target tst_categoryappinforeader \
    && ctest --test-dir build-dev -R tst_categoryappinforeader --output-on-failure
```

Expected: 6 tests PASS.

If unpack_AppInfo signature differs in your pisock version (pre-vs-post 0.12.5), check `pi-appinfo.h` for the actual signature and adjust the cast — older versions take `unsigned char *` and a length, newer take a different shape.

- [ ] **Step 2.8: Commit in submodule, separately**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git add categoryappinforeader.h categoryappinforeader.cpp
git commit -m "$(cat <<'EOF'
feat(calendar): CategoryAppInfoReader (Phase E.10 Task 2)

Pure-function pisock wrapper: parses Datebook AppInfo block into 16
category names; populates CategoryMappingStore for slots 1..15. Slot 0
forced to "Unfiled" if blank. Tested against synthesised pack_AppInfo
output and truncation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

cd /home/clinton/dev/WildPalms
git add tests/plugins/calendar/CMakeLists.txt \
        tests/plugins/calendar/tst_categoryappinforeader.cpp \
        tests/plugins/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(calendar): tst_categoryappinforeader (Phase E.10 Task 2)

Adds parent-side test harness for the new submodule reader. Wires
tests/plugins/calendar/ into the build.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

(The submodule pointer bump in the parent is deferred to Task 8.)

---

## Task 3: `IcsTranscoder` (in submodule)

Goal: thin namespace-scope helpers that go Palm-record-bytes ↔ iCal bytes by composing existing `DatebookCodec` (E.6) with `KCalendarCore::ICalFormat`. Used by `CalendarBlobBackend` in Task 4.

**Files:**
- Create (in submodule): `icstranscoder.h`, `icstranscoder.cpp`
- Create (in parent): `tests/plugins/calendar/tst_icstranscoder.cpp`
- Modify (in parent): `tests/plugins/calendar/CMakeLists.txt`

- [ ] **Step 3.1: Create the header**

Create `src/plugins/calendar/icstranscoder.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_ICSTRANSCODER_H
#define WILDPALMS_CALENDAR_ICSTRANSCODER_H

#include <optional>

#include <QByteArray>

#include "palm/sync/palmrecord.h"

namespace WildPalms::CalendarPlugin {

/**
 * @brief Encode a Palm Datebook record into iCalendar VCALENDAR bytes.
 *
 * Composes DatebookCodec::decode (Palm bytes -> Event::Ptr) with
 * KCalendarCore::ICalFormat::toString. Returns empty QByteArray on
 * decode failure or on empty event.
 *
 * Pure function. The `record.category` field is preserved on the
 * Event via DatebookCodec's existing X-WP-PALM-CATEGORY-SLOT
 * property, so re-encoding round-trips the slot.
 */
QByteArray encodePalmToIcs(const WildPalms::PalmSync::PalmRecord &record);

/**
 * @brief Decode VCALENDAR bytes into a PalmRecord with the given slot.
 *
 * `slotHint` is forwarded to DatebookCodec::encode and stamped into
 * `PalmRecord::category`. The Event's X-WP-PALM-RECORDID property
 * (if present) populates `PalmRecord::recordId`; otherwise recordId
 * stays 0 and the device assigns on write.
 *
 * Returns std::nullopt if `icsBytes` doesn't parse as a single
 * VEVENT, or if encoding to Palm bytes fails.
 */
std::optional<WildPalms::PalmSync::PalmRecord>
decodeIcsToPalm(const QByteArray &icsBytes, int slotHint);

} // namespace WildPalms::CalendarPlugin

#endif // WILDPALMS_CALENDAR_ICSTRANSCODER_H
```

- [ ] **Step 3.2: Write the failing tests**

Create `tests/plugins/calendar/tst_icstranscoder.cpp`:

```cpp
#include <QtTest/QtTest>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/calendar/icstranscoder.h"
#include "palm/calendar/datebookcodec.h"
#include "palm/sync/palmrecord.h"

using WildPalms::CalendarPlugin::encodePalmToIcs;
using WildPalms::CalendarPlugin::decodeIcsToPalm;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord makeMeetingRecord(int slot)
{
    KCalendarCore::Event::Ptr e(new KCalendarCore::Event);
    e->setUid(QStringLiteral("meeting-uid-1"));
    e->setSummary(QStringLiteral("Standup"));
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(9, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(9, 30)));
    return DatebookCodec::encode(e, slot);
}

} // namespace

class TestIcsTranscoder : public QObject
{
    Q_OBJECT
private slots:
    void encodeProducesParseableIcs();
    void encodePreservesSummary();
    void roundTripPreservesSlot();
    void decodeWithEmptyBytesReturnsNullopt();
    void decodeWithGarbageReturnsNullopt();
    void decodePreservesRecordIdWhenPresent();
};

void TestIcsTranscoder::encodeProducesParseableIcs()
{
    PalmRecord pr = makeMeetingRecord(0);
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(!ics.isEmpty());
    QVERIFY(ics.contains("BEGIN:VCALENDAR"));
    QVERIFY(ics.contains("BEGIN:VEVENT"));
    QVERIFY(ics.contains("END:VCALENDAR"));
}

void TestIcsTranscoder::encodePreservesSummary()
{
    PalmRecord pr = makeMeetingRecord(2);
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("SUMMARY:Standup"));
}

void TestIcsTranscoder::roundTripPreservesSlot()
{
    PalmRecord pr1 = makeMeetingRecord(7);
    QByteArray ics = encodePalmToIcs(pr1);
    auto pr2opt = decodeIcsToPalm(ics, 7);
    QVERIFY(pr2opt.has_value());
    QCOMPARE(static_cast<int>(pr2opt->category), 7);
}

void TestIcsTranscoder::decodeWithEmptyBytesReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray(), 0).has_value());
}

void TestIcsTranscoder::decodeWithGarbageReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray("not an ics"), 0).has_value());
}

void TestIcsTranscoder::decodePreservesRecordIdWhenPresent()
{
    PalmRecord pr1 = makeMeetingRecord(0);
    pr1.recordId = 42;
    // Re-encode through DatebookCodec so the X-WP-PALM-RECORDID prop
    // is set on the Event before encoding to ICS.
    auto decoded = DatebookCodec::decode(pr1);
    QVERIFY(decoded.isValid());
    decoded.event->setCustomProperty("WildPalms",
        QByteArray("PALM-RECORDID"), QStringLiteral("42"));
    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    cal->addEvent(decoded.event);
    QByteArray ics = fmt.toString(cal).toUtf8();

    auto pr2opt = decodeIcsToPalm(ics, 0);
    QVERIFY(pr2opt.has_value());
    QCOMPARE(pr2opt->recordId, static_cast<std::uint32_t>(42));
}

QTEST_MAIN(TestIcsTranscoder)
#include "tst_icstranscoder.moc"
```

- [ ] **Step 3.3: Add the test target to CMake**

Append to `tests/plugins/calendar/CMakeLists.txt`:

```cmake
# --- Task 3: IcsTranscoder ---
add_executable(tst_icstranscoder
    tst_icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
)
target_include_directories(tst_icstranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_icstranscoder
    PRIVATE
        WildPalmsPalmCalendar   # DatebookCodec
        WildPalmsPalmSync       # PalmRecord
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_icstranscoder COMMAND tst_icstranscoder)
```

- [ ] **Step 3.4: Run failing test, verify**

```bash
cmake --build build-dev --target tst_icstranscoder 2>&1 | head -20
```

Expected: FAIL ("icstranscoder.cpp: No such file").

- [ ] **Step 3.5: Implement `IcsTranscoder`**

Create `src/plugins/calendar/icstranscoder.cpp`:

```cpp
#include "icstranscoder.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "palm/calendar/datebookcodec.h"

namespace WildPalms::CalendarPlugin {

QByteArray encodePalmToIcs(const WildPalms::PalmSync::PalmRecord &record)
{
    auto decoded = WildPalms::PalmCalendar::DatebookCodec::decode(record);
    if (!decoded.isValid()) return {};

    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!cal->addEvent(decoded.event)) return {};

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

    auto events = cal->events();
    if (events.isEmpty()) return std::nullopt;
    auto event = events.first();
    if (!event) return std::nullopt;

    return WildPalms::PalmCalendar::DatebookCodec::encode(event, slotHint);
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 3.6: Build, run, verify pass**

```bash
cmake --build build-dev --target tst_icstranscoder \
    && ctest --test-dir build-dev -R tst_icstranscoder --output-on-failure
```

Expected: 6 tests PASS.

- [ ] **Step 3.7: Commit**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git add icstranscoder.h icstranscoder.cpp
git commit -m "$(cat <<'EOF'
feat(calendar): IcsTranscoder (Phase E.10 Task 3)

Thin composer over DatebookCodec + KCalendarCore::ICalFormat. Used by
CalendarBlobBackend (Task 4) to present iCal bytes at the
IBlobBackend boundary. Round-trip preserves category slot via the
existing X-WP-PALM-CATEGORY-SLOT property.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

cd /home/clinton/dev/WildPalms
git add tests/plugins/calendar/CMakeLists.txt \
        tests/plugins/calendar/tst_icstranscoder.cpp
git commit -m "test(calendar): tst_icstranscoder (Phase E.10 Task 3)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `CalendarBlobBackend` (in submodule)

Goal: transcoding `IBlobBackend` that wraps `PalmBackend`'s `palm:datebook` collection. Surfaces one collection per populated category slot (`palm:calendar/<N>`). Routes records by `PalmRecord::category` on read and by collection-id-parsed slot on write.

**Files:**
- Create (in submodule): `calendarblobbackend.h`, `calendarblobbackend.cpp`
- Create (in parent): `tests/plugins/calendar/tst_calendarblobbackend.cpp`
- Modify (in parent): `tests/plugins/calendar/CMakeLists.txt`

- [ ] **Step 4.1: Create the header**

Create `src/plugins/calendar/calendarblobbackend.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_CALENDARBLOBBACKEND_H
#define WILDPALMS_CALENDAR_CALENDARBLOBBACKEND_H

#include "iblobbackend.h"

#include <QObject>

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::CalendarPlugin {

/**
 * @brief Transcoding IBlobBackend wrapping PalmBackend's palm:datebook.
 *
 * Surfaces one collection per populated category slot:
 *   - "palm:calendar/0"   "Unfiled" (always present)
 *   - "palm:calendar/<N>" 1..15, present iff
 *     `categoryStore->slotName("DatebookDB", N)` is non-empty.
 *
 * Records route to/from these collections by PalmRecord::category.
 * loadRecords transcodes wire bytes -> iCal bytes via IcsTranscoder;
 * createRecord/updateRecord transcode iCal -> wire and forward to
 * PalmBackend's category-aware createPalmRecord/updatePalmRecord.
 *
 * Lifetime: does NOT own palmBackend or categoryStore. Caller retains
 * ownership; both must outlive the backend.
 */
class CalendarBlobBackend : public Kalburator::Sync::IBlobBackend
{
    Q_OBJECT
public:
    static constexpr const char *BackendId        = "calendar";
    static constexpr const char *PalmDbName       = "DatebookDB";
    static constexpr const char *CollectionPrefix = "palm:calendar/";

    explicit CalendarBlobBackend(
        WildPalms::PalmSync::PalmBackend *palmBackend,
        const WildPalms::PalmCalendar::CategoryMappingStore *categoryStore,
        QObject *parent = nullptr);
    ~CalendarBlobBackend() override;

    // --- Identity ---
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- Collections ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override;

    // --- Records ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    // --- Change detection ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId, const QDateTime &since) override;
    bool        supportsDeleteTracking() const override;

    // --- Helpers (exposed for tests) ---
    /// Parse "palm:calendar/<N>" -> N. Returns -1 on bad input.
    static int slotFromCollectionId(const QString &collectionId);
    /// Produce "palm:calendar/<N>".
    static QString collectionIdForSlot(int slot);

private:
    WildPalms::PalmSync::PalmBackend                     *m_palmBackend = nullptr;
    const WildPalms::PalmCalendar::CategoryMappingStore  *m_categoryStore = nullptr;
};

} // namespace WildPalms::CalendarPlugin

#endif // WILDPALMS_CALENDAR_CALENDARBLOBBACKEND_H
```

- [ ] **Step 4.2: Write the failing tests**

Create `tests/plugins/calendar/tst_calendarblobbackend.cpp`:

```cpp
#include <QtTest/QtTest>

#include "plugins/calendar/calendarblobbackend.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/calendar/datebookcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

#include <KCalendarCore/Event>

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using WildPalms::CalendarPlugin::CalendarBlobBackend;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord eventRecord(const QString &uid, int slot)
{
    KCalendarCore::Event::Ptr e(new KCalendarCore::Event);
    e->setUid(uid);
    e->setSummary(QStringLiteral("Event ") + uid);
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(10, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(11, 0)));
    auto pr = DatebookCodec::encode(e, slot);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace

class TestCalendarBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void backendIdAndDisplayName();
    void availableCollectionsReflectsStore();
    void availableCollectionsAlwaysIncludesUnfiled();
    void loadRecordsFiltersBySlot();
    void loadRecordsReturnsIcsContentType();
    void createRecordRoutesToSlot();
    void updateRecordPreservesSlot();
    void deleteRecordForwards();
    void slotParsingHelpers();
};

void TestCalendarBlobBackend::backendIdAndDisplayName()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    CalendarBlobBackend backend(&pb, &store);
    QCOMPARE(backend.backendId(), QStringLiteral("calendar"));
    QVERIFY(!backend.displayName().isEmpty());
    QVERIFY(backend.isAvailable());
}

void TestCalendarBlobBackend::availableCollectionsReflectsStore()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Personal"));
    CalendarBlobBackend backend(&pb, &store);

    auto cols = backend.availableCollections();
    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    QCOMPARE(ids.size(), 3);          // 0 + 1 + 3
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/3")));
}

void TestCalendarBlobBackend::availableCollectionsAlwaysIncludesUnfiled()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;       // empty
    CalendarBlobBackend backend(&pb, &store);

    auto cols = backend.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, QStringLiteral("palm:calendar/0"));
}

void TestCalendarBlobBackend::loadRecordsFiltersBySlot()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("a", 0));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("b", 1));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("c", 1));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("d", 2));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 2, QStringLiteral("Personal"));
    CalendarBlobBackend backend(&pb, &store);

    QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/0")).size(), 1);
    QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/1")).size(), 2);
    QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/2")).size(), 1);
}

void TestCalendarBlobBackend::loadRecordsReturnsIcsContentType()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("a", 0));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    CalendarBlobBackend backend(&pb, &store);

    auto recs = backend.loadRecords(QStringLiteral("palm:calendar/0"));
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().type, QStringLiteral("text/calendar"));
    QVERIFY(!recs.first().data.isEmpty());
    QVERIFY(recs.first().data.contains("BEGIN:VEVENT"));
}

void TestCalendarBlobBackend::createRecordRoutesToSlot()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("Travel"));
    CalendarBlobBackend backend(&pb, &store);

    // Build an ICS payload from a slot-7 event ... but write into
    // collection palm:calendar/5. The collection slot wins.
    auto seedPr = eventRecord("new-uid", 7);
    BackendRecord br;
    br.id   = QString();
    br.data = WildPalms::CalendarPlugin::encodePalmToIcs(seedPr);
    br.type = QStringLiteral("text/calendar");

    QString newId = backend.createRecord(
        QStringLiteral("palm:calendar/5"), br);
    QVERIFY(!newId.isEmpty());

    auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 5);
}

void TestCalendarBlobBackend::updateRecordPreservesSlot()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    auto seedId = dev.createRecord(QStringLiteral("DatebookDB"),
                                   eventRecord("u-1", 4));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 4, QStringLiteral("Volunteering"));
    CalendarBlobBackend backend(&pb, &store);

    auto recs = backend.loadRecords(QStringLiteral("palm:calendar/4"));
    QCOMPARE(recs.size(), 1);
    auto br = recs.first();
    // Mutate summary by string-replacement (cheap, sufficient).
    br.data.replace("SUMMARY:Event u-1", "SUMMARY:Event u-1 (revised)");
    QVERIFY(backend.updateRecord(br));

    auto stored = dev.readRecord(QStringLiteral("DatebookDB"), seedId);
    QVERIFY(stored.has_value());
    QCOMPARE(static_cast<int>(stored->category), 4);
}

void TestCalendarBlobBackend::deleteRecordForwards()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    auto seedId = dev.createRecord(QStringLiteral("DatebookDB"),
                                   eventRecord("d-1", 0));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    CalendarBlobBackend backend(&pb, &store);

    auto recs = backend.loadRecords(QStringLiteral("palm:calendar/0"));
    QCOMPARE(recs.size(), 1);
    QVERIFY(backend.deleteRecord(recs.first().id));
    QVERIFY(!dev.readRecord(QStringLiteral("DatebookDB"), seedId).has_value());
}

void TestCalendarBlobBackend::slotParsingHelpers()
{
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:calendar/0")), 0);
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:calendar/15")), 15);
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:calendar/16")), -1);
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("not-a-calendar-id")), -1);
    QCOMPARE(CalendarBlobBackend::collectionIdForSlot(7),
             QStringLiteral("palm:calendar/7"));
}

QTEST_MAIN(TestCalendarBlobBackend)
#include "tst_calendarblobbackend.moc"
```

- [ ] **Step 4.3: Add the test target**

Append to `tests/plugins/calendar/CMakeLists.txt`:

```cmake
# --- Task 4: CalendarBlobBackend ---
add_executable(tst_calendarblobbackend
    tst_calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
)
target_include_directories(tst_calendarblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_calendarblobbackend
    PRIVATE
        WildPalmsPalmCalendar
        WildPalmsPalmSync
        WildPalmsCore
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_calendarblobbackend COMMAND tst_calendarblobbackend)
```

- [ ] **Step 4.4: Run failing test**

```bash
cmake --build build-dev --target tst_calendarblobbackend 2>&1 | head -20
```

Expected: FAIL ("calendarblobbackend.cpp: No such file").

- [ ] **Step 4.5: Implement `CalendarBlobBackend`**

Create `src/plugins/calendar/calendarblobbackend.cpp`:

```cpp
#include "calendarblobbackend.h"

#include "icstranscoder.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

#include "backendrecord.h"
#include "collectioninfo.h"

#include <QDateTime>
#include <QStringList>

namespace WildPalms::CalendarPlugin {

namespace {

QString idForPalmRecord(std::uint32_t recordId)
{
    return WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral("DatebookDB"), recordId);
}

bool decodeId(const QString &id, std::uint32_t *outRecordId)
{
    QString dbName;
    return WildPalms::PalmSync::PalmBackend::decodeRecordId(id, &dbName, outRecordId)
        && dbName == QLatin1String("DatebookDB");
}

} // namespace

CalendarBlobBackend::CalendarBlobBackend(
    WildPalms::PalmSync::PalmBackend *palmBackend,
    const WildPalms::PalmCalendar::CategoryMappingStore *categoryStore,
    QObject *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_palmBackend(palmBackend)
    , m_categoryStore(categoryStore)
{
}

CalendarBlobBackend::~CalendarBlobBackend() = default;

QString CalendarBlobBackend::backendId()   const { return QStringLiteral("calendar"); }
QString CalendarBlobBackend::displayName() const { return QStringLiteral("Palm Calendar"); }
bool    CalendarBlobBackend::isAvailable() const
{
    return m_palmBackend != nullptr && m_palmBackend->isAvailable();
}

QList<Kalburator::Sync::CollectionInfo> CalendarBlobBackend::availableCollections()
{
    QList<Kalburator::Sync::CollectionInfo> out;

    Kalburator::Sync::CollectionInfo unfiled;
    unfiled.id   = collectionIdForSlot(0);
    unfiled.name = QStringLiteral("Unfiled");
    unfiled.type = QStringLiteral("calendar");
    out.append(unfiled);

    if (!m_categoryStore) return out;

    const QList<int> slots = m_categoryStore->populatedSlots(
        QStringLiteral("DatebookDB"));
    for (int slot : slots) {
        Kalburator::Sync::CollectionInfo info;
        info.id   = collectionIdForSlot(slot);
        info.name = m_categoryStore->slotName(
            QStringLiteral("DatebookDB"), slot);
        info.type = QStringLiteral("calendar");
        out.append(info);
    }
    return out;
}

Kalburator::Sync::CollectionInfo CalendarBlobBackend::collectionInfo(
    const QString &collectionId)
{
    for (const auto &c : availableCollections()) {
        if (c.id == collectionId) return c;
    }
    return {};
}

QString CalendarBlobBackend::createCollection(
    const Kalburator::Sync::CollectionInfo &)
{
    // Slots are governed by the device's AppInfo block — plugin
    // doesn't create new ones. Returning empty signals "not supported".
    return {};
}

QList<Kalburator::Sync::BackendRecord> CalendarBlobBackend::loadRecords(
    const QString &collectionId)
{
    const int slot = slotFromCollectionId(collectionId);
    QList<Kalburator::Sync::BackendRecord> out;
    if (slot < 0 || !m_palmBackend) return out;

    const auto records = m_palmBackend->loadPalmRecords(QStringLiteral("DatebookDB"));
    for (const auto &pr : records) {
        if (static_cast<int>(pr.category) != slot) continue;
        QByteArray ics = encodePalmToIcs(pr);
        if (ics.isEmpty()) continue;   // skip undecodable records (e.g. tombstones)

        Kalburator::Sync::BackendRecord br;
        br.id           = idForPalmRecord(pr.recordId);
        br.data         = ics;
        br.type         = QStringLiteral("text/calendar");
        br.lastModified = pr.lastModified;
        out.append(br);
    }
    return out;
}

std::optional<Kalburator::Sync::BackendRecord>
CalendarBlobBackend::loadRecord(const QString &recordId)
{
    std::uint32_t rid = 0;
    if (!decodeId(recordId, &rid) || !m_palmBackend) return std::nullopt;
    auto pr = m_palmBackend->loadPalmRecord(QStringLiteral("DatebookDB"), rid);
    if (!pr) return std::nullopt;

    QByteArray ics = encodePalmToIcs(*pr);
    if (ics.isEmpty()) return std::nullopt;

    Kalburator::Sync::BackendRecord br;
    br.id           = recordId;
    br.data         = ics;
    br.type         = QStringLiteral("text/calendar");
    br.lastModified = pr->lastModified;
    return br;
}

QString CalendarBlobBackend::createRecord(
    const QString &collectionId,
    const Kalburator::Sync::BackendRecord &record)
{
    const int slot = slotFromCollectionId(collectionId);
    if (slot < 0 || !m_palmBackend) return {};

    auto prOpt = decodeIcsToPalm(record.data, slot);
    if (!prOpt) return {};

    auto pr = *prOpt;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.lastModified = record.lastModified.isValid()
        ? record.lastModified
        : QDateTime::currentDateTimeUtc();

    const auto newId = m_palmBackend->createPalmRecord(
        QStringLiteral("DatebookDB"), pr);
    if (newId == 0) return {};
    return idForPalmRecord(newId);
}

bool CalendarBlobBackend::updateRecord(
    const Kalburator::Sync::BackendRecord &record)
{
    std::uint32_t rid = 0;
    if (!decodeId(record.id, &rid) || !m_palmBackend) return false;

    // Look up the existing record to recover its slot (the
    // BackendRecord's id alone doesn't carry the slot).
    auto existing = m_palmBackend->loadPalmRecord(
        QStringLiteral("DatebookDB"), rid);
    if (!existing) return false;
    const int slot = static_cast<int>(existing->category);

    auto prOpt = decodeIcsToPalm(record.data, slot);
    if (!prOpt) return false;

    auto pr = *prOpt;
    pr.recordId     = rid;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.lastModified = record.lastModified.isValid()
        ? record.lastModified
        : QDateTime::currentDateTimeUtc();

    return m_palmBackend->updatePalmRecord(QStringLiteral("DatebookDB"), pr);
}

bool CalendarBlobBackend::deleteRecord(const QString &recordId)
{
    return m_palmBackend && m_palmBackend->deleteRecord(recordId);
}

QList<Kalburator::Sync::BackendRecord>
CalendarBlobBackend::modifiedSince(const QString &collectionId,
                                   const QDateTime &since)
{
    const int slot = slotFromCollectionId(collectionId);
    QList<Kalburator::Sync::BackendRecord> out;
    if (slot < 0 || !m_palmBackend) return out;

    // Forward to PalmBackend's underlying list, then filter+transcode.
    const auto records = m_palmBackend->loadPalmRecords(QStringLiteral("DatebookDB"));
    for (const auto &pr : records) {
        if (static_cast<int>(pr.category) != slot) continue;
        if (since.isValid() && pr.lastModified <= since) continue;
        QByteArray ics = encodePalmToIcs(pr);
        if (ics.isEmpty()) continue;

        Kalburator::Sync::BackendRecord br;
        br.id           = idForPalmRecord(pr.recordId);
        br.data         = ics;
        br.type         = QStringLiteral("text/calendar");
        br.lastModified = pr.lastModified;
        out.append(br);
    }
    return out;
}

QStringList CalendarBlobBackend::deletedSince(const QString &,
                                              const QDateTime &since)
{
    if (!m_palmBackend) return {};
    // PalmBackend's deletedSince already returns ids encoded for the
    // "palm:datebook" collection — translate to ours by re-encoding.
    QStringList out;
    const QString sourceCollection =
        WildPalms::PalmSync::PalmBackend::encodeCollectionId(
            QStringLiteral("DatebookDB"));
    for (const auto &id : m_palmBackend->deletedSince(sourceCollection, since)) {
        out.append(id);   // id encoding is collection-independent
    }
    return out;
}

bool CalendarBlobBackend::supportsDeleteTracking() const
{
    return m_palmBackend && m_palmBackend->supportsDeleteTracking();
}

int CalendarBlobBackend::slotFromCollectionId(const QString &collectionId)
{
    static constexpr QLatin1String prefix(CollectionPrefix);
    if (!collectionId.startsWith(prefix)) return -1;
    bool ok = false;
    const int slot = collectionId.mid(prefix.size()).toInt(&ok);
    if (!ok || slot < 0 || slot > 15) return -1;
    return slot;
}

QString CalendarBlobBackend::collectionIdForSlot(int slot)
{
    return QString::fromLatin1(CollectionPrefix) + QString::number(slot);
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 4.6: Build, run, verify pass**

```bash
cmake --build build-dev --target tst_calendarblobbackend \
    && ctest --test-dir build-dev -R tst_calendarblobbackend --output-on-failure
```

Expected: 9 tests PASS.

If any test fails on `loadRecord` /`deleteRecord` id handling, double-check `PalmBackend::encodeRecordId` / `decodeRecordId` actually format ids as `"palm:DatebookDB:<n>"` — match the format the production code uses (see `src/palm/sync/palmbackend.cpp` for the exact convention).

- [ ] **Step 4.7: Commit**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git add calendarblobbackend.h calendarblobbackend.cpp
git commit -m "$(cat <<'EOF'
feat(calendar): CalendarBlobBackend (Phase E.10 Task 4)

Transcoding IBlobBackend wrapping PalmBackend's palm:datebook.
One collection per populated category slot; routes records by
PalmRecord::category. Tested: backend identity, collection
enumeration (always-present Unfiled), slot filtering, ICS round-trip,
slot preservation through create/update/delete forwarding.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

cd /home/clinton/dev/WildPalms
git add tests/plugins/calendar/CMakeLists.txt \
        tests/plugins/calendar/tst_calendarblobbackend.cpp
git commit -m "test(calendar): tst_calendarblobbackend (Phase E.10 Task 4)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `CalendarConflictHandler` (in submodule)

Goal: `QSyncCore::ConflictHandler` that composes `PalmConflictHandler` and adds three calendar-aware overlays (alarm-only, EXDATE-only, DTSTART-tz-only) before falling through. Both source and target snapshots arrive as iCal bytes (CalendarBlobBackend transcoded source side; target stores iCal verbatim).

**Files:**
- Create (in submodule): `calendarconflicthandler.h`, `calendarconflicthandler.cpp`
- Create (in parent): `tests/plugins/calendar/tst_calendarconflicthandler.cpp`
- Modify (in parent): `tests/plugins/calendar/CMakeLists.txt`

- [ ] **Step 5.1: Create the header**

Create `src/plugins/calendar/calendarconflicthandler.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_CALENDARCONFLICTHANDLER_H
#define WILDPALMS_CALENDAR_CALENDARCONFLICTHANDLER_H

#include "conflictpolicy.h"   // brings in ConflictHandler base

#include <memory>

namespace WildPalms::PalmSync { class IPalmDatabaseAccess; }
namespace WildPalms::PalmConflict {
class PalmConflictHandler;
struct PalmBackendConfig;
}

namespace WildPalms::CalendarPlugin {

/**
 * @brief ConflictHandler with calendar-aware overlays + Palm delegation.
 *
 * Resolution order:
 *   1. Decode both sides as KCalendarCore::Event::Ptr from iCal bytes.
 *      If either side fails to decode, delegate straight to the inner
 *      PalmConflictHandler (which will fall through to base
 *      ConflictPolicy resolution).
 *   2. Detect calendar-specific shapes:
 *        a. **Alarm-only diff** -> ConflictDecision::Merge with merged
 *           alarms (mergedContent = re-serialised iCal with the union
 *           of both sides' alarms).
 *        b. **EXDATE-only diff** -> Merge with EXDATE-list union.
 *        c. **DTSTART tz-only** with one floating + one zoned -> Merge
 *           preferring the floating side (Palm semantics).
 *   3. Otherwise -> delegate to PalmConflictHandler::handleConflict.
 *
 * Owns its inner PalmConflictHandler — constructed from the
 * (device, config) pair the plugin passes through.
 *
 * Lifetime: does NOT own device or config (forwarded to the inner
 * PalmConflictHandler which also borrows). Both must outlive this
 * handler.
 */
class CalendarConflictHandler : public Kalburator::Sync::QSyncCore::ConflictHandler
{
public:
    CalendarConflictHandler(WildPalms::PalmSync::IPalmDatabaseAccess *device,
                            const WildPalms::PalmConflict::PalmBackendConfig *config);
    ~CalendarConflictHandler() override;

    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &policy) override;

    bool canPrompt() const override { return false; }

    /// Test hook: which overlay (if any) was last applied.
    /// Values: "", "alarm", "exdate", "tz", "delegated".
    const QString &lastOverlay() const { return m_lastOverlay; }

private:
    std::unique_ptr<WildPalms::PalmConflict::PalmConflictHandler> m_palm;
    QString m_lastOverlay;
};

} // namespace WildPalms::CalendarPlugin

#endif // WILDPALMS_CALENDAR_CALENDARCONFLICTHANDLER_H
```

- [ ] **Step 5.2: Write the failing tests**

Create `tests/plugins/calendar/tst_calendarconflicthandler.cpp`:

```cpp
#include <QtTest/QtTest>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/calendar/calendarconflicthandler.h"

#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/conflict/palmbackendconfig.h"

#include "conflictrecord.h"
#include "conflictpolicy.h"

using KCalendarCore::Alarm;
using KCalendarCore::Event;
using KCalendarCore::ICalFormat;
using KCalendarCore::MemoryCalendar;
using Kalburator::Sync::QSyncCore::ConflictDecision;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictRecord;
using Kalburator::Sync::QSyncCore::ConflictType;
using Kalburator::Sync::QSyncCore::RecordSnapshot;
using WildPalms::CalendarPlugin::CalendarConflictHandler;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

namespace {

QByteArray serialiseEvent(const Event::Ptr &event)
{
    auto cal = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::utc()));
    cal->addEvent(event);
    return ICalFormat().toString(cal).toUtf8();
}

Event::Ptr baseEvent()
{
    Event::Ptr e(new Event);
    e->setUid(QStringLiteral("conflict-uid"));
    e->setSummary(QStringLiteral("Meeting"));
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(9, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(10, 0)));
    return e;
}

Event::Ptr withAlarm(int minutesBefore)
{
    auto e = baseEvent();
    auto alarm = e->newAlarm();
    alarm->setType(Alarm::Display);
    alarm->setStartOffset(KCalendarCore::Duration(-minutesBefore * 60));
    return e;
}

Event::Ptr withExdate(const QDate &exdate)
{
    auto e = baseEvent();
    auto rrule = e->recurrence();
    rrule->setDaily(1);
    rrule->setDuration(10);
    e->recurrence()->addExDateTime(QDateTime(exdate, QTime(9, 0)));
    return e;
}

ConflictRecord makeConflict(const QByteArray &sourceIcs,
                            const QByteArray &targetIcs)
{
    ConflictRecord cr;
    cr.conflictId = QStringLiteral("c1");
    cr.type = ConflictType::BothModified;
    cr.source.id = QStringLiteral("palm:DatebookDB:1");
    cr.target.id = QStringLiteral("palm:DatebookDB:1");
    cr.source.content = sourceIcs;
    cr.target.content = targetIcs;
    cr.source.contentType = QStringLiteral("text/calendar");
    cr.target.contentType = QStringLiteral("text/calendar");
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.lastModified = QDateTime::currentDateTimeUtc();
    return cr;
}

} // namespace

class TestCalendarConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void alarmOnlyDiffMergesAlarms();
    void exdateOnlyDiffMergesExdates();
    void tzOnlyDiffPrefersFloating();
    void unrelatedDiffsDelegateToPalm();
    void undecodableContentDelegatesToPalm();
};

void TestCalendarConflictHandler::alarmOnlyDiffMergesAlarms()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = withAlarm(15);
    auto tgt = withAlarm(60);
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    auto decision = h.handleConflict(cr, policy);
    QCOMPARE(decision, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("alarm"));
    QVERIFY(!cr.mergedContent.isEmpty());
    // Both alarm offsets present in merged ICS.
    QVERIFY(cr.mergedContent.contains("PT15M") || cr.mergedContent.contains("-PT15M"));
    QVERIFY(cr.mergedContent.contains("PT1H")  || cr.mergedContent.contains("-PT1H"));
}

void TestCalendarConflictHandler::exdateOnlyDiffMergesExdates()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = withExdate(QDate(2026, 5, 5));
    auto tgt = withExdate(QDate(2026, 5, 7));
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    auto decision = h.handleConflict(cr, policy);
    QCOMPARE(decision, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("exdate"));
    QVERIFY(cr.mergedContent.contains("20260505"));
    QVERIFY(cr.mergedContent.contains("20260507"));
}

void TestCalendarConflictHandler::tzOnlyDiffPrefersFloating()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = baseEvent();   // floating local time
    auto tgt = baseEvent();
    tgt->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(9, 0),
                              QTimeZone("America/New_York")));
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    auto decision = h.handleConflict(cr, policy);
    QCOMPARE(decision, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("tz"));
    // Merged content should reflect the floating side (no TZID).
    QVERIFY(!cr.mergedContent.contains("TZID="));
}

void TestCalendarConflictHandler::unrelatedDiffsDelegateToPalm()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = baseEvent();
    auto tgt = baseEvent();
    tgt->setSummary(QStringLiteral("Meeting (rescheduled)"));
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    h.handleConflict(cr, policy);
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestCalendarConflictHandler::undecodableContentDelegatesToPalm()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto cr = makeConflict(QByteArray("not-ics"), QByteArray("also-not"));
    ConflictPolicy policy;
    h.handleConflict(cr, policy);
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

QTEST_MAIN(TestCalendarConflictHandler)
#include "tst_calendarconflicthandler.moc"
```

- [ ] **Step 5.3: Add the test target**

Append to `tests/plugins/calendar/CMakeLists.txt`:

```cmake
# --- Task 5: CalendarConflictHandler ---
add_executable(tst_calendarconflicthandler
    tst_calendarconflicthandler.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarconflicthandler.cpp
)
target_include_directories(tst_calendarconflicthandler
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_calendarconflicthandler
    PRIVATE
        WildPalmsPalmConflict
        WildPalmsPalmSync
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_calendarconflicthandler COMMAND tst_calendarconflicthandler)
```

- [ ] **Step 5.4: Run failing test**

```bash
cmake --build build-dev --target tst_calendarconflicthandler 2>&1 | head -20
```

Expected: FAIL.

- [ ] **Step 5.5: Implement `CalendarConflictHandler`**

Create `src/plugins/calendar/calendarconflicthandler.cpp`:

```cpp
#include "calendarconflicthandler.h"

#include <algorithm>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Recurrence>

#include "conflictrecord.h"

#include "palm/conflict/palmconflicthandler.h"
#include "palm/conflict/palmbackendconfig.h"

namespace WildPalms::CalendarPlugin {

namespace {

KCalendarCore::Event::Ptr decodeFirstEvent(const QByteArray &icsBytes)
{
    if (icsBytes.isEmpty()) return {};
    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromString(cal, QString::fromUtf8(icsBytes))) return {};
    auto events = cal->events();
    return events.isEmpty() ? KCalendarCore::Event::Ptr() : events.first();
}

QByteArray serialiseEvent(const KCalendarCore::Event::Ptr &event)
{
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    cal->addEvent(event);
    return KCalendarCore::ICalFormat().toString(cal).toUtf8();
}

// Compare events ignoring fields named in `ignored`. Returns true if
// events match on every other significant field.
struct EventDiff {
    bool summaryDiffers = false;
    bool dtStartTimeDiffers = false;     // wall-clock differs
    bool dtStartTzDiffers = false;       // only timezone differs
    bool dtEndDiffers = false;
    bool descriptionDiffers = false;
    bool locationDiffers = false;
    bool alarmsDiffer = false;
    bool exdatesDiffer = false;
    bool recurrenceShapeDiffers = false;
};

EventDiff diffEvents(const KCalendarCore::Event::Ptr &a,
                     const KCalendarCore::Event::Ptr &b)
{
    EventDiff d;
    d.summaryDiffers = a->summary() != b->summary();

    const QDateTime as = a->dtStart(), bs = b->dtStart();
    if (as.toUTC() != bs.toUTC()) {
        d.dtStartTimeDiffers = true;
    } else if (as.timeSpec() != bs.timeSpec()
               || (as.timeSpec() == Qt::TimeZone && as.timeZone() != bs.timeZone())) {
        d.dtStartTzDiffers = true;
    }
    d.dtEndDiffers       = a->dtEnd() != b->dtEnd();
    d.descriptionDiffers = a->description() != b->description();
    d.locationDiffers    = a->location() != b->location();

    auto sortedAlarms = [](const KCalendarCore::Event::Ptr &e) {
        QList<int> offsets;
        for (const auto &al : e->alarms()) {
            offsets.append(al->startOffset().asSeconds());
        }
        std::sort(offsets.begin(), offsets.end());
        return offsets;
    };
    d.alarmsDiffer = sortedAlarms(a) != sortedAlarms(b);

    auto sortedExdates = [](const KCalendarCore::Event::Ptr &e) {
        QList<QDateTime> ds;
        for (const auto &dt : e->recurrence()->exDateTimes()) ds.append(dt.toUTC());
        std::sort(ds.begin(), ds.end());
        return ds;
    };
    d.exdatesDiffer = sortedExdates(a) != sortedExdates(b);

    // Recurrence "shape" = duration, frequency, type. EXDATE handled
    // separately above so it doesn't trip the shape check.
    auto ar = a->recurrence();
    auto br = b->recurrence();
    d.recurrenceShapeDiffers =
        ar->recurrenceType() != br->recurrenceType()
        || ar->frequency() != br->frequency()
        || ar->duration() != br->duration();

    return d;
}

bool onlyAlarmsDiffer(const EventDiff &d)
{
    return d.alarmsDiffer
        && !d.summaryDiffers && !d.dtStartTimeDiffers && !d.dtStartTzDiffers
        && !d.dtEndDiffers && !d.descriptionDiffers && !d.locationDiffers
        && !d.exdatesDiffer && !d.recurrenceShapeDiffers;
}

bool onlyExdatesDiffer(const EventDiff &d)
{
    return d.exdatesDiffer
        && !d.summaryDiffers && !d.dtStartTimeDiffers && !d.dtStartTzDiffers
        && !d.dtEndDiffers && !d.descriptionDiffers && !d.locationDiffers
        && !d.alarmsDiffer && !d.recurrenceShapeDiffers;
}

bool onlyTzDiffers(const EventDiff &d)
{
    return d.dtStartTzDiffers
        && !d.dtStartTimeDiffers && !d.summaryDiffers && !d.dtEndDiffers
        && !d.descriptionDiffers && !d.locationDiffers
        && !d.alarmsDiffer && !d.exdatesDiffer && !d.recurrenceShapeDiffers;
}

KCalendarCore::Event::Ptr mergeAlarms(const KCalendarCore::Event::Ptr &base,
                                      const KCalendarCore::Event::Ptr &other)
{
    auto out = KCalendarCore::Event::Ptr(base->clone());
    out->clearAlarms();
    QList<int> seen;
    auto addUnique = [&](const KCalendarCore::Event::Ptr &src) {
        for (const auto &al : src->alarms()) {
            const int s = al->startOffset().asSeconds();
            if (seen.contains(s)) continue;
            seen.append(s);
            auto dup = out->newAlarm();
            dup->setType(al->type());
            dup->setStartOffset(al->startOffset());
        }
    };
    addUnique(base);
    addUnique(other);
    return out;
}

KCalendarCore::Event::Ptr mergeExdates(const KCalendarCore::Event::Ptr &base,
                                       const KCalendarCore::Event::Ptr &other)
{
    auto out = KCalendarCore::Event::Ptr(base->clone());
    for (const auto &dt : other->recurrence()->exDateTimes()) {
        if (!out->recurrence()->exDateTimes().contains(dt)) {
            out->recurrence()->addExDateTime(dt);
        }
    }
    return out;
}

KCalendarCore::Event::Ptr pickFloatingSide(const KCalendarCore::Event::Ptr &a,
                                           const KCalendarCore::Event::Ptr &b)
{
    const bool aFloating = (a->dtStart().timeSpec() == Qt::LocalTime);
    return aFloating ? KCalendarCore::Event::Ptr(a->clone())
                     : KCalendarCore::Event::Ptr(b->clone());
}

} // namespace

CalendarConflictHandler::CalendarConflictHandler(
    WildPalms::PalmSync::IPalmDatabaseAccess *device,
    const WildPalms::PalmConflict::PalmBackendConfig *config)
    : m_palm(std::make_unique<WildPalms::PalmConflict::PalmConflictHandler>(
          device, config))
{
}

CalendarConflictHandler::~CalendarConflictHandler() = default;

Kalburator::Sync::QSyncCore::ConflictDecision
CalendarConflictHandler::handleConflict(
    Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
    const Kalburator::Sync::QSyncCore::ConflictPolicy &policy)
{
    auto src = decodeFirstEvent(conflict.source.content);
    auto tgt = decodeFirstEvent(conflict.target.content);
    if (!src || !tgt) {
        m_lastOverlay = QStringLiteral("delegated");
        return m_palm->handleConflict(conflict, policy);
    }

    const EventDiff d = diffEvents(src, tgt);

    if (onlyAlarmsDiffer(d)) {
        auto merged = mergeAlarms(src, tgt);
        conflict.mergedContent = serialiseEvent(merged);
        m_lastOverlay = QStringLiteral("alarm");
        return Kalburator::Sync::QSyncCore::ConflictDecision::Merge;
    }
    if (onlyExdatesDiffer(d)) {
        auto merged = mergeExdates(src, tgt);
        conflict.mergedContent = serialiseEvent(merged);
        m_lastOverlay = QStringLiteral("exdate");
        return Kalburator::Sync::QSyncCore::ConflictDecision::Merge;
    }
    if (onlyTzDiffers(d)) {
        auto chosen = pickFloatingSide(src, tgt);
        conflict.mergedContent = serialiseEvent(chosen);
        m_lastOverlay = QStringLiteral("tz");
        return Kalburator::Sync::QSyncCore::ConflictDecision::Merge;
    }

    m_lastOverlay = QStringLiteral("delegated");
    return m_palm->handleConflict(conflict, policy);
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 5.6: Build, run, verify pass**

```bash
cmake --build build-dev --target tst_calendarconflicthandler \
    && ctest --test-dir build-dev -R tst_calendarconflicthandler --output-on-failure
```

Expected: 5 tests PASS.

If "tzOnlyDiffPrefersFloating" fails because `KCalendarCore` defaults `LocalTime` to UTC on serialise/deserialise, consider switching the floating-side check to `dtStart().timeZone().id().isEmpty()` after a fromString round-trip. That's a stable property regardless of in-memory `timeSpec`.

- [ ] **Step 5.7: Commit**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git add calendarconflicthandler.h calendarconflicthandler.cpp
git commit -m "$(cat <<'EOF'
feat(calendar): CalendarConflictHandler (Phase E.10 Task 5)

Composes PalmConflictHandler with three calendar-aware overlays:
alarm-only diff merges alarms; EXDATE-only diff merges EXDATE lists;
DTSTART tz-only diff prefers floating (Palm semantics). Other shapes
delegate to PalmConflictHandler. Decode failures also delegate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

cd /home/clinton/dev/WildPalms
git add tests/plugins/calendar/CMakeLists.txt \
        tests/plugins/calendar/tst_calendarconflicthandler.cpp
git commit -m "test(calendar): tst_calendarconflicthandler (Phase E.10 Task 5)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `CalendarBackendPlugin` + manifest + CMake toggle (in submodule)

Goal: the `IBackendPlugin` shell. Owns the per-session `CategoryMappingStore`, builds it from AppInfo at `createBackends`, returns both `blob` and `calendar` backends, returns the conflict handler, exposes `CalendarView` as the main view. Wraps everything behind the `WILDPALMS_CALENDAR_PLUGIN_V2` CMake toggle.

**Files:**
- Create (in submodule): `calendarbackendplugin.h`, `calendarbackendplugin.cpp`, `calendar-backend-plugin.json`
- Modify (in submodule): `CMakeLists.txt`
- Create (in parent): `tests/plugins/calendar/tst_calendarbackendplugin.cpp`
- Modify (in parent): `tests/plugins/calendar/CMakeLists.txt`

- [ ] **Step 6.1: Create the JSON manifest**

Create `src/plugins/calendar/calendar-backend-plugin.json`:

```json
{
    "KPlugin": {
        "Id": "calendar",
        "Name": "Calendar Sync",
        "Description": "Syncs Palm DatebookDB to iCalendar files via virtual category sub-calendars.",
        "Icon": "view-calendar",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["DatebookDB"],
    "X-WildPalms-ClaimDescriptions": {
        "DatebookDB": "Syncs DatebookDB to iCalendar files; one virtual sub-calendar per Palm category."
    },
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 20
}
```

- [ ] **Step 6.2: Create the plugin header**

Create `src/plugins/calendar/calendarbackendplugin.h`:

```cpp
#ifndef WILDPALMS_CALENDAR_CALENDARBACKENDPLUGIN_H
#define WILDPALMS_CALENDAR_CALENDARBACKENDPLUGIN_H

#include <memory>

#include <QObject>

#include "core/ibackendplugin.h"

namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
namespace WildPalms::PalmConflict { struct PalmBackendConfig; }
class PalmDeviceConnection;

namespace WildPalms::CalendarPlugin {

/**
 * @brief Second new-ABI WildPalms plugin (after Memo, E.9).
 *
 * Provides:
 *   - CalendarBlobBackend wrapping the shared PalmBackend (one
 *     collection per populated category slot).
 *   - PalmCalendarBackend (typed SyncBackend, returned for future
 *     PlanStan routing + the unified-runtime calendar tab).
 *   - CalendarConflictHandler (calendar-aware overlays + Palm
 *     delegation).
 *
 * Owns the per-session CategoryMappingStore, populated from the
 * Datebook AppInfo block at createBackends() time.
 *
 * Surfaces CalendarView as a main-window tab (reused unchanged from
 * the legacy CalendarConduit).
 */
class CalendarBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit CalendarBackendPlugin(QObject *parent = nullptr);
    ~CalendarBackendPlugin() override;

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
    PalmDeviceConnection *m_device = nullptr;   // borrowed; cached for createConflictHandler
};

} // namespace WildPalms::CalendarPlugin

#endif // WILDPALMS_CALENDAR_CALENDARBACKENDPLUGIN_H
```

- [ ] **Step 6.3: Create the plugin tests**

Create `tests/plugins/calendar/tst_calendarbackendplugin.cpp`:

```cpp
#include <QtTest/QtTest>

#include "plugins/calendar/calendarbackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

using WildPalms::CalendarPlugin::CalendarBackendPlugin;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestCalendarBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void identityFields();
    void claimsDatebookDB();
    void hasMainView();
    void createBackendsReturnsBothSlots();
    void createConflictHandlerNonNullAfterCreateBackends();
};

void TestCalendarBackendPlugin::identityFields()
{
    CalendarBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("calendar"));
    QVERIFY(!p.displayName().isEmpty());
    QVERIFY(!p.description().isEmpty());
    QCOMPARE(p.version(), QStringLiteral("2.0"));
}

void TestCalendarBackendPlugin::claimsDatebookDB()
{
    CalendarBackendPlugin p;
    QCOMPARE(p.claimedDatabases(), QStringList{QStringLiteral("DatebookDB")});
}

void TestCalendarBackendPlugin::hasMainView()
{
    CalendarBackendPlugin p;
    QVERIFY(p.hasMainView());
    QVERIFY(!p.mainViewName().isEmpty());
}

void TestCalendarBackendPlugin::createBackendsReturnsBothSlots()
{
    CalendarBackendPlugin p;
    MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);

    auto provided = p.createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);
    QVERIFY(provided.calendar != nullptr);
    QCOMPARE(provided.blob->backendId(), QStringLiteral("calendar"));
    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarBackendPlugin::createConflictHandlerNonNullAfterCreateBackends()
{
    CalendarBackendPlugin p;
    MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);
    auto provided = p.createBackends(nullptr, &conn);
    delete provided.blob;
    delete provided.calendar;

    auto *h = p.createConflictHandler();
    QVERIFY(h != nullptr);
    delete h;
}

QTEST_MAIN(TestCalendarBackendPlugin)
#include "tst_calendarbackendplugin.moc"
```

- [ ] **Step 6.4: Add the test target**

Append to `tests/plugins/calendar/CMakeLists.txt`:

```cmake
# --- Task 6: CalendarBackendPlugin (in-process, no .so loading) ---
add_executable(tst_calendarbackendplugin
    tst_calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarconflicthandler.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarview.cpp
)
target_include_directories(tst_calendarbackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_calendarbackendplugin
    PRIVATE
        WildPalmsPalmCalendar
        WildPalmsPalmConflict
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
add_test(NAME tst_calendarbackendplugin COMMAND tst_calendarbackendplugin)
```

- [ ] **Step 6.5: Implement the plugin**

Create `src/plugins/calendar/calendarbackendplugin.cpp`:

```cpp
#include "calendarbackendplugin.h"

#include "calendarblobbackend.h"
#include "calendarconflicthandler.h"
#include "calendarview.h"
#include "categoryappinforeader.h"
#include "icstranscoder.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/calendar/palmcalendarbackend.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/palmbackend.h"

#include "conflictrecord.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QIcon>
#include <QString>
#include <QWidget>

namespace WildPalms::CalendarPlugin {

CalendarBackendPlugin::CalendarBackendPlugin(QObject *parent)
    : QObject(parent)
    , m_categoryStore(std::make_unique<WildPalms::PalmCalendar::CategoryMappingStore>())
    , m_palmConfig(std::make_unique<WildPalms::PalmConflict::PalmBackendConfig>())
{
}

CalendarBackendPlugin::~CalendarBackendPlugin() = default;

QString CalendarBackendPlugin::pluginId()    const { return QStringLiteral("calendar"); }
QString CalendarBackendPlugin::displayName() const { return QStringLiteral("Calendar"); }
QIcon   CalendarBackendPlugin::icon()        const
{
    return QIcon::fromTheme(QStringLiteral("view-calendar"));
}
QString CalendarBackendPlugin::description() const
{
    return QStringLiteral(
        "Synchronizes Palm DatebookDB with iCalendar files via virtual category sub-calendars");
}
QString CalendarBackendPlugin::version()     const { return QStringLiteral("2.0"); }

QStringList CalendarBackendPlugin::claimedDatabases() const
{
    return { QStringLiteral("DatebookDB") };
}

WildPalms::IBackendPlugin::ProvidedBackends
CalendarBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *host,
                                      PalmDeviceConnection         *device)
{
    Q_UNUSED(host)
    ProvidedBackends out;
    if (!device) return out;

    m_device = device;

    auto *palmBackend = device->palmBackend();
    if (palmBackend) {
        // Populate the category store from AppInfo. Failure is non-fatal:
        // the backend still surfaces palm:calendar/0 ("Unfiled").
        populateFromAppInfo(*m_categoryStore,
                            QStringLiteral("DatebookDB"),
                            palmBackend->readAppBlock(QStringLiteral("DatebookDB")));
        out.blob = new CalendarBlobBackend(palmBackend, m_categoryStore.get());
    }

    if (device->device()) {
        out.calendar = new WildPalms::PalmCalendar::PalmCalendarBackend(
            device->device(), m_categoryStore.get());
    }
    return out;
}

Kalburator::Sync::QSyncCore::ConflictHandler *
CalendarBackendPlugin::createConflictHandler()
{
    if (!m_device || !m_device->device()) return nullptr;
    return new CalendarConflictHandler(m_device->device(), m_palmConfig.get());
}

bool CalendarBackendPlugin::hasMainView() const { return true; }

QWidget *CalendarBackendPlugin::createMainView(QWidget *parent) const
{
    return new CalendarView(parent);
}

QString CalendarBackendPlugin::mainViewName() const { return QStringLiteral("Calendar"); }

QIcon CalendarBackendPlugin::mainViewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("view-calendar"));
}

void CalendarBackendPlugin::enrichConflictSnapshot(
    Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
    bool /*isSourceSide*/) const
{
    if (snapshot.content.isEmpty()) return;

    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromString(cal, QString::fromUtf8(snapshot.content))) return;
    auto events = cal->events();
    if (events.isEmpty()) return;
    auto event = events.first();
    if (!event) return;

    snapshot.metadata[QStringLiteral("title")] = event->summary();
    snapshot.metadata[QStringLiteral("dtStart")] = event->dtStart().toString(Qt::ISODate);
    snapshot.contentType = QStringLiteral("text/calendar");
}

QString CalendarBackendPlugin::formatConflictRecordHtml(
    const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const
{
    QString html;
    const QString title   = snapshot.metadata.value(QStringLiteral("title")).toString();
    const QString dtStart = snapshot.metadata.value(QStringLiteral("dtStart")).toString();
    if (!title.isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(title.toHtmlEscaped());
    }
    if (!dtStart.isEmpty()) {
        html += QStringLiteral("<p><b>Starts:</b> %1</p>").arg(dtStart.toHtmlEscaped());
    }
    html += QStringLiteral("<pre>%1</pre>")
        .arg(QString::fromUtf8(snapshot.content).toHtmlEscaped());
    return html;
}

} // namespace WildPalms::CalendarPlugin

#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(CalendarBackendPluginFactory,
                           "calendar-backend-plugin.json",
                           registerPlugin<WildPalms::CalendarPlugin::CalendarBackendPlugin>();)

#include "calendarbackendplugin.moc"
```

- [ ] **Step 6.6: Update the submodule's `CMakeLists.txt`**

Replace `src/plugins/calendar/CMakeLists.txt` with:

```cmake
option(WILDPALMS_CALENDAR_PLUGIN_V2 "Build the new IBackendPlugin-based Calendar plugin" ON)

if (WILDPALMS_CALENDAR_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_calendar_v2
        SOURCES
            calendarbackendplugin.cpp   calendarbackendplugin.h
            calendarblobbackend.cpp     calendarblobbackend.h
            calendarconflicthandler.cpp calendarconflicthandler.h
            categoryappinforeader.cpp   categoryappinforeader.h
            icstranscoder.cpp           icstranscoder.h
            calendarview.cpp            calendarview.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_calendar_v2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(wildpalms_calendar_v2
        PRIVATE
            WildPalmsCore
            WildPalmsPalmSync
            WildPalmsPalmCalendar
            WildPalmsPalmCodecs
            WildPalmsPalmConflict
            KF6::CoreAddons
            KF6::CalendarCore
            KF6::I18n
            KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_calendar
        SOURCES
            calendarconduit.cpp
            calendarconduit.h
            calendarmapper.cpp
            calendarmapper.h
            calendarview.cpp
            calendarview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_calendar
        WildPalmsCore
        KF6::CoreAddons
        KF6::CalendarCore
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
    )
endif ()
```

- [ ] **Step 6.7: Build and run**

```bash
cmake --build build-dev --target wildpalms_calendar_v2 tst_calendarbackendplugin \
    && ctest --test-dir build-dev -R tst_calendarbackendplugin --output-on-failure
```

Expected: plugin `.so` builds; 5 plugin tests PASS.

- [ ] **Step 6.8: Verify the legacy toggle still works**

```bash
cmake -S /home/clinton/dev/WildPalms -B build-dev-legacy \
    -DWILDPALMS_CALENDAR_PLUGIN_V2=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev-legacy --target wildpalms_calendar
ls build-dev-legacy/lib/wildpalms/conduits/  # confirm libwildpalms_calendar.so is there
```

Expected: legacy `wildpalms_calendar` `.so` builds. (You can delete `build-dev-legacy/` after verification — it exists only to confirm the OFF path.)

- [ ] **Step 6.9: Commit**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git add calendarbackendplugin.h calendarbackendplugin.cpp \
        calendar-backend-plugin.json CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(calendar): CalendarBackendPlugin + V2 toggle (Phase E.10 Task 6)

Glue: returns both ProvidedBackends slots (CalendarBlobBackend +
PalmCalendarBackend), reuses CalendarView untouched, surfaces a
CalendarConflictHandler. CMake toggle WILDPALMS_CALENDAR_PLUGIN_V2=ON
swaps the new plugin in at wildpalms/plugins/; legacy CalendarConduit
remains buildable when off.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

cd /home/clinton/dev/WildPalms
git add tests/plugins/calendar/CMakeLists.txt \
        tests/plugins/calendar/tst_calendarbackendplugin.cpp
git commit -m "test(calendar): tst_calendarbackendplugin (Phase E.10 Task 6)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: End-to-end test `tst_calendar_v2` (in parent)

Goal: load the real `wildpalms_calendar_v2.so` via `BackendPluginManager`, wire its `CalendarBlobBackend` into `BlobSyncEngine::twoWayWithBaseline` against a `MockBlobBackend` target, prove four DateBk6-style scenarios across multiple categories.

**Files:**
- Create (in parent): `tests/plugins/calendar/tst_calendar_v2.cpp`
- Modify (in parent): `tests/plugins/calendar/CMakeLists.txt`

- [ ] **Step 7.1: Write the test**

Create `tests/plugins/calendar/tst_calendar_v2.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QCryptographicHash>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <pi-appinfo.h>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/calendar/datebookcodec.h"
#include "plugins/calendar/icstranscoder.h"
#include "plugins/calendar/calendarblobbackend.h"
#include "runtime/backendpluginmanager.h"

#include "blobsyncengine.h"
#include "blobbaselinestore.h"
#include "mockblobbackend.h"
#include "conflicthandlerregistry.h"
#include "conflictstore.h"
#include "conflictpolicy.h"

using Kalburator::Sync::CollectionInfo;
using WildPalms::CalendarPlugin::CalendarBlobBackend;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

namespace {

// Synthesise a Datebook AppInfo block naming three slots (1..3).
QByteArray buildAppInfo()
{
    AppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Work"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Travel") };
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray utf = names[i].toUtf8().left(15);
        std::memcpy(info.category.name[i], utf.constData(), utf.size());
        info.category.name[i][utf.size()] = '\0';
        info.category.ID[i] = static_cast<unsigned char>(i);
    }
    info.category.lastUniqueID = 15;
    QByteArray buf(4096, '\0');
    const int written = pack_AppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    buf.resize(written);
    return buf;
}

PalmRecord makeEvent(const QString &uid, int slot, int hourOfDay)
{
    KCalendarCore::Event::Ptr e(new KCalendarCore::Event);
    e->setUid(uid);
    e->setSummary(QStringLiteral("Event ") + uid);
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(hourOfDay, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(hourOfDay + 1, 0)));
    auto pr = DatebookCodec::encode(e, slot);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

} // namespace

class TestCalendarV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSyncCreatesPerCategoryRecords();
    void modifyTargetPropagatesToPalm();
    void deletePalmRemovesTargetRecord();
    void idempotentNoopSyncChangesNothing();

private:
    void seedPalm(MockPalmDatabaseAccess *dev) const;
    static CollectionInfo targetCollection(int slot);
};

void TestCalendarV2::initTestCase()
{
    QCoreApplication::addLibraryPath(
        QStringLiteral(CMAKE_BINARY_DIR "/lib"));
}

void TestCalendarV2::seedPalm(MockPalmDatabaseAccess *dev) const
{
    dev->createDatabase(QStringLiteral("DatebookDB"));
    dev->setAppBlock(QStringLiteral("DatebookDB"), buildAppInfo());

    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e1", 0, 9));   // Unfiled
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e2", 1, 10));  // Work
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e3", 1, 11));  // Work
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e4", 2, 12));  // Personal
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e5", 2, 13));  // Personal
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e6", 3, 14));  // Travel
}

CollectionInfo TestCalendarV2::targetCollection(int slot)
{
    CollectionInfo info;
    info.id   = CalendarBlobBackend::collectionIdForSlot(slot);
    info.name = QStringLiteral("target-%1").arg(slot);
    info.type = QStringLiteral("calendar");
    return info;
}

void TestCalendarV2::freshSyncCreatesPerCategoryRecords()
{
    // Build the plugin via BackendPluginManager.
    WildPalms::BackendPluginManager mgr;
    mgr.discoverPlugins(QStringLiteral("wildpalms/plugins"));
    auto *plugin = mgr.pluginForId(QStringLiteral("calendar"));
    QVERIFY2(plugin, "calendar plugin failed to load — check build dir lib path");

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);

    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob);

    // Target side: one MockBlobBackend per category collection.
    Kalburator::Sync::MockBlobBackend target;
    for (int slot : {0, 1, 2, 3}) {
        target.addCollection(targetCollection(slot));
    }

    Kalburator::Sync::BlobBaselineStore baseline;
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    for (int slot : {0, 1, 2, 3}) {
        auto result = engine.twoWayWithBaseline(
            provided.blob, &target,
            CalendarBlobBackend::collectionIdForSlot(slot),
            targetCollection(slot).id,
            &baseline, &registry, policy);
        QVERIFY(result.errors.isEmpty());
    }

    // Assert per-collection counts on the target.
    QCOMPARE(target.recordsIn(targetCollection(0).id).size(), 1);  // e1
    QCOMPARE(target.recordsIn(targetCollection(1).id).size(), 2);  // e2,e3
    QCOMPARE(target.recordsIn(targetCollection(2).id).size(), 2);  // e4,e5
    QCOMPARE(target.recordsIn(targetCollection(3).id).size(), 1);  // e6

    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarV2::modifyTargetPropagatesToPalm()
{
    WildPalms::BackendPluginManager mgr;
    mgr.discoverPlugins(QStringLiteral("wildpalms/plugins"));
    auto *plugin = mgr.pluginForId(QStringLiteral("calendar"));
    QVERIFY(plugin);

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);
    auto provided = plugin->createBackends(nullptr, &conn);

    Kalburator::Sync::MockBlobBackend target;
    target.addCollection(targetCollection(1));
    Kalburator::Sync::BlobBaselineStore baseline;
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    // First sync.
    engine.twoWayWithBaseline(provided.blob, &target,
        CalendarBlobBackend::collectionIdForSlot(1),
        targetCollection(1).id, &baseline, &registry, policy);

    // Mutate target side: rename e2's summary by re-encoding.
    auto recs = target.recordsIn(targetCollection(1).id);
    QVERIFY(!recs.isEmpty());
    auto br = recs.first();
    br.data.replace("SUMMARY:Event e2", "SUMMARY:Event e2 (target-edit)");
    QVERIFY(target.updateRecord(br));

    // Re-sync; expect Palm side to pick up the change.
    engine.twoWayWithBaseline(provided.blob, &target,
        CalendarBlobBackend::collectionIdForSlot(1),
        targetCollection(1).id, &baseline, &registry, policy);

    auto palmRecs = dev.readAllRecords(QStringLiteral("DatebookDB"));
    bool found = false;
    for (const auto &pr : palmRecs) {
        if (pr.category != 1) continue;
        auto decoded = DatebookCodec::decode(pr);
        if (!decoded.isValid()) continue;
        if (decoded.event->summary().contains(QStringLiteral("target-edit"))) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "target-side edit did not propagate to Palm");

    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarV2::deletePalmRemovesTargetRecord()
{
    WildPalms::BackendPluginManager mgr;
    mgr.discoverPlugins(QStringLiteral("wildpalms/plugins"));
    auto *plugin = mgr.pluginForId(QStringLiteral("calendar"));
    QVERIFY(plugin);

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);
    auto provided = plugin->createBackends(nullptr, &conn);

    Kalburator::Sync::MockBlobBackend target;
    target.addCollection(targetCollection(2));
    Kalburator::Sync::BlobBaselineStore baseline;
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    engine.twoWayWithBaseline(provided.blob, &target,
        CalendarBlobBackend::collectionIdForSlot(2),
        targetCollection(2).id, &baseline, &registry, policy);

    QCOMPARE(target.recordsIn(targetCollection(2).id).size(), 2);

    // Delete one record on Palm side.
    auto palmRecs = dev.readAllRecords(QStringLiteral("DatebookDB"));
    for (const auto &pr : palmRecs) {
        if (pr.category == 2) {
            QVERIFY(dev.deleteRecord(QStringLiteral("DatebookDB"), pr.recordId));
            break;
        }
    }

    engine.twoWayWithBaseline(provided.blob, &target,
        CalendarBlobBackend::collectionIdForSlot(2),
        targetCollection(2).id, &baseline, &registry, policy);

    QCOMPARE(target.recordsIn(targetCollection(2).id).size(), 1);

    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarV2::idempotentNoopSyncChangesNothing()
{
    WildPalms::BackendPluginManager mgr;
    mgr.discoverPlugins(QStringLiteral("wildpalms/plugins"));
    auto *plugin = mgr.pluginForId(QStringLiteral("calendar"));
    QVERIFY(plugin);

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);
    auto provided = plugin->createBackends(nullptr, &conn);

    Kalburator::Sync::MockBlobBackend target;
    target.addCollection(targetCollection(1));
    Kalburator::Sync::BlobBaselineStore baseline;
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    // First sync.
    engine.twoWayWithBaseline(provided.blob, &target,
        CalendarBlobBackend::collectionIdForSlot(1),
        targetCollection(1).id, &baseline, &registry, policy);
    auto recsAfterFirst = target.recordsIn(targetCollection(1).id);

    // Capture content hashes for the target side after the first sync.
    QStringList hashesAfterFirst;
    for (const auto &r : recsAfterFirst) hashesAfterFirst << sha256Hex(r.data);
    std::sort(hashesAfterFirst.begin(), hashesAfterFirst.end());

    // Second sync should be a noop.
    engine.twoWayWithBaseline(provided.blob, &target,
        CalendarBlobBackend::collectionIdForSlot(1),
        targetCollection(1).id, &baseline, &registry, policy);
    auto recsAfterSecond = target.recordsIn(targetCollection(1).id);
    QStringList hashesAfterSecond;
    for (const auto &r : recsAfterSecond) hashesAfterSecond << sha256Hex(r.data);
    std::sort(hashesAfterSecond.begin(), hashesAfterSecond.end());

    QCOMPARE(recsAfterSecond.size(), recsAfterFirst.size());
    QCOMPARE(hashesAfterSecond, hashesAfterFirst);

    delete provided.blob;
    delete provided.calendar;
}

QTEST_MAIN(TestCalendarV2)
#include "tst_calendar_v2.moc"
```

- [ ] **Step 7.2: Add the test target**

Append to `tests/plugins/calendar/CMakeLists.txt`:

```cmake
# --- Task 7: tst_calendar_v2 — end-to-end via BackendPluginManager ---
add_executable(tst_calendar_v2 tst_calendar_v2.cpp)
target_compile_definitions(tst_calendar_v2
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_include_directories(tst_calendar_v2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_calendar_v2
    PRIVATE
        WildPalmsRuntime
        WildPalmsPalmCalendar
        WildPalmsPalmSync
        WildPalmsCore
        Kalburator::Sync
        KF6::CoreAddons
        KF6::CalendarCore
        Qt::Test
        Qt::Core
        ${PILOT_LINK_LIBRARIES}
)
# Loads the plugin .so at runtime; ensure the plugin builds first.
add_dependencies(tst_calendar_v2 wildpalms_calendar_v2)
add_test(NAME tst_calendar_v2 COMMAND tst_calendar_v2)
```

(If the test references `BackendPluginManager::pluginForId(...)` and the actual API has a different name, run `grep -n "class BackendPluginManager" /home/clinton/dev/WildPalms/src/runtime/backendpluginmanager.h` before this step and substitute the real method name. Likewise for `MockBlobBackend::recordsIn` / `addCollection` — these match memo's usage; check `tests/plugins/memo/tst_memo_v2.cpp` if names differ.)

- [ ] **Step 7.3: Build and run**

```bash
cd /home/clinton/dev/WildPalms
cmake --build build-dev --target tst_calendar_v2 wildpalms_calendar_v2
ctest --test-dir build-dev -R tst_calendar_v2 --output-on-failure
```

Expected: 4 scenarios PASS.

- [ ] **Step 7.4: Run the full ctest suite to confirm no regressions**

```bash
ctest --test-dir build-dev --output-on-failure
```

Expected: every previously-passing test still passes; new calendar tests appear in the output.

- [ ] **Step 7.5: Commit**

```bash
git add tests/plugins/calendar/CMakeLists.txt \
        tests/plugins/calendar/tst_calendar_v2.cpp
git commit -m "$(cat <<'EOF'
test(calendar): tst_calendar_v2 end-to-end (Phase E.10 Task 7)

Loads wildpalms_calendar_v2.so via BackendPluginManager, drives
BlobSyncEngine::twoWayWithBaseline against a MockBlobBackend target
across four DateBk6-style category slots. Covers fresh sync,
target-side modify propagation, Palm-side delete propagation, and
idempotent noop. Per Decision #5, MockBlobBackend stands in for
LocalBlobBackend (cross-id-space mapping deferred to E.15+).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Submodule pointer bump + docs/memory updates

Goal: bump the calendar submodule pointer in the parent, mark E.10 landed in the master spec and integration plan, write a memory note.

**Files:**
- Modify: `src/plugins/calendar` (submodule pointer)
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`
- Create: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e10_calendar.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`

- [ ] **Step 8.1: Bump the submodule pointer**

```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/calendar
git status   # confirm only src/plugins/calendar pointer + the docs/memory edits below are pending
```

(The actual pointer bump happens automatically on `git add` of the submodule path — `git status` will show "modified content" → "new commits".)

- [ ] **Step 8.2: Flip row E.10 in the master spec**

Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. Find the line starting with `| **E.10** | Rewrite **Calendar**` (around line 588). Replace it with:

```markdown
| ✅ **E.10** | Calendar rewritten as `IBackendPlugin` (`CalendarBackendPlugin` + `CalendarBlobBackend` + `CalendarConflictHandler` + `CategoryAppInfoReader` + `IcsTranscoder`). First real consumer of `PalmBackend` and `PalmCalendarBackend`. AppInfo-block parsing landed (`CategoryAppInfoReader`); `IPalmDatabaseAccess::readAppBlock` + `PalmBackend::readAppBlock` + `PilotLinkPalmDatabaseAccess::readAppBlock` plumbing. Virtual sub-collections `palm:calendar/<N>` (one per populated category slot, slot 0 always present). `CalendarConflictHandler` adds three calendar-aware overlays (alarm-only, EXDATE-only, DTSTART-tz-only) before delegating to `PalmConflictHandler`. CalendarView reused untouched. CMake toggle `WILDPALMS_CALENDAR_PLUGIN_V2=ON`; legacy `CalendarConduit` remains buildable. `tst_calendar_v2` runs end-to-end via `BackendPluginManager` against `MockBlobBackend` (per `tst_memo_v2`'s id-space deferral). Landed 2026-04-24. Plan: `docs/superpowers/plans/2026-04-24-phase-e10-calendar-plugin.md`. | WP | E.9 | WP ctest passes; ~30 calendar tests cover reader/transcoder/blob-backend/conflict-handler/plugin metadata + 4 e2e scenarios. |
```

- [ ] **Step 8.3: Update the integration plan**

Edit `docs/plans/2026-04-20-libkalburator-integration.md`. Find the row for E.10 in the Phase E sub-phases table; flip it to landed, mirroring the format of E.9.

```bash
grep -n "E\.10\|E\.9" /home/clinton/dev/WildPalms/docs/plans/2026-04-20-libkalburator-integration.md | head -10
```

(Substitute the precise edit based on the file's actual table format.)

- [ ] **Step 8.4: Write the memory note**

Create `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e10_calendar.md`:

```markdown
---
name: Phase E.10 Calendar plugin landed
description: Calendar is the second new-ABI plugin; AppInfo reader + calendar-aware conflict overlays landed; deferrals tracked
type: project
---
Phase E.10 landed 2026-04-24. Calendar is the second plugin on the
new IBackendPlugin ABI (after Memo E.9).

**Why:** First real consumer of both PalmBackend and
PalmCalendarBackend. Establishes the "plugin returns both
ProvidedBackends slots (blob + calendar)" pattern future plugins
follow when they have typed-record consumers.

**How to apply (for E.11/E.12):** Mirror the Calendar layout —
plugin owns CategoryMappingStore (populated from AppInfo at
createBackends), returns blob backend wrapping PalmBackend, returns
typed backend if applicable, registers a domain-specific
ConflictHandler that delegates to PalmConflictHandler for the shared
overlays.

**Status of artifacts:**
- Spec: `docs/superpowers/specs/2026-04-24-phase-e10-calendar-plugin-design.md`
- Plan: `docs/superpowers/plans/2026-04-24-phase-e10-calendar-plugin.md`
- Toggle: `WILDPALMS_CALENDAR_PLUGIN_V2` (default ON). Legacy
  `CalendarConduit` still builds when OFF; both paths exercised in CI.
- Submodule: `src/plugins/calendar/` is a git submodule
  (`wildpalms-conduit-calendar`). Task commits split between
  submodule (source) and parent (test + pointer bump).

**E.10 landed surface additions:**
- `IPalmDatabaseAccess::readAppBlock(const QString&) const` — pure
  virtual; mock has `setAppBlock(dbName, bytes)` test setter; pilot-link
  forwards to `KPilotLink::readAppBlock`.
- `PalmBackend::readAppBlock(const QString&) const` — pass-through.
- `CategoryAppInfoReader` (in submodule) parses Datebook AppInfo via
  pisock's `unpack_AppInfo`; populates `CategoryMappingStore`.
- `CalendarConflictHandler` overlays: alarm-only diff merges alarms;
  EXDATE-only merges; DTSTART-tz-only prefers floating.

**E.10 deferred items (still open):**
- `IDMappingStore` for cross-id-space `LocalBlobBackend` smoke (E.15+).
- Live-device `PilotLinkPalmDatabaseAccess::readAppBlock` integration
  test (E.18, POSE64).
- `ConflictDialog` new-plugin lookup-path regression (open from E.9).
- `CategoryMappingStore` rename/move to `src/palm/` (E.11/E.12).
- Main-window typed-calendar-tab consumption of PalmCalendarBackend
  (E.16/E.17 unified runtime).
- Registry-side `lookupHandler("palm")` API addition — plugin
  instantiates its own PalmConflictHandler instead.
```

- [ ] **Step 8.5: Add to the memory index**

Edit `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`. Append:

```markdown
- [project_phase_e10_calendar.md](project_phase_e10_calendar.md) — E.10 landed 2026-04-24; Calendar is second new-ABI plugin; AppInfo reader landed; CalendarConflictHandler with three overlays
```

- [ ] **Step 8.6: Verify no other tests regressed before committing**

```bash
cd /home/clinton/dev/WildPalms
ctest --test-dir build-dev --output-on-failure
```

Expected: every test PASSES, including the new calendar tests and all pre-existing ones.

- [ ] **Step 8.7: Commit the parent-side documentation + submodule pointer**

```bash
git add src/plugins/calendar \
        docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md \
        docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "$(cat <<'EOF'
docs(phase-e10): land Calendar plugin; bump submodule pointer

E.10 row flipped to landed in the master Phase-E spec. Calendar
plugin submodule pointer advanced to include Tasks 2-6 commits
(CategoryAppInfoReader, IcsTranscoder, CalendarBlobBackend,
CalendarConflictHandler, CalendarBackendPlugin + V2 toggle).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

The memory note is committed to the user's memory store, not the repo, so no separate commit is needed for it.

---

## Self-review checklist

Verify before declaring complete:

- [ ] All 5 spec decisions covered:
  - Decision #1 (per-slot collections) → Task 4 (`availableCollections` returns one per populated slot).
  - Decision #2 (AppInfo parsing now) → Tasks 1 + 2 (surface addition + reader).
  - Decision #3 (CalendarConflictHandler with three overlays) → Task 5.
  - Decision #4 (reuse CalendarView) → Task 6 (Step 6.5 returns `new CalendarView(parent)`).
  - Decision #5 (MockBlobBackend) → Task 7 (test uses `MockBlobBackend`).

- [ ] All file additions in the spec's "in scope" list have a corresponding task.
- [ ] All "out of scope" items are flagged in the plan header (Scope explicitly excluded).
- [ ] Each test target has its own dedicated `add_executable` + `add_test`.
- [ ] Submodule commits and parent commits are separated within each task.
- [ ] Final ctest run included in Task 8 to catch regressions before declaring done.

If anything fails to compile or tests fail in unexpected ways, the most likely culprits are:

1. **API name drift** — `MockBlobBackend::addCollection`/`recordsIn` and `BackendPluginManager::pluginForId` are guesses based on memo's usage; verify against `tests/plugins/memo/tst_memo_v2.cpp` and the actual `BackendPluginManager` header before deep debugging.
2. **`PalmBackend::encodeRecordId` format** — Task 4's `idForPalmRecord` assumes `encodeRecordId("DatebookDB", N)` returns the same string `loadRecord` will accept. If the format differs (e.g. `palm:datebook:N` vs `palm:DatebookDB:N`), align both encode and decode helpers.
3. **`KCalendarCore::ICalFormat` round-trip lossiness** — see Risk R1 in the spec. If alarm/EXDATE/tz fields don't survive round-trip cleanly, you may need to stash the original Palm record bytes in an `X-WildPalms-PalmRecord` iCal property; address as a follow-up rather than expanding scope.
4. **pisock function signatures** — `unpack_AppInfo`/`pack_AppInfo` signatures vary by version. If the mock data construction in the tests fails, check `pi-appinfo.h` directly.
