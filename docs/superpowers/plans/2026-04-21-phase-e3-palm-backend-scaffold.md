# Phase E.3 — PalmBackend scaffold (WP) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land `PalmBackend : Kalburator::Sync::IBlobBackend` in WP with a
mock-device abstraction so `BlobSyncEngine::twoWayWithBaseline` can
round-trip synthetic Palm records against a `MockBlobBackend`. First
WP-side sub-phase of Phase E; no real pilot-link involvement yet (E.4
wires DLP).

**Architecture:** `PalmBackend` delegates all device I/O to an abstract
`IPalmDatabaseAccess` interface. `MockPalmDatabaseAccess` is an
in-memory implementation used by E.3 tests. E.4 will add a
pilot-link-backed implementation. A new value type `PalmRecord`
(category + attributes + data + lastModified) carries Palm-specific
metadata across the interface without leaking into
`Kalburator::Sync::BackendRecord` (honours
`memory/feedback_library_vs_backend_responsibility.md`).

A new WP static library `WildPalmsPalmSync` houses all three files
plus `palmrecord.h`. The library PUBLIC-links `Kalburator::Sync` and
`Qt::Core`; does **not** link pisock, so E.3 stays free of the
pilot-link dependency graph. Existing `src/palm/pilotrecord.{h,cpp}`
(the pilot-link-wrapper class) is untouched — E.4 will bridge the
two types.

**Tech Stack:** C++20, Qt6 (Core, Test), Qt's `QCryptographicHash` for
SHA-256, Kalburator::Sync (`IBlobBackend`, `BlobSyncEngine`,
`BlobBaselineStore`, `MockBlobBackend`, `ConflictHandlerRegistry`,
`ConflictStore`). Build with CMake 3.19+. Tests use `QTEST_MAIN` +
`QTemporaryDir`.

**Repo:** All work in `~/dev/WildPalms/`. Nothing in libkalburator
changes. No upstream commit gate.

**Spec reference:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
§"WP-side class layout" (the `PalmBackend` row) + the sub-phases table
(E.3 row).

---

## File Structure

| Path | Role | Created / Modified |
|---|---|---|
| `src/palm/sync/palmrecord.h` | Value type for device I/O | Create |
| `src/palm/sync/ipalmdatabaseaccess.h` | Abstract device-access interface | Create |
| `src/palm/sync/mockpalmdatabaseaccess.h` | In-memory mock header | Create |
| `src/palm/sync/mockpalmdatabaseaccess.cpp` | In-memory mock impl | Create |
| `src/palm/sync/palmbackend.h` | `IBlobBackend` subclass header | Create |
| `src/palm/sync/palmbackend.cpp` | `IBlobBackend` impl | Create |
| `src/palm/sync/CMakeLists.txt` | New static lib `WildPalmsPalmSync` | Create |
| `src/CMakeLists.txt` | `add_subdirectory(palm/sync)` | Modify |
| `tests/palmsync/CMakeLists.txt` | Test target wiring | Create |
| `tests/palmsync/tst_mockpalmdatabaseaccess.cpp` | Mock unit tests | Create |
| `tests/palmsync/tst_palmbackend.cpp` | Backend unit tests | Create |
| `tests/palmsync/tst_palmbackend_roundtrip.cpp` | End-to-end engine round-trip | Create |
| `tests/CMakeLists.txt` | `add_subdirectory(palmsync)` + `add_palm_sync_test` helper | Modify |
| `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` | Mark E.3 ✅ in sub-phases table | Modify |

**Why a `src/palm/sync/` subdirectory?** Existing `src/palm/` code
(devicesession, kpilotdevicelink, etc.) is pisock-linked and lives in
`WildPalmsCore`. E.3 introduces Kalburator-linked code that the
Phase-D quarantine says must stay separate. Putting the new files
under `src/palm/sync/` makes the separation visible and keeps each
CMake target's source list clean. Later sub-phases (E.4 onward) will
relocate or rename as the structure firms up; for E.3 scaffold,
`src/palm/sync/` is low-friction.

---

## Task 1: New value type `PalmRecord`

**Files:** Create `~/dev/WildPalms/src/palm/sync/palmrecord.h`.

- [ ] **Step 1: Write the header.**

File `~/dev/WildPalms/src/palm/sync/palmrecord.h`:

```cpp
#ifndef WILDPALMS_SYNC_PALMRECORD_H
#define WILDPALMS_SYNC_PALMRECORD_H

#include <cstdint>

#include <QByteArray>
#include <QDateTime>

namespace WildPalms::PalmSync {

/**
 * @brief Device-side representation of a Palm database record.
 *
 * Carries the fields that survive a Palm DLP round-trip. Used across
 * the IPalmDatabaseAccess interface; converted to/from
 * Kalburator::Sync::BackendRecord inside PalmBackend.
 *
 * This is a scaffold-phase type. It is intentionally distinct from
 * WP's existing ::PilotRecord class (which wraps pilot-link and lives
 * in WildPalmsCore); the two are bridged in Phase E.4 when the real
 * DLP adapter lands.
 */
struct PalmRecord {
    std::uint32_t recordId = 0;   ///< 32-bit Palm unique ID.
    std::uint8_t  category  = 0;  ///< 4-bit category slot (0..15).
    std::uint8_t  attributes = 0; ///< Flag byte: Deleted / Dirty / Secret / Archived.
    QByteArray    data;           ///< Record payload.
    QDateTime     lastModified;   ///< Mock-tracked; real DLP fills from
                                  ///  database modification time.

    // Attribute-flag constants mirror ::PilotRecord::Attribute so
    // callers bridging to pilot-link see the same bit layout.
    static constexpr std::uint8_t AttrDeleted  = 0x80;
    static constexpr std::uint8_t AttrDirty    = 0x40;
    static constexpr std::uint8_t AttrBusy     = 0x20;
    static constexpr std::uint8_t AttrSecret   = 0x10;
    static constexpr std::uint8_t AttrArchived = 0x08;

    bool isDeleted()  const { return attributes & AttrDeleted;  }
    bool isDirty()    const { return attributes & AttrDirty;    }
    bool isSecret()   const { return attributes & AttrSecret;   }
    bool isArchived() const { return attributes & AttrArchived; }

    bool operator==(const PalmRecord &other) const = default;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_PALMRECORD_H
```

- [ ] **Step 2: Do NOT commit yet.** Paired with Tasks 2-3 so the
  scaffold commit is self-contained.

---

## Task 2: Abstract device-access interface `IPalmDatabaseAccess`

**Files:** Create `~/dev/WildPalms/src/palm/sync/ipalmdatabaseaccess.h`.

- [ ] **Step 1: Write the header.**

File `~/dev/WildPalms/src/palm/sync/ipalmdatabaseaccess.h`:

```cpp
#ifndef WILDPALMS_SYNC_IPALMDATABASEACCESS_H
#define WILDPALMS_SYNC_IPALMDATABASEACCESS_H

#include <optional>

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "palmrecord.h"

namespace WildPalms::PalmSync {

/**
 * @brief Synchronous device-facing API used by PalmBackend.
 *
 * Abstracts the Palm DLP operations PalmBackend needs, so the backend
 * is testable without pilot-link and so the real DLP adapter (E.4)
 * and the mock (E.3) share a single contract.
 *
 * Methods are blocking. PalmBackend is expected to run on a worker
 * thread when a real device is in play; the E.3 mock is fast enough
 * to call directly from tests.
 */
class IPalmDatabaseAccess {
public:
    virtual ~IPalmDatabaseAccess() = default;

    /// Databases visible on the device. Each entry is a Palm DB name
    /// ("DatebookDB", "MemoDB", "AddressDB", "ToDoDB", ...).
    virtual QStringList availableDatabases() const = 0;

    /// True if the device currently exposes the named database.
    virtual bool hasDatabase(const QString &dbName) const = 0;

    /// Create an empty database. Returns true on success. No-op if the
    /// database already exists.
    virtual bool createDatabase(const QString &dbName) = 0;

    /// All records from a database. Order is implementation-defined;
    /// PalmBackend does not rely on ordering.
    virtual QList<PalmRecord> readAllRecords(const QString &dbName) const = 0;

    /// A specific record, or nullopt if missing.
    virtual std::optional<PalmRecord> readRecord(
        const QString &dbName, std::uint32_t recordId) const = 0;

    /// Create a record. If `record.recordId == 0`, the implementation
    /// assigns an ID (matches Palm DLP semantics where the device
    /// allocates IDs). Returns the ID actually assigned, or 0 on
    /// failure.
    virtual std::uint32_t createRecord(const QString &dbName,
                                       const PalmRecord &record) = 0;

    /// Update in place. recordId must be non-zero and must exist.
    /// Returns true on success.
    virtual bool updateRecord(const QString &dbName,
                              const PalmRecord &record) = 0;

    /// Delete a record by ID. Returns true on success; false if the
    /// record did not exist.
    virtual bool deleteRecord(const QString &dbName,
                              std::uint32_t recordId) = 0;

    /// Records modified strictly after `since`. Optional capability —
    /// implementations that can't distinguish return the full list
    /// (the engine's baseline store compensates).
    virtual QList<PalmRecord> recordsModifiedSince(
        const QString &dbName, const QDateTime &since) const = 0;

    /// Record IDs deleted strictly after `since`. Optional capability.
    virtual QList<std::uint32_t> recordsDeletedSince(
        const QString &dbName, const QDateTime &since) const = 0;

    /// Whether the impl tracks deletions natively. PalmBackend surfaces
    /// this via IBlobBackend::supportsDeleteTracking().
    virtual bool supportsDeleteTracking() const = 0;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_IPALMDATABASEACCESS_H
```

- [ ] **Step 2: Do NOT commit yet.** Paired with Task 3.

---

## Task 3: In-memory mock + unit tests + WildPalmsPalmSync CMake target

**Files:**
- Create: `~/dev/WildPalms/src/palm/sync/mockpalmdatabaseaccess.h`
- Create: `~/dev/WildPalms/src/palm/sync/mockpalmdatabaseaccess.cpp`
- Create: `~/dev/WildPalms/src/palm/sync/CMakeLists.txt`
- Modify: `~/dev/WildPalms/src/CMakeLists.txt`
- Create: `~/dev/WildPalms/tests/palmsync/CMakeLists.txt`
- Create: `~/dev/WildPalms/tests/palmsync/tst_mockpalmdatabaseaccess.cpp`
- Modify: `~/dev/WildPalms/tests/CMakeLists.txt`

- [ ] **Step 1: Write the mock header.**

File `~/dev/WildPalms/src/palm/sync/mockpalmdatabaseaccess.h`:

```cpp
#ifndef WILDPALMS_SYNC_MOCKPALMDATABASEACCESS_H
#define WILDPALMS_SYNC_MOCKPALMDATABASEACCESS_H

#include <QHash>
#include <QMap>

#include "ipalmdatabaseaccess.h"

namespace WildPalms::PalmSync {

/**
 * @brief In-memory IPalmDatabaseAccess for tests.
 *
 * Stores records in per-database hash maps keyed by recordId. Assigns
 * new record IDs as monotonically increasing 32-bit counters per
 * database, mirroring Palm DLP's assignment semantics closely enough
 * for scaffold-level tests.
 *
 * Tracks deletions by keeping a per-database list of
 * (recordId, deletedAt) pairs so recordsDeletedSince() can answer
 * queries without a full scan. Not thread-safe; PalmBackend serialises
 * access.
 */
class MockPalmDatabaseAccess : public IPalmDatabaseAccess {
public:
    MockPalmDatabaseAccess() = default;

    QStringList availableDatabases() const override;
    bool hasDatabase(const QString &dbName) const override;
    bool createDatabase(const QString &dbName) override;

    QList<PalmRecord> readAllRecords(const QString &dbName) const override;
    std::optional<PalmRecord> readRecord(const QString &dbName,
                                         std::uint32_t recordId) const override;

    std::uint32_t createRecord(const QString &dbName,
                               const PalmRecord &record) override;
    bool updateRecord(const QString &dbName,
                      const PalmRecord &record) override;
    bool deleteRecord(const QString &dbName,
                      std::uint32_t recordId) override;

    QList<PalmRecord> recordsModifiedSince(
        const QString &dbName, const QDateTime &since) const override;
    QList<std::uint32_t> recordsDeletedSince(
        const QString &dbName, const QDateTime &since) const override;
    bool supportsDeleteTracking() const override { return true; }

private:
    struct Database {
        QHash<std::uint32_t, PalmRecord> records;
        QMap<QDateTime, std::uint32_t>   deletionLog; // deletedAt -> recordId
        std::uint32_t                    nextId = 1;
    };

    QHash<QString, Database> m_dbs;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_MOCKPALMDATABASEACCESS_H
```

- [ ] **Step 2: Write the mock implementation.**

File `~/dev/WildPalms/src/palm/sync/mockpalmdatabaseaccess.cpp`:

```cpp
#include "mockpalmdatabaseaccess.h"

namespace WildPalms::PalmSync {

QStringList MockPalmDatabaseAccess::availableDatabases() const
{
    return QStringList(m_dbs.keyBegin(), m_dbs.keyEnd());
}

bool MockPalmDatabaseAccess::hasDatabase(const QString &dbName) const
{
    return m_dbs.contains(dbName);
}

bool MockPalmDatabaseAccess::createDatabase(const QString &dbName)
{
    if (!m_dbs.contains(dbName)) {
        m_dbs.insert(dbName, Database{});
    }
    return true;
}

QList<PalmRecord> MockPalmDatabaseAccess::readAllRecords(
    const QString &dbName) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    return it->records.values();
}

std::optional<PalmRecord> MockPalmDatabaseAccess::readRecord(
    const QString &dbName, std::uint32_t recordId) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return std::nullopt;
    const auto rIt = it->records.constFind(recordId);
    if (rIt == it->records.constEnd()) return std::nullopt;
    return *rIt;
}

std::uint32_t MockPalmDatabaseAccess::createRecord(
    const QString &dbName, const PalmRecord &record)
{
    if (!m_dbs.contains(dbName)) return 0;
    Database &db = m_dbs[dbName];
    PalmRecord stored = record;
    if (stored.recordId == 0) {
        stored.recordId = db.nextId++;
    } else {
        db.nextId = std::max<std::uint32_t>(db.nextId, stored.recordId + 1);
    }
    if (!stored.lastModified.isValid()) {
        stored.lastModified = QDateTime::currentDateTimeUtc();
    }
    db.records.insert(stored.recordId, stored);
    return stored.recordId;
}

bool MockPalmDatabaseAccess::updateRecord(const QString &dbName,
                                          const PalmRecord &record)
{
    if (!m_dbs.contains(dbName)) return false;
    Database &db = m_dbs[dbName];
    if (record.recordId == 0) return false;
    if (!db.records.contains(record.recordId)) return false;
    PalmRecord stored = record;
    if (!stored.lastModified.isValid()) {
        stored.lastModified = QDateTime::currentDateTimeUtc();
    }
    db.records[record.recordId] = stored;
    return true;
}

bool MockPalmDatabaseAccess::deleteRecord(const QString &dbName,
                                          std::uint32_t recordId)
{
    if (!m_dbs.contains(dbName)) return false;
    Database &db = m_dbs[dbName];
    if (db.records.remove(recordId) == 0) return false;
    db.deletionLog.insert(QDateTime::currentDateTimeUtc(), recordId);
    return true;
}

QList<PalmRecord> MockPalmDatabaseAccess::recordsModifiedSince(
    const QString &dbName, const QDateTime &since) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    QList<PalmRecord> out;
    for (const auto &rec : it->records) {
        if (rec.lastModified > since) out.append(rec);
    }
    return out;
}

QList<std::uint32_t> MockPalmDatabaseAccess::recordsDeletedSince(
    const QString &dbName, const QDateTime &since) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    QList<std::uint32_t> out;
    for (auto dIt = it->deletionLog.upperBound(since);
         dIt != it->deletionLog.constEnd(); ++dIt) {
        out.append(dIt.value());
    }
    return out;
}

} // namespace WildPalms::PalmSync
```

- [ ] **Step 3: Write the CMake target.**

File `~/dev/WildPalms/src/palm/sync/CMakeLists.txt`:

```cmake
# WildPalmsPalmSync — PalmBackend + device-access abstraction.
# Phase E.3 of the libkalburator integration. PUBLIC-links
# Kalburator::Sync so PalmBackend can implement IBlobBackend; does
# NOT link pisock (the pilot-link adapter lands in E.4 and lives
# elsewhere).
#
# Kept deliberately separate from WildPalmsCore so the Phase-D
# quarantine boundary is preserved at the library-graph level through
# Phase E — E.3 is the first Kalburator-linked Palm-side code and
# sits on its own.

add_library(WildPalmsPalmSync STATIC
    palmrecord.h
    ipalmdatabaseaccess.h
    mockpalmdatabaseaccess.cpp
    mockpalmdatabaseaccess.h
    palmbackend.cpp
    palmbackend.h
)

target_include_directories(WildPalmsPalmSync
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmSync
    PUBLIC
        Qt::Core
        Kalburator::Sync
)

set_target_properties(WildPalmsPalmSync PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

Note: `palmbackend.cpp` / `palmbackend.h` are referenced here but
created in Task 4. Either (a) stub them now as empty files so this
CMake target builds, or (b) land this CMakeLists.txt together with
Task 4. Task 4's commit landing order handles (b).

For Step 3 right now, create empty placeholder files so the CMake
configure step succeeds in Step 5:

```bash
cd ~/dev/WildPalms
touch src/palm/sync/palmbackend.h src/palm/sync/palmbackend.cpp
```

They'll be filled in by Task 4.

- [ ] **Step 4: Wire into the src tree.**

Edit `~/dev/WildPalms/src/CMakeLists.txt`. Find the block where other
subdirectories / targets are added (typically near
`add_subdirectory(fullsync)` if present, else near the
`add_library(WildPalmsCore ...)` declaration). Add a new line:

```cmake
add_subdirectory(palm/sync)
```

Place it *before* any line that references `WildPalmsPalmSync` as a
link dependency (there are none in E.3; this is a standalone lib for
now).

If `add_subdirectory(fullsync)` does not appear in `src/CMakeLists.txt`,
check the top-level `~/dev/WildPalms/CMakeLists.txt` for where
`add_subdirectory(src)` is called and ensure the new subdir is picked
up. The canonical location for the `add_subdirectory(palm/sync)` call
is after `add_library(WildPalmsCore ...)` is fully defined.

- [ ] **Step 5: Write the mock unit tests.**

File `~/dev/WildPalms/tests/palmsync/tst_mockpalmdatabaseaccess.cpp`:

```cpp
#include <QtTest/QtTest>

#include "mockpalmdatabaseaccess.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestMockPalmDatabaseAccess : public QObject
{
    Q_OBJECT
private slots:
    void createDatabaseMakesItVisible();
    void createRecordAssignsIdWhenZero();
    void createRecordKeepsExplicitId();
    void readRecordReturnsNulloptWhenMissing();
    void updateRecordFailsForMissingId();
    void deleteRecordLogsDeletion();
    void modifiedSinceFiltersByTimestamp();
    void deletedSinceFiltersByTimestamp();
};

void TestMockPalmDatabaseAccess::createDatabaseMakesItVisible()
{
    MockPalmDatabaseAccess dev;
    QVERIFY(!dev.hasDatabase(QStringLiteral("MemoDB")));
    QVERIFY(dev.createDatabase(QStringLiteral("MemoDB")));
    QVERIFY(dev.hasDatabase(QStringLiteral("MemoDB")));
    QCOMPARE(dev.availableDatabases(),
             QStringList() << QStringLiteral("MemoDB"));
}

void TestMockPalmDatabaseAccess::createRecordAssignsIdWhenZero()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.data = QByteArrayLiteral("hello");
    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);
    QVERIFY(id > 0);

    const auto got = dev.readRecord(QStringLiteral("MemoDB"), id);
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("hello"));
    QCOMPARE(got->recordId, id);
    QVERIFY(got->lastModified.isValid());
}

void TestMockPalmDatabaseAccess::createRecordKeepsExplicitId()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.recordId = 42;
    rec.data = QByteArrayLiteral("body");
    QCOMPARE(dev.createRecord(QStringLiteral("MemoDB"), rec), 42u);

    // Next auto-assigned ID must not collide with 42.
    PalmRecord next;
    next.data = QByteArrayLiteral("next");
    QVERIFY(dev.createRecord(QStringLiteral("MemoDB"), next) > 42u);
}

void TestMockPalmDatabaseAccess::readRecordReturnsNulloptWhenMissing()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), 99).has_value());
    QVERIFY(!dev.readRecord(QStringLiteral("NoDB"), 1).has_value());
}

void TestMockPalmDatabaseAccess::updateRecordFailsForMissingId()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.recordId = 7;
    rec.data = QByteArrayLiteral("x");
    QVERIFY(!dev.updateRecord(QStringLiteral("MemoDB"), rec));

    // recordId 0 is a hard error — always fails.
    PalmRecord zero;
    zero.data = QByteArrayLiteral("x");
    QVERIFY(!dev.updateRecord(QStringLiteral("MemoDB"), zero));
}

void TestMockPalmDatabaseAccess::deleteRecordLogsDeletion()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.data = QByteArrayLiteral("x");
    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);

    QDateTime beforeDelete = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), id));
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), id).has_value());

    const auto deleted = dev.recordsDeletedSince(
        QStringLiteral("MemoDB"), beforeDelete);
    QCOMPARE(deleted, QList<std::uint32_t>() << id);
}

void TestMockPalmDatabaseAccess::modifiedSinceFiltersByTimestamp()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord old;
    old.data = QByteArrayLiteral("old");
    old.lastModified = QDateTime::fromString(
        QStringLiteral("2020-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    dev.createRecord(QStringLiteral("MemoDB"), old);

    PalmRecord fresh;
    fresh.data = QByteArrayLiteral("fresh");
    // Auto-assigned lastModified == now.
    dev.createRecord(QStringLiteral("MemoDB"), fresh);

    const auto cutoff = QDateTime::fromString(
        QStringLiteral("2024-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    const auto modified = dev.recordsModifiedSince(
        QStringLiteral("MemoDB"), cutoff);
    QCOMPARE(modified.size(), 1);
    QCOMPARE(modified.first().data, QByteArrayLiteral("fresh"));
}

void TestMockPalmDatabaseAccess::deletedSinceFiltersByTimestamp()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.data = QByteArrayLiteral("gone");
    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);
    dev.deleteRecord(QStringLiteral("MemoDB"), id);

    const auto future = QDateTime::currentDateTimeUtc().addSecs(3600);
    QCOMPARE(dev.recordsDeletedSince(QStringLiteral("MemoDB"), future),
             QList<std::uint32_t>());
}

QTEST_MAIN(TestMockPalmDatabaseAccess)
#include "tst_mockpalmdatabaseaccess.moc"
```

- [ ] **Step 6: Write the tests CMakeLists.**

File `~/dev/WildPalms/tests/palmsync/CMakeLists.txt`:

```cmake
# Phase E.3 — PalmBackend scaffold tests.
# Each test links WildPalmsPalmSync + Kalburator::Sync. Deliberately
# does NOT link WildPalmsCore or pisock.

function(add_palm_sync_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsPalmSync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_sync_test(tst_mockpalmdatabaseaccess tst_mockpalmdatabaseaccess.cpp)
```

- [ ] **Step 7: Hook tests into the existing tests tree.**

Edit `~/dev/WildPalms/tests/CMakeLists.txt`. Append after the existing
`add_fullsync_test(...)` block:

```cmake
# ============================================================
# Phase E.3 — PalmBackend + mock device access
# ============================================================

add_subdirectory(palmsync)
```

- [ ] **Step 8: Configure + build.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
```

Expected: clean build. `tst_mockpalmdatabaseaccess` executable
produced. If `palmbackend.{h,cpp}` empty stubs cause link errors in
`WildPalmsPalmSync`, add minimal placeholder content (`#pragma once`
in the header; an empty file with a single anonymous-namespace
dummy in the .cpp, e.g. `namespace { int placeholder() { return 0; } }`)
— those get overwritten in Task 4.

- [ ] **Step 9: Run the mock tests.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure -R tst_mockpalmdatabaseaccess
```

Expected: PASS (8 slots).

- [ ] **Step 10: Run full WP ctest to confirm no regressions.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure
```

Expected: all previously-passing tests still pass; new
`tst_mockpalmdatabaseaccess` adds to the count.

- [ ] **Step 11: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/sync/ src/CMakeLists.txt \
        tests/palmsync/ tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-sync): scaffold WildPalmsPalmSync lib + mock device access

Phase E.3 task 1: new static library WildPalmsPalmSync housing
PalmRecord (device-side value type), IPalmDatabaseAccess (abstract
device interface), and MockPalmDatabaseAccess (in-memory impl for
tests). PalmBackend lands in a subsequent commit.

Library PUBLIC-links Kalburator::Sync + Qt::Core. Deliberately does
not link pisock — the pilot-link adapter is deferred to Phase E.4.

Eight unit tests in tests/palmsync/ cover CRUD, auto-assigned and
explicit record IDs, nullopt-on-missing reads, deletion logging,
and timestamp-filtered modifiedSince / deletedSince.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: PalmBackend skeleton — identity + first passing test

**Files:**
- Modify: `~/dev/WildPalms/src/palm/sync/palmbackend.h` (replace placeholder)
- Modify: `~/dev/WildPalms/src/palm/sync/palmbackend.cpp` (replace placeholder)
- Create: `~/dev/WildPalms/tests/palmsync/tst_palmbackend.cpp`
- Modify: `~/dev/WildPalms/tests/palmsync/CMakeLists.txt`

- [ ] **Step 1: Write the header.**

File `~/dev/WildPalms/src/palm/sync/palmbackend.h`:

```cpp
#ifndef WILDPALMS_SYNC_PALMBACKEND_H
#define WILDPALMS_SYNC_PALMBACKEND_H

#include "iblobbackend.h"

namespace WildPalms::PalmSync {

class IPalmDatabaseAccess;

/**
 * @brief Kalburator::Sync::IBlobBackend implementation backed by a
 *        Palm device abstraction.
 *
 * One instance is intended to be owned by the application runtime and
 * shared across plugins. Each Palm database is surfaced as a
 * CollectionInfo whose id is "palm:<dbname>" (lowercase, no "DB"
 * suffix; e.g. "palm:memo" for MemoDB). Record IDs are encoded as
 * "palm:<dbname>:<numericId>".
 *
 * Does not own the IPalmDatabaseAccess; caller is responsible for
 * keeping it alive for the backend's lifetime.
 */
class PalmBackend : public Kalburator::Sync::IBlobBackend {
    Q_OBJECT
public:
    explicit PalmBackend(IPalmDatabaseAccess *device,
                         QObject *parent = nullptr);
    ~PalmBackend() override;

    // --- Identity ---
    QString backendId() const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- Collections ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(
        const QString &collectionId) override;
    QString createCollection(
        const Kalburator::Sync::CollectionInfo &info) override;

    // --- Records ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(
        const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(
        const QString &recordId) override;
    QString createRecord(
        const QString &collectionId,
        const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(
        const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    // --- Change detection ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;
    bool supportsDeleteTracking() const override;

    // --- ID encoding (exposed for tests and for callers that need to
    //     round-trip between PalmRecord and BackendRecord IDs).   ---
    static QString  encodeRecordId(const QString &dbName,
                                   std::uint32_t recordId);
    static bool     decodeRecordId(const QString &encoded,
                                   QString *dbNameOut,
                                   std::uint32_t *recordIdOut);
    static QString  encodeCollectionId(const QString &dbName);
    static bool     decodeCollectionId(const QString &collectionId,
                                       QString *dbNameOut);

private:
    IPalmDatabaseAccess *m_device = nullptr;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_PALMBACKEND_H
```

- [ ] **Step 2: Write the initial implementation — identity + ID codecs + empty stubs for the rest.**

File `~/dev/WildPalms/src/palm/sync/palmbackend.cpp`:

```cpp
#include "palmbackend.h"

#include <QCryptographicHash>

#include "backendrecord.h"
#include "collectioninfo.h"
#include "ipalmdatabaseaccess.h"
#include "palmrecord.h"

namespace WildPalms::PalmSync {

namespace {
constexpr const char kCollectionPrefix[] = "palm:";
} // namespace

PalmBackend::PalmBackend(IPalmDatabaseAccess *device, QObject *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_device(device)
{
}

PalmBackend::~PalmBackend() = default;

QString PalmBackend::backendId() const
{
    return QStringLiteral("palm");
}

QString PalmBackend::displayName() const
{
    return QStringLiteral("Palm OS Device");
}

bool PalmBackend::isAvailable() const
{
    return m_device != nullptr;
}

// --- ID encoding ---

QString PalmBackend::encodeCollectionId(const QString &dbName)
{
    // "DatebookDB" -> "palm:datebook", "MemoDB" -> "palm:memo" etc.
    QString bare = dbName;
    if (bare.endsWith(QStringLiteral("DB"))) {
        bare.chop(2);
    }
    return QStringLiteral("%1%2").arg(QString::fromLatin1(kCollectionPrefix),
                                      bare.toLower());
}

bool PalmBackend::decodeCollectionId(const QString &collectionId,
                                     QString *dbNameOut)
{
    const auto prefix = QString::fromLatin1(kCollectionPrefix);
    if (!collectionId.startsWith(prefix)) return false;
    const auto bare = collectionId.mid(prefix.size());
    if (bare.isEmpty()) return false;
    if (bare.contains(QLatin1Char(':'))) return false; // that's a record id
    if (dbNameOut) {
        // Round-trip mapping: "memo" -> "MemoDB". Capitalise the first
        // letter and append "DB".
        QString titled = bare;
        titled[0] = titled[0].toUpper();
        *dbNameOut = titled + QStringLiteral("DB");
    }
    return true;
}

QString PalmBackend::encodeRecordId(const QString &dbName,
                                    std::uint32_t recordId)
{
    return QStringLiteral("%1:%2").arg(encodeCollectionId(dbName))
                                  .arg(recordId);
}

bool PalmBackend::decodeRecordId(const QString &encoded,
                                 QString *dbNameOut,
                                 std::uint32_t *recordIdOut)
{
    // Format: "palm:<bare>:<numeric>"
    const auto parts = encoded.split(QLatin1Char(':'));
    if (parts.size() != 3) return false;
    if (parts[0] != QStringLiteral("palm")) return false;
    bool ok = false;
    const auto numeric = parts[2].toUInt(&ok);
    if (!ok) return false;
    if (dbNameOut) {
        QString titled = parts[1];
        titled[0] = titled[0].toUpper();
        *dbNameOut = titled + QStringLiteral("DB");
    }
    if (recordIdOut) *recordIdOut = numeric;
    return true;
}

// --- Empty stubs; filled in by Tasks 5-7 ---

QList<Kalburator::Sync::CollectionInfo> PalmBackend::availableCollections()
{
    return {};
}

Kalburator::Sync::CollectionInfo PalmBackend::collectionInfo(const QString &)
{
    return {};
}

QString PalmBackend::createCollection(const Kalburator::Sync::CollectionInfo &)
{
    return {};
}

QList<Kalburator::Sync::BackendRecord> PalmBackend::loadRecords(const QString &)
{
    return {};
}

std::optional<Kalburator::Sync::BackendRecord> PalmBackend::loadRecord(
    const QString &)
{
    return std::nullopt;
}

QString PalmBackend::createRecord(const QString &,
                                  const Kalburator::Sync::BackendRecord &)
{
    return {};
}

bool PalmBackend::updateRecord(const Kalburator::Sync::BackendRecord &)
{
    return false;
}

bool PalmBackend::deleteRecord(const QString &)
{
    return false;
}

QList<Kalburator::Sync::BackendRecord> PalmBackend::modifiedSince(
    const QString &, const QDateTime &)
{
    return {};
}

QStringList PalmBackend::deletedSince(const QString &, const QDateTime &)
{
    return {};
}

bool PalmBackend::supportsDeleteTracking() const
{
    return m_device && m_device->supportsDeleteTracking();
}

} // namespace WildPalms::PalmSync
```

- [ ] **Step 3: Write the first test — identity + ID codecs.**

File `~/dev/WildPalms/tests/palmsync/tst_palmbackend.cpp`:

```cpp
#include <QtTest/QtTest>

#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

class TestPalmBackend : public QObject
{
    Q_OBJECT
private slots:
    void identity();
    void collectionIdRoundTrip();
    void recordIdRoundTrip();
    void decodeCollectionIdRejectsRecordIds();
};

void TestPalmBackend::identity()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);

    QCOMPARE(backend.backendId(), QStringLiteral("palm"));
    QCOMPARE(backend.displayName(), QStringLiteral("Palm OS Device"));
    QVERIFY(backend.isAvailable());

    PalmBackend detached(nullptr);
    QVERIFY(!detached.isAvailable());
}

void TestPalmBackend::collectionIdRoundTrip()
{
    QCOMPARE(PalmBackend::encodeCollectionId(QStringLiteral("MemoDB")),
             QStringLiteral("palm:memo"));
    QCOMPARE(PalmBackend::encodeCollectionId(QStringLiteral("DatebookDB")),
             QStringLiteral("palm:datebook"));

    QString db;
    QVERIFY(PalmBackend::decodeCollectionId(QStringLiteral("palm:memo"), &db));
    QCOMPARE(db, QStringLiteral("MemoDB"));

    QVERIFY(PalmBackend::decodeCollectionId(QStringLiteral("palm:datebook"), &db));
    QCOMPARE(db, QStringLiteral("DatebookDB"));

    QVERIFY(!PalmBackend::decodeCollectionId(QStringLiteral("notpalm:memo"), &db));
    QVERIFY(!PalmBackend::decodeCollectionId(QStringLiteral("palm:"), &db));
}

void TestPalmBackend::recordIdRoundTrip()
{
    const auto encoded = PalmBackend::encodeRecordId(
        QStringLiteral("MemoDB"), 42);
    QCOMPARE(encoded, QStringLiteral("palm:memo:42"));

    QString db;
    std::uint32_t id = 0;
    QVERIFY(PalmBackend::decodeRecordId(encoded, &db, &id));
    QCOMPARE(db, QStringLiteral("MemoDB"));
    QCOMPARE(id, 42u);

    QVERIFY(!PalmBackend::decodeRecordId(QStringLiteral("palm:memo"), &db, &id));
    QVERIFY(!PalmBackend::decodeRecordId(
        QStringLiteral("palm:memo:notanumber"), &db, &id));
}

void TestPalmBackend::decodeCollectionIdRejectsRecordIds()
{
    QString db;
    QVERIFY(!PalmBackend::decodeCollectionId(
        QStringLiteral("palm:memo:42"), &db));
}

QTEST_MAIN(TestPalmBackend)
#include "tst_palmbackend.moc"
```

- [ ] **Step 4: Register the new test executable.**

Edit `~/dev/WildPalms/tests/palmsync/CMakeLists.txt`. Append after the
existing `add_palm_sync_test(tst_mockpalmdatabaseaccess ...)` line:

```cmake
add_palm_sync_test(tst_palmbackend tst_palmbackend.cpp)
```

- [ ] **Step 5: Build + run the test.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmbackend
```

Expected: 4 tests PASS.

- [ ] **Step 6: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/sync/palmbackend.h src/palm/sync/palmbackend.cpp \
        tests/palmsync/tst_palmbackend.cpp \
        tests/palmsync/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-sync): PalmBackend skeleton + ID codecs

Phase E.3 task 2: PalmBackend class declaring the full IBlobBackend
surface plus the palm:<db> / palm:<db>:<id> encoding used to round-
trip between Kalburator record IDs and Palm database+recordId pairs.

Identity methods wired (backendId == "palm", displayName == "Palm OS
Device", isAvailable == device != nullptr). Remaining virtuals are
stubs; subsequent tasks fill them in alongside their tests.

Four unit tests cover identity and ID codec round-trip.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Collections API

**Files:**
- Modify: `~/dev/WildPalms/src/palm/sync/palmbackend.cpp`
- Modify: `~/dev/WildPalms/tests/palmsync/tst_palmbackend.cpp`

- [ ] **Step 1: Add failing tests.**

Add slot declarations to `tst_palmbackend.cpp` (after the existing
slots, before the `QTEST_MAIN` line). Keep them in the same private
slots section:

```cpp
    void availableCollectionsReflectsDevice();
    void collectionInfoReturnsEmptyForUnknown();
    void createCollectionDelegatesToDevice();
```

Add slot bodies near the other test bodies:

```cpp
void TestPalmBackend::availableCollectionsReflectsDevice()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    dev.createDatabase(QStringLiteral("DatebookDB"));

    PalmBackend backend(&dev);
    const auto cols = backend.availableCollections();
    QStringList ids;
    for (const auto &c : cols) ids.append(c.id);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, QStringList()
             << QStringLiteral("palm:datebook")
             << QStringLiteral("palm:memo"));
}

void TestPalmBackend::collectionInfoReturnsEmptyForUnknown()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);
    const auto info = backend.collectionInfo(
        QStringLiteral("palm:nonexistent"));
    QCOMPARE(info.id, QString());
}

void TestPalmBackend::createCollectionDelegatesToDevice()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);

    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("palm:memo");
    info.type = QStringLiteral("memos");
    const auto created = backend.createCollection(info);
    QCOMPARE(created, QStringLiteral("palm:memo"));
    QVERIFY(dev.hasDatabase(QStringLiteral("MemoDB")));
}
```

Build + run. Expected: 3 new failures (stubs return empty).

- [ ] **Step 2: Implement the collections methods.**

In `palmbackend.cpp`, replace the three stub implementations with:

```cpp
namespace {

QString collectionTypeForDb(const QString &dbName)
{
    if (dbName == QStringLiteral("MemoDB"))     return QStringLiteral("memos");
    if (dbName == QStringLiteral("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QStringLiteral("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QStringLiteral("ToDoDB"))     return QStringLiteral("todos");
    return QStringLiteral("binary");
}

Kalburator::Sync::CollectionInfo makeCollectionInfo(const QString &dbName)
{
    Kalburator::Sync::CollectionInfo info;
    info.id   = PalmBackend::encodeCollectionId(dbName);
    info.name = dbName;
    info.type = collectionTypeForDb(dbName);
    return info;
}

} // namespace

QList<Kalburator::Sync::CollectionInfo> PalmBackend::availableCollections()
{
    if (!m_device) return {};
    QList<Kalburator::Sync::CollectionInfo> out;
    for (const auto &dbName : m_device->availableDatabases()) {
        out.append(makeCollectionInfo(dbName));
    }
    return out;
}

Kalburator::Sync::CollectionInfo PalmBackend::collectionInfo(
    const QString &collectionId)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device || !m_device->hasDatabase(dbName)) return {};
    return makeCollectionInfo(dbName);
}

QString PalmBackend::createCollection(
    const Kalburator::Sync::CollectionInfo &info)
{
    QString dbName;
    if (!decodeCollectionId(info.id, &dbName)) return {};
    if (!m_device) return {};
    if (!m_device->createDatabase(dbName)) return {};
    return info.id;
}
```

Place the anonymous-namespace helpers near the top of the file (after
the `kCollectionPrefix` constant), not inline with the method bodies,
so they're visible to all subsequent tasks.

- [ ] **Step 3: Build + test.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmbackend
```

Expected: 7 tests PASS (4 original + 3 new).

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/sync/palmbackend.cpp tests/palmsync/tst_palmbackend.cpp
git commit -m "$(cat <<'EOF'
feat(palm-sync): PalmBackend collections API

Phase E.3 task 3: availableCollections / collectionInfo /
createCollection delegate to IPalmDatabaseAccess. Collection IDs
follow the "palm:<bare-db-name>" convention; collectionInfo returns
default-constructed on unknown IDs (IBlobBackend contract).

DB-name-to-collection-type mapping is hard-coded for the four stock
Palm databases; unknowns fall through to "binary".

Three new tests cover enumeration, unknown lookup, and create-
collection delegation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Record CRUD

**Files:**
- Modify: `~/dev/WildPalms/src/palm/sync/palmbackend.cpp`
- Modify: `~/dev/WildPalms/tests/palmsync/tst_palmbackend.cpp`

- [ ] **Step 1: Add failing tests.**

Slot declarations:

```cpp
    void loadRecordsExposesBackendRecords();
    void loadRecordFindsById();
    void createRecordAssignsId();
    void updateRecordWritesBack();
    void deleteRecordRemovesFromDevice();
    void contentHashIsSha256OfData();
```

Add slot bodies. Declaration of a local helper at file scope (near the
top of the `.cpp`, above the test class body):

```cpp
static PalmRecord makePalm(std::uint32_t id, const QByteArray &payload)
{
    PalmRecord r;
    r.recordId = id;
    r.data = payload;
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}
```

Test bodies:

```cpp
void TestPalmBackend::loadRecordsExposesBackendRecords()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto id1 = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("a")));
    const auto id2 = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("bb")));

    PalmBackend backend(&dev);
    const auto records = backend.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(records.size(), 2);

    QStringList ids;
    for (const auto &r : records) ids.append(r.id);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids,
             QStringList()
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), id1)
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), id2));
}

void TestPalmBackend::loadRecordFindsById()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto id = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("hello")));

    PalmBackend backend(&dev);
    const auto got = backend.loadRecord(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), id));
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("hello"));
}

void TestPalmBackend::createRecordAssignsId()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend backend(&dev);

    Kalburator::Sync::BackendRecord rec;
    rec.data = QByteArrayLiteral("new");
    rec.type = QStringLiteral("memos");
    rec.contentHash = QStringLiteral("ignored-replaced-by-backend");

    const auto id = backend.createRecord(QStringLiteral("palm:memo"), rec);
    QVERIFY(!id.isEmpty());
    QVERIFY(id.startsWith(QStringLiteral("palm:memo:")));

    // Round-trip: loading it back should match.
    const auto got = backend.loadRecord(id);
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("new"));
}

void TestPalmBackend::updateRecordWritesBack()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto devId = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("orig")));

    PalmBackend backend(&dev);

    Kalburator::Sync::BackendRecord rec;
    rec.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), devId);
    rec.data = QByteArrayLiteral("updated");
    QVERIFY(backend.updateRecord(rec));

    const auto got = backend.loadRecord(rec.id);
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("updated"));
}

void TestPalmBackend::deleteRecordRemovesFromDevice()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto devId = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("gone")));

    PalmBackend backend(&dev);
    QVERIFY(backend.deleteRecord(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), devId)));
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), devId).has_value());
}

void TestPalmBackend::contentHashIsSha256OfData()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto devId = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("abc")));

    PalmBackend backend(&dev);
    const auto got = backend.loadRecord(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), devId));
    QVERIFY(got.has_value());

    const auto expected = QCryptographicHash::hash(
        QByteArrayLiteral("abc"), QCryptographicHash::Sha256).toHex();
    QCOMPARE(got->contentHash, QString::fromLatin1(expected));
}
```

The last test requires `#include <QCryptographicHash>` at the top of
`tst_palmbackend.cpp`.

Build + run; expected: 6 new failures.

- [ ] **Step 2: Implement CRUD.**

Replace the stubs in `palmbackend.cpp`. Add these helpers near the top
of the file, next to `makeCollectionInfo`:

```cpp
namespace {

QString hashForData(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

Kalburator::Sync::BackendRecord palmToBackend(const QString &dbName,
                                              const PalmRecord &pr)
{
    Kalburator::Sync::BackendRecord br;
    br.id = PalmBackend::encodeRecordId(dbName, pr.recordId);
    br.type = collectionTypeForDb(dbName);
    br.data = pr.data;
    br.contentHash = hashForData(pr.data);
    br.lastModified = pr.lastModified;
    br.isDeleted = pr.isDeleted();
    return br;
}

PalmRecord backendToPalm(const Kalburator::Sync::BackendRecord &br,
                         std::uint32_t existingId = 0)
{
    PalmRecord pr;
    pr.recordId = existingId;
    pr.data = br.data;
    pr.lastModified = br.lastModified.isValid()
        ? br.lastModified
        : QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace
```

Then replace the five CRUD stubs:

```cpp
QList<Kalburator::Sync::BackendRecord> PalmBackend::loadRecords(
    const QString &collectionId)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    QList<Kalburator::Sync::BackendRecord> out;
    for (const auto &pr : m_device->readAllRecords(dbName)) {
        out.append(palmToBackend(dbName, pr));
    }
    return out;
}

std::optional<Kalburator::Sync::BackendRecord> PalmBackend::loadRecord(
    const QString &recordId)
{
    QString dbName;
    std::uint32_t numericId = 0;
    if (!decodeRecordId(recordId, &dbName, &numericId)) return std::nullopt;
    if (!m_device) return std::nullopt;

    const auto palm = m_device->readRecord(dbName, numericId);
    if (!palm.has_value()) return std::nullopt;
    return palmToBackend(dbName, *palm);
}

QString PalmBackend::createRecord(
    const QString &collectionId,
    const Kalburator::Sync::BackendRecord &record)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    PalmRecord pr = backendToPalm(record);
    const auto newId = m_device->createRecord(dbName, pr);
    if (newId == 0) return {};
    return encodeRecordId(dbName, newId);
}

bool PalmBackend::updateRecord(const Kalburator::Sync::BackendRecord &record)
{
    QString dbName;
    std::uint32_t numericId = 0;
    if (!decodeRecordId(record.id, &dbName, &numericId)) return false;
    if (!m_device) return false;

    PalmRecord pr = backendToPalm(record, numericId);
    return m_device->updateRecord(dbName, pr);
}

bool PalmBackend::deleteRecord(const QString &recordId)
{
    QString dbName;
    std::uint32_t numericId = 0;
    if (!decodeRecordId(recordId, &dbName, &numericId)) return false;
    if (!m_device) return false;
    return m_device->deleteRecord(dbName, numericId);
}
```

Add `#include <QCryptographicHash>` at the top of `palmbackend.cpp`.

- [ ] **Step 3: Build + test.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmbackend
```

Expected: 13 tests PASS.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/sync/palmbackend.cpp tests/palmsync/tst_palmbackend.cpp
git commit -m "$(cat <<'EOF'
feat(palm-sync): PalmBackend record CRUD

Phase E.3 task 4: loadRecords / loadRecord / createRecord /
updateRecord / deleteRecord delegate to IPalmDatabaseAccess via
PalmRecord <-> BackendRecord conversion helpers (anonymous
namespace). contentHash is SHA-256 of data bytes.

Six new tests cover enumeration, lookup, create-with-assigned-ID,
update round-trip, delete propagation, and hash computation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Change detection

**Files:**
- Modify: `~/dev/WildPalms/src/palm/sync/palmbackend.cpp`
- Modify: `~/dev/WildPalms/tests/palmsync/tst_palmbackend.cpp`

- [ ] **Step 1: Add failing tests.**

Slot declarations:

```cpp
    void modifiedSinceFiltersByTimestamp();
    void deletedSincePropagatesEncodedIds();
    void supportsDeleteTrackingFollowsDevice();
```

Slot bodies:

```cpp
void TestPalmBackend::modifiedSinceFiltersByTimestamp()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    // Old record
    PalmRecord oldRec;
    oldRec.data = QByteArrayLiteral("old");
    oldRec.lastModified = QDateTime::fromString(
        QStringLiteral("2020-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    dev.createRecord(QStringLiteral("MemoDB"), oldRec);

    // Fresh record (mock fills lastModified = now)
    PalmRecord freshRec;
    freshRec.data = QByteArrayLiteral("fresh");
    dev.createRecord(QStringLiteral("MemoDB"), freshRec);

    PalmBackend backend(&dev);
    const auto cutoff = QDateTime::fromString(
        QStringLiteral("2024-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    const auto modified = backend.modifiedSince(
        QStringLiteral("palm:memo"), cutoff);

    QCOMPARE(modified.size(), 1);
    QCOMPARE(modified.first().data, QByteArrayLiteral("fresh"));
}

void TestPalmBackend::deletedSincePropagatesEncodedIds()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    const auto idA = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("a")));
    const auto idB = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("b")));

    const auto before = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), idA));
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), idB));

    PalmBackend backend(&dev);
    const auto deleted = backend.deletedSince(
        QStringLiteral("palm:memo"), before);
    QStringList sorted = deleted;
    std::sort(sorted.begin(), sorted.end());
    QCOMPARE(sorted, QStringList()
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), idA)
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), idB));
}

void TestPalmBackend::supportsDeleteTrackingFollowsDevice()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);
    QVERIFY(backend.supportsDeleteTracking()); // mock returns true
}
```

- [ ] **Step 2: Implement.**

In `palmbackend.cpp`, replace the `modifiedSince` and `deletedSince`
stubs with:

```cpp
QList<Kalburator::Sync::BackendRecord> PalmBackend::modifiedSince(
    const QString &collectionId, const QDateTime &since)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    QList<Kalburator::Sync::BackendRecord> out;
    for (const auto &pr : m_device->recordsModifiedSince(dbName, since)) {
        out.append(palmToBackend(dbName, pr));
    }
    return out;
}

QStringList PalmBackend::deletedSince(const QString &collectionId,
                                      const QDateTime &since)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    QStringList out;
    for (auto id : m_device->recordsDeletedSince(dbName, since)) {
        out.append(encodeRecordId(dbName, id));
    }
    return out;
}
```

`supportsDeleteTracking()` was already wired up in Task 4.

- [ ] **Step 3: Build + test.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmbackend
```

Expected: 16 tests PASS.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/sync/palmbackend.cpp tests/palmsync/tst_palmbackend.cpp
git commit -m "$(cat <<'EOF'
feat(palm-sync): PalmBackend change detection

Phase E.3 task 5: modifiedSince / deletedSince proxy through to the
device. Deleted-record IDs are re-encoded into the palm:<db>:<id>
form so callers (the engine's baseline-diff loop) see a consistent
record-id namespace.

Three new tests.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: End-to-end round-trip through BlobSyncEngine

This is the Phase-E spec's E.3 exit gate: "PalmBackend can round-trip
synthetic BackendRecords through the engine." Uses
`BlobSyncEngine::twoWayWithBaseline` with `PalmBackend` on one side
and `MockBlobBackend` on the other.

**Files:**
- Create: `~/dev/WildPalms/tests/palmsync/tst_palmbackend_roundtrip.cpp`
- Modify: `~/dev/WildPalms/tests/palmsync/CMakeLists.txt`

- [ ] **Step 1: Write the round-trip test.**

File `~/dev/WildPalms/tests/palmsync/tst_palmbackend_roundtrip.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"
#include "blobsyncengine.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictstore.h"
#include "mockblobbackend.h"

#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"

using Kalburator::Sync::BlobBaselineStore;
using Kalburator::Sync::BlobSyncEngine;
using Kalburator::Sync::BlobSyncResult;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;
using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

class TestPalmBackendRoundTrip : public QObject
{
    Q_OBJECT
private slots:
    void palmSideRecordPropagatesToMock();
    void mockSideRecordPropagatesToPalm();
    void deletionOnPalmPropagatesToMockViaBaseline();

private:
    static QString dbPathIn(const QTemporaryDir &dir)
    {
        return dir.filePath(QStringLiteral(".planstan-sync.db"));
    }

    static CollectionInfo mockCollection(const QString &id)
    {
        CollectionInfo info;
        info.id = id;
        info.name = id;
        info.type = QStringLiteral("memos");
        return info;
    }
};

void TestPalmBackendRoundTrip::palmSideRecordPropagatesToMock()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Palm side: one MemoDB record.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord pr;
    pr.data = QByteArrayLiteral("palm-content");
    pr.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), pr);

    PalmBackend palm(&dev);

    // Mock side: matching collection, no records.
    MockBlobBackend mock;
    mock.createCollection(mockCollection(QStringLiteral("palm:memo")));

    BlobBaselineStore baseline(dbPathIn(dir));
    QVERIFY(baseline.isOpen());
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e3-roundtrip"),
        &baseline, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(r.targetStats.created, 1);

    const auto mockRecs = mock.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(mockRecs.size(), 1);
    QCOMPARE(mockRecs.first().data, QByteArrayLiteral("palm-content"));
}

void TestPalmBackendRoundTrip::mockSideRecordPropagatesToPalm()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend palm(&dev);

    MockBlobBackend mock;
    mock.createCollection(mockCollection(QStringLiteral("palm:memo")));

    // Mock side seeded; palm side empty.
    BackendRecord br;
    br.id = QStringLiteral("palm:memo:7");
    br.type = QStringLiteral("memos");
    br.data = QByteArrayLiteral("from-mock");
    br.contentHash = QStringLiteral("ignored-by-palm-backend");
    br.lastModified = QDateTime::currentDateTimeUtc();
    mock.createRecord(QStringLiteral("palm:memo"), br);

    BlobBaselineStore baseline(dbPathIn(dir));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e3-rt2"),
        &baseline, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(r.sourceStats.created, 1);

    const auto palmRecs = palm.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(palmRecs.size(), 1);
    QCOMPARE(palmRecs.first().data, QByteArrayLiteral("from-mock"));
}

void TestPalmBackendRoundTrip::deletionOnPalmPropagatesToMockViaBaseline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord pr;
    pr.data = QByteArrayLiteral("will-be-deleted");
    pr.lastModified = QDateTime::currentDateTimeUtc();
    const auto devId = dev.createRecord(QStringLiteral("MemoDB"), pr);

    PalmBackend palm(&dev);
    MockBlobBackend mock;
    mock.createCollection(mockCollection(QStringLiteral("palm:memo")));

    BlobBaselineStore baseline(dbPathIn(dir));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;
    BlobSyncEngine engine;

    // First sync populates baseline and propagates the record.
    BlobSyncResult r1 = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e3-del"),
        &baseline, &reg, &store, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QCOMPARE(mock.loadRecords(QStringLiteral("palm:memo")).size(), 1);

    // Delete on the Palm side.
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), devId));

    // Second sync: baseline sees the deletion and propagates to mock.
    BlobSyncResult r2 = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e3-del"),
        &baseline, &reg, &store, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QCOMPARE(r2.targetStats.deleted, 1);
    QCOMPARE(mock.loadRecords(QStringLiteral("palm:memo")).size(), 0);
}

QTEST_MAIN(TestPalmBackendRoundTrip)
#include "tst_palmbackend_roundtrip.moc"
```

- [ ] **Step 2: Register the test.**

Append to `~/dev/WildPalms/tests/palmsync/CMakeLists.txt`:

```cmake
add_palm_sync_test(tst_palmbackend_roundtrip tst_palmbackend_roundtrip.cpp)
```

- [ ] **Step 3: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmbackend_roundtrip
```

Expected: 3 tests PASS. This exercises the full engine path:
`PalmBackend::loadRecords/createRecord/updateRecord/deleteRecord`
called through `BlobSyncEngine::twoWayWithBaseline` with a real
`BlobBaselineStore` SQLite database.

- [ ] **Step 4: Full WP ctest.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. New counts: 8 mock-device + 16 backend + 3
round-trip = 27 new test slots across 3 new executables in
`tests/palmsync/`.

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/WildPalms
git add tests/palmsync/tst_palmbackend_roundtrip.cpp \
        tests/palmsync/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(palm-sync): end-to-end round-trip through BlobSyncEngine

Phase E.3 task 6: exercise PalmBackend against MockBlobBackend via
BlobSyncEngine::twoWayWithBaseline + a real BlobBaselineStore. Three
scenarios: palm-side create propagates to mock, mock-side create
propagates to palm, palm-side deletion propagates via baseline on the
second sync.

This is the E.3 exit gate per the Phase-E spec.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Mark E.3 done in the spec

**Files:**
- Modify: `~/dev/WildPalms/docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`

- [ ] **Step 1: Update the sub-phases table.**

In the sub-phases table, change the E.3 row from:

```markdown
| **E.3** | `PalmBackend : IBlobBackend` scaffold. Implements all `IBlobBackend` methods against a mock device (no real pilot-link yet). Unit tests against `BlobSyncEngine` with `MockBlobBackend` as counterparty. | WP | E.2 | `ctest` passes; `PalmBackend` can round-trip synthetic `BackendRecord`s through the engine. |
```

To:

```markdown
| ✅ **E.3** | `PalmBackend : IBlobBackend` scaffold. Implements all `IBlobBackend` methods against a mock device (no real pilot-link yet). Unit tests against `BlobSyncEngine` with `MockBlobBackend` as counterparty. New static lib `WildPalmsPalmSync` houses PalmBackend + IPalmDatabaseAccess + MockPalmDatabaseAccess + PalmRecord value type. Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e3-palm-backend-scaffold.md`. | WP | E.2 | `ctest` passes; `PalmBackend` round-trips synthetic `BackendRecord`s through the engine (tst_palmbackend_roundtrip). |
```

- [ ] **Step 2: Commit.**

```bash
cd ~/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "$(cat <<'EOF'
docs(phase-e): mark E.3 landed in spec sub-phases table

PalmBackend scaffold landed 2026-04-21. All E.3 exit criteria met:
WildPalmsPalmSync static library builds, full ctest passes, and the
end-to-end round-trip test exercises PalmBackend through
BlobSyncEngine::twoWayWithBaseline.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

Spec coverage (Phase-E spec §"WP-side class layout" PalmBackend row +
sub-phases table E.3 row):

- `PalmBackend : IBlobBackend` — Tasks 4-7. ✓
- "Exposes one CollectionInfo per Palm database" — Task 5. ✓
- "Opaque `BackendRecord.data` carries the raw Palm record bytes" —
  Task 6 (`palmToBackend` copies bytes through unchanged). ✓
- "Implements all `IBlobBackend` methods against a mock device" — mock
  defined in Task 3, backend wired in Tasks 4-7. ✓
- "Unit tests against `BlobSyncEngine` with `MockBlobBackend`" —
  Task 8 exit-gate test. ✓
- "round-trip synthetic `BackendRecord`s through the engine" —
  Task 8's three scenarios. ✓

Deferred to E.4 (per spec):
- Real pilot-link wiring (separate adapter implementing
  `IPalmDatabaseAccess`).
- Codec delegation from `src/palm/codecs/` (those codecs live today in
  `src/plugins/*/mapper.cpp`; they stay there for E.3).
- Category slot in `PalmRecord.category` is populated by the mock but
  not yet exposed through `BackendRecord`. The virtual sub-calendar
  mechanism (spec §"Category routing") lands in E.6 on
  `PalmCalendarBackend`, not here. E.3's `BackendRecord.data` is
  opaque to categories deliberately.

Placeholder scan:
- No TBD / TODO / FIXME in any task body.
- Every code block is complete and compilable (assuming helper
  anonymous-namespace placements specified in Task 5 Step 2 and
  Task 6 Step 2 are followed).
- The "empty-stub placeholder files" instruction in Task 3 Step 3
  is explicit and includes a minimal fallback (one anonymous-namespace
  function) if the empty placeholders don't satisfy the linker.

Type consistency:
- `PalmRecord` struct matches across palmrecord.h, mock, and backend
  conversion helpers.
- `IPalmDatabaseAccess` method signatures match across the header, the
  mock's override declarations, and the backend's call sites.
- `PalmBackend::encodeRecordId` / `decodeRecordId` signatures match
  across declaration, implementation, and test usage.
- `BackendRecord` fields used (`id`, `type`, `data`, `contentHash`,
  `lastModified`, `isDeleted`) match the libkalburator header
  (verified against `~/dev/libkalburator/src/types/backendrecord.h`).
- `CollectionInfo` fields used (`id`, `name`, `type`) match the
  libkalburator header.
- `MockBlobBackend`'s ctor takes no arguments (verified against
  `~/dev/libkalburator/src/blob/mockblobbackend.h`); no backendId arg
  is passed in Task 8.

No gaps detected.

---

## Follow-up plans

After this plan lands:

- **E.4** — `PalmBackend` wired to real pilot-link. Implements a
  second `IPalmDatabaseAccess` (likely `PilotLinkPalmDatabaseAccess`)
  that wraps WP's existing DLP code. Relocates `src/plugins/*/mapper.cpp`
  codec logic into `src/palm/codecs/`. Drafted after E.3 executes.
- **E.5** — `PalmConflictHandler` + `PalmBackendConfig`. Drafted after
  E.4.
- **E.6** — `PalmCalendarBackend` with virtual sub-calendars per
  category slot.
- **E.7+** — typed adapters and plugin-ABI work per the Phase-E spec.
