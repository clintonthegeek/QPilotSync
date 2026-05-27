#include <QtTest/QtTest>

#include "mockkpilotlink.h"
#include "pilotlinkpalmdatabaseaccess.h"

using WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPilotLinkBatching : public QObject
{
    Q_OBJECT
private slots:
    void consecutiveWritesOpenOnce();
    void readBetweenWritesFlushes();
    void writeToDifferentDbReopens();
    void flushPendingWritesClosesHandle();
};

static PalmRecord makeRec(const QByteArray &data)
{
    PalmRecord r;
    r.recordId = 0;       // new record
    r.category = 0;
    r.data = data;
    return r;
}

void TestPilotLinkBatching::consecutiveWritesOpenOnce()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    for (int i = 0; i < 50; ++i)
        dev.createRecord(QStringLiteral("DatebookDB"), makeRec(QByteArray::number(i)));

    QCOMPARE(link.writeRecordCalls, 50);
    QCOMPARE(link.openDatabaseCalls, 1);   // opened once, not 50x
    QCOMPARE(link.closeDatabaseCalls, 0);  // still open until flush/read
}

void TestPilotLinkBatching::readBetweenWritesFlushes()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("a"));
    QCOMPARE(link.openDatabaseCalls, 1);
    QCOMPARE(link.closeDatabaseCalls, 0);

    dev.readAllRecords(QStringLiteral("DatebookDB"));   // read flushes the write handle
    QCOMPARE(link.closeDatabaseCalls, 2);               // write handle closed + read's own RO handle closed
    // (read opens its own RO handle; that is allowed to open/close as before)

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("b"));
    QCOMPARE(link.openDatabaseCalls, 3);   // 1 write + 1 read + 1 new write handle
}

void TestPilotLinkBatching::writeToDifferentDbReopens()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("a"));
    dev.createRecord(QStringLiteral("MemoDB"), makeRec("b"));   // different db -> close+open
    QCOMPARE(link.openDatabaseCalls, 2);
    QCOMPARE(link.closeDatabaseCalls, 1);
}

void TestPilotLinkBatching::flushPendingWritesClosesHandle()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("a"));
    QCOMPARE(link.closeDatabaseCalls, 0);
    dev.flushPendingWrites();
    QCOMPARE(link.closeDatabaseCalls, 1);
}

QTEST_MAIN(TestPilotLinkBatching)
#include "tst_pilotlinkbatching.moc"
