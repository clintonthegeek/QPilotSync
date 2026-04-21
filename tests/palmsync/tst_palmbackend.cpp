#include <QtTest/QtTest>

#include "backendrecord.h"
#include "collectioninfo.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

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

QTEST_MAIN(TestPalmBackend)
#include "tst_palmbackend.moc"
