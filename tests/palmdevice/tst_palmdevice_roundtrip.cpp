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
