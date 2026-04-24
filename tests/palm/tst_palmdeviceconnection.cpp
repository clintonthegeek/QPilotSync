#include <QtTest/QtTest>
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"

class TestPalmDeviceConnection : public QObject {
    Q_OBJECT
private slots:
    void exposesDeviceAndPalmBackend();
    void palmBackendIsUsableAcrossCalls();
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

QTEST_MAIN(TestPalmDeviceConnection)
#include "tst_palmdeviceconnection.moc"
