/**
 * @file test_syncengine.cpp
 * @brief Unit tests for SyncEngine class
 *
 * Tests the sync engine orchestration and configuration.
 * Note: Conduit tests require complex mock objects and are limited here.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include <QTemporaryDir>
#include "sync/syncengine.h"
#include "sync/localfilebackend.h"

using namespace Sync;

class TestSyncEngine : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== Construction Tests ==========
    void testConstruction();
    void testDefaultState();
    void testDeviceLinkDefault();
    void testBackendDefault();

    // ========== Backend Tests ==========
    void testSetBackend();

    // ========== Configuration Tests ==========
    void testSetStateDirectory();
    void testConflictPolicyDefault();

    // ========== Sync State Tests ==========
    void testIsSyncingDefault();
    void testRegisteredConduitsEmpty();

    // ========== Callback Tests ==========
    void testSetProgressCallback();
    void testSetCancelCheck();

private:
    QTemporaryDir *m_tempDir;
    SyncEngine *m_engine;
};

void TestSyncEngine::initTestCase()
{
    qDebug() << "Starting SyncEngine tests";
}

void TestSyncEngine::cleanupTestCase()
{
    qDebug() << "SyncEngine tests complete";
}

void TestSyncEngine::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    m_engine = new SyncEngine();
    m_engine->setStateDirectory(m_tempDir->path());
}

void TestSyncEngine::cleanup()
{
    delete m_engine;
    delete m_tempDir;
    m_engine = nullptr;
    m_tempDir = nullptr;
}

// ========== Construction Tests ==========

void TestSyncEngine::testConstruction()
{
    SyncEngine engine;
    // Should construct without crashing
    QVERIFY(true);
}

void TestSyncEngine::testDefaultState()
{
    SyncEngine engine;
    QVERIFY(!engine.isSyncing());
}

void TestSyncEngine::testDeviceLinkDefault()
{
    SyncEngine engine;
    QVERIFY(engine.deviceLink() == nullptr);
}

void TestSyncEngine::testBackendDefault()
{
    SyncEngine engine;
    QVERIFY(engine.backend() == nullptr);
}

// ========== Backend Tests ==========

void TestSyncEngine::testSetBackend()
{
    LocalFileBackend *backend = new LocalFileBackend(m_tempDir->path());
    m_engine->setBackend(backend);

    QCOMPARE(m_engine->backend(), backend);
}

// ========== Configuration Tests ==========

void TestSyncEngine::testSetStateDirectory()
{
    QString testPath = m_tempDir->path() + "/state";
    m_engine->setStateDirectory(testPath);

    // State directory should be set (we can verify indirectly)
    // No exception means it worked
    QVERIFY(true);
}

void TestSyncEngine::testConflictPolicyDefault()
{
    SyncEngine engine;
    // Default should be AskUser
    QCOMPARE(engine.conflictPolicy(), ConflictResolution::AskUser);
}

// ========== Sync State Tests ==========

void TestSyncEngine::testIsSyncingDefault()
{
    QVERIFY(!m_engine->isSyncing());
}

void TestSyncEngine::testRegisteredConduitsEmpty()
{
    SyncEngine engine;
    QVERIFY(engine.registeredConduits().isEmpty());
}

// ========== Callback Tests ==========

void TestSyncEngine::testSetProgressCallback()
{
    int callCount = 0;

    m_engine->setProgressCallback([&](int, int, const QString&) {
        callCount++;
    });

    // Callback is set - verify it doesn't crash
    QVERIFY(true);
}

void TestSyncEngine::testSetCancelCheck()
{
    bool cancelled = false;

    m_engine->setCancelCheck([&]() {
        return cancelled;
    });

    // Callback is set - verify it doesn't crash
    QVERIFY(true);
}

QTEST_MAIN(TestSyncEngine)
#include "test_syncengine.moc"
