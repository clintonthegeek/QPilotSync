#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/mockpalmfileinstaller.h"
#include "plugins/install/installactionplugin.h"
#include "runtime/simpleactioncontext.h"

using namespace WildPalms;
using namespace WildPalms::PalmSync;

namespace {
QJsonObject makeFile(const QString &path, const QString &name)
{
    QJsonObject o;
    o["path"]         = path;
    o["display_name"] = name;
    return o;
}
} // namespace

class TestInstallActionPlugin : public QObject
{
    Q_OBJECT

private slots:
    void execute_emptyList_returnsTrue()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QJsonObject params;
        params["files"] = QJsonArray{};
        QVERIFY(action.execute(&ctx, &conn, params));
        QCOMPARE(inst.installedPaths().size(), 0);
    }

    void execute_installsAllAndReturnsTrue()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a.prc")));
        files.append(makeFile(QStringLiteral("/tmp/b.pdb"), QStringLiteral("b.pdb")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(action.execute(&ctx, &conn, params));
        QCOMPARE(inst.installedPaths(), (QStringList{
            QStringLiteral("/tmp/a.prc"), QStringLiteral("/tmp/b.pdb")}));
        QCOMPARE(ctx.total(), 2);
        QCOMPARE(ctx.current(), 2);
    }

    void execute_partialFailure_returnsFalseEmitsSignals()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        inst.setNextResult(true);
        inst.setNextResult(false, QStringLiteral("simulated"));
        inst.setNextResult(true);

        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QSignalSpy spyOk    (&action, &InstallActionPlugin::fileInstalled);
        QSignalSpy spyFail  (&action, &InstallActionPlugin::fileFailed);

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a")));
        files.append(makeFile(QStringLiteral("/tmp/b.prc"), QStringLiteral("b")));
        files.append(makeFile(QStringLiteral("/tmp/c.prc"), QStringLiteral("c")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(!action.execute(&ctx, &conn, params));
        QCOMPARE(spyOk.count(),   2);
        QCOMPARE(spyFail.count(), 1);
        QCOMPARE(inst.installedPaths().size(), 3);
    }

    void execute_cancellation_stopsLoop()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QObject::connect(&action, &InstallActionPlugin::fileInstalled,
                          [&ctx](const QString &) { ctx.cancel(); });

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a")));
        files.append(makeFile(QStringLiteral("/tmp/b.prc"), QStringLiteral("b")));
        files.append(makeFile(QStringLiteral("/tmp/c.prc"), QStringLiteral("c")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(!action.execute(&ctx, &conn, params));
        QCOMPARE(inst.installedPaths().size(), 1);
    }

    void execute_noFileInstaller_returnsFalse()
    {
        MockPalmDatabaseAccess  db;
        PalmDeviceConnection    conn(&db);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(!action.execute(&ctx, &conn, params));
    }

    void preconditions_requiresDevice()
    {
        InstallActionPlugin a;
        QCOMPARE(a.preconditions().requiresDeviceConnection, true);
        QVERIFY(a.preconditions().requiresFiles.isEmpty());
    }

    void identity_metadata()
    {
        InstallActionPlugin a;
        QCOMPARE(a.pluginId(),    QStringLiteral("install"));
        QCOMPARE(a.displayName(), QStringLiteral("Install Files"));
        QCOMPARE(a.version(),     QStringLiteral("2.0.0"));
    }
};

QTEST_MAIN(TestInstallActionPlugin)
#include "tst_installactionplugin.moc"
