#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "sync/syncengine.h"
#include "sync/conduit.h"
#include "sync/localfilebackend.h"

using namespace Sync;

namespace QSyncCore {
struct RecordSnapshot;
}

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
    bool canSyncToPalm() const override { return true; }
    bool canSyncFromPalm() const override { return true; }
    bool writeModifiedCategories(SyncContext *) override { return true; }
    void enrichConflictSnapshot(QSyncCore::RecordSnapshot &, bool) const override {}
    QString formatConflictRecordHtml(const QSyncCore::RecordSnapshot &) const override { return {}; }

    BackendRecord *palmToBackend(PilotRecord *, SyncContext *) override { return nullptr; }
    PilotRecord *backendToPalm(BackendRecord *, SyncContext *) override { return nullptr; }
    bool recordsEqual(PilotRecord *, BackendRecord *, const SyncContext *) const override { return true; }
    QString palmRecordDescription(PilotRecord *, const SyncContext *) const override { return "mock"; }

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
    bool canSyncToPalm() const override { return true; }
    bool canSyncFromPalm() const override { return true; }
    bool writeModifiedCategories(SyncContext *) override { return true; }
    void enrichConflictSnapshot(QSyncCore::RecordSnapshot &, bool) const override {}
    QString formatConflictRecordHtml(const QSyncCore::RecordSnapshot &) const override { return {}; }

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
    void testCancellationStopsIteration();
    void testContextPalmDatabaseCorrect();
    void testEmptyDatabaseListRunsOnce();
};

void TestMultiDatabase::testPerDatabaseIteration()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"CompanionA", "CompanionB", "MainDB-Personal", "MainDB-Work"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // Should have been called 4 times (2 literal + 2 glob matches)
    QCOMPARE(conduit.syncedDatabases.size(), 4);
}

void TestMultiDatabase::testDatabaseOrdering()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"CompanionA", "CompanionB", "MainDB-Personal", "MainDB-Work"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // CompanionA first, CompanionB second, then glob matches in device list order
    QCOMPARE(conduit.syncedDatabases.at(0), "CompanionA");
    QCOMPARE(conduit.syncedDatabases.at(1), "CompanionB");
    QCOMPARE(conduit.syncedDatabases.at(2), "MainDB-Personal");
    QCOMPARE(conduit.syncedDatabases.at(3), "MainDB-Work");
}

void TestMultiDatabase::testGlobExpansion()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"MainDB-Personal", "MainDB-Work", "OtherDB"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // CompanionA and CompanionB not found on device, should be skipped
    // MainDB-* matches MainDB-Personal and MainDB-Work
    QCOMPARE(conduit.syncedDatabases.size(), 2);
    QVERIFY(conduit.syncedDatabases.contains("MainDB-Personal"));
    QVERIFY(conduit.syncedDatabases.contains("MainDB-Work"));
    QVERIFY(!conduit.syncedDatabases.contains("OtherDB"));
}

void TestMultiDatabase::testSingleDatabaseRegression()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"SimpleDB"});

    MockSingleDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockSingle");

    QCOMPARE(conduit.syncedDatabase, "SimpleDB");
}

void TestMultiDatabase::testPerDatabaseStateIsolation()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"CompanionA", "CompanionB", "MainDB-Personal", "MainDB-Work"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // Each database should get its own state key
    QCOMPARE(conduit.stateKeys.size(), 4);
    QCOMPARE(conduit.stateKeys.at(0), "mockMulti/CompanionA");
    QCOMPARE(conduit.stateKeys.at(1), "mockMulti/CompanionB");
    QCOMPARE(conduit.stateKeys.at(2), "mockMulti/MainDB-Personal");
    QCOMPARE(conduit.stateKeys.at(3), "mockMulti/MainDB-Work");
}

void TestMultiDatabase::testPerDatabaseCollectionId()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"CompanionA", "CompanionB", "MainDB-Personal", "MainDB-Work"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    QCOMPARE(conduit.collectionIds.at(0), "mockMulti/CompanionA");
    QCOMPARE(conduit.collectionIds.at(1), "mockMulti/CompanionB");
    QCOMPARE(conduit.collectionIds.at(2), "mockMulti/MainDB-Personal");
    QCOMPARE(conduit.collectionIds.at(3), "mockMulti/MainDB-Work");
}

void TestMultiDatabase::testMissingDatabaseSkipped()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    // Device only has CompanionA - CompanionB and MainDB-* are missing
    engine.setPalmDatabaseList({"CompanionA"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // Only CompanionA should be synced
    QCOMPARE(conduit.syncedDatabases.size(), 1);
    QCOMPARE(conduit.syncedDatabases.first(), "CompanionA");
}

void TestMultiDatabase::testCancellationStopsIteration()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"CompanionA", "CompanionB", "MainDB-Personal", "MainDB-Work"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    // Cancel after first database
    int callCount = 0;
    engine.setCancelCheck([&callCount]() -> bool {
        return callCount++ > 0;
    });

    engine.syncConduit("mockMulti");

    // Should have stopped after first database
    QCOMPARE(conduit.syncedDatabases.size(), 1);
    QCOMPARE(conduit.syncedDatabases.first(), "CompanionA");
}

void TestMultiDatabase::testContextPalmDatabaseCorrect()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({"CompanionA", "CompanionB"});

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // The mock records context->palmDatabase in syncedDatabases
    QCOMPARE(conduit.syncedDatabases.size(), 2);
    QCOMPARE(conduit.syncedDatabases.at(0), "CompanionA");
    QCOMPARE(conduit.syncedDatabases.at(1), "CompanionB");
}

void TestMultiDatabase::testEmptyDatabaseListRunsOnce()
{
    SyncEngine engine;
    QTemporaryDir tempDir;
    engine.setStateDirectory(tempDir.path());
    engine.setBackend(new LocalFileBackend(tempDir.path() + "/sync"));
    engine.setPalmDatabaseList({});  // Empty device

    MockMultiDbConduit conduit;
    engine.registerConduit(&conduit);

    engine.syncConduit("mockMulti");

    // Should run once with empty database name
    QCOMPARE(conduit.syncedDatabases.size(), 1);
    QVERIFY(conduit.syncedDatabases.first().isEmpty());
}

QTEST_MAIN(TestMultiDatabase)
#include "test_multidatabase.moc"
