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

    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord pr;
    pr.data = QByteArrayLiteral("palm-content");
    pr.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), pr);

    PalmBackend palm(&dev);

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
