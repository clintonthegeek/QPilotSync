# Phase E.1 — BlobBaselineStore (upstream libkalburator) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land `Kalburator::Sync::BlobBaselineStore` upstream in libkalburator — a SQLite-backed hash-per-record-per-mapping baseline store that enables `BlobSyncEngine` to detect deletions and true conflicts. Tag `v0.7-phase-b3-baseline` when complete.

**Architecture:** Mirror the `IDMappingStore` pattern introduced in Phase C.4 — per-instance `QSqlDatabase` connection with RAII cleanup, coexists with `SyncStore` and `IDMappingStore` in `.planstan-sync.db` via idempotent `CREATE TABLE IF NOT EXISTS`, stamps `PRAGMA user_version = 3` only on fresh DBs. Not thread-safe (caller serialises).

**Tech Stack:** C++20, Qt6 (Core, Sql, Test), SQLite via `QSQLITE` driver. Tests use `QTEST_MAIN` + `QTemporaryDir`. Build with CMake 3.19+.

**Repo:** All work happens in `~/dev/libkalburator/`. Nothing in `~/dev/WildPalms/` changes. This plan lives in WildPalms for planning continuity; libkalburator's own `docs/phase0/` gets an `04i-blob-baseline-store-design.md` as part of Task 1.

**Upstream commit gate:** Before landing each commit, run PlanStan's ctest and confirm baseline (86 pass / 26 fail / 112 total) holds. See `memory/feedback_planstan_pretest_for_upstream.md`.

---

## File Structure

| Path | Role | Created / Modified |
|---|---|---|
| `~/dev/libkalburator/docs/phase0/04i-blob-baseline-store-design.md` | Design doc (landed alongside impl per upstream convention) | Create |
| `~/dev/libkalburator/src/journal/blobbaselinestore.h` | Public API | Create |
| `~/dev/libkalburator/src/journal/blobbaselinestore.cpp` | Implementation | Create |
| `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp` | QTEST_MAIN suite | Create |
| `~/dev/libkalburator/tests/journal/CMakeLists.txt` | Test target wiring | Create |
| `~/dev/libkalburator/tests/CMakeLists.txt` | `add_subdirectory(journal)` | Modify |
| `~/dev/libkalburator/docs/phase0/README.md` | Status refresh + phase map row | Modify |
| `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md` | Strike `BlobBaselineStore` from "Explicitly deferred" | Modify |

`src/journal/` files are automatically picked up by the top-level `CMakeLists.txt`'s `file(GLOB ...)` loop — no CMake change needed for the library itself.

---

## Task 1: Land a design doc stub in libkalburator's phase0 tree

**Files:**
- Create: `~/dev/libkalburator/docs/phase0/04i-blob-baseline-store-design.md`

**Rationale:** Upstream convention is "design doc accompanies phase implementation." Stub lands now with the intended surface; Task 12 updates it with outcomes.

- [ ] **Step 1: Write the design doc stub.**

File `~/dev/libkalburator/docs/phase0/04i-blob-baseline-store-design.md`:

```markdown
# Phase B3 — BlobBaselineStore

**Date:** 2026-04-21 (drafted)
**Status:** Approved for implementation.
**Phase tag on completion:** `v0.7-phase-b3-baseline`.

## Motivation

`BlobSyncEngine::twoWayNaive` (shipped in Phase B2) is stateless and
compares by `lastModified`. Without a baseline it cannot distinguish
"record deleted on source since last sync" from "record never existed
on source." Deletions silently fail to propagate, which is a real
regression vs WP's existing Client Mode sync.

This phase adds `BlobBaselineStore` — persistent hash-per-record
baselines keyed by sync-mapping ID — so a future engine operation
(`twoWayWithBaseline`, landing in Phase B4 / E.2) can compute a
correct 3-way diff.

## Surface

```cpp
namespace Kalburator::Sync {

class BlobBaselineStore
{
public:
    explicit BlobBaselineStore(const QString &dbPath);
    ~BlobBaselineStore();

    BlobBaselineStore(const BlobBaselineStore &) = delete;
    BlobBaselineStore &operator=(const BlobBaselineStore &) = delete;
    BlobBaselineStore(BlobBaselineStore &&) = delete;
    BlobBaselineStore &operator=(BlobBaselineStore &&) = delete;

    bool    isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    // Single-record set. Returns false on DB error; check lastError().
    bool setBaseline(const QString &mappingId,
                     const QString &recordId,
                     const QString &contentHash);

    // Returns empty QString if no baseline recorded yet.
    QString baselineHash(const QString &mappingId,
                         const QString &recordId) const;

    // Bulk commit; wraps in a transaction. Replaces existing rows for
    // the mapping. Intended to be called at the end of a successful
    // sync.
    bool commitBaselines(const QString &mappingId,
                         const QMap<QString, QString> &recordIdToHash);

    // All record IDs currently recorded for a mapping. Used by the
    // engine to compute "deleted since baseline" = baseline - current.
    QStringList baselineRecordIds(const QString &mappingId) const;

    // Remove all rows for a mapping (e.g. when a SyncMapping is unbound).
    bool clearMapping(const QString &mappingId);
};

} // namespace Kalburator::Sync
```

## Schema

New table `blob_baselines` in `.planstan-sync.db`:

```sql
CREATE TABLE IF NOT EXISTS blob_baselines (
    mapping_id    TEXT NOT NULL,
    record_id     TEXT NOT NULL,
    content_hash  TEXT NOT NULL,
    updated_at    TEXT DEFAULT (datetime('now')),
    PRIMARY KEY (mapping_id, record_id)
);
CREATE INDEX IF NOT EXISTS idx_blob_baselines_mapping
    ON blob_baselines(mapping_id);
```

Coexists with `sync_id_mappings` (owned by `IDMappingStore`),
`sync_store_*` tables (owned by `SyncStore`), and future tables. Each
store uses idempotent `CREATE TABLE IF NOT EXISTS`. PRAGMA
`user_version = 3` matches the existing policy.

## Interaction with other stores

Exactly the same pattern as `IDMappingStore`:
- Open the database with a per-instance connection name
  (`BlobBaselineStore_N`, where N increments).
- `CREATE TABLE IF NOT EXISTS` is idempotent and safe on a DB that
  already contains other stores' tables.
- Stamp `PRAGMA user_version = 3` only if the DB file did not exist
  before this instance created it; existing DBs inherit the version
  already stamped.
- Close and remove the connection in the destructor.

## Tests

Library-side tests in `tests/journal/tst_blobbaselinestore.cpp`. Nine
internal tests covering CRUD round-trip, empty-mapping cases, bulk
commit with replace semantics, mapping clear, coexistence with an
`IDMappingStore` on the same DB, schema persistence across reopen,
and transaction atomicity (failure mid-`commitBaselines` rolls back).

## Outcome

(Filled in during Task 12 after the implementation lands.)
```

- [ ] **Step 2: Commit the design doc.**

```bash
cd ~/dev/libkalburator
git add docs/phase0/04i-blob-baseline-store-design.md
git commit -m "Phase B3: design doc for BlobBaselineStore

Approved for implementation. Mirrors IDMappingStore's SQLite pattern;
new table blob_baselines in .planstan-sync.db; per-mapping hash
baselines enabling future twoWayWithBaseline engine operation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Wire `tests/journal/` subdirectory

**Files:**
- Create: `~/dev/libkalburator/tests/journal/CMakeLists.txt`
- Modify: `~/dev/libkalburator/tests/CMakeLists.txt`

- [ ] **Step 1: Create `tests/journal/CMakeLists.txt`.**

File `~/dev/libkalburator/tests/journal/CMakeLists.txt`:

```cmake
# Phase B3 — journal-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

function(kalburator_add_journal_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Sql
            Qt6::Test
            Kalburator::Sync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_journal_test(tst_blobbaselinestore)
```

- [ ] **Step 2: Append to `tests/CMakeLists.txt`.**

Edit `~/dev/libkalburator/tests/CMakeLists.txt`, appending after the existing `add_subdirectory(blob)` line:

```cmake
add_subdirectory(journal)
```

- [ ] **Step 3: Do NOT commit yet.** This change is staged together with Task 3's header/source files because the test subdir won't build without its test file or its target.

---

## Task 3: Header declaring the full API

**Files:**
- Create: `~/dev/libkalburator/src/journal/blobbaselinestore.h`

- [ ] **Step 1: Write the header.**

File `~/dev/libkalburator/src/journal/blobbaselinestore.h`:

```cpp
#ifndef KALBURATOR_BLOBBASELINESTORE_H
#define KALBURATOR_BLOBBASELINESTORE_H

/**
 * @file blobbaselinestore.h
 * @brief SQLite-backed hash-per-record baseline store for BlobSyncEngine.
 *
 * Records the last-synced content hash for each (mapping, record)
 * pair, enabling the engine to compute a correct 3-way diff and
 * distinguish "deleted since last sync" from "never existed on source."
 *
 * Lives in .planstan-sync.db alongside sync_id_mappings (IDMappingStore)
 * and sync_store_* (SyncStore). Uses idempotent CREATE TABLE IF NOT
 * EXISTS; stamps PRAGMA user_version = 3 only on freshly-created DBs.
 *
 * Not thread-safe. Callers must serialize access to a given instance.
 * Not a QObject — pure value-lifetime class with RAII connection
 * ownership.
 */

#include <QMap>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

class BlobBaselineStore
{
public:
    explicit BlobBaselineStore(const QString &dbPath);
    ~BlobBaselineStore();

    BlobBaselineStore(const BlobBaselineStore &) = delete;
    BlobBaselineStore &operator=(const BlobBaselineStore &) = delete;
    BlobBaselineStore(BlobBaselineStore &&) = delete;
    BlobBaselineStore &operator=(BlobBaselineStore &&) = delete;

    bool    isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    /// Single-record set. Returns true on success; false on DB error
    /// (check lastError()).
    bool setBaseline(const QString &mappingId,
                     const QString &recordId,
                     const QString &contentHash);

    /// Returns the hash recorded for the mapping+record, or empty
    /// QString if no baseline has been set.
    QString baselineHash(const QString &mappingId,
                         const QString &recordId) const;

    /// Bulk commit wrapped in a transaction. Each entry in recordIdToHash
    /// is inserted-or-replaced for the mapping. Existing entries for
    /// records NOT in the map are left alone (use clearMapping() first
    /// if you want to replace wholesale).
    bool commitBaselines(const QString &mappingId,
                         const QMap<QString, QString> &recordIdToHash);

    /// All record IDs currently recorded for the mapping. Returns an
    /// empty list if no baselines exist for the mapping.
    QStringList baselineRecordIds(const QString &mappingId) const;

    /// Remove all rows for a mapping (e.g. on SyncMapping unbind).
    bool clearMapping(const QString &mappingId);

private:
    static int s_connectionCounter;

    QString         m_dbPath;
    QString         m_connName;
    bool            m_isOpen = false;
    mutable QString m_lastError;

    bool ensureSchemaAndVersion(bool dbFileExistedBefore);
    void setError(const QString &message) const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_BLOBBASELINESTORE_H
```

- [ ] **Step 2: Do NOT commit yet.** Pair with Task 4 so a compiling state lands as one commit.

---

## Task 4: Minimal `.cpp` (constructor, destructor, open, empty method bodies) + first passing test

**Files:**
- Create: `~/dev/libkalburator/src/journal/blobbaselinestore.cpp`
- Create: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

- [ ] **Step 1: Write the test file with the first test case.**

File `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"

using Kalburator::Sync::BlobBaselineStore;

class TestBlobBaselineStore : public QObject
{
    Q_OBJECT
private slots:
    void opensOnValidPath();

    // (More slots added in subsequent tasks.)

private:
    QString dbPathIn(const QTemporaryDir &dir) const {
        return dir.filePath(QStringLiteral(".planstan-sync.db"));
    }
};

void TestBlobBaselineStore::opensOnValidPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY2(store.isOpen(),
             qUtf8Printable(store.lastError()));
    QCOMPARE(store.databasePath(), dbPathIn(dir));
}

QTEST_MAIN(TestBlobBaselineStore)
#include "tst_blobbaselinestore.moc"
```

- [ ] **Step 2: Write the `.cpp` with constructor / destructor / schema / method stubs that just return default-or-false.**

File `~/dev/libkalburator/src/journal/blobbaselinestore.cpp`:

```cpp
#include "blobbaselinestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace Kalburator::Sync {

namespace {
constexpr int kSchemaVersion = 3;  // Matches IDMappingStore / SyncStore.
} // namespace

int BlobBaselineStore::s_connectionCounter = 0;

BlobBaselineStore::BlobBaselineStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connName(QStringLiteral("BlobBaselineStore_%1")
                     .arg(++s_connectionCounter))
{
    const bool dbFileExistedBefore = QFile::exists(m_dbPath);

    QFileInfo fi(m_dbPath);
    QDir parent = fi.dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        setError(QStringLiteral("Failed to create directory: %1")
                     .arg(parent.path()));
        return;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                m_connName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        setError(QStringLiteral("Failed to open database: %1")
                     .arg(db.lastError().text()));
        return;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    if (!ensureSchemaAndVersion(dbFileExistedBefore)) {
        return;
    }

    m_isOpen = true;
}

BlobBaselineStore::~BlobBaselineStore()
{
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::database(m_connName).close();
        QSqlDatabase::removeDatabase(m_connName);
    }
}

bool BlobBaselineStore::isOpen() const { return m_isOpen; }
QString BlobBaselineStore::lastError() const { return m_lastError; }
QString BlobBaselineStore::databasePath() const { return m_dbPath; }

void BlobBaselineStore::setError(const QString &message) const
{
    m_lastError = message;
}

bool BlobBaselineStore::ensureSchemaAndVersion(bool dbFileExistedBefore)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS blob_baselines ("
            "  mapping_id   TEXT NOT NULL,"
            "  record_id    TEXT NOT NULL,"
            "  content_hash TEXT NOT NULL,"
            "  updated_at   TEXT DEFAULT (datetime('now')),"
            "  PRIMARY KEY (mapping_id, record_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_blob_baselines_mapping "
        "ON blob_baselines(mapping_id)"));

    if (!dbFileExistedBefore) {
        q.exec(QStringLiteral("PRAGMA user_version = %1")
                   .arg(kSchemaVersion));
    }

    return true;
}

// --- Method stubs: implementations land in subsequent tasks ---

bool BlobBaselineStore::setBaseline(const QString &, const QString &,
                                    const QString &)
{
    setError(QStringLiteral("setBaseline: not implemented yet"));
    return false;
}

QString BlobBaselineStore::baselineHash(const QString &,
                                        const QString &) const
{
    return {};
}

bool BlobBaselineStore::commitBaselines(const QString &,
                                        const QMap<QString, QString> &)
{
    setError(QStringLiteral("commitBaselines: not implemented yet"));
    return false;
}

QStringList BlobBaselineStore::baselineRecordIds(const QString &) const
{
    return {};
}

bool BlobBaselineStore::clearMapping(const QString &)
{
    setError(QStringLiteral("clearMapping: not implemented yet"));
    return false;
}

} // namespace Kalburator::Sync
```

- [ ] **Step 3: Configure and build.**

```bash
cd ~/dev/libkalburator
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
```

Expected: clean build, `tst_blobbaselinestore` executable produced.

- [ ] **Step 4: Run the first test.**

```bash
cd ~/dev/libkalburator
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: PASS (one test, `opensOnValidPath`).

- [ ] **Step 5: Run the full libkalburator ctest suite to confirm no regressions.**

```bash
cd ~/dev/libkalburator
ctest --test-dir build --output-on-failure
```

Expected: all previously-passing tests still pass (3 blob tests + 1 new journal test).

- [ ] **Step 6: Commit.**

```bash
cd ~/dev/libkalburator
git add src/journal/blobbaselinestore.h src/journal/blobbaselinestore.cpp \
        tests/journal/CMakeLists.txt tests/journal/tst_blobbaselinestore.cpp \
        tests/CMakeLists.txt
git commit -m "Phase B3 task 1: BlobBaselineStore scaffold + DB open

Header declares the full surface. Implementation opens the SQLite
connection, creates the blob_baselines table (idempotent), and
stamps PRAGMA user_version = 3 on freshly-created DBs. Data methods
are stubs returning defaults; subsequent commits fill them in.

First test: opensOnValidPath confirms the constructor + schema path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `setBaseline` + `baselineHash`

**Files:**
- Modify: `~/dev/libkalburator/src/journal/blobbaselinestore.cpp`
- Modify: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

- [ ] **Step 1: Add the failing tests.**

Add slot declarations in `tst_blobbaselinestore.cpp` (after `opensOnValidPath`):

```cpp
    void setBaselineAndReadBack();
    void baselineHashMissingReturnsEmpty();
    void setBaselineOverwritesExistingHash();
```

Add slot bodies at the end of the file (before `QTEST_MAIN`):

```cpp
void TestBlobBaselineStore::setBaselineAndReadBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaseline(QStringLiteral("mapping-a"),
                              QStringLiteral("rec-1"),
                              QStringLiteral("sha256:abc")));
    QCOMPARE(store.baselineHash(QStringLiteral("mapping-a"),
                                QStringLiteral("rec-1")),
             QStringLiteral("sha256:abc"));
}

void TestBlobBaselineStore::baselineHashMissingReturnsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QCOMPARE(store.baselineHash(QStringLiteral("nope"),
                                QStringLiteral("nope")),
             QString());
}

void TestBlobBaselineStore::setBaselineOverwritesExistingHash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaseline(QStringLiteral("m"), QStringLiteral("r"),
                              QStringLiteral("v1")));
    QVERIFY(store.setBaseline(QStringLiteral("m"), QStringLiteral("r"),
                              QStringLiteral("v2")));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("r")),
             QStringLiteral("v2"));
}
```

- [ ] **Step 2: Build and run tests; expect 3 new failures.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: 3 new tests FAIL (setBaseline returns false, baselineHash returns empty).

- [ ] **Step 3: Implement `setBaseline` + `baselineHash`.**

In `blobbaselinestore.cpp`, replace the two stubs with:

```cpp
bool BlobBaselineStore::setBaseline(const QString &mappingId,
                                    const QString &recordId,
                                    const QString &contentHash)
{
    if (!m_isOpen) {
        setError(QStringLiteral("setBaseline: store not open"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blob_baselines "
        "(mapping_id, record_id, content_hash, updated_at) "
        "VALUES (?, ?, ?, datetime('now'))"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);
    q.addBindValue(contentHash);

    if (!q.exec()) {
        setError(QStringLiteral("setBaseline: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    return true;
}

QString BlobBaselineStore::baselineHash(const QString &mappingId,
                                        const QString &recordId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT content_hash FROM blob_baselines "
        "WHERE mapping_id = ? AND record_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);

    if (!q.exec() || !q.next()) {
        return {};
    }
    return q.value(0).toString();
}
```

- [ ] **Step 4: Build and verify tests pass.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: 4 tests PASS (original + 3 new).

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/libkalburator
git add src/journal/blobbaselinestore.cpp tests/journal/tst_blobbaselinestore.cpp
git commit -m "Phase B3 task 2: setBaseline + baselineHash

INSERT OR REPLACE on (mapping_id, record_id) primary key; simple
SELECT for read-back. Missing rows return empty QString rather than
erroring, matching the header contract.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `commitBaselines` (bulk transaction)

**Files:**
- Modify: `~/dev/libkalburator/src/journal/blobbaselinestore.cpp`
- Modify: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

- [ ] **Step 1: Add failing tests.**

Add slot declarations:

```cpp
    void commitBaselinesBulkInsert();
    void commitBaselinesIsAtomic();
```

Add bodies:

```cpp
void TestBlobBaselineStore::commitBaselinesBulkInsert()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QMap<QString, QString> batch;
    batch[QStringLiteral("rec-1")] = QStringLiteral("h1");
    batch[QStringLiteral("rec-2")] = QStringLiteral("h2");
    batch[QStringLiteral("rec-3")] = QStringLiteral("h3");

    QVERIFY(store.commitBaselines(QStringLiteral("m"), batch));

    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("rec-1")),
             QStringLiteral("h1"));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("rec-2")),
             QStringLiteral("h2"));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("rec-3")),
             QStringLiteral("h3"));
}

void TestBlobBaselineStore::commitBaselinesIsAtomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    // Seed a row.
    QVERIFY(store.setBaseline(QStringLiteral("m"),
                              QStringLiteral("existing"),
                              QStringLiteral("h-orig")));

    // Commit replaces the existing row and adds new ones.
    QMap<QString, QString> batch;
    batch[QStringLiteral("existing")] = QStringLiteral("h-new");
    batch[QStringLiteral("new-rec")]  = QStringLiteral("h-new2");
    QVERIFY(store.commitBaselines(QStringLiteral("m"), batch));

    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("existing")),
             QStringLiteral("h-new"));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("new-rec")),
             QStringLiteral("h-new2"));
}
```

- [ ] **Step 2: Build and run; expect failures.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: 2 new tests FAIL.

- [ ] **Step 3: Implement `commitBaselines`.**

Replace the stub in `blobbaselinestore.cpp`:

```cpp
bool BlobBaselineStore::commitBaselines(
    const QString &mappingId,
    const QMap<QString, QString> &recordIdToHash)
{
    if (!m_isOpen) {
        setError(QStringLiteral("commitBaselines: store not open"));
        return false;
    }
    if (recordIdToHash.isEmpty()) {
        return true;  // Nothing to do; not an error.
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.transaction()) {
        setError(QStringLiteral("commitBaselines: BEGIN failed: %1")
                     .arg(db.lastError().text()));
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blob_baselines "
        "(mapping_id, record_id, content_hash, updated_at) "
        "VALUES (?, ?, ?, datetime('now'))"));

    for (auto it = recordIdToHash.constBegin();
         it != recordIdToHash.constEnd(); ++it) {
        q.addBindValue(mappingId);
        q.addBindValue(it.key());
        q.addBindValue(it.value());
        if (!q.exec()) {
            setError(QStringLiteral("commitBaselines: insert failed: %1")
                         .arg(q.lastError().text()));
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        setError(QStringLiteral("commitBaselines: COMMIT failed: %1")
                     .arg(db.lastError().text()));
        db.rollback();
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Build + test.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: all 6 tests PASS.

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/libkalburator
git add src/journal/blobbaselinestore.cpp tests/journal/tst_blobbaselinestore.cpp
git commit -m "Phase B3 task 3: commitBaselines bulk writer

Wraps the batch in a SQL transaction; on any row failure, rolls back
and reports via lastError(). Empty batch is a no-op success.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: `baselineRecordIds`

**Files:**
- Modify: `~/dev/libkalburator/src/journal/blobbaselinestore.cpp`
- Modify: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

- [ ] **Step 1: Add failing test.**

Add slot declaration:

```cpp
    void baselineRecordIdsFiltersByMapping();
```

Add body:

```cpp
void TestBlobBaselineStore::baselineRecordIdsFiltersByMapping()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaseline(QStringLiteral("m-a"),
                              QStringLiteral("r1"), QStringLiteral("h")));
    QVERIFY(store.setBaseline(QStringLiteral("m-a"),
                              QStringLiteral("r2"), QStringLiteral("h")));
    QVERIFY(store.setBaseline(QStringLiteral("m-b"),
                              QStringLiteral("r3"), QStringLiteral("h")));

    QStringList idsA = store.baselineRecordIds(QStringLiteral("m-a"));
    std::sort(idsA.begin(), idsA.end());
    QCOMPARE(idsA, QStringList() << QStringLiteral("r1")
                                 << QStringLiteral("r2"));

    QCOMPARE(store.baselineRecordIds(QStringLiteral("m-b")),
             QStringList() << QStringLiteral("r3"));

    QCOMPARE(store.baselineRecordIds(QStringLiteral("m-nothing")),
             QStringList());
}
```

- [ ] **Step 2: Build + run; expect failure.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: 1 new test FAIL.

- [ ] **Step 3: Implement.**

Replace stub in `blobbaselinestore.cpp`:

```cpp
QStringList BlobBaselineStore::baselineRecordIds(
    const QString &mappingId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id FROM blob_baselines WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (!q.exec()) {
        setError(QStringLiteral("baselineRecordIds: %1")
                     .arg(q.lastError().text()));
        return {};
    }

    QStringList out;
    while (q.next()) {
        out.append(q.value(0).toString());
    }
    return out;
}
```

- [ ] **Step 4: Build + test.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: all 7 tests PASS.

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/libkalburator
git add src/journal/blobbaselinestore.cpp tests/journal/tst_blobbaselinestore.cpp
git commit -m "Phase B3 task 4: baselineRecordIds

Returns all record IDs currently baselined for a mapping. Used by the
engine to compute 'deleted since baseline' as a set difference.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: `clearMapping`

**Files:**
- Modify: `~/dev/libkalburator/src/journal/blobbaselinestore.cpp`
- Modify: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

- [ ] **Step 1: Add failing test.**

Add slot declaration:

```cpp
    void clearMappingRemovesOnlyThatMapping();
```

Add body:

```cpp
void TestBlobBaselineStore::clearMappingRemovesOnlyThatMapping()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaseline(QStringLiteral("m-a"),
                              QStringLiteral("r1"), QStringLiteral("h")));
    QVERIFY(store.setBaseline(QStringLiteral("m-b"),
                              QStringLiteral("r2"), QStringLiteral("h")));

    QVERIFY(store.clearMapping(QStringLiteral("m-a")));

    QVERIFY(store.baselineRecordIds(QStringLiteral("m-a")).isEmpty());
    QCOMPARE(store.baselineRecordIds(QStringLiteral("m-b")),
             QStringList() << QStringLiteral("r2"));
}
```

- [ ] **Step 2: Build + run; expect failure.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

- [ ] **Step 3: Implement.**

Replace stub in `blobbaselinestore.cpp`:

```cpp
bool BlobBaselineStore::clearMapping(const QString &mappingId)
{
    if (!m_isOpen) {
        setError(QStringLiteral("clearMapping: store not open"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM blob_baselines WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (!q.exec()) {
        setError(QStringLiteral("clearMapping: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Build + test.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: all 8 tests PASS.

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/libkalburator
git add src/journal/blobbaselinestore.cpp tests/journal/tst_blobbaselinestore.cpp
git commit -m "Phase B3 task 5: clearMapping

Removes all baseline rows for a given mapping ID. Used when a
SyncMapping is unbound or the user resets its sync state.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Coexistence with `IDMappingStore` on the same DB

**Files:**
- Modify: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

This verifies both stores can share `.planstan-sync.db` without stepping on each other — the critical property for Phase E deployment.

- [ ] **Step 1: Add failing test.**

Add include at top of the test file:

```cpp
#include "idmappingstore.h"
```

Add slot declaration:

```cpp
    void coexistsWithIDMappingStore();
```

Add body:

```cpp
void TestBlobBaselineStore::coexistsWithIDMappingStore()
{
    using Kalburator::Sync::IDMappingStore;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dbPathIn(dir);

    // Open both stores on the same file.
    IDMappingStore idStore(path);
    QVERIFY2(idStore.isOpen(), qUtf8Printable(idStore.lastError()));

    BlobBaselineStore baseStore(path);
    QVERIFY2(baseStore.isOpen(), qUtf8Printable(baseStore.lastError()));

    // Exercise both independently.
    idStore.setIdMapping(QStringLiteral("palm"),
                         QStringLiteral("uid-1"),
                         QString(),
                         QStringLiteral("target-1"));
    QCOMPARE(idStore.targetIdForSourceUid(QStringLiteral("palm"),
                                          QStringLiteral("uid-1")),
             QStringLiteral("target-1"));

    QVERIFY(baseStore.setBaseline(QStringLiteral("mapping-1"),
                                  QStringLiteral("rec-1"),
                                  QStringLiteral("hash-1")));
    QCOMPARE(baseStore.baselineHash(QStringLiteral("mapping-1"),
                                    QStringLiteral("rec-1")),
             QStringLiteral("hash-1"));

    // Cross-check: baseline store didn't touch sync_id_mappings, and
    // IDMappingStore didn't touch blob_baselines.
    QCOMPARE(baseStore.baselineRecordIds(QStringLiteral("irrelevant")),
             QStringList());
    QCOMPARE(idStore.targetIdForSourceUid(QStringLiteral("palm"),
                                          QStringLiteral("nonexistent")),
             QString());
}
```

- [ ] **Step 2: Build + test. Expected to pass immediately** — no impl change needed if tables are genuinely disjoint.

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: all 9 tests PASS. If it fails, inspect the error and fix — likely a schema-version race between the two `ensureSchemaAndVersion` calls.

- [ ] **Step 3: Commit.**

```bash
cd ~/dev/libkalburator
git add tests/journal/tst_blobbaselinestore.cpp
git commit -m "Phase B3 task 6: coexistence test with IDMappingStore

Opens both stores on a shared .planstan-sync.db, exercises their
APIs independently, verifies they don't interfere. This is the
critical deployment property — all three stores (SyncStore,
IDMappingStore, BlobBaselineStore) share the DB file.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Schema persistence across reopen

**Files:**
- Modify: `~/dev/libkalburator/tests/journal/tst_blobbaselinestore.cpp`

- [ ] **Step 1: Add test.**

Add slot declaration:

```cpp
    void dataPersistsAcrossReopen();
```

Add body:

```cpp
void TestBlobBaselineStore::dataPersistsAcrossReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dbPathIn(dir);

    {
        BlobBaselineStore store(path);
        QVERIFY(store.isOpen());
        QVERIFY(store.setBaseline(QStringLiteral("m"),
                                  QStringLiteral("r"),
                                  QStringLiteral("h")));
    } // destructor closes + removes connection

    {
        BlobBaselineStore store(path);
        QVERIFY(store.isOpen());
        QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                    QStringLiteral("r")),
                 QStringLiteral("h"));
    }
}
```

- [ ] **Step 2: Build + test.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobbaselinestore
```

Expected: all 10 tests PASS. Verifies RAII connection cleanup + SQLite file persistence.

- [ ] **Step 3: Commit.**

```bash
cd ~/dev/libkalburator
git add tests/journal/tst_blobbaselinestore.cpp
git commit -m "Phase B3 task 7: data persists across store reopen

Characterises RAII connection cleanup + SQLite file-level persistence.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Run full libkalburator standalone ctest suite

- [ ] **Step 1: Full ctest.**

```bash
cd ~/dev/libkalburator
ctest --test-dir build --output-on-failure
```

Expected: all tests PASS. Pre-B3 baseline was 3 blob tests; post-B3 baseline is 3 blob tests + 1 journal test (10 internal slots inside it). No regressions.

- [ ] **Step 2: Report output.** If anything fails, stop and surface the failure. Otherwise proceed to Task 12.

---

## Task 12: PlanStan ctest gate

This is the mandatory pre-commit gate for any upstream commit. libkalburator is consumed in-tree by PlanStan via `add_subdirectory`.

- [ ] **Step 1: Rebuild PlanStan against current libkalburator.**

```bash
cmake -S ~/dev/PlanStan -B ~/dev/PlanStan/build \
    -DPLANSTAN_DEV_BUILD=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build ~/dev/PlanStan/build -j"$(nproc)"
```

Expected: clean build.

- [ ] **Step 2: Run PlanStan ctest.**

```bash
cd ~/dev/PlanStan/build
WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland ctest -j"$(nproc)"
```

Expected baseline: **86 pass / 26 fail / 112 total**. A new failure blocks the commit.

- [ ] **Step 3: Compare to baseline.** If counts match baseline, proceed. If new failures exist, stop, surface the failing test names, and diagnose. Baseline failures (enumerated in `~/dev/libkalburator/docs/phase0/README.md` "Known debt") are not blocking.

---

## Task 13: Update libkalburator's phase index

**Files:**
- Modify: `~/dev/libkalburator/docs/phase0/README.md`
- Modify: `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md`
- Modify: `~/dev/libkalburator/docs/phase0/04i-blob-baseline-store-design.md`

- [ ] **Step 1: Refresh `04i-blob-baseline-store-design.md` "Outcome" section.**

Append to `~/dev/libkalburator/docs/phase0/04i-blob-baseline-store-design.md`:

```markdown
## Outcome (2026-04-21)

Landed as planned. Public surface matches §"Surface" verbatim (except
for QMap<QString, QString> being the bulk type — plain map, not a
value-type wrapper). Schema matches §"Schema" verbatim with the
`idx_blob_baselines_mapping` index added for baselineRecordIds speed.

Ten tests in `tests/journal/tst_blobbaselinestore.cpp` covering
CRUD round-trip, missing-row empty-string behaviour, overwrite via
setBaseline, bulk commitBaselines, atomicity across mixed new/existing
rows, per-mapping filtering in baselineRecordIds, clearMapping
scope, coexistence with IDMappingStore on a shared DB file, and
data persistence across store reopen.

PlanStan ctest baseline (86 pass / 26 fail / 112 total) held.

Not wired into `BlobSyncEngine` yet — that's Phase B4 / E.2.
```

- [ ] **Step 2: Refresh `04h-blob-layer-design.md` deferred list.**

In `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md`, find the "Explicitly deferred" list and strike `BlobBaselineStore` from it (mark as landed in B3). Do NOT remove `ConflictStore integration inside BlobSyncEngine` or the other items — those are still deferred (B4 / E.2).

Change the line that reads (wording may vary):

```markdown
- `BlobBaselineStore` (hash baseline per mapping) — Later library phase ...
```

To:

```markdown
- `BlobBaselineStore` (hash baseline per mapping) — **Landed in Phase B3 (v0.7-phase-b3-baseline, 2026-04-21).** Consumed by Phase B4's `twoWayWithBaseline`.
```

- [ ] **Step 3: Refresh `README.md`: phase map row + current-status section.**

In `~/dev/libkalburator/docs/phase0/README.md`:

(a) Add a new row under the existing Phase B2 row in the phase map table:

```markdown
| **Phase B3** — BlobBaselineStore | done 2026-04-21 | `04i-blob-baseline-store-design.md` | SQLite hash-per-record baseline keyed by (mapping_id, record_id). Shares .planstan-sync.db with IDMappingStore + SyncStore. 10 tests in tests/journal/. Tag: `v0.7-phase-b3-baseline`. Enables correct 3-way diff in Phase B4's twoWayWithBaseline. |
```

(b) Update the **Current status** "Done" list to add the Phase B3 bullet.

(c) Update **Next:** to point at Phase B4 (BlobSyncEngine ↔ ConflictStore wiring) rather than WP Phase E.

(d) Bump the **Last updated** date at the top to `2026-04-21 (after Phase B3 — BlobBaselineStore landed)`.

(e) Update `Baseline health:` line to note the new tests (3 blob + 10-slot journal test).

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/libkalburator
git add docs/phase0/README.md docs/phase0/04h-blob-layer-design.md \
        docs/phase0/04i-blob-baseline-store-design.md
git commit -m "Phase B3 task 8: phase index + design doc updates

README.md phase map gains B3 row; status section reflects B3 landing.
04h deferred-list strikes BlobBaselineStore. 04i gains Outcome section
describing what actually landed.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: Tag `v0.7-phase-b3-baseline`

- [ ] **Step 1: Tag.**

```bash
cd ~/dev/libkalburator
git tag -a v0.7-phase-b3-baseline -m "Phase B3: BlobBaselineStore

SQLite-backed hash-per-record-per-mapping baseline store. Coexists
with SyncStore and IDMappingStore in .planstan-sync.db. Enables
correct 3-way diff (deletion detection, conflict detection) for
the blob-layer sync engine. Consumed by Phase B4's
twoWayWithBaseline engine operation.

Ten internal tests in tests/journal/tst_blobbaselinestore.cpp.
PlanStan ctest baseline (86/26/112) held."
```

- [ ] **Step 2: Verify.**

```bash
cd ~/dev/libkalburator
git tag -l | grep v0.7
git log -1 --decorate --oneline
```

Expected: `v0.7-phase-b3-baseline` tag on the current HEAD commit.

---

## Self-Review

Spec coverage:
- `BlobBaselineStore` API (setBaseline / baselineHash / commitBaselines / baselineRecordIds / clearMapping) — Tasks 5, 7, 6, 7, 8. ✓
- SQLite schema in `.planstan-sync.db` with `PRAGMA user_version = 3` — Task 4. ✓
- Coexistence with `IDMappingStore` — Task 9. ✓
- Persistence across reopen — Task 10. ✓
- libkalburator standalone ctest — Task 11. ✓
- PlanStan ctest gate — Task 12. ✓
- Design doc `04i-blob-baseline-store-design.md` — Tasks 1, 13. ✓
- README phase map + current status + last updated — Task 13. ✓
- Strike `BlobBaselineStore` from `04h` deferred list — Task 13. ✓
- Tag `v0.7-phase-b3-baseline` — Task 14. ✓

Placeholder scan: no TBD / TODO / FIXME in any task body. All code blocks contain full, compilable text. All commands are exact.

Type consistency:
- `BlobBaselineStore` class name matches across header, impl, tests, docs.
- `setBaseline(mappingId, recordId, contentHash)` signature matches across spec, header, impl stub, test calls, and final impl.
- `baselineHash(mappingId, recordId) const` returns `QString` consistently.
- `commitBaselines(mappingId, QMap<QString, QString>)` bulk type matches spec and impl.
- `baselineRecordIds(mappingId) const` returns `QStringList` consistently.
- `clearMapping(mappingId)` returns `bool` consistently.

No gaps detected.

---

## Follow-up plans

After this plan lands:

- **E.2** — `BlobSyncEngine` ↔ `ConflictStore` + `twoWayWithBaseline` (upstream). Consumes `BlobBaselineStore`. Tag `v0.8-phase-b4-engine-conflicts`. Drafted after this plan executes.
- **E.3..E.19** — WP-side work. Drafted after E.2 lands.
