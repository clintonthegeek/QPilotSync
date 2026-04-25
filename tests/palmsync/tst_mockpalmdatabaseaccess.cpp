#include <QtTest/QtTest>

#include "mockpalmdatabaseaccess.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestMockPalmDatabaseAccess : public QObject
{
    Q_OBJECT
private slots:
    void createDatabaseMakesItVisible();
    void createRecordAssignsIdWhenZero();
    void createRecordKeepsExplicitId();
    void readRecordReturnsNulloptWhenMissing();
    void updateRecordFailsForMissingId();
    void deleteRecordLogsDeletion();
    void modifiedSinceFiltersByTimestamp();
    void deletedSinceFiltersByTimestamp();
    void appBlockRoundTrip();
};

void TestMockPalmDatabaseAccess::createDatabaseMakesItVisible()
{
    MockPalmDatabaseAccess dev;
    QVERIFY(!dev.hasDatabase(QStringLiteral("MemoDB")));
    QVERIFY(dev.createDatabase(QStringLiteral("MemoDB")));
    QVERIFY(dev.hasDatabase(QStringLiteral("MemoDB")));
    QCOMPARE(dev.availableDatabases(),
             QStringList() << QStringLiteral("MemoDB"));
}

void TestMockPalmDatabaseAccess::createRecordAssignsIdWhenZero()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.data = QByteArrayLiteral("hello");
    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);
    QVERIFY(id > 0);

    const auto got = dev.readRecord(QStringLiteral("MemoDB"), id);
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("hello"));
    QCOMPARE(got->recordId, id);
    QVERIFY(got->lastModified.isValid());
}

void TestMockPalmDatabaseAccess::createRecordKeepsExplicitId()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.recordId = 42;
    rec.data = QByteArrayLiteral("body");
    QCOMPARE(dev.createRecord(QStringLiteral("MemoDB"), rec), 42u);

    // Next auto-assigned ID must not collide with 42.
    PalmRecord next;
    next.data = QByteArrayLiteral("next");
    QVERIFY(dev.createRecord(QStringLiteral("MemoDB"), next) > 42u);
}

void TestMockPalmDatabaseAccess::readRecordReturnsNulloptWhenMissing()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), 99).has_value());
    QVERIFY(!dev.readRecord(QStringLiteral("NoDB"), 1).has_value());
}

void TestMockPalmDatabaseAccess::updateRecordFailsForMissingId()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.recordId = 7;
    rec.data = QByteArrayLiteral("x");
    QVERIFY(!dev.updateRecord(QStringLiteral("MemoDB"), rec));

    // recordId 0 is a hard error — always fails.
    PalmRecord zero;
    zero.data = QByteArrayLiteral("x");
    QVERIFY(!dev.updateRecord(QStringLiteral("MemoDB"), zero));
}

void TestMockPalmDatabaseAccess::deleteRecordLogsDeletion()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.data = QByteArrayLiteral("x");
    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);

    QDateTime beforeDelete = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), id));
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), id).has_value());

    const auto deleted = dev.recordsDeletedSince(
        QStringLiteral("MemoDB"), beforeDelete);
    QCOMPARE(deleted, QList<std::uint32_t>() << id);
}

void TestMockPalmDatabaseAccess::modifiedSinceFiltersByTimestamp()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord old;
    old.data = QByteArrayLiteral("old");
    old.lastModified = QDateTime::fromString(
        QStringLiteral("2020-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    dev.createRecord(QStringLiteral("MemoDB"), old);

    PalmRecord fresh;
    fresh.data = QByteArrayLiteral("fresh");
    // Auto-assigned lastModified == now.
    dev.createRecord(QStringLiteral("MemoDB"), fresh);

    const auto cutoff = QDateTime::fromString(
        QStringLiteral("2024-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    const auto modified = dev.recordsModifiedSince(
        QStringLiteral("MemoDB"), cutoff);
    QCOMPARE(modified.size(), 1);
    QCOMPARE(modified.first().data, QByteArrayLiteral("fresh"));
}

void TestMockPalmDatabaseAccess::deletedSinceFiltersByTimestamp()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord rec;
    rec.data = QByteArrayLiteral("gone");
    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);
    dev.deleteRecord(QStringLiteral("MemoDB"), id);

    const auto future = QDateTime::currentDateTimeUtc().addSecs(3600);
    QCOMPARE(dev.recordsDeletedSince(QStringLiteral("MemoDB"), future),
             QList<std::uint32_t>());
}

void TestMockPalmDatabaseAccess::appBlockRoundTrip()
{
    MockPalmDatabaseAccess dev;

    // Empty for unknown database.
    QCOMPARE(dev.readAppBlock(QStringLiteral("DatebookDB")), QByteArray());

    // setAppBlock auto-creates database.
    const QByteArray bytes("\x01\x02\x03appinfo-payload", 19);
    dev.setAppBlock(QStringLiteral("DatebookDB"), bytes);
    QCOMPARE(dev.readAppBlock(QStringLiteral("DatebookDB")), bytes);

    // Overwriting works.
    const QByteArray bytes2("other", 5);
    dev.setAppBlock(QStringLiteral("DatebookDB"), bytes2);
    QCOMPARE(dev.readAppBlock(QStringLiteral("DatebookDB")), bytes2);
}

QTEST_MAIN(TestMockPalmDatabaseAccess)
#include "tst_mockpalmdatabaseaccess.moc"
