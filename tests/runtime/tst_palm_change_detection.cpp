#include <QtTest>
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPalmChangeDetection : public QObject {
    Q_OBJECT
private slots:
    void mockRevision_emptyForUnknownDb();
    void mockRevision_bumpsOnWrite();
    void palmBackendForwardsRevision();
};

void TestPalmChangeDetection::mockRevision_emptyForUnknownDb()
{
    MockPalmDatabaseAccess dev;
    QCOMPARE(dev.databaseRevision("NoSuchDB"), QString());
}

void TestPalmChangeDetection::mockRevision_bumpsOnWrite()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("DatebookDB");
    const QString r0 = dev.databaseRevision("DatebookDB");
    PalmRecord rec;                       // default record is fine for the bump
    dev.createRecord("DatebookDB", rec);
    const QString r1 = dev.databaseRevision("DatebookDB");
    QVERIFY(!r1.isEmpty());
    QVERIFY(r0 != r1);
}

void TestPalmChangeDetection::palmBackendForwardsRevision()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("AddressDB");
    WildPalms::PalmSync::PalmBackend backend(&dev);
    PalmRecord rec;
    dev.createRecord("AddressDB", rec);
    QCOMPARE(backend.databaseRevision("AddressDB"), dev.databaseRevision("AddressDB"));
    QVERIFY(!backend.databaseRevision("AddressDB").isEmpty());
}

QTEST_MAIN(TestPalmChangeDetection)
#include "tst_palm_change_detection.moc"
