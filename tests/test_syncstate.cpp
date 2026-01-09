/**
 * @file test_syncstate.cpp
 * @brief Unit tests for SyncState class
 *
 * Tests sync state management including ID mappings, baseline tracking,
 * and persistence.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "sync/syncstate.h"

using namespace Sync;

class TestSyncState : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== Construction Tests ==========
    void testConstruction();
    void testIsFirstSyncInitial();

    // ========== ID Mapping Tests ==========
    void testMapIds();
    void testMapIdsOverwrite();
    void testRemovePalmMapping();
    void testRemovePCMapping();
    void testPcIdForPalm();
    void testPalmIdForPC();
    void testHasPalmMapping();
    void testHasPCMapping();
    void testAllPalmIds();
    void testAllPCIds();
    void testGetMapping();

    // ========== Category Tests ==========
    void testUpdateCategories();

    // ========== Baseline Tests ==========
    void testSaveBaseline();
    void testBaselineHash();
    void testHasFileChangedNewFile();
    void testHasFileChangedUnchanged();
    void testHasFileChangedModified();

    // ========== Sync Metadata Tests ==========
    void testLastSyncTime();
    void testLastSyncPC();
    void testIsFirstSyncAfterMapping();

    // ========== Validation Tests ==========
    void testValidateMappingsValid();
    void testValidateMappingsMissing();
    void testValidateMappingsExtra();

    // ========== Persistence Tests ==========
    void testSaveAndLoad();
    void testLoadNonExistent();
    void testClear();

    // ========== Signal Tests ==========
    void testStateChangedSignal();

private:
    QTemporaryDir *m_tempDir;
    SyncState *m_state;
};

void TestSyncState::initTestCase()
{
    qDebug() << "Starting SyncState tests";
}

void TestSyncState::cleanupTestCase()
{
    qDebug() << "SyncState tests complete";
}

void TestSyncState::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    m_state = new SyncState("testuser", "testconduit");
    m_state->setStateDirectory(m_tempDir->path());
}

void TestSyncState::cleanup()
{
    delete m_state;
    delete m_tempDir;
    m_state = nullptr;
    m_tempDir = nullptr;
}

// ========== Construction Tests ==========

void TestSyncState::testConstruction()
{
    SyncState state("user", "conduit");
    QVERIFY(state.isFirstSync());
}

void TestSyncState::testIsFirstSyncInitial()
{
    QVERIFY(m_state->isFirstSync());
}

// ========== ID Mapping Tests ==========

void TestSyncState::testMapIds()
{
    m_state->mapIds("palm123", "pc/file.txt");

    QCOMPARE(m_state->pcIdForPalm("palm123"), QString("pc/file.txt"));
    QCOMPARE(m_state->palmIdForPC("pc/file.txt"), QString("palm123"));
}

void TestSyncState::testMapIdsOverwrite()
{
    m_state->mapIds("palm123", "pc/file1.txt");
    m_state->mapIds("palm123", "pc/file2.txt");

    // Old PC mapping should be removed
    QCOMPARE(m_state->pcIdForPalm("palm123"), QString("pc/file2.txt"));
    QVERIFY(!m_state->hasPCMapping("pc/file1.txt"));
}

void TestSyncState::testRemovePalmMapping()
{
    m_state->mapIds("palm123", "pc/file.txt");
    m_state->removePalmMapping("palm123");

    QVERIFY(!m_state->hasPalmMapping("palm123"));
    QVERIFY(!m_state->hasPCMapping("pc/file.txt"));
}

void TestSyncState::testRemovePCMapping()
{
    m_state->mapIds("palm123", "pc/file.txt");
    m_state->removePCMapping("pc/file.txt");

    QVERIFY(!m_state->hasPalmMapping("palm123"));
    QVERIFY(!m_state->hasPCMapping("pc/file.txt"));
}

void TestSyncState::testPcIdForPalm()
{
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");

    QCOMPARE(m_state->pcIdForPalm("palm1"), QString("pc1"));
    QCOMPARE(m_state->pcIdForPalm("palm2"), QString("pc2"));
    QVERIFY(m_state->pcIdForPalm("nonexistent").isEmpty());
}

void TestSyncState::testPalmIdForPC()
{
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");

    QCOMPARE(m_state->palmIdForPC("pc1"), QString("palm1"));
    QCOMPARE(m_state->palmIdForPC("pc2"), QString("palm2"));
    QVERIFY(m_state->palmIdForPC("nonexistent").isEmpty());
}

void TestSyncState::testHasPalmMapping()
{
    m_state->mapIds("palm123", "pc/file.txt");

    QVERIFY(m_state->hasPalmMapping("palm123"));
    QVERIFY(!m_state->hasPalmMapping("palm456"));
}

void TestSyncState::testHasPCMapping()
{
    m_state->mapIds("palm123", "pc/file.txt");

    QVERIFY(m_state->hasPCMapping("pc/file.txt"));
    QVERIFY(!m_state->hasPCMapping("pc/other.txt"));
}

void TestSyncState::testAllPalmIds()
{
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");
    m_state->mapIds("palm3", "pc3");

    QStringList ids = m_state->allPalmIds();
    QCOMPARE(ids.size(), 3);
    QVERIFY(ids.contains("palm1"));
    QVERIFY(ids.contains("palm2"));
    QVERIFY(ids.contains("palm3"));
}

void TestSyncState::testAllPCIds()
{
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");
    m_state->mapIds("palm3", "pc3");

    QStringList ids = m_state->allPCIds();
    QCOMPARE(ids.size(), 3);
    QVERIFY(ids.contains("pc1"));
    QVERIFY(ids.contains("pc2"));
    QVERIFY(ids.contains("pc3"));
}

void TestSyncState::testGetMapping()
{
    m_state->mapIds("palm123", "pc/file.txt");

    IDMapping mapping = m_state->getMapping("palm123");
    QCOMPARE(mapping.palmId, QString("palm123"));
    QCOMPARE(mapping.pcId, QString("pc/file.txt"));
}

// ========== Category Tests ==========

void TestSyncState::testUpdateCategories()
{
    m_state->mapIds("palm123", "pc/file.txt");
    m_state->updateCategories("palm123", "Business", {"Work", "Important"});

    IDMapping mapping = m_state->getMapping("palm123");
    QCOMPARE(mapping.palmCategory, QString("Business"));
    QCOMPARE(mapping.pcCategories.size(), 2);
    QVERIFY(mapping.pcCategories.contains("Work"));
    QVERIFY(mapping.pcCategories.contains("Important"));
}

// ========== Baseline Tests ==========

void TestSyncState::testSaveBaseline()
{
    QMap<QString, QString> hashes;
    hashes["file1.txt"] = "abc123";
    hashes["file2.txt"] = "def456";

    m_state->saveBaseline(hashes);

    QCOMPARE(m_state->baselineHash("file1.txt"), QString("abc123"));
    QCOMPARE(m_state->baselineHash("file2.txt"), QString("def456"));
}

void TestSyncState::testBaselineHash()
{
    QMap<QString, QString> hashes;
    hashes["file1.txt"] = "hash123";
    m_state->saveBaseline(hashes);

    QCOMPARE(m_state->baselineHash("file1.txt"), QString("hash123"));
    QVERIFY(m_state->baselineHash("nonexistent").isEmpty());
}

void TestSyncState::testHasFileChangedNewFile()
{
    // No baseline saved yet
    QVERIFY(m_state->hasFileChanged("newfile.txt", "somehash"));
}

void TestSyncState::testHasFileChangedUnchanged()
{
    QMap<QString, QString> hashes;
    hashes["file.txt"] = "samehash";
    m_state->saveBaseline(hashes);

    QVERIFY(!m_state->hasFileChanged("file.txt", "samehash"));
}

void TestSyncState::testHasFileChangedModified()
{
    QMap<QString, QString> hashes;
    hashes["file.txt"] = "oldhash";
    m_state->saveBaseline(hashes);

    QVERIFY(m_state->hasFileChanged("file.txt", "newhash"));
}

// ========== Sync Metadata Tests ==========

void TestSyncState::testLastSyncTime()
{
    QDateTime now = QDateTime::currentDateTime();
    m_state->setLastSyncTime(now);

    QCOMPARE(m_state->lastSyncTime(), now);
}

void TestSyncState::testLastSyncPC()
{
    m_state->setLastSyncPC("MyComputer");

    QCOMPARE(m_state->lastSyncPC(), QString("MyComputer"));
}

void TestSyncState::testIsFirstSyncAfterMapping()
{
    m_state->mapIds("palm1", "pc1");

    QVERIFY(!m_state->isFirstSync());
}

// ========== Validation Tests ==========

void TestSyncState::testValidateMappingsValid()
{
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");

    QStringList palmIds;
    palmIds << "palm1" << "palm2";

    QVERIFY(m_state->validateMappings(palmIds));
}

void TestSyncState::testValidateMappingsMissing()
{
    m_state->mapIds("palm1", "pc1");

    QStringList palmIds;
    palmIds << "palm1" << "palm2";  // palm2 has no mapping

    QVERIFY(!m_state->validateMappings(palmIds));
}

void TestSyncState::testValidateMappingsExtra()
{
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");
    m_state->mapIds("palm3", "pc3");  // Extra mapping

    QStringList palmIds;
    palmIds << "palm1" << "palm2";

    QVERIFY(!m_state->validateMappings(palmIds));
}

// ========== Persistence Tests ==========

void TestSyncState::testSaveAndLoad()
{
    // Set up state
    m_state->mapIds("palm1", "pc1");
    m_state->mapIds("palm2", "pc2");
    m_state->updateCategories("palm1", "Business", {"Work"});
    m_state->setLastSyncTime(QDateTime(QDate(2024, 6, 15), QTime(10, 30, 0)));
    m_state->setLastSyncPC("TestPC");

    QMap<QString, QString> hashes;
    hashes["pc1"] = "hash1";
    m_state->saveBaseline(hashes);

    // Save
    QVERIFY(m_state->save());

    // Create new state and load
    SyncState state2("testuser", "testconduit");
    state2.setStateDirectory(m_tempDir->path());
    QVERIFY(state2.load());

    // Verify loaded data
    QCOMPARE(state2.pcIdForPalm("palm1"), QString("pc1"));
    QCOMPARE(state2.pcIdForPalm("palm2"), QString("pc2"));
    QCOMPARE(state2.getMapping("palm1").palmCategory, QString("Business"));
    QCOMPARE(state2.lastSyncPC(), QString("TestPC"));
    QCOMPARE(state2.baselineHash("pc1"), QString("hash1"));
}

void TestSyncState::testLoadNonExistent()
{
    // Loading from a path with no existing state should succeed
    SyncState state("newuser", "newconduit");
    state.setStateDirectory(m_tempDir->path());
    QVERIFY(state.load());
    QVERIFY(state.isFirstSync());
}

void TestSyncState::testClear()
{
    m_state->mapIds("palm1", "pc1");
    m_state->setLastSyncTime(QDateTime::currentDateTime());
    m_state->setLastSyncPC("TestPC");

    QMap<QString, QString> hashes;
    hashes["pc1"] = "hash1";
    m_state->saveBaseline(hashes);

    // Clear everything
    m_state->clear();

    QVERIFY(m_state->isFirstSync());
    QVERIFY(m_state->allPalmIds().isEmpty());
    QVERIFY(m_state->allPCIds().isEmpty());
    QVERIFY(!m_state->lastSyncTime().isValid());
    QVERIFY(m_state->lastSyncPC().isEmpty());
}

// ========== Signal Tests ==========

void TestSyncState::testStateChangedSignal()
{
    QSignalSpy spy(m_state, &SyncState::stateChanged);

    m_state->mapIds("palm1", "pc1");
    QCOMPARE(spy.count(), 1);

    m_state->removePalmMapping("palm1");
    QCOMPARE(spy.count(), 2);

    m_state->setLastSyncTime(QDateTime::currentDateTime());
    QCOMPARE(spy.count(), 3);

    m_state->clear();
    QCOMPARE(spy.count(), 4);
}

QTEST_MAIN(TestSyncState)
#include "test_syncstate.moc"
