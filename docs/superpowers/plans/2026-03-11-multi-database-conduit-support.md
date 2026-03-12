# Multi-Database Conduit Support Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable WildPalms sync conduits to claim and sync multiple Palm databases with heterogeneous record structures, with per-database state isolation and configurable sync ordering.

**Architecture:** The sync engine iterates a conduit's claimed databases in declared array order, calling `sync()` once per database with per-database SyncState. Three ISyncConduit methods gain a `const SyncContext*` parameter so multi-database conduits can dispatch by database type. Globs in database claims are expanded against the Palm's actual database list at sync time.

**Tech Stack:** Qt6/C++20, KDE Frameworks 6, pilot-link (dlp_FindDBInfo)

**Spec:** `docs/superpowers/specs/2026-03-11-multi-database-conduit-support-design.md`

---

## Chunk 1: Interface Changes and Plugin Updates

These tasks must be done together — partial changes leave the code in a non-compiling state. The goal is to update all signatures and restore compilation + passing tests.

### Task 1: Update ISyncConduit Interface Signatures

**Files:**
- Modify: `src/core/isyncconduit.h:35-39`

- [ ] **Step 1: Update the three method signatures**

In `src/core/isyncconduit.h`, change lines 35-39 from:

```cpp
    virtual bool recordsEqual(PilotRecord *palmRecord,
                               BackendRecord *backendRecord) const = 0;
    virtual QString palmRecordDescription(PilotRecord *record) const = 0;
    virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                      const QList<BackendRecord *> &candidates) = 0;
```

to:

```cpp
    virtual bool recordsEqual(PilotRecord *palmRecord,
                               BackendRecord *backendRecord,
                               const SyncContext *context) const = 0;
    virtual QString palmRecordDescription(PilotRecord *record,
                                           const SyncContext *context) const = 0;
    virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                      const QList<BackendRecord *> &candidates,
                                      const SyncContext *context) = 0;
```

### Task 2: Update SyncConduitBase Signatures

**Files:**
- Modify: `src/sync/conduit.h:346-378`

- [ ] **Step 1: Update recordsEqual declaration**

At line 364, change:

```cpp
    bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override = 0;
```

to:

```cpp
    bool recordsEqual(PilotRecord *palm, BackendRecord *backend,
                       const SyncContext *context) const override = 0;
```

- [ ] **Step 2: Update findMatch declaration**

At lines 372-373, change:

```cpp
    BackendRecord* findMatch(PilotRecord *palmRecord,
                              const QList<BackendRecord*> &candidates) override;
```

to:

```cpp
    BackendRecord* findMatch(PilotRecord *palmRecord,
                              const QList<BackendRecord*> &candidates,
                              const SyncContext *context) override;
```

- [ ] **Step 3: Update palmRecordDescription declaration**

At line 378, change:

```cpp
    QString palmRecordDescription(PilotRecord *record) const override = 0;
```

to:

```cpp
    QString palmRecordDescription(PilotRecord *record,
                                   const SyncContext *context) const override = 0;
```

### Task 3: Update All Plugin Conduit Signatures

**Files:**
- Modify: `src/plugins/memo/memoconduit.h:54,56`
- Modify: `src/plugins/memo/memoconduit.cpp:77,117`
- Modify: `src/plugins/calendar/calendarconduit.h:54,56`
- Modify: `src/plugins/calendar/calendarconduit.cpp:74,112`
- Modify: `src/plugins/contacts/contactconduit.h:54,56`
- Modify: `src/plugins/contacts/contactconduit.cpp:74,114`
- Modify: `src/plugins/todos/todoconduit.h:40,42`
- Modify: `src/plugins/todos/todoconduit.cpp:84,130`
- Modify: `src/plugins/webcalendar/webcalendarconduit.h:143-151`

For each plugin, the same mechanical change applies. Add `const SyncContext *context` parameter, mark it `Q_UNUSED(context)` in the implementation.

- [ ] **Step 1: Update MemoConduit**

In `memoconduit.h`, update the two declarations:
```cpp
    bool recordsEqual(PilotRecord *palm, BackendRecord *backend,
                       const SyncContext *context) const override;
    QString palmRecordDescription(PilotRecord *record,
                                   const SyncContext *context) const override;
```

In `memoconduit.cpp`, update the two definitions (line 77 and 117):
```cpp
bool MemoConduit::recordsEqual(PilotRecord *palm, BackendRecord *backend,
                                const SyncContext *context) const
{
    Q_UNUSED(context)
    // ... existing body unchanged ...
}

QString MemoConduit::palmRecordDescription(PilotRecord *record,
                                            const SyncContext *context) const
{
    Q_UNUSED(context)
    // ... existing body unchanged ...
}
```

- [ ] **Step 2: Update CalendarConduit**

Same pattern as Step 1. Files: `calendarconduit.h` lines 54,56 and `calendarconduit.cpp` lines 74,112.

- [ ] **Step 3: Update ContactConduit**

Same pattern. Files: `contactconduit.h` lines 54,56 and `contactconduit.cpp` lines 74,114.

- [ ] **Step 4: Update TodoConduit**

Same pattern. Files: `todoconduit.h` lines 40,42 and `todoconduit.cpp` lines 84,130.

- [ ] **Step 5: Update WebCalendarConduit**

In `webcalendarconduit.h`, update the inline stubs at lines 143-151:

```cpp
    bool recordsEqual(PilotRecord *, BackendRecord *,
                       const SyncContext *) const override { return false; }
    QString palmRecordDescription(PilotRecord *,
                                   const SyncContext *) const override { return {}; }
```

### Task 4: Thread Context Through conduit.cpp Call Sites

**Files:**
- Modify: `src/sync/conduit.cpp` — 9 call sites for `palmRecordDescription`, 1 for `findMatch`

All `palmRecordDescription` calls are inside methods that already have a `SyncContext *context` parameter. Each call changes from `palmRecordDescription(record)` to `palmRecordDescription(record, context)`.

- [ ] **Step 1: Update palmRecordDescription calls**

Update all 9 call sites in `conduit.cpp`. Each is a simple parameter addition:

| Line | Current | New |
|------|---------|-----|
| 262 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 266 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 499 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 788 | `palmRecordDescription(existingRecord)` | `palmRecordDescription(existingRecord, context)` |
| 894 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 929 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 988 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 1031 | `palmRecordDescription(palmRecord)` | `palmRecordDescription(palmRecord, context)` |
| 1481 | `palmRecordDescription(palmRecord)` (inside `findMatch()`) | `palmRecordDescription(palmRecord, context)` |

- [ ] **Step 2: Update findMatch definition and call site**

At line 1478, update the definition:

```cpp
BackendRecord* SyncConduitBase::findMatch(PilotRecord *palmRecord,
                                           const QList<BackendRecord*> &candidates,
                                           const SyncContext *context)
{
    QString palmDesc = palmRecordDescription(palmRecord, context).toLower().trimmed();
    // ... rest unchanged ...
}
```

At line 494, update the call site:

```cpp
BackendRecord *match = findMatch(palmRecord, candidates, context);
```

### Task 5: Build and Run Existing Tests

- [ ] **Step 1: Build the project**

```bash
cd build && cmake --build . 2>&1 | tail -20
```

Expected: Clean build with no errors. All interface mismatches caught and resolved in Tasks 1-4.

- [ ] **Step 2: Run existing tests**

```bash
cd build && ctest --output-on-failure
```

Expected: All tests pass. This is a regression check — the interface changes shouldn't alter behavior.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "refactor: add SyncContext parameter to record conversion interface

Add const SyncContext* to recordsEqual, palmRecordDescription, and
findMatch across ISyncConduit, SyncConduitBase, and all plugin conduits.
This enables multi-database conduits to dispatch by context->palmDatabase.
Existing single-database conduits ignore the parameter."
```

---

## Chunk 2: Per-Database Sync Engine Changes

### Task 6: Write Multi-Database Sync Engine Tests

**Files:**
- Create: `tests/test_multidatabase.cpp`
- Modify: `tests/CMakeLists.txt`

These tests verify the sync engine's per-database iteration behavior using a mock multi-database conduit.

- [ ] **Step 1: Create the test file with a mock multi-database conduit**

Create `tests/test_multidatabase.cpp`. The mock conduit claims multiple databases and records which databases it receives via `sync()`:

```cpp
#include <QTest>
#include <QSignalSpy>
#include "sync/syncengine.h"
#include "sync/conduit.h"
#include "sync/localfilebackend.h"

using namespace Sync;

// Mock conduit that claims multiple databases and records sync calls
class MockMultiDbConduit : public SyncConduitBase
{
    Q_OBJECT
public:
    explicit MockMultiDbConduit(QObject *parent = nullptr) : SyncConduitBase(parent) {}

    QString conduitId() const override { return "mockMulti"; }
    QString displayName() const override { return "Mock Multi-DB"; }
    QStringList palmDatabaseNames() const override {
        return {"CompanionA", "CompanionB", "MainDB-*"};
    }
    QString fileExtension() const override { return ".dat"; }
    QIcon icon() const override { return {}; }
    QString description() const override { return "Test conduit"; }

    BackendRecord *palmToBackend(PilotRecord *, SyncContext *) override { return nullptr; }
    PilotRecord *backendToPalm(BackendRecord *, SyncContext *) override { return nullptr; }
    bool recordsEqual(PilotRecord *, BackendRecord *, const SyncContext *) const override { return true; }
    QString palmRecordDescription(PilotRecord *, const SyncContext *) const override { return "mock"; }

    // Override sync() to just record what database we were called with
    SyncResult sync(SyncContext *context) override {
        syncedDatabases.append(context->palmDatabase);
        stateKeys.append(context->state ? context->state->conduitId() : "null");
        collectionIds.append(context->collectionId);
        SyncResult result;
        result.success = true;
        return result;
    }

    QStringList syncedDatabases;
    QStringList stateKeys;
    QStringList collectionIds;
};

// Mock conduit that claims a single database (regression test)
class MockSingleDbConduit : public SyncConduitBase
{
    Q_OBJECT
public:
    explicit MockSingleDbConduit(QObject *parent = nullptr) : SyncConduitBase(parent) {}

    QString conduitId() const override { return "mockSingle"; }
    QString displayName() const override { return "Mock Single-DB"; }
    QStringList palmDatabaseNames() const override { return {"SimpleDB"}; }
    QString fileExtension() const override { return ".dat"; }
    QIcon icon() const override { return {}; }
    QString description() const override { return "Test conduit"; }

    BackendRecord *palmToBackend(PilotRecord *, SyncContext *) override { return nullptr; }
    PilotRecord *backendToPalm(BackendRecord *, SyncContext *) override { return nullptr; }
    bool recordsEqual(PilotRecord *, BackendRecord *, const SyncContext *) const override { return true; }
    QString palmRecordDescription(PilotRecord *, const SyncContext *) const override { return "mock"; }

    SyncResult sync(SyncContext *context) override {
        syncedDatabase = context->palmDatabase;
        SyncResult result;
        result.success = true;
        return result;
    }

    QString syncedDatabase;
};

class TestMultiDatabase : public QObject
{
    Q_OBJECT

private slots:
    void testPerDatabaseIteration();
    void testDatabaseOrdering();
    void testGlobExpansion();
    void testSingleDatabaseRegression();
    void testPerDatabaseStateIsolation();
    void testPerDatabaseCollectionId();
    void testMissingDatabaseSkipped();
};

void TestMultiDatabase::testPerDatabaseIteration()
{
    // A multi-db conduit should get sync() called once per database
    // For now, just verify the mock works - full test after engine changes
    MockMultiDbConduit conduit;
    QCOMPARE(conduit.palmDatabaseNames().size(), 3);
}

void TestMultiDatabase::testDatabaseOrdering()
{
    // Verify that sync() calls arrive in declared array order
    // (CompanionA, CompanionB, then expanded MainDB-* matches)
    MockMultiDbConduit conduit;
    QStringList expected = conduit.palmDatabaseNames();
    QCOMPARE(expected.first(), "CompanionA");
    QCOMPARE(expected.last(), "MainDB-*");
}

void TestMultiDatabase::testGlobExpansion()
{
    // Verify glob pattern matching against a database list
    QRegularExpression re(QRegularExpression::wildcardToRegularExpression("MainDB-*"));
    QVERIFY(re.match("MainDB-Personal").hasMatch());
    QVERIFY(re.match("MainDB-Work").hasMatch());
    QVERIFY(!re.match("OtherDB").hasMatch());
    QVERIFY(!re.match("MainDB").hasMatch()); // No dash
}

void TestMultiDatabase::testSingleDatabaseRegression()
{
    MockSingleDbConduit conduit;
    QCOMPARE(conduit.palmDatabaseNames().size(), 1);
    QCOMPARE(conduit.palmDatabaseNames().first(), "SimpleDB");
}

void TestMultiDatabase::testPerDatabaseStateIsolation()
{
    // State keys should be conduitId/databaseName
    // This test will be extended after engine changes
    QString conduitId = "mockMulti";
    QString dbName = "CompanionA";
    QString key = conduitId + "/" + dbName;
    QCOMPARE(key, "mockMulti/CompanionA");
}

void TestMultiDatabase::testPerDatabaseCollectionId()
{
    // Collection IDs should be conduitId/databaseName
    QString conduitId = "mockMulti";
    QString dbName = "CompanionA";
    QString collectionId = conduitId + "/" + dbName;
    QCOMPARE(collectionId, "mockMulti/CompanionA");
}

void TestMultiDatabase::testMissingDatabaseSkipped()
{
    // A claimed database not on the device should be skipped
    QStringList palmDatabases = {"CompanionA", "MainDB-Personal"};
    QString claimed = "CompanionB";
    QVERIFY(!palmDatabases.contains(claimed));
}

QTEST_MAIN(TestMultiDatabase)
#include "test_multidatabase.moc"
```

- [ ] **Step 2: Add to CMakeLists.txt**

Add the new test to `tests/CMakeLists.txt` using the existing helper function:

```cmake
add_wildpalms_test(test_multidatabase test_multidatabase.cpp)
```

- [ ] **Step 3: Build and run the new tests**

```bash
cd build && cmake .. && cmake --build . --target test_multidatabase && ./test_multidatabase
```

Expected: All tests pass (they test fundamentals and mocks, not engine changes yet).

- [ ] **Step 4: Commit**

```bash
git add tests/test_multidatabase.cpp tests/CMakeLists.txt
git commit -m "test: add multi-database conduit test scaffolding

Mock conduits for multi-database and single-database scenarios.
Initial tests verify mock setup, glob matching, and key construction.
Full integration tests will be added as engine changes land."
```

### Task 7: Implement Per-Database State and Iteration in syncConduit()

**Files:**
- Modify: `src/sync/syncengine.h:277,323` — update `stateForConduit` signature, add members
- Modify: `src/sync/syncengine.cpp:423-556,621-636` — rewrite `syncConduit()` and `stateForConduit()`
- Modify: `src/sync/syncengine.cpp:119-277` — add database list caching to `syncAll()`/`syncAllOrdered()`

- [ ] **Step 1: Update stateForConduit in header and implementation**

In `src/sync/syncengine.h`, at line 277, change:

```cpp
    SyncState* stateForConduit(const QString &conduitId);
```

to:

```cpp
    SyncState* stateForConduit(const QString &conduitId,
                                const QString &databaseName);
```

In `src/sync/syncengine.cpp`, at lines 621-636, change:

```cpp
SyncState* SyncEngine::stateForConduit(const QString &conduitId)
{
    if (!m_states.contains(conduitId)) {
        QString userName = m_palmUserName.isEmpty() ? "default" : m_palmUserName;
        SyncState *state = new SyncState(userName, conduitId, this);

        // Use the configured state directory (within PalmSync/.state/)
        if (!m_stateDirectory.isEmpty()) {
            state->setStateDirectory(m_stateDirectory);
        }

        state->load();
        m_states[conduitId] = state;
    }
    return m_states[conduitId];
}
```

to:

```cpp
SyncState* SyncEngine::stateForConduit(const QString &conduitId,
                                        const QString &databaseName)
{
    QString key = conduitId + QStringLiteral("/") + databaseName;
    if (!m_states.contains(key)) {
        QString userName = m_palmUserName.isEmpty() ? "default" : m_palmUserName;
        SyncState *state = new SyncState(userName, key, this);

        if (!m_stateDirectory.isEmpty()) {
            state->setStateDirectory(m_stateDirectory);
        }

        state->load();
        m_states[key] = state;
    }
    return m_states[key];
}
```

- [ ] **Step 2: Add m_palmDatabaseList and expandDatabaseName to header**

In `src/sync/syncengine.h`, add to the private members section (near the other member variables):

```cpp
    QStringList m_palmDatabaseList;  ///< Cached list of databases on the Palm device
```

And add to the private methods section:

```cpp
    QStringList expandDatabaseName(const QString &nameOrGlob) const;
```

- [ ] **Step 3: Add database list caching to syncAll()**

In `src/sync/syncengine.cpp`, in `syncAll()`, after the Palm username section (around line 155, after `m_palmUserName = "default";`) and before `m_syncing = true;`, add:

```cpp
    // Cache the device's database list for glob expansion
    m_palmDatabaseList.clear();
    if (m_deviceLink) {
        m_palmDatabaseList = m_deviceLink->listDatabases();
        emit logMessage(QString("Device has %1 databases").arg(m_palmDatabaseList.size()));
    }
```

Add the same block to `syncAllOrdered()` at the equivalent location — between line 309 (`m_palmUserName = "default";` block end) and line 311 (`m_syncing = true;`).

- [ ] **Step 4: Add the expandDatabaseName helper implementation**

In `src/sync/syncengine.cpp`:

```cpp
QStringList SyncEngine::expandDatabaseName(const QString &nameOrGlob) const
{
    // Check if this is a glob pattern (contains * or ?)
    if (!nameOrGlob.contains(QLatin1Char('*')) && !nameOrGlob.contains(QLatin1Char('?'))) {
        // Literal name — check if it exists on the device
        if (m_palmDatabaseList.contains(nameOrGlob)) {
            return {nameOrGlob};
        }
        return {};  // Not found on device
    }

    // Glob pattern — expand against device database list
    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(nameOrGlob));
    QStringList matches;
    for (const QString &dbName : m_palmDatabaseList) {
        if (re.match(dbName).hasMatch()) {
            matches.append(dbName);
        }
    }
    return matches;
}
```

- [ ] **Step 5: Rewrite syncConduit() for per-database iteration**

In `src/sync/syncengine.cpp`, replace the body of `syncConduit()` (lines 423-556). The new version:

```cpp
SyncResult SyncEngine::syncConduit(const QString &conduitId, SyncMode mode)
{
    SyncResult result;
    result.startTime = QDateTime::currentDateTime();

    IConduit *cond = m_conduits.value(conduitId);
    if (!cond) {
        result.success = false;
        result.errorMessage = QString("Unknown conduit: %1").arg(conduitId);
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    m_currentConduit = conduitId;
    emit conduitStarted(conduitId);
    emit logMessage(QString("=== %1 ===").arg(cond->displayName()));

    // Determine databases to sync
    ISyncConduit *syncCond = dynamic_cast<ISyncConduit*>(cond);
    QStringList databasesToSync;

    if (syncCond) {
        // Expand each claimed database name/glob against the device list
        for (const QString &nameOrGlob : syncCond->palmDatabaseNames()) {
            QStringList expanded = expandDatabaseName(nameOrGlob);
            if (expanded.isEmpty()) {
                emit logMessage(QString("  Database '%1' not found on device, skipping")
                                .arg(nameOrGlob));
            }
            databasesToSync.append(expanded);
        }
    }

    if (databasesToSync.isEmpty() && syncCond) {
        // No databases found — run sync once with empty database name
        // (preserves behavior for conduits that don't require a device database)
        databasesToSync.append(QString());
    }

    // If not a sync conduit (tool conduit), run once with no database
    if (!syncCond) {
        databasesToSync = {QString()};
    }

    result.success = true;

    for (const QString &dbName : databasesToSync) {
        if (m_cancelled || (m_cancelCheck && m_cancelCheck())) {
            emit logMessage("Sync cancelled by user");
            break;
        }

        if (!dbName.isEmpty()) {
            emit logMessage(QString("  Syncing database: %1").arg(dbName));
        }

        // Get or create per-database sync state
        SyncState *state = dbName.isEmpty()
            ? stateForConduit(conduitId, conduitId)  // Fallback for tool conduits
            : stateForConduit(conduitId, dbName);

        // Build sync context
        SyncContext context;
        context.deviceLink = m_deviceLink;
        context.backend = m_backend;
        context.state = state;
        context.mode = mode;
        context.conflictPolicy = m_conflictPolicy;
        context.userName = m_palmUserName;
        context.palmDatabase = dbName;
        context.collectionId = dbName.isEmpty()
            ? conduitId
            : conduitId + QStringLiteral("/") + dbName;

        // Provide the full list of active databases for this conduit
        if (syncCond) {
            context.activeDatabases = databasesToSync;
        }

        // Populate sync folder path from backend (if local file backend)
        if (auto *localBackend = dynamic_cast<LocalFileBackend*>(m_backend)) {
            context.syncFolderPath = localBackend->basePath();
        }

        // Set up conflict handling system
        QSyncCore::AutomaticConflictHandler autoHandler(state->conflictStore());
        QSyncCore::ConflictHandler *conflictHandler =
            m_externalHandler ? m_externalHandler : &autoHandler;

        // Configure conflict policy from engine settings
        QSyncCore::ConflictPolicy conflictSettings;

        if (m_conflictAutoResolve == "palm_wins") {
            conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::SourceAlwaysWins;
        } else if (m_conflictAutoResolve == "pc_wins") {
            conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::TargetAlwaysWins;
        } else if (m_conflictAutoResolve == "newer_wins") {
            conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::NewerWins;
        } else if (m_conflictAutoResolve == "older_wins") {
            conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::OlderWins;
        } else if (m_conflictAutoResolve == "duplicate") {
            conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::DuplicateAll;
        } else {
            conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::None;
        }

        if (m_conflictFallback == "skip") {
            conflictSettings.fallback = QSyncCore::FallbackBehavior::Skip;
        } else if (m_conflictFallback == "use_default") {
            conflictSettings.fallback = QSyncCore::FallbackBehavior::UseDefault;
        } else {
            conflictSettings.fallback = QSyncCore::FallbackBehavior::Defer;
        }

        if (m_conflictPromptStrategy == "first_only") {
            conflictSettings.promptStrategy = QSyncCore::PromptStrategy::OnFirstConflict;
        } else if (m_conflictPromptStrategy == "batch_at_end") {
            conflictSettings.promptStrategy = QSyncCore::PromptStrategy::Never;
            conflictSettings.fallback = QSyncCore::FallbackBehavior::Defer;
        } else {
            conflictSettings.promptStrategy = QSyncCore::PromptStrategy::Always;
        }

        if (m_conflictConnectionBehavior == "disconnect_and_defer") {
            conflictSettings.connectionBehavior =
                QSyncCore::ConnectionBehavior::DisconnectAndDefer;
        } else if (m_conflictConnectionBehavior == "timeout_and_defer") {
            conflictSettings.connectionBehavior =
                QSyncCore::ConnectionBehavior::TimeoutThenDefer;
        } else {
            conflictSettings.connectionBehavior =
                QSyncCore::ConnectionBehavior::KeepAlive;
        }

        conflictSettings.promptTimeoutSeconds = m_conflictTimeoutSeconds;

        context.conflictHandler = conflictHandler;
        context.conflictSettings = conflictSettings;

        // Pass cancellation check to conduit
        auto *syncBase = dynamic_cast<SyncConduitBase*>(cond);
        if (syncBase && m_cancelCheck) {
            syncBase->setCancelCheck(m_cancelCheck);
        }

        // Run the sync for this database
        SyncResult dbResult = cond->sync(&context);

        // Clear cancellation check
        if (syncBase) {
            syncBase->setCancelCheck(nullptr);
        }

        // Capture any files queued for installation
        if (!context.installQueue.isEmpty()) {
            m_pendingInstalls.append(context.installQueue);
            emit logMessage(QString("%1 queued %2 file(s) for installation")
                            .arg(cond->displayName()).arg(context.installQueue.size()));
        }

        // Accumulate results
        result.palmStats.created += dbResult.palmStats.created;
        result.palmStats.updated += dbResult.palmStats.updated;
        result.palmStats.deleted += dbResult.palmStats.deleted;
        result.palmStats.unchanged += dbResult.palmStats.unchanged;
        result.palmStats.conflicts += dbResult.palmStats.conflicts;
        result.palmStats.errors += dbResult.palmStats.errors;

        result.pcStats.created += dbResult.pcStats.created;
        result.pcStats.updated += dbResult.pcStats.updated;
        result.pcStats.deleted += dbResult.pcStats.deleted;
        result.pcStats.unchanged += dbResult.pcStats.unchanged;
        result.pcStats.conflicts += dbResult.pcStats.conflicts;
        result.pcStats.errors += dbResult.pcStats.errors;

        result.warnings.append(dbResult.warnings);

        if (!dbResult.success) {
            result.success = false;
            emit logMessage(QString("  Database '%1' sync failed: %2")
                            .arg(dbName, dbResult.errorMessage));
            // Continue with remaining databases — partial sync is better than no sync
        }
    }

    result.endTime = QDateTime::currentDateTime();
    m_currentConduit.clear();

    emit conduitFinished(conduitId, result);

    return result;
}
```

- [ ] **Step 6: Build and run all tests**

```bash
cd build && cmake .. && cmake --build . && ctest --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: implement per-database sync iteration in sync engine

syncConduit() now iterates a conduit's claimed databases in declared
array order, calling sync() once per database with per-database
SyncState and collectionId. Globs are expanded against the device's
database list (cached at sync start). Missing databases are skipped.
Per-database failures log and continue with remaining databases."
```

---

## Chunk 3: Documentation and Final Tests

### Task 8: Expand Multi-Database Tests

**Files:**
- Modify: `tests/test_multidatabase.cpp`

Now that the engine changes are in place, expand the tests. Since `syncConduit()` requires a device link and backend, add a test-only setter `setPalmDatabaseList()` to `SyncEngine` so tests can inject a mock database list without a real device. Alternatively, test `expandDatabaseName()` directly if it can be made accessible (e.g., via a friend class or by making the test class a friend).

- [ ] **Step 1: Add a test-only setter for the Palm database list**

In `src/sync/syncengine.h`, add to the public section:

```cpp
    /// Test support: inject a Palm database list without a device connection
    void setPalmDatabaseList(const QStringList &list) { m_palmDatabaseList = list; }
```

- [ ] **Step 2: Add engine integration tests**

Add these test methods to `TestMultiDatabase` in `test_multidatabase.cpp`:

- **testExpandDatabaseNameLiteral** — set a Palm database list, verify literal name found/not found
- **testExpandDatabaseNameGlob** — set a Palm database list with `MainDB-Personal` and `MainDB-Work`, verify `MainDB-*` glob expands to both
- **testCancellationStopsIteration** — register a multi-db conduit, start sync, cancel after first database, verify remaining databases were not synced
- **testPalmRecordDescriptionReceivesContext** — create a mock conduit that records `context->palmDatabase` when `palmRecordDescription` is called, verify it receives the correct database name
- **testRecordsEqualReceivesContext** — same pattern, verify `context->palmDatabase` is set correctly when `recordsEqual` would be called by a multi-database conduit's sync override

- [ ] **Step 2: Build and run tests**

```bash
cd build && cmake --build . --target test_multidatabase && ./test_multidatabase -v
```

Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_multidatabase.cpp src/sync/syncengine.h
git commit -m "test: expand multi-database engine integration tests

Add test-only setPalmDatabaseList() setter. Tests cover glob expansion,
cancellation during per-database iteration, and context parameter
threading to record conversion methods."
```

### Task 9: Update Plugin Developer Guide

**Files:**
- Modify: `docs/plugin-developer-guide.md`

- [ ] **Step 1: Add database ordering section**

Add a new section (after the existing database claims section) explaining:

- `X-WildPalms-PalmDatabases` array order determines sync order within a conduit
- Example: `["ShadTags", "ShadViews", "ShadFilters", "ShadCat", "ShadP-*"]` — tags and views sync before lists for referential integrity
- Glob patterns are expanded against the device's actual database list at sync time
- The engine calls `sync()` once per resolved database, setting `context->palmDatabase`
- Databases not found on the device are skipped silently

- [ ] **Step 2: Update method signatures in the guide**

Update all code examples showing `recordsEqual`, `palmRecordDescription`, and `findMatch` to include the `const SyncContext *context` parameter. Add a note explaining multi-database conduits use `context->palmDatabase` to dispatch to different codecs.

- [ ] **Step 3: Add multi-database conduit section**

Add a section covering:
- Per-database SyncState isolation (each database gets independent ID mappings and baselines)
- Per-database collectionId (`conduitId/databaseName`)
- How to write a multi-database conduit: check `context->palmDatabase` in your overrides
- Error handling: per-database failures don't stop other databases

- [ ] **Step 4: Commit**

```bash
git add docs/plugin-developer-guide.md
git commit -m "docs: add multi-database conduit documentation to plugin guide"
```

### Task 10: Update Architecture Documentation

**Files:**
- Modify: `docs/SYNC_ENGINE_ARCHITECTURE.md`

- [ ] **Step 1: Update sync engine section**

Update the architecture doc to describe:
- Per-database sync loop within a conduit
- Per-database SyncState scoping (`conduitId/databaseName`)
- Database list discovery from Palm device (cached per sync session)
- Glob expansion mechanism using `QRegularExpression::wildcardToRegularExpression`
- Updated method signatures with `const SyncContext*`

- [ ] **Step 2: Commit**

```bash
git add docs/SYNC_ENGINE_ARCHITECTURE.md
git commit -m "docs: update architecture doc for multi-database conduit support"
```

### Task 11: Final Verification

- [ ] **Step 1: Full clean rebuild**

```bash
cd build && cmake .. && cmake --build . --clean-first
```

Expected: Clean build, no warnings.

- [ ] **Step 2: Run full test suite**

```bash
cd build && ctest --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 3: Review git log**

```bash
git log --oneline -10
```

Verify commit sequence is clean and tells a coherent story.
