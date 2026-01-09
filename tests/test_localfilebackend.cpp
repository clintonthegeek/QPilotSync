/**
 * @file test_localfilebackend.cpp
 * @brief Unit tests for LocalFileBackend class
 *
 * Tests file-based storage backend for sync operations.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "sync/localfilebackend.h"

using namespace Sync;

class TestLocalFileBackend : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== Construction Tests ==========
    void testConstruction();
    void testBackendId();
    void testDisplayName();
    void testBasePath();

    // ========== Availability Tests ==========
    void testIsAvailableValid();
    void testIsAvailableInvalid();

    // ========== File Extension Tests ==========
    void testDefaultExtensions();
    void testSetFileExtension();
    void testFileExtensionUnknownCollection();

    // ========== Collection Tests ==========
    void testAvailableCollections();
    void testCollectionInfo();
    void testCreateCollection();

    // ========== Record Operations Tests ==========
    void testCreateRecord();
    void testLoadRecords();
    void testLoadRecordById();
    void testUpdateRecord();
    void testDeleteRecord();

    // ========== Hash Calculation ==========
    void testCalculateHash();
    void testCalculateHashConsistent();
    void testCalculateHashDifferent();

private:
    QTemporaryDir *m_tempDir;
    LocalFileBackend *m_backend;
};

void TestLocalFileBackend::initTestCase()
{
    qDebug() << "Starting LocalFileBackend tests";
}

void TestLocalFileBackend::cleanupTestCase()
{
    qDebug() << "LocalFileBackend tests complete";
}

void TestLocalFileBackend::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    m_backend = new LocalFileBackend(m_tempDir->path());
}

void TestLocalFileBackend::cleanup()
{
    delete m_backend;
    delete m_tempDir;
    m_backend = nullptr;
    m_tempDir = nullptr;
}

// ========== Construction Tests ==========

void TestLocalFileBackend::testConstruction()
{
    LocalFileBackend backend("/some/path");
    QCOMPARE(backend.basePath(), QString("/some/path"));
}

void TestLocalFileBackend::testBackendId()
{
    QCOMPARE(m_backend->backendId(), QString("local-file"));
}

void TestLocalFileBackend::testDisplayName()
{
    QCOMPARE(m_backend->displayName(), QString("Local Files"));
}

void TestLocalFileBackend::testBasePath()
{
    QCOMPARE(m_backend->basePath(), m_tempDir->path());
}

// ========== Availability Tests ==========

void TestLocalFileBackend::testIsAvailableValid()
{
    // Temp dir should exist and be writable
    QVERIFY(m_backend->isAvailable());
}

void TestLocalFileBackend::testIsAvailableInvalid()
{
    LocalFileBackend backend("/nonexistent/path/that/does/not/exist");
    QVERIFY(!backend.isAvailable());
}

// ========== File Extension Tests ==========

void TestLocalFileBackend::testDefaultExtensions()
{
    QCOMPARE(m_backend->fileExtension("memos"), QString(".md"));
    QCOMPARE(m_backend->fileExtension("contacts"), QString(".vcf"));
    QCOMPARE(m_backend->fileExtension("calendar"), QString(".ics"));
    QCOMPARE(m_backend->fileExtension("todos"), QString(".ics"));
}

void TestLocalFileBackend::testSetFileExtension()
{
    m_backend->setFileExtension("custom", ".txt");
    QCOMPARE(m_backend->fileExtension("custom"), QString(".txt"));
}

void TestLocalFileBackend::testFileExtensionUnknownCollection()
{
    // Unknown collections should return empty or default
    QString ext = m_backend->fileExtension("unknown");
    // Can be empty or default
    QVERIFY(ext.isEmpty() || ext.startsWith("."));
}

// ========== Collection Tests ==========

void TestLocalFileBackend::testAvailableCollections()
{
    // Create some collections
    QDir(m_tempDir->path()).mkdir("memos");
    QDir(m_tempDir->path()).mkdir("contacts");

    QList<CollectionInfo> collections = m_backend->availableCollections();

    // Should find at least the directories we created
    bool foundMemos = false;
    bool foundContacts = false;
    for (const CollectionInfo &info : collections) {
        if (info.id == "memos") foundMemos = true;
        if (info.id == "contacts") foundContacts = true;
    }

    QVERIFY(foundMemos);
    QVERIFY(foundContacts);
}

void TestLocalFileBackend::testCollectionInfo()
{
    QDir(m_tempDir->path()).mkdir("memos");

    CollectionInfo info = m_backend->collectionInfo("memos");

    QCOMPARE(info.id, QString("memos"));
}

void TestLocalFileBackend::testCreateCollection()
{
    QString id = m_backend->createCollection(CollectionInfo{"newcollection", "New Collection", "test"});

    QVERIFY(!id.isEmpty());
    QVERIFY(QDir(m_tempDir->path()).exists("newcollection"));
}

// ========== Record Operations Tests ==========

void TestLocalFileBackend::testCreateRecord()
{
    // Create collection first
    QDir(m_tempDir->path()).mkdir("memos");

    BackendRecord record;
    record.data = "Test memo content";
    record.displayName = "Test";

    QString recordId = m_backend->createRecord("memos", record);

    QVERIFY(!recordId.isEmpty());
}

void TestLocalFileBackend::testLoadRecords()
{
    // Create collection and add a file
    QString memosDir = m_tempDir->path() + "/memos";
    QDir(m_tempDir->path()).mkdir("memos");

    QFile file(memosDir + "/test.md");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Test content");
    file.close();

    QList<BackendRecord*> records = m_backend->loadRecords("memos");

    QCOMPARE(records.size(), 1);

    // Cleanup
    qDeleteAll(records);
}

void TestLocalFileBackend::testLoadRecordById()
{
    // Create collection and add a file
    QString memosDir = m_tempDir->path() + "/memos";
    QDir(m_tempDir->path()).mkdir("memos");

    QFile file(memosDir + "/specific.md");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Specific content");
    file.close();

    BackendRecord *record = m_backend->loadRecord(memosDir + "/specific.md");

    QVERIFY(record != nullptr);
    QCOMPARE(QString::fromUtf8(record->data), QString("Specific content"));

    delete record;
}

void TestLocalFileBackend::testUpdateRecord()
{
    // Create collection and add a file
    QString memosDir = m_tempDir->path() + "/memos";
    QDir(m_tempDir->path()).mkdir("memos");

    QFile file(memosDir + "/update.md");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Original content");
    file.close();

    BackendRecord record;
    record.id = memosDir + "/update.md";
    record.data = "Updated content";

    bool result = m_backend->updateRecord(record);
    QVERIFY(result);

    // Verify content was updated
    QFile readFile(memosDir + "/update.md");
    QVERIFY(readFile.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(readFile.readAll());
    QCOMPARE(content, QString("Updated content"));
}

void TestLocalFileBackend::testDeleteRecord()
{
    // Create collection and add a file
    QString memosDir = m_tempDir->path() + "/memos";
    QDir(m_tempDir->path()).mkdir("memos");

    QFile file(memosDir + "/delete.md");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Delete me");
    file.close();

    QString recordId = memosDir + "/delete.md";
    QVERIFY(QFile::exists(recordId));

    bool result = m_backend->deleteRecord(recordId);
    QVERIFY(result);

    QVERIFY(!QFile::exists(recordId));
}

// ========== Hash Calculation ==========

void TestLocalFileBackend::testCalculateHash()
{
    QByteArray data = "Test data for hashing";
    QString hash = LocalFileBackend::calculateHash(data);

    QVERIFY(!hash.isEmpty());
    QVERIFY(hash.length() > 10);  // Should be a reasonable hash length
}

void TestLocalFileBackend::testCalculateHashConsistent()
{
    QByteArray data = "Same content";

    QString hash1 = LocalFileBackend::calculateHash(data);
    QString hash2 = LocalFileBackend::calculateHash(data);

    QCOMPARE(hash1, hash2);
}

void TestLocalFileBackend::testCalculateHashDifferent()
{
    QString hash1 = LocalFileBackend::calculateHash("Content A");
    QString hash2 = LocalFileBackend::calculateHash("Content B");

    QVERIFY(hash1 != hash2);
}

QTEST_MAIN(TestLocalFileBackend)
#include "test_localfilebackend.moc"
