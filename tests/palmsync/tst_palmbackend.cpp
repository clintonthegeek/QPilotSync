#include <QCryptographicHash>
#include <QtTest/QtTest>

#include "backendrecord.h"
#include "collectioninfo.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmrecord.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

static PalmRecord makePalm(std::uint32_t id, const QByteArray &payload)
{
    PalmRecord r;
    r.recordId = id;
    r.data = payload;
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

class TestPalmBackend : public QObject
{
    Q_OBJECT
private slots:
    void identity();
    void collectionIdRoundTrip();
    void recordIdRoundTrip();
    void decodeCollectionIdRejectsRecordIds();

    void availableCollectionsReflectsDevice();
    void collectionInfoReturnsEmptyForUnknown();
    void createCollectionDelegatesToDevice();

    void loadRecordsExposesBackendRecords();
    void loadRecordFindsById();
    void createRecordAssignsId();
    void updateRecordWritesBack();
    void deleteRecordRemovesFromDevice();
    void contentHashIsSha256OfData();
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

QTEST_MAIN(TestPalmBackend)
#include "tst_palmbackend.moc"
