#include <QtTest>
#include <QTemporaryDir>
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrevisionstore.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPalmChangeDetection : public QObject {
    Q_OBJECT
private slots:
    void mockRevision_emptyForUnknownDb();
    void mockRevision_bumpsOnWrite();
    void palmBackendForwardsRevision();
    void revisionStore_persistsAcrossInstances();
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

void TestPalmChangeDetection::revisionStore_persistsAcrossInstances()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/palm-revisions.ini";
    {
        WildPalms::PalmSync::PalmRevisionStore s(path);
        QVERIFY(s.token("palm:calendar").isEmpty());
        s.setToken("palm:calendar", "42");
    }
    WildPalms::PalmSync::PalmRevisionStore s2(path);  // fresh instance, same file
    QCOMPARE(s2.token("palm:calendar"), QString("42"));
}

QTEST_MAIN(TestPalmChangeDetection)
#include "tst_palm_change_detection.moc"
