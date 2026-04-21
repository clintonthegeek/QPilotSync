#include <QtTest/QtTest>

#include "mockkpilotlink.h"

class TestMockKPilotLink : public QObject
{
    Q_OBJECT
private slots:
    void constructsAndConnects();
    void seedAndOpenDatabase();
    void seedAndReadRecord();
};

void TestMockKPilotLink::constructsAndConnects()
{
    MockKPilotLink link;
    QVERIFY(!link.isConnected());
    QVERIFY(link.openConnection());
    QVERIFY(link.isConnected());
    link.closeConnection();
    QVERIFY(!link.isConnected());
}

void TestMockKPilotLink::seedAndOpenDatabase()
{
    MockKPilotLink link;
    QVERIFY(link.seedDatabase(QStringLiteral("MemoDB")));
    QCOMPARE(link.listDatabases(),
             QStringList() << QStringLiteral("MemoDB"));

    const int h = link.openDatabase(QStringLiteral("MemoDB"));
    QVERIFY(h > 0);
    QVERIFY(link.closeDatabase(h));
    QVERIFY(!link.closeDatabase(h)); // second close fails
}

void TestMockKPilotLink::seedAndReadRecord()
{
    MockKPilotLink link;
    QVERIFY(link.seedDatabase(QStringLiteral("MemoDB")));
    QVERIFY(link.seedRecord(QStringLiteral("MemoDB"), 42, 1, 0,
                            QByteArrayLiteral("hello")));

    const int h = link.openDatabase(QStringLiteral("MemoDB"), true);
    QVERIFY(h > 0);
    auto *rec = link.readRecordById(h, 42);
    QVERIFY(rec);
    QCOMPARE(rec->recordId(), 42);
    QCOMPARE(rec->data(), QByteArrayLiteral("hello"));
    delete rec;
}

QTEST_MAIN(TestMockKPilotLink)
#include "tst_mockkpilotlink.moc"
