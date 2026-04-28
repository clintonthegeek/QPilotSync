#include <QtTest/QtTest>
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/mockpalmfileinstaller.h"
#include "palm/sync/palmbackend.h"

class TestPalmDeviceConnection : public QObject {
    Q_OBJECT
private slots:
    void exposesDeviceAndPalmBackend();
    void palmBackendIsUsableAcrossCalls();
    void fileInstaller_returnsConfiguredInstance();
    void fileInstaller_isNullByDefault();
};

void TestPalmDeviceConnection::exposesDeviceAndPalmBackend()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);

    QCOMPARE(conn.device(), &dev);
    QVERIFY(conn.palmBackend() != nullptr);
    QCOMPARE(conn.palmBackend()->backendId(), QStringLiteral("palm"));
}

void TestPalmDeviceConnection::palmBackendIsUsableAcrossCalls()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    PalmDeviceConnection conn(&dev);

    auto *pb = conn.palmBackend();
    QVERIFY(pb == conn.palmBackend()); // same pointer — not reconstructed

    const auto collections = pb->availableCollections();
    bool sawMemo = false;
    for (const auto &c : collections) {
        if (c.id == QStringLiteral("palm:memo")) sawMemo = true;
    }
    QVERIFY(sawMemo);
}

void TestPalmDeviceConnection::fileInstaller_returnsConfiguredInstance()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess  db;
    WildPalms::PalmSync::MockPalmFileInstaller   installer;
    PalmDeviceConnection conn(&db, &installer);
    QCOMPARE(conn.fileInstaller(),
             static_cast<WildPalms::PalmSync::IPalmFileInstaller*>(&installer));
}

void TestPalmDeviceConnection::fileInstaller_isNullByDefault()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess db;
    PalmDeviceConnection conn(&db);
    QVERIFY(conn.fileInstaller() == nullptr);
}

QTEST_MAIN(TestPalmDeviceConnection)
#include "tst_palmdeviceconnection.moc"
