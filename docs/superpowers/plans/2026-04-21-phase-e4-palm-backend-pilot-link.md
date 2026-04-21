# Phase E.4 — PalmBackend wired to real pilot-link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land `PilotLinkPalmDatabaseAccess` — a second implementation
of `IPalmDatabaseAccess` that routes DLP calls through WP's existing
`KPilotLink` device abstraction. Unit-testable via a `MockKPilotLink`
implementing the same interface. Full device integration is
deferred to E.18 (POSE64 sandbox); E.4 lands compile-ready
infrastructure plus in-memory tests that prove the conversion logic
and DLP-call dispatch are correct.

**Architecture:** `KPilotLink` is already an abstract interface in
`src/palm/kpilotlink.h` with `KPilotDeviceLink` as the concrete
pilot-link implementation. E.4 adds a new `MockKPilotLink` test
double (in-memory per-database record store) so the new adapter can
be exercised without libpisock at runtime. The adapter itself is a
thin wrapper — each `IPalmDatabaseAccess` method opens the named
database, runs one or two DLP calls, and closes. Production will
later want to keep database handles open across operations; for the
scaffold, per-op open/close is simpler and correct.

**PilotRecord ↔ PalmRecord bridge:** pure free functions in the new
module. `PilotRecord` (WildPalmsCore, pisock-linked) and `PalmRecord`
(WildPalmsPalmSync, Kalburator-linked) both carry the same semantic
fields; conversion is mechanical.

**CMake home:** new static lib `WildPalmsPalmDevice` at
`src/palm/device/`. Links `WildPalmsCore` (for `PilotRecord` +
`KPilotLink`) + `WildPalmsPalmSync` (for `IPalmDatabaseAccess` +
`PalmRecord`). This is the first place both deps converge at the
library-graph level — which is expected as Phase E unifies the two
modes. `WildPalmsCore` itself is untouched (Phase-D quarantine
remains on `WildPalmsCore` in isolation; the quarantine crosses into
`WildPalmsPalmSync` only via the opt-in `WildPalmsPalmDevice`
consumer).

**Tech Stack:** C++20, Qt6 (Core, Test), existing WP `KPilotLink`
abstraction + `PilotRecord` wrapper. No new runtime dependency.

**Repo:** All work in `~/dev/WildPalms/`. No upstream changes.

**Scope not in E.4:**

- Codec relocation from `src/plugins/*/mapper.cpp` into
  `src/palm/codecs/`. The Phase-E spec's E.4 row mentions this, but
  closer reading shows no consumer needs the relocated codecs until
  E.6 (`PalmCalendarBackend` using `DatebookCodec`) and E.7 (typed
  adapters for memos/contacts/todos using their respective codecs).
  Relocation lands alongside the first consumer rather than in E.4
  speculatively.
- Live-device integration test. Deferred to E.18 (POSE64 sandbox).
  E.4's tests cover conversion logic + DLP-call dispatch sequencing
  against `MockKPilotLink`.
- App-layer runtime wiring. Nothing constructs a
  `PilotLinkPalmDatabaseAccess` at app startup yet; E.16 handles
  app-level plumbing when the unified runtime lands.
- Keep-open optimization. Per-op open/close is the scaffold pattern;
  a later sub-phase can add caching.
- Extending `KPilotLink` with a `createDatabase` primitive.
  `IPalmDatabaseAccess::createDatabase` will return `false` when the
  database does not already exist; Palm software rarely creates
  databases (apps do). A follow-up phase adds `dlp_CreateDB` wiring if
  needed.

**Spec reference:**
`docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
§"WP-side class layout" (PalmBackend row) + the sub-phases table
(E.4 row).

---

## File Structure

| Path | Role | Created / Modified |
|---|---|---|
| `src/palm/device/palmrecord_bridge.h` | `PilotRecord <-> PalmRecord` free functions | Create |
| `src/palm/device/palmrecord_bridge.cpp` | Bridge impl | Create |
| `src/palm/device/pilotlinkpalmdatabaseaccess.h` | Adapter header | Create |
| `src/palm/device/pilotlinkpalmdatabaseaccess.cpp` | Adapter impl | Create |
| `src/palm/device/CMakeLists.txt` | New static lib `WildPalmsPalmDevice` | Create |
| `src/CMakeLists.txt` | `add_subdirectory(palm/device)` | Modify |
| `tests/palmdevice/CMakeLists.txt` | Test target wiring | Create |
| `tests/palmdevice/mockkpilotlink.h` | In-memory `KPilotLink` test double | Create |
| `tests/palmdevice/mockkpilotlink.cpp` | | Create |
| `tests/palmdevice/tst_palmrecord_bridge.cpp` | Bridge unit tests | Create |
| `tests/palmdevice/tst_pilotlinkpalmdatabaseaccess.cpp` | Adapter unit tests | Create |
| `tests/palmdevice/tst_palmdevice_roundtrip.cpp` | End-to-end via PalmBackend + BlobSyncEngine | Create |
| `tests/CMakeLists.txt` | `add_subdirectory(palmdevice)` | Modify |
| `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` | Mark E.4 ✅ | Modify |

**Why `src/palm/device/`?** It's the natural sibling to
`src/palm/sync/` (the Kalburator-side lib from E.3). `device/` is where
pilot-link adaptation lives; `sync/` is where backend abstraction
lives. Both grow further as E.5+ lands.

---

## Task 1: PilotRecord ↔ PalmRecord bridge

**Files:**
- Create: `src/palm/device/palmrecord_bridge.h`
- Create: `src/palm/device/palmrecord_bridge.cpp`
- Create: `src/palm/device/CMakeLists.txt`
- Create: `tests/palmdevice/CMakeLists.txt`
- Create: `tests/palmdevice/tst_palmrecord_bridge.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the bridge header.**

File `src/palm/device/palmrecord_bridge.h`:

```cpp
#ifndef WILDPALMS_DEVICE_PALMRECORD_BRIDGE_H
#define WILDPALMS_DEVICE_PALMRECORD_BRIDGE_H

#include "pilotrecord.h"
#include "palmrecord.h"

namespace WildPalms::PalmDevice {

/// Convert pilot-link ::PilotRecord (WildPalmsCore) into
/// WildPalms::PalmSync::PalmRecord (WildPalmsPalmSync). lastModified
/// is left invalid because the Palm record layer does not carry a
/// per-record modification time; callers may stamp it from context
/// (e.g. the database header's modification time, or "now" for
/// freshly-read records).
WildPalms::PalmSync::PalmRecord fromPilotRecord(const PilotRecord &src);

/// Inverse: build a ::PilotRecord (ownership-free, caller-constructed)
/// from a PalmRecord. The returned PilotRecord is NOT heap-allocated;
/// callers that need the pilot-link DLP API's `PilotRecord *` shape
/// should wrap with `new PilotRecord(toPilotRecord(pr))`.
///
/// If src.recordId is 0, the returned PilotRecord's id is 0 — DLP
/// interprets this as "assign a new record ID on write."
PilotRecord toPilotRecord(const WildPalms::PalmSync::PalmRecord &src);

} // namespace WildPalms::PalmDevice

#endif // WILDPALMS_DEVICE_PALMRECORD_BRIDGE_H
```

- [ ] **Step 2: Write the bridge implementation.**

File `src/palm/device/palmrecord_bridge.cpp`:

```cpp
#include "palmrecord_bridge.h"

namespace WildPalms::PalmDevice {

WildPalms::PalmSync::PalmRecord fromPilotRecord(const PilotRecord &src)
{
    WildPalms::PalmSync::PalmRecord out;
    out.recordId   = static_cast<std::uint32_t>(src.recordId());
    out.category   = static_cast<std::uint8_t>(src.category() & 0x0F);
    out.attributes = static_cast<std::uint8_t>(src.attributes() & 0xFF);
    out.data       = src.data();
    // lastModified stays default-constructed; callers stamp as needed.
    return out;
}

PilotRecord toPilotRecord(const WildPalms::PalmSync::PalmRecord &src)
{
    return PilotRecord(
        static_cast<int>(src.recordId),
        static_cast<int>(src.category),
        static_cast<int>(src.attributes),
        src.data);
}

} // namespace WildPalms::PalmDevice
```

- [ ] **Step 3: Write the CMake target.**

File `src/palm/device/CMakeLists.txt`:

```cmake
# WildPalmsPalmDevice — pilot-link adapter bridging WildPalmsCore's
# PilotRecord / KPilotLink abstractions onto the Kalburator-side
# IPalmDatabaseAccess interface (WildPalmsPalmSync).
#
# Phase E.4 of the libkalburator integration. First library-graph
# convergence point between WildPalmsCore (pisock-linked) and
# WildPalmsPalmSync (Kalburator-linked). Transitively pulls both in.

add_library(WildPalmsPalmDevice STATIC
    palmrecord_bridge.h
    palmrecord_bridge.cpp
    pilotlinkpalmdatabaseaccess.h
    pilotlinkpalmdatabaseaccess.cpp
)

target_include_directories(WildPalmsPalmDevice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmDevice
    PUBLIC
        Qt::Core
        WildPalmsCore
        WildPalmsPalmSync
)

set_target_properties(WildPalmsPalmDevice PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

`pilotlinkpalmdatabaseaccess.{h,cpp}` are referenced here but filled
in by Task 3. Land placeholder files now so the CMake target builds:

```bash
cd ~/dev/WildPalms
cat > src/palm/device/pilotlinkpalmdatabaseaccess.h <<'EOF'
// Placeholder — filled in by Phase E.4 task 3.
#ifndef WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H
#define WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H
#endif
EOF
cat > src/palm/device/pilotlinkpalmdatabaseaccess.cpp <<'EOF'
// Placeholder — filled in by Phase E.4 task 3.
#include "pilotlinkpalmdatabaseaccess.h"
namespace { [[maybe_unused]] int wp_pilotlink_placeholder() { return 0; } }
EOF
```

- [ ] **Step 4: Hook into the src tree.**

Edit `src/CMakeLists.txt`. After the existing `add_subdirectory(palm/sync)`
(landed in E.3 task 1), add:

```cmake
# Pilot-link adapter for IPalmDatabaseAccess (Phase E.4 of libkalburator
# integration). Bridges WildPalmsCore (pisock) and WildPalmsPalmSync
# (Kalburator::Sync).
add_subdirectory(palm/device)
```

- [ ] **Step 5: Write the bridge unit tests.**

File `tests/palmdevice/tst_palmrecord_bridge.cpp`:

```cpp
#include <QtTest/QtTest>

#include "palmrecord_bridge.h"

using WildPalms::PalmDevice::fromPilotRecord;
using WildPalms::PalmDevice::toPilotRecord;
using WildPalms::PalmSync::PalmRecord;

class TestPalmRecordBridge : public QObject
{
    Q_OBJECT
private slots:
    void fromPilotPreservesFields();
    void toPilotPreservesFields();
    void roundTripIsIdentity();
    void fromPilotMasksCategoryTo4Bits();
};

void TestPalmRecordBridge::fromPilotPreservesFields()
{
    PilotRecord src(42, 3, PilotRecord::AttrDirty | PilotRecord::AttrSecret,
                    QByteArrayLiteral("payload"));
    const auto out = fromPilotRecord(src);
    QCOMPARE(out.recordId, 42u);
    QCOMPARE(out.category, static_cast<std::uint8_t>(3));
    QCOMPARE(out.attributes,
             static_cast<std::uint8_t>(
                 PalmRecord::AttrDirty | PalmRecord::AttrSecret));
    QCOMPARE(out.data, QByteArrayLiteral("payload"));
    QVERIFY(!out.lastModified.isValid());
}

void TestPalmRecordBridge::toPilotPreservesFields()
{
    PalmRecord src;
    src.recordId = 7;
    src.category = 11;
    src.attributes = PalmRecord::AttrArchived;
    src.data = QByteArrayLiteral("body");

    const auto out = toPilotRecord(src);
    QCOMPARE(out.recordId(), 7);
    QCOMPARE(out.category(), 11);
    QCOMPARE(out.attributes(),
             static_cast<int>(PalmRecord::AttrArchived));
    QCOMPARE(out.data(), QByteArrayLiteral("body"));
}

void TestPalmRecordBridge::roundTripIsIdentity()
{
    PilotRecord original(99, 5, PilotRecord::AttrDeleted,
                         QByteArrayLiteral("x"));
    const auto intermediate = fromPilotRecord(original);
    const auto back = toPilotRecord(intermediate);
    QCOMPARE(back.recordId(), original.recordId());
    QCOMPARE(back.category(), original.category());
    QCOMPARE(back.attributes(), original.attributes());
    QCOMPARE(back.data(), original.data());
}

void TestPalmRecordBridge::fromPilotMasksCategoryTo4Bits()
{
    // Palm category is 4 bits; higher bits should be masked off.
    PilotRecord src(1, 0xFF, 0, QByteArray());
    const auto out = fromPilotRecord(src);
    QCOMPARE(out.category, static_cast<std::uint8_t>(0x0F));
}

QTEST_MAIN(TestPalmRecordBridge)
#include "tst_palmrecord_bridge.moc"
```

- [ ] **Step 6: Write the tests CMakeLists.**

File `tests/palmdevice/CMakeLists.txt`:

```cmake
# Phase E.4 — PilotLink-backed PalmDatabaseAccess adapter tests.
# Link WildPalmsPalmDevice (transitively WildPalmsCore + pisock +
# WildPalmsPalmSync + Kalburator::Sync).

function(add_palm_device_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            WildPalmsPalmDevice
            pisock
            bluetooth
            usb
    )
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/palm
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_device_test(tst_palmrecord_bridge tst_palmrecord_bridge.cpp)
```

- [ ] **Step 7: Hook into the tests tree.**

Edit `tests/CMakeLists.txt`. After the existing
`add_subdirectory(palmsync)` line (landed in E.3 task 1), add:

```cmake
# ============================================================
# Phase E.4 — Pilot-link adapter for PalmBackend
# ============================================================

add_subdirectory(palmdevice)
```

- [ ] **Step 8: Configure + build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmrecord_bridge
```

Expected: 4 tests PASS.

- [ ] **Step 9: Full WP ctest.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure
```

Expected: 20/20 pass (19 pre-E.4 + new bridge test).

- [ ] **Step 10: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/device/ src/CMakeLists.txt \
        tests/palmdevice/ tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-device): scaffold WildPalmsPalmDevice + PilotRecord bridge

Phase E.4 task 1: new static library WildPalmsPalmDevice bridges
WildPalmsCore's pilot-link-linked PilotRecord onto WildPalmsPalmSync's
Kalburator-linked PalmRecord. Free functions fromPilotRecord() /
toPilotRecord() perform the round-trip-identity conversion.

First library-graph convergence between WildPalmsCore (pisock) and
WildPalmsPalmSync (Kalburator::Sync). WildPalmsCore itself is
untouched; the convergence is via the opt-in WildPalmsPalmDevice
consumer.

Four bridge unit tests: field preservation both directions,
round-trip identity, category 4-bit masking. Full WP ctest at
20/20.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: MockKPilotLink in-memory test double

**Files:**
- Create: `tests/palmdevice/mockkpilotlink.h`
- Create: `tests/palmdevice/mockkpilotlink.cpp`

The mock lives in `tests/palmdevice/` rather than `src/palm/device/`
because no production code needs it and keeping it test-only clarifies
intent. A follow-up can promote it to `src/palm/device/` if E.5+ grows
tests that also need it.

- [ ] **Step 1: Write the mock header.**

File `tests/palmdevice/mockkpilotlink.h`:

```cpp
#ifndef WILDPALMS_TESTS_MOCKKPILOTLINK_H
#define WILDPALMS_TESTS_MOCKKPILOTLINK_H

#include <QHash>

#include "kpilotlink.h"
#include "pilotrecord.h"

/**
 * @brief In-memory KPilotLink implementation for Phase E.4 tests.
 *
 * Tracks databases as per-DB record tables. Each database yields a
 * non-zero handle assigned at open time. Read methods return
 * heap-allocated PilotRecord*, matching KPilotLink's contract.
 *
 * Not thread-safe. Tests drive it from a single thread.
 */
class MockKPilotLink : public KPilotLink
{
    Q_OBJECT
public:
    explicit MockKPilotLink(QObject *parent = nullptr);
    ~MockKPilotLink() override;

    // Connection management
    bool openConnection() override;
    void closeConnection() override;
    LinkStatus status() const override { return m_status; }

    // User / sys info — stubbed for tests that don't need them.
    bool readUserInfo(struct PilotUser &user) override;
    bool writeUserInfo(const struct PilotUser &user) override;
    bool readSysInfo(struct SysInfo &sysInfo) override;
    bool readStorageInfo(int cardNo, struct CardInfo &cardInfo) override;

    // Database operations
    int openDatabase(const QString &dbName, bool readWrite = false) override;
    bool closeDatabase(int handle) override;
    QStringList listDatabases() override;

    // Record operations
    QList<PilotRecord*> readAllRecords(int dbHandle) override;
    PilotRecord* readRecordByIndex(int dbHandle, int index) override;
    PilotRecord* readRecordById(int dbHandle, int recordId) override;
    bool writeRecord(int dbHandle, PilotRecord *record) override;
    bool deleteRecord(int dbHandle, int recordId) override;
    QList<PilotRecord*> readModifiedRecords(int dbHandle) override;
    bool resetDBIndex(int dbHandle) override;

    // AppInfo
    bool readAppBlock(int dbHandle, unsigned char *buffer,
                      size_t *size) override;
    bool writeAppBlock(int dbHandle, const unsigned char *buffer,
                       size_t size) override;

    // Sync lifecycle
    bool beginSync() override { return true; }
    bool endSync() override { return true; }

    // State
    bool isConnected() const override { return m_connected; }
    bool cleanUpDatabase(int dbHandle) override;
    bool resetSyncFlags(int dbHandle) override;

    // --- Test helpers ---

    /// Create an empty database (fails if name already taken).
    bool seedDatabase(const QString &dbName);

    /// Seed a record directly (bypasses openDatabase). recordId
    /// must be non-zero and unique within the database.
    bool seedRecord(const QString &dbName, int recordId, int category,
                    int attributes, const QByteArray &data);

    /// Raw access for test assertions.
    bool hasRecord(const QString &dbName, int recordId) const;
    QByteArray recordData(const QString &dbName, int recordId) const;

private:
    struct Row {
        int         recordId;
        int         category;
        int         attributes;
        QByteArray  data;
    };

    struct Database {
        QString           name;
        QHash<int, Row>   rows;     // recordId -> Row
        QByteArray        appBlock;
        int               nextId = 1;
    };

    bool    m_connected = false;
    QHash<QString, Database> m_dbs;
    QHash<int, QString>      m_handles; // handle -> dbName
    int     m_nextHandle = 1;

    Database *dbForHandle(int handle);
    const Database *dbForHandle(int handle) const;
};

#endif // WILDPALMS_TESTS_MOCKKPILOTLINK_H
```

- [ ] **Step 2: Write the mock implementation.**

File `tests/palmdevice/mockkpilotlink.cpp`:

```cpp
#include "mockkpilotlink.h"

MockKPilotLink::MockKPilotLink(QObject *parent)
    : KPilotLink(parent)
{
}

MockKPilotLink::~MockKPilotLink() = default;

bool MockKPilotLink::openConnection()
{
    m_connected = true;
    setStatus(DeviceOpen);
    return true;
}

void MockKPilotLink::closeConnection()
{
    m_connected = false;
    setStatus(Init);
}

bool MockKPilotLink::readUserInfo(struct PilotUser &)       { return false; }
bool MockKPilotLink::writeUserInfo(const struct PilotUser &) { return false; }
bool MockKPilotLink::readSysInfo(struct SysInfo &)          { return false; }
bool MockKPilotLink::readStorageInfo(int, struct CardInfo &) { return false; }

int MockKPilotLink::openDatabase(const QString &dbName, bool /*readWrite*/)
{
    if (!m_dbs.contains(dbName)) return -1;
    const int h = m_nextHandle++;
    m_handles.insert(h, dbName);
    return h;
}

bool MockKPilotLink::closeDatabase(int handle)
{
    return m_handles.remove(handle) > 0;
}

QStringList MockKPilotLink::listDatabases()
{
    return QStringList(m_dbs.keyBegin(), m_dbs.keyEnd());
}

MockKPilotLink::Database *MockKPilotLink::dbForHandle(int handle)
{
    const auto it = m_handles.constFind(handle);
    if (it == m_handles.constEnd()) return nullptr;
    auto dbIt = m_dbs.find(*it);
    return dbIt == m_dbs.end() ? nullptr : &*dbIt;
}

const MockKPilotLink::Database *MockKPilotLink::dbForHandle(int handle) const
{
    const auto it = m_handles.constFind(handle);
    if (it == m_handles.constEnd()) return nullptr;
    const auto dbIt = m_dbs.constFind(*it);
    return dbIt == m_dbs.constEnd() ? nullptr : &*dbIt;
}

QList<PilotRecord*> MockKPilotLink::readAllRecords(int dbHandle)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db) return {};
    QList<PilotRecord*> out;
    out.reserve(db->rows.size());
    for (auto it = db->rows.constBegin(); it != db->rows.constEnd(); ++it) {
        out.append(new PilotRecord(it->recordId, it->category,
                                   it->attributes, it->data));
    }
    return out;
}

PilotRecord *MockKPilotLink::readRecordByIndex(int dbHandle, int index)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db) return nullptr;
    if (index < 0 || index >= db->rows.size()) return nullptr;
    auto it = db->rows.constBegin();
    std::advance(it, index);
    return new PilotRecord(it->recordId, it->category,
                           it->attributes, it->data);
}

PilotRecord *MockKPilotLink::readRecordById(int dbHandle, int recordId)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db) return nullptr;
    const auto it = db->rows.constFind(recordId);
    if (it == db->rows.constEnd()) return nullptr;
    return new PilotRecord(it->recordId, it->category,
                           it->attributes, it->data);
}

bool MockKPilotLink::writeRecord(int dbHandle, PilotRecord *record)
{
    Database *db = dbForHandle(dbHandle);
    if (!db || !record) return false;

    int id = record->recordId();
    if (id == 0) {
        id = db->nextId++;
        record->setRecordId(id);
    } else {
        db->nextId = std::max(db->nextId, id + 1);
    }
    Row row{id, record->category(), record->attributes(), record->data()};
    db->rows.insert(id, row);
    return true;
}

bool MockKPilotLink::deleteRecord(int dbHandle, int recordId)
{
    Database *db = dbForHandle(dbHandle);
    if (!db) return false;
    return db->rows.remove(recordId) > 0;
}

QList<PilotRecord*> MockKPilotLink::readModifiedRecords(int dbHandle)
{
    // No modified-flag bookkeeping in the scaffold mock; return all.
    return readAllRecords(dbHandle);
}

bool MockKPilotLink::resetDBIndex(int) { return true; }

bool MockKPilotLink::readAppBlock(int dbHandle, unsigned char *buffer,
                                  size_t *size)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db || !size) return false;
    const auto len = static_cast<size_t>(db->appBlock.size());
    if (*size < len) {
        *size = len;
        return false;
    }
    std::memcpy(buffer, db->appBlock.constData(), len);
    *size = len;
    return true;
}

bool MockKPilotLink::writeAppBlock(int dbHandle, const unsigned char *buffer,
                                   size_t size)
{
    Database *db = dbForHandle(dbHandle);
    if (!db) return false;
    db->appBlock = QByteArray(reinterpret_cast<const char *>(buffer),
                              static_cast<int>(size));
    return true;
}

bool MockKPilotLink::cleanUpDatabase(int) { return true; }
bool MockKPilotLink::resetSyncFlags(int)  { return true; }

bool MockKPilotLink::seedDatabase(const QString &dbName)
{
    if (m_dbs.contains(dbName)) return false;
    Database db;
    db.name = dbName;
    m_dbs.insert(dbName, db);
    return true;
}

bool MockKPilotLink::seedRecord(const QString &dbName, int recordId,
                                int category, int attributes,
                                const QByteArray &data)
{
    if (!m_dbs.contains(dbName)) return false;
    Database &db = m_dbs[dbName];
    if (db.rows.contains(recordId)) return false;
    Row row{recordId, category, attributes, data};
    db.rows.insert(recordId, row);
    db.nextId = std::max(db.nextId, recordId + 1);
    return true;
}

bool MockKPilotLink::hasRecord(const QString &dbName, int recordId) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return false;
    return it->rows.contains(recordId);
}

QByteArray MockKPilotLink::recordData(const QString &dbName,
                                      int recordId) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    const auto rIt = it->rows.constFind(recordId);
    if (rIt == it->rows.constEnd()) return {};
    return rIt->data;
}
```

The `#include <cstring>` needed for `std::memcpy` is implicit via Qt
headers on most toolchains; if the build complains, add it explicitly
at the top of the `.cpp`.

- [ ] **Step 3: Register the mock as a sibling compile target.**

Mock `.cpp` isn't a standalone test executable — tests compile it
in directly. Do NOT add a separate `add_palm_device_test` call for
the mock. Instead, later tests that need the mock will list its `.cpp`
alongside their own test source (same pattern as
`test_calendarmapper` linking `calendarmapper.cpp` from the plugin
source tree, per `tests/CMakeLists.txt:45-47`). Task 4 does this.

- [ ] **Step 4: Sanity-compile the mock via a trivial test.**

Add a minimal test that just constructs a `MockKPilotLink` and seeds
a database, proving the mock compiles in isolation. File
`tests/palmdevice/tst_mockkpilotlink.cpp`:

```cpp
#include <QtTest/QtTest>

#include "mockkpilotlink.h"

class TestMockKPilotLink : public QObject
{
    Q_OBJECT
private slots:
    void constructsAndConnects();
    void seedAndOpenDatabase();
    void seedAndReadRecord();
};

void TestMockKPilotLink::constructsAndConnects()
{
    MockKPilotLink link;
    QVERIFY(!link.isConnected());
    QVERIFY(link.openConnection());
    QVERIFY(link.isConnected());
    link.closeConnection();
    QVERIFY(!link.isConnected());
}

void TestMockKPilotLink::seedAndOpenDatabase()
{
    MockKPilotLink link;
    QVERIFY(link.seedDatabase(QStringLiteral("MemoDB")));
    QCOMPARE(link.listDatabases(),
             QStringList() << QStringLiteral("MemoDB"));

    const int h = link.openDatabase(QStringLiteral("MemoDB"));
    QVERIFY(h > 0);
    QVERIFY(link.closeDatabase(h));
    QVERIFY(!link.closeDatabase(h)); // second close fails
}

void TestMockKPilotLink::seedAndReadRecord()
{
    MockKPilotLink link;
    QVERIFY(link.seedDatabase(QStringLiteral("MemoDB")));
    QVERIFY(link.seedRecord(QStringLiteral("MemoDB"), 42, 1, 0,
                            QByteArrayLiteral("hello")));

    const int h = link.openDatabase(QStringLiteral("MemoDB"), true);
    QVERIFY(h > 0);
    auto *rec = link.readRecordById(h, 42);
    QVERIFY(rec);
    QCOMPARE(rec->recordId(), 42);
    QCOMPARE(rec->data(), QByteArrayLiteral("hello"));
    delete rec;
}

QTEST_MAIN(TestMockKPilotLink)
#include "tst_mockkpilotlink.moc"
```

Append to `tests/palmdevice/CMakeLists.txt`:

```cmake
add_palm_device_test(tst_mockkpilotlink
    tst_mockkpilotlink.cpp
    mockkpilotlink.cpp
)
```

- [ ] **Step 5: Build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_mockkpilotlink
ctest --test-dir build --output-on-failure -R tst_mockkpilotlink
```

Expected: 3 tests PASS.

- [ ] **Step 6: Commit.**

```bash
cd ~/dev/WildPalms
git add tests/palmdevice/mockkpilotlink.h tests/palmdevice/mockkpilotlink.cpp \
        tests/palmdevice/tst_mockkpilotlink.cpp \
        tests/palmdevice/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(palm-device): MockKPilotLink in-memory test double

Phase E.4 task 2: MockKPilotLink is a KPilotLink implementation
backed by per-database QHash<recordId, Row> tables. Supports the
full KPilotLink surface (open/close connection, database ops,
record CRUD, AppInfo block, stubbed user/sys info).

Lives in tests/palmdevice/ because no production code needs it.
Follow-up phases can promote it to src/palm/device/ if more test
suites need it.

Three compile+behaviour-sanity tests for the mock itself.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: PilotLinkPalmDatabaseAccess adapter

**Files:**
- Replace: `src/palm/device/pilotlinkpalmdatabaseaccess.h`
- Replace: `src/palm/device/pilotlinkpalmdatabaseaccess.cpp`

- [ ] **Step 1: Write the header.**

File `src/palm/device/pilotlinkpalmdatabaseaccess.h`:

```cpp
#ifndef WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H
#define WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H

#include "ipalmdatabaseaccess.h"

class KPilotLink;

namespace WildPalms::PalmDevice {

/**
 * @brief IPalmDatabaseAccess wrapping a KPilotLink.
 *
 * Per-operation open/close — each method resolves the database by
 * name, opens a DLP handle, runs one-or-two DLP calls, and closes.
 * Scaffold pattern; a production implementation should cache open
 * handles to avoid per-op overhead on real hardware.
 *
 * Does not own the KPilotLink. Caller is responsible for lifetime
 * (and for ensuring the link is connected before any method is
 * called).
 *
 * Semantic caveats versus the mock in WildPalmsPalmSync:
 *
 * - `supportsDeleteTracking()` returns false. The Palm DLP protocol
 *   has a "modified since last sync" flag (dlp_ReadNextModifiedRec)
 *   but not a per-record deletion log keyed by timestamp; the
 *   engine's baseline store (Phase B3) handles deletion detection.
 * - `recordsModifiedSince()` ignores the timestamp parameter and
 *   delegates to KPilotLink::readModifiedRecords(), which returns
 *   records with the Dirty attribute set. The engine treats the
 *   result as "candidate modifications"; the baseline compares
 *   content hashes for authoritative change detection.
 * - `recordsDeletedSince()` returns an empty list. The engine
 *   derives deletions from the baseline diff.
 * - `createDatabase()` returns false when the database does not
 *   already exist. Palm software rarely creates databases (apps do);
 *   adding dlp_CreateDB wiring is a follow-up.
 */
class PilotLinkPalmDatabaseAccess
    : public WildPalms::PalmSync::IPalmDatabaseAccess
{
public:
    explicit PilotLinkPalmDatabaseAccess(KPilotLink *link);
    ~PilotLinkPalmDatabaseAccess() override = default;

    QStringList availableDatabases() const override;
    bool hasDatabase(const QString &dbName) const override;
    bool createDatabase(const QString &dbName) override;

    QList<WildPalms::PalmSync::PalmRecord>
        readAllRecords(const QString &dbName) const override;
    std::optional<WildPalms::PalmSync::PalmRecord>
        readRecord(const QString &dbName,
                   std::uint32_t recordId) const override;

    std::uint32_t createRecord(
        const QString &dbName,
        const WildPalms::PalmSync::PalmRecord &record) override;
    bool updateRecord(
        const QString &dbName,
        const WildPalms::PalmSync::PalmRecord &record) override;
    bool deleteRecord(const QString &dbName,
                      std::uint32_t recordId) override;

    QList<WildPalms::PalmSync::PalmRecord>
        recordsModifiedSince(const QString &dbName,
                             const QDateTime &since) const override;
    QList<std::uint32_t>
        recordsDeletedSince(const QString &dbName,
                            const QDateTime &since) const override;
    bool supportsDeleteTracking() const override { return false; }

private:
    // Scope guard opens a database on construction, closes on
    // destruction. Handle is -1 if the open failed.
    class DbScope {
    public:
        DbScope(KPilotLink *link, const QString &dbName, bool rw);
        ~DbScope();
        int handle() const { return m_handle; }
        bool ok() const { return m_handle >= 0; }
    private:
        KPilotLink *m_link;
        int         m_handle;
    };

    KPilotLink *m_link = nullptr;
};

} // namespace WildPalms::PalmDevice

#endif // WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H
```

- [ ] **Step 2: Write the implementation.**

File `src/palm/device/pilotlinkpalmdatabaseaccess.cpp`:

```cpp
#include "pilotlinkpalmdatabaseaccess.h"

#include <memory>

#include "kpilotlink.h"
#include "palmrecord_bridge.h"
#include "pilotrecord.h"

namespace WildPalms::PalmDevice {

PilotLinkPalmDatabaseAccess::DbScope::DbScope(KPilotLink *link,
                                              const QString &dbName,
                                              bool rw)
    : m_link(link)
    , m_handle(link ? link->openDatabase(dbName, rw) : -1)
{
}

PilotLinkPalmDatabaseAccess::DbScope::~DbScope()
{
    if (m_link && m_handle >= 0) {
        m_link->closeDatabase(m_handle);
    }
}

PilotLinkPalmDatabaseAccess::PilotLinkPalmDatabaseAccess(KPilotLink *link)
    : m_link(link)
{
}

QStringList PilotLinkPalmDatabaseAccess::availableDatabases() const
{
    if (!m_link) return {};
    return m_link->listDatabases();
}

bool PilotLinkPalmDatabaseAccess::hasDatabase(const QString &dbName) const
{
    if (!m_link) return false;
    return m_link->listDatabases().contains(dbName);
}

bool PilotLinkPalmDatabaseAccess::createDatabase(const QString &dbName)
{
    // See header: Palm DB creation is not wired in the scaffold.
    // Treat as a no-op success for databases that already exist.
    return hasDatabase(dbName);
}

QList<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::readAllRecords(const QString &dbName) const
{
    if (!m_link) return {};
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return {};

    const auto raw = m_link->readAllRecords(scope.handle());
    QList<WildPalms::PalmSync::PalmRecord> out;
    out.reserve(raw.size());
    for (auto *rec : raw) {
        out.append(fromPilotRecord(*rec));
        delete rec;
    }
    return out;
}

std::optional<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::readRecord(const QString &dbName,
                                        std::uint32_t recordId) const
{
    if (!m_link) return std::nullopt;
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return std::nullopt;

    std::unique_ptr<PilotRecord> rec(
        m_link->readRecordById(scope.handle(),
                               static_cast<int>(recordId)));
    if (!rec) return std::nullopt;
    return fromPilotRecord(*rec);
}

std::uint32_t PilotLinkPalmDatabaseAccess::createRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return 0;
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return 0;

    PilotRecord bridged = toPilotRecord(record);
    if (!m_link->writeRecord(scope.handle(), &bridged)) return 0;
    return static_cast<std::uint32_t>(bridged.recordId());
}

bool PilotLinkPalmDatabaseAccess::updateRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return false;
    if (record.recordId == 0) return false;
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return false;

    PilotRecord bridged = toPilotRecord(record);
    return m_link->writeRecord(scope.handle(), &bridged);
}

bool PilotLinkPalmDatabaseAccess::deleteRecord(const QString &dbName,
                                               std::uint32_t recordId)
{
    if (!m_link) return false;
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return false;
    return m_link->deleteRecord(scope.handle(),
                                static_cast<int>(recordId));
}

QList<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::recordsModifiedSince(const QString &dbName,
                                                  const QDateTime &) const
{
    // `since` is ignored; DLP lacks per-record timestamps. The engine
    // falls back to baseline-based diff.
    if (!m_link) return {};
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return {};

    const auto raw = m_link->readModifiedRecords(scope.handle());
    QList<WildPalms::PalmSync::PalmRecord> out;
    out.reserve(raw.size());
    for (auto *rec : raw) {
        out.append(fromPilotRecord(*rec));
        delete rec;
    }
    return out;
}

QList<std::uint32_t>
PilotLinkPalmDatabaseAccess::recordsDeletedSince(const QString &,
                                                 const QDateTime &) const
{
    // See header: deletion tracking is baseline-based, not DLP-based.
    return {};
}

} // namespace WildPalms::PalmDevice
```

- [ ] **Step 3: Build.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target WildPalmsPalmDevice
```

Expected: clean build. No tests added yet; Task 4 covers them.

- [ ] **Step 4: Do NOT commit yet.** Paired with Task 4 so the adapter
  lands with its unit tests in one commit.

---

## Task 4: PilotLinkPalmDatabaseAccess unit tests

**Files:**
- Create: `tests/palmdevice/tst_pilotlinkpalmdatabaseaccess.cpp`
- Modify: `tests/palmdevice/CMakeLists.txt`

- [ ] **Step 1: Write the adapter tests.**

File `tests/palmdevice/tst_pilotlinkpalmdatabaseaccess.cpp`:

```cpp
#include <QtTest/QtTest>

#include "mockkpilotlink.h"
#include "pilotlinkpalmdatabaseaccess.h"

using WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPilotLinkPalmDatabaseAccess : public QObject
{
    Q_OBJECT
private slots:
    void availableDatabasesListsMockSeeded();
    void hasDatabaseAnswersMembership();
    void readAllRecordsRoundTripsBytes();
    void readRecordFindsSeededById();
    void readRecordReturnsNulloptWhenMissing();
    void createRecordAssignsIdAndPersists();
    void updateRecordWritesBack();
    void deleteRecordRemovesFromDevice();
    void supportsDeleteTrackingIsFalse();
    void recordsDeletedSinceIsEmpty();
};

void TestPilotLinkPalmDatabaseAccess::availableDatabasesListsMockSeeded()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedDatabase(QStringLiteral("DatebookDB"));

    PilotLinkPalmDatabaseAccess dev(&link);
    auto names = dev.availableDatabases();
    std::sort(names.begin(), names.end());
    QCOMPARE(names, QStringList()
             << QStringLiteral("DatebookDB")
             << QStringLiteral("MemoDB"));
}

void TestPilotLinkPalmDatabaseAccess::hasDatabaseAnswersMembership()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(dev.hasDatabase(QStringLiteral("MemoDB")));
    QVERIFY(!dev.hasDatabase(QStringLiteral("DatebookDB")));
}

void TestPilotLinkPalmDatabaseAccess::readAllRecordsRoundTripsBytes()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 1, 0, 0,
                    QByteArrayLiteral("first"));
    link.seedRecord(QStringLiteral("MemoDB"), 2, 3, 0,
                    QByteArrayLiteral("second"));

    PilotLinkPalmDatabaseAccess dev(&link);
    const auto recs = dev.readAllRecords(QStringLiteral("MemoDB"));
    QCOMPARE(recs.size(), 2);

    QByteArrayList payloads;
    for (const auto &r : recs) payloads.append(r.data);
    std::sort(payloads.begin(), payloads.end());
    QCOMPARE(payloads, QByteArrayList()
             << QByteArrayLiteral("first")
             << QByteArrayLiteral("second"));
}

void TestPilotLinkPalmDatabaseAccess::readRecordFindsSeededById()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 42, 0, 0,
                    QByteArrayLiteral("hello"));

    PilotLinkPalmDatabaseAccess dev(&link);
    const auto got = dev.readRecord(QStringLiteral("MemoDB"), 42);
    QVERIFY(got.has_value());
    QCOMPARE(got->recordId, 42u);
    QCOMPARE(got->data, QByteArrayLiteral("hello"));
}

void TestPilotLinkPalmDatabaseAccess::readRecordReturnsNulloptWhenMissing()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), 99).has_value());
    QVERIFY(!dev.readRecord(QStringLiteral("NoDB"), 1).has_value());
}

void TestPilotLinkPalmDatabaseAccess::createRecordAssignsIdAndPersists()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));

    PilotLinkPalmDatabaseAccess dev(&link);
    PalmRecord rec;
    rec.data = QByteArrayLiteral("new");

    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);
    QVERIFY(id > 0);
    QVERIFY(link.hasRecord(QStringLiteral("MemoDB"),
                           static_cast<int>(id)));
    QCOMPARE(link.recordData(QStringLiteral("MemoDB"),
                             static_cast<int>(id)),
             QByteArrayLiteral("new"));
}

void TestPilotLinkPalmDatabaseAccess::updateRecordWritesBack()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 7, 0, 0,
                    QByteArrayLiteral("orig"));

    PilotLinkPalmDatabaseAccess dev(&link);
    PalmRecord rec;
    rec.recordId = 7;
    rec.data = QByteArrayLiteral("updated");
    QVERIFY(dev.updateRecord(QStringLiteral("MemoDB"), rec));
    QCOMPARE(link.recordData(QStringLiteral("MemoDB"), 7),
             QByteArrayLiteral("updated"));

    // recordId==0 is rejected.
    PalmRecord zero;
    zero.data = QByteArrayLiteral("no");
    QVERIFY(!dev.updateRecord(QStringLiteral("MemoDB"), zero));
}

void TestPilotLinkPalmDatabaseAccess::deleteRecordRemovesFromDevice()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 7, 0, 0,
                    QByteArrayLiteral("gone"));

    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), 7));
    QVERIFY(!link.hasRecord(QStringLiteral("MemoDB"), 7));
}

void TestPilotLinkPalmDatabaseAccess::supportsDeleteTrackingIsFalse()
{
    MockKPilotLink link;
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(!dev.supportsDeleteTracking());
}

void TestPilotLinkPalmDatabaseAccess::recordsDeletedSinceIsEmpty()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(dev.recordsDeletedSince(QStringLiteral("MemoDB"),
                                    QDateTime()).isEmpty());
}

QTEST_MAIN(TestPilotLinkPalmDatabaseAccess)
#include "tst_pilotlinkpalmdatabaseaccess.moc"
```

- [ ] **Step 2: Register the test.**

Append to `tests/palmdevice/CMakeLists.txt`:

```cmake
add_palm_device_test(tst_pilotlinkpalmdatabaseaccess
    tst_pilotlinkpalmdatabaseaccess.cpp
    mockkpilotlink.cpp
)
```

- [ ] **Step 3: Build + test.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_pilotlinkpalmdatabaseaccess
ctest --test-dir build --output-on-failure -R tst_pilotlinkpalmdatabaseaccess
```

Expected: 10 tests PASS.

- [ ] **Step 4: Commit (paired with Task 3's adapter).**

```bash
cd ~/dev/WildPalms
git add src/palm/device/pilotlinkpalmdatabaseaccess.h \
        src/palm/device/pilotlinkpalmdatabaseaccess.cpp \
        tests/palmdevice/tst_pilotlinkpalmdatabaseaccess.cpp \
        tests/palmdevice/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-device): PilotLinkPalmDatabaseAccess adapter

Phase E.4 task 3: IPalmDatabaseAccess implementation that routes
operations through an abstract KPilotLink*. Per-op DbScope opens
and closes the database around each call; scaffold pattern, can be
optimised later with handle caching.

Semantic notes (captured in the header):
- supportsDeleteTracking() is false — DLP lacks a deletion log keyed
  by timestamp; engine's baseline (Phase B3) handles deletion detection.
- recordsModifiedSince() ignores timestamp, delegates to
  readModifiedRecords() (Dirty-flag-based).
- recordsDeletedSince() returns empty.
- createDatabase() is a no-op-success for existing DBs. dlp_CreateDB
  wiring is a follow-up phase.

Ten unit tests exercise the adapter against MockKPilotLink covering
availableDatabases / hasDatabase / readAllRecords / readRecord /
createRecord / updateRecord / deleteRecord / supportsDeleteTracking /
recordsDeletedSince.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: End-to-end: PilotLink adapter + PalmBackend + BlobSyncEngine round-trip

**Files:**
- Create: `tests/palmdevice/tst_palmdevice_roundtrip.cpp`
- Modify: `tests/palmdevice/CMakeLists.txt`

Proves the full chain compiles and works: `MockKPilotLink` →
`PilotLinkPalmDatabaseAccess` → `PalmBackend` →
`BlobSyncEngine::twoWayWithBaseline` → `MockBlobBackend`. Complements
E.3's `tst_palmbackend_roundtrip` (which used the
`MockPalmDatabaseAccess` instead of the real pilot-link path).

- [ ] **Step 1: Write the round-trip test.**

File `tests/palmdevice/tst_palmdevice_roundtrip.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"
#include "blobsyncengine.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictstore.h"
#include "mockblobbackend.h"

#include "mockkpilotlink.h"
#include "palmbackend.h"
#include "pilotlinkpalmdatabaseaccess.h"

using Kalburator::Sync::BlobBaselineStore;
using Kalburator::Sync::BlobSyncEngine;
using Kalburator::Sync::BlobSyncResult;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;
using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictStore;
using WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

class TestPalmDeviceRoundTrip : public QObject
{
    Q_OBJECT
private slots:
    void palmSideRecordPropagatesToMockBlobBackend();
    void deletionOnPalmSidePropagatesViaBaseline();

private:
    static QString dbPathIn(const QTemporaryDir &dir)
    {
        return dir.filePath(QStringLiteral(".planstan-sync.db"));
    }

    static CollectionInfo mockBlobCollection(const QString &id)
    {
        CollectionInfo info;
        info.id = id;
        info.name = id;
        info.type = QStringLiteral("memos");
        return info;
    }
};

void TestPalmDeviceRoundTrip::palmSideRecordPropagatesToMockBlobBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 11, 0, 0,
                    QByteArrayLiteral("via-pilotlink"));

    PilotLinkPalmDatabaseAccess dev(&link);
    PalmBackend palm(&dev);

    MockBlobBackend mock;
    mock.createCollection(mockBlobCollection(QStringLiteral("palm:memo")));

    BlobBaselineStore baseline(dbPathIn(dir));
    QVERIFY(baseline.isOpen());
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e4-rt1"),
        &baseline, &reg, &store, policy);
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    const auto mockRecs = mock.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(mockRecs.size(), 1);
    QCOMPARE(mockRecs.first().data, QByteArrayLiteral("via-pilotlink"));
}

void TestPalmDeviceRoundTrip::deletionOnPalmSidePropagatesViaBaseline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 21, 0, 0,
                    QByteArrayLiteral("to-be-deleted"));

    PilotLinkPalmDatabaseAccess dev(&link);
    PalmBackend palm(&dev);

    MockBlobBackend mock;
    mock.createCollection(mockBlobCollection(QStringLiteral("palm:memo")));

    BlobBaselineStore baseline(dbPathIn(dir));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;
    BlobSyncEngine engine;

    // First sync: populate baseline + propagate to mock.
    BlobSyncResult r1 = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e4-del"),
        &baseline, &reg, &store, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QCOMPARE(mock.loadRecords(QStringLiteral("palm:memo")).size(), 1);

    // Delete on the Palm side via the adapter path.
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), 21));

    // Second sync: baseline sees the deletion.
    BlobSyncResult r2 = engine.twoWayWithBaseline(
        &palm, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e4-del"),
        &baseline, &reg, &store, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QCOMPARE(r2.targetStats.deleted, 1);
    QCOMPARE(mock.loadRecords(QStringLiteral("palm:memo")).size(), 0);
}

QTEST_MAIN(TestPalmDeviceRoundTrip)
#include "tst_palmdevice_roundtrip.moc"
```

- [ ] **Step 2: Register the test.**

Append to `tests/palmdevice/CMakeLists.txt`:

```cmake
add_palm_device_test(tst_palmdevice_roundtrip
    tst_palmdevice_roundtrip.cpp
    mockkpilotlink.cpp
)
```

- [ ] **Step 3: Build + test.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_palmdevice_roundtrip
ctest --test-dir build --output-on-failure -R tst_palmdevice_roundtrip
```

Expected: 2 tests PASS.

- [ ] **Step 4: Full WP ctest.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. 23 total (19 pre-E.4 + `tst_palmrecord_bridge`
+ `tst_mockkpilotlink` + `tst_pilotlinkpalmdatabaseaccess` +
`tst_palmdevice_roundtrip`).

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/WildPalms
git add tests/palmdevice/tst_palmdevice_roundtrip.cpp \
        tests/palmdevice/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(palm-device): end-to-end via PilotLink adapter + PalmBackend

Phase E.4 task 4: mirrors Phase E.3's round-trip test but routes the
Palm side through PilotLinkPalmDatabaseAccess + MockKPilotLink rather
than through MockPalmDatabaseAccess directly. Proves the full chain
compiles and propagates records + deletions through the engine.

Two scenarios: fresh record propagates MockKPilotLink -> adapter ->
PalmBackend -> engine -> MockBlobBackend; delete-on-palm propagates
via baseline on the second sync.

Full WP ctest at 23/23.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Mark E.4 done in the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`

- [ ] **Step 1: Update the sub-phases table.**

In the sub-phases table, change the E.4 row from:

```markdown
| **E.4** | `PalmBackend` wired to real pilot-link. Reads/writes live Datebook/AddressDB/MemoDB/ToDoDB via DLP. `src/palm/codecs/` relocated from plugin mappers. | WP | E.3 | WP ctest passes; manual smoke test against POSE64 emulator optional. |
```

To:

```markdown
| ✅ **E.4** | `PalmBackend` wired to real pilot-link via new `PilotLinkPalmDatabaseAccess` adapter over the existing abstract `KPilotLink` interface. New static lib `WildPalmsPalmDevice` at `src/palm/device/` houses adapter + `PilotRecord <-> PalmRecord` bridge. Tests use a new `MockKPilotLink` test double (KPilotLink is already abstract). Codec relocation deferred to E.6/E.7 when concrete consumers land. Live-device integration test deferred to E.18 (POSE64 sandbox). Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e4-palm-backend-pilot-link.md`. | WP | E.3 | WP ctest passes; adapter round-trips records through `BlobSyncEngine::twoWayWithBaseline` (tst_palmdevice_roundtrip). |
```

- [ ] **Step 2: Commit.**

```bash
cd ~/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "$(cat <<'EOF'
docs(phase-e): mark E.4 landed in spec sub-phases table

PilotLinkPalmDatabaseAccess adapter landed 2026-04-21. New static
lib WildPalmsPalmDevice bridges pilot-link and Kalburator sides.
Live-device integration test deferred to E.18; codec relocation
deferred to E.6/E.7.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

Spec coverage (Phase-E spec E.4 row):
- "`PalmBackend` wired to real pilot-link" — Task 3. ✓
- "Reads/writes live Datebook/AddressDB/MemoDB/ToDoDB via DLP" —
  DLP wiring via KPilotLink abstraction. ✓ (Live-device testing
  deferred to E.18 per plan scope-not-in-E.4 list.)
- "`src/palm/codecs/` relocated from plugin mappers" — explicitly
  deferred to E.6/E.7 per plan scope-not-in-E.4 list. The spec row
  will be updated on E.4 landing to note this deferral.
- "WP ctest passes" — Task 5 exit gate. ✓
- "manual smoke test against POSE64 emulator optional" — not done in
  E.4; E.18 covers it. ✓ (optional per spec)

Deferred explicitly:
- Codec relocation (E.6 / E.7 when consumers need them).
- Live-device integration test (E.18).
- App-layer runtime wiring (E.16).
- Handle-caching optimization (follow-up).
- `dlp_CreateDB` wiring (follow-up).

Placeholder scan: no TBD / TODO / FIXME in any task body. Every code
block is complete and compilable. Task 1 Step 3's placeholder-file
creation via `cat > file <<EOF` is explicit.

Type consistency:
- `PilotRecord` (::PilotRecord, global namespace, from WildPalmsCore)
  and `PalmRecord` (`WildPalms::PalmSync::PalmRecord`, from
  WildPalmsPalmSync) are consistently namespaced across the bridge
  header, impl, and tests.
- `KPilotLink` abstract class usage matches its actual interface
  (verified against `src/palm/kpilotlink.h`): openDatabase signature
  (`QString &, bool readWrite = false` -> `int`), closeDatabase
  (`int` -> `bool`), readAllRecords (`int` -> `QList<PilotRecord*>`),
  readRecordById (`int, int` -> `PilotRecord*`), writeRecord
  (`int, PilotRecord*` -> `bool`), deleteRecord (`int, int` -> `bool`),
  listDatabases (`QStringList`), readModifiedRecords
  (`int` -> `QList<PilotRecord*>`).
- `IPalmDatabaseAccess` method overrides match its header exactly
  (verified against `src/palm/sync/ipalmdatabaseaccess.h`): all method
  signatures, return types, and const-ness preserved.
- `MockKPilotLink` implements all pure-virtual methods of `KPilotLink`
  — verified against `src/palm/kpilotlink.h`'s 21 virtuals.

No gaps detected.

---

## Follow-up plans

After this plan lands:

- **E.5** — `PalmConflictHandler` + `PalmBackendConfig` +
  `ConnectionBehavior`. Registers with `ConflictHandlerRegistry`.
  Drafted after E.4 executes.
- **E.6** — `PalmCalendarBackend : SyncBackend` with virtual
  sub-calendars per category slot. First consumer of `DatebookCodec`;
  codec relocation happens here.
- **E.7** — typed adapters for contacts / memos / todos with codec
  relocation for their record formats.
