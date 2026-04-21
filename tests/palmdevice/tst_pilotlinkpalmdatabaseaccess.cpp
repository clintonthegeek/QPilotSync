#include <QtTest/QtTest>

#include "mockkpilotlink.h"
#include "pilotlinkpalmdatabaseaccess.h"

using WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPilotLinkPalmDatabaseAccess : public QObject
{
    Q_OBJECT
private slots:
    void availableDatabasesListsMockSeeded();
    void hasDatabaseAnswersMembership();
    void readAllRecordsRoundTripsBytes();
    void readRecordFindsSeededById();
    void readRecordReturnsNulloptWhenMissing();
    void createRecordAssignsIdAndPersists();
    void updateRecordWritesBack();
    void deleteRecordRemovesFromDevice();
    void supportsDeleteTrackingIsFalse();
    void recordsDeletedSinceIsEmpty();
};

void TestPilotLinkPalmDatabaseAccess::availableDatabasesListsMockSeeded()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedDatabase(QStringLiteral("DatebookDB"));

    PilotLinkPalmDatabaseAccess dev(&link);
    auto names = dev.availableDatabases();
    std::sort(names.begin(), names.end());
    QCOMPARE(names, QStringList()
             << QStringLiteral("DatebookDB")
             << QStringLiteral("MemoDB"));
}

void TestPilotLinkPalmDatabaseAccess::hasDatabaseAnswersMembership()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(dev.hasDatabase(QStringLiteral("MemoDB")));
    QVERIFY(!dev.hasDatabase(QStringLiteral("DatebookDB")));
}

void TestPilotLinkPalmDatabaseAccess::readAllRecordsRoundTripsBytes()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 1, 0, 0,
                    QByteArrayLiteral("first"));
    link.seedRecord(QStringLiteral("MemoDB"), 2, 3, 0,
                    QByteArrayLiteral("second"));

    PilotLinkPalmDatabaseAccess dev(&link);
    const auto recs = dev.readAllRecords(QStringLiteral("MemoDB"));
    QCOMPARE(recs.size(), 2);

    QByteArrayList payloads;
    for (const auto &r : recs) payloads.append(r.data);
    std::sort(payloads.begin(), payloads.end());
    QCOMPARE(payloads, QByteArrayList()
             << QByteArrayLiteral("first")
             << QByteArrayLiteral("second"));
}

void TestPilotLinkPalmDatabaseAccess::readRecordFindsSeededById()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 42, 0, 0,
                    QByteArrayLiteral("hello"));

    PilotLinkPalmDatabaseAccess dev(&link);
    const auto got = dev.readRecord(QStringLiteral("MemoDB"), 42);
    QVERIFY(got.has_value());
    QCOMPARE(got->recordId, 42u);
    QCOMPARE(got->data, QByteArrayLiteral("hello"));
}

void TestPilotLinkPalmDatabaseAccess::readRecordReturnsNulloptWhenMissing()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), 99).has_value());
    QVERIFY(!dev.readRecord(QStringLiteral("NoDB"), 1).has_value());
}

void TestPilotLinkPalmDatabaseAccess::createRecordAssignsIdAndPersists()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));

    PilotLinkPalmDatabaseAccess dev(&link);
    PalmRecord rec;
    rec.data = QByteArrayLiteral("new");

    const auto id = dev.createRecord(QStringLiteral("MemoDB"), rec);
    QVERIFY(id > 0);
    QVERIFY(link.hasRecord(QStringLiteral("MemoDB"),
                           static_cast<int>(id)));
    QCOMPARE(link.recordData(QStringLiteral("MemoDB"),
                             static_cast<int>(id)),
             QByteArrayLiteral("new"));
}

void TestPilotLinkPalmDatabaseAccess::updateRecordWritesBack()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 7, 0, 0,
                    QByteArrayLiteral("orig"));

    PilotLinkPalmDatabaseAccess dev(&link);
    PalmRecord rec;
    rec.recordId = 7;
    rec.data = QByteArrayLiteral("updated");
    QVERIFY(dev.updateRecord(QStringLiteral("MemoDB"), rec));
    QCOMPARE(link.recordData(QStringLiteral("MemoDB"), 7),
             QByteArrayLiteral("updated"));

    // recordId==0 is rejected.
    PalmRecord zero;
    zero.data = QByteArrayLiteral("no");
    QVERIFY(!dev.updateRecord(QStringLiteral("MemoDB"), zero));
}

void TestPilotLinkPalmDatabaseAccess::deleteRecordRemovesFromDevice()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    link.seedRecord(QStringLiteral("MemoDB"), 7, 0, 0,
                    QByteArrayLiteral("gone"));

    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), 7));
    QVERIFY(!link.hasRecord(QStringLiteral("MemoDB"), 7));
}

void TestPilotLinkPalmDatabaseAccess::supportsDeleteTrackingIsFalse()
{
    MockKPilotLink link;
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(!dev.supportsDeleteTracking());
}

void TestPilotLinkPalmDatabaseAccess::recordsDeletedSinceIsEmpty()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);
    QVERIFY(dev.recordsDeletedSince(QStringLiteral("MemoDB"),
                                    QDateTime()).isEmpty());
}

QTEST_MAIN(TestPilotLinkPalmDatabaseAccess)
#include "tst_pilotlinkpalmdatabaseaccess.moc"
