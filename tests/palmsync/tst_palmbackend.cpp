#include <QtTest/QtTest>

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

QTEST_MAIN(TestPalmBackend)
#include "tst_palmbackend.moc"
