#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QThread>

#include "runtime/palmdeviceaccess.h"
#include "mockkpilotdevicelink.h"

class TstPalmDeviceAccessConnect : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void emptyConstructor_does_not_crash_on_overrides();
    void connect_emits_started_then_complete_on_success();
    void connect_emits_complete_false_on_failure();
    void disconnect_emits_disconnected_when_connected();
    void is_connected_tracks_state();
    void multiple_connect_calls_rejected();
};

void TstPalmDeviceAccessConnect::initTestCase()
{
    // Required for HandshakeResult to be passed via Qt signals.
    qRegisterMetaType<HandshakeResult>("HandshakeResult");
}

void TstPalmDeviceAccessConnect::emptyConstructor_does_not_crash_on_overrides()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;

    // Pre-Task-2 fix: m_impl is null; calling overrides should return
    // sane defaults, not crash.
    QCOMPARE(access.availableDatabases(), QStringList{});
    QCOMPARE(access.hasDatabase(QStringLiteral("foo")), false);
    QCOMPARE(access.supportsDeleteTracking(), false);
    QVERIFY(!access.isConnected());
    QVERIFY(!access.isConnecting());
}

void TstPalmDeviceAccessConnect::connect_emits_started_then_complete_on_success()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;

    // Inject a factory that returns a pre-configured success mock.
    HandshakeResult expected;
    expected.userInfoValid = true;
    expected.userName = QStringLiteral("Test User");
    expected.userId = 12345;
    expected.socket = -1; // -1 to keep PalmTickle from actually trying dlp_*

    access.setLinkFactoryForTest([&expected](const QStringList &paths) {
        auto *mock = new MockKPilotDeviceLink(paths);
        mock->setNextResultSuccess(expected);
        return mock;
    });

    QSignalSpy started(&access, &PalmDeviceAccess::connectionStarted);
    QSignalSpy complete(&access, &PalmDeviceAccess::connectionComplete);

    access.connectDevice({QStringLiteral("/dev/mock")});

    QVERIFY(complete.wait(2000));
    QCOMPARE(started.count(), 1);
    QCOMPARE(complete.count(), 1);
    const auto args = complete.takeFirst();
    QCOMPARE(args.at(0).toBool(), true);
    QCOMPARE(args.at(1).toString(), QString());
    QVERIFY(access.isConnected());
    QCOMPARE(access.handshakeUserName(), QStringLiteral("Test User"));
    QCOMPARE(access.handshakeUserId(), 12345u);
}

void TstPalmDeviceAccessConnect::connect_emits_complete_false_on_failure()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;

    access.setLinkFactoryForTest([](const QStringList &paths) {
        auto *mock = new MockKPilotDeviceLink(paths);
        mock->setNextResultFailure(QStringLiteral("simulated failure"));
        return mock;
    });

    QSignalSpy complete(&access, &PalmDeviceAccess::connectionComplete);
    access.connectDevice({QStringLiteral("/dev/nonexistent")});

    QVERIFY(complete.wait(2000));
    QCOMPARE(complete.count(), 1);
    const auto args = complete.takeFirst();
    QCOMPARE(args.at(0).toBool(), false);
    QCOMPARE(args.at(1).toString(), QStringLiteral("simulated failure"));
    QVERIFY(!access.isConnected());
}

void TstPalmDeviceAccessConnect::disconnect_emits_disconnected_when_connected()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;

    HandshakeResult result;
    result.userInfoValid = true;
    result.socket = -1;
    access.setLinkFactoryForTest([&result](const QStringList &paths) {
        auto *mock = new MockKPilotDeviceLink(paths);
        mock->setNextResultSuccess(result);
        return mock;
    });

    QSignalSpy complete(&access, &PalmDeviceAccess::connectionComplete);
    QSignalSpy disconnected(&access, &PalmDeviceAccess::deviceDisconnected);

    access.connectDevice({QStringLiteral("/dev/mock")});
    QVERIFY(complete.wait(2000));
    QVERIFY(access.isConnected());

    access.disconnectDevice();
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(!access.isConnected());
}

void TstPalmDeviceAccessConnect::is_connected_tracks_state()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;

    QVERIFY(!access.isConnected());
    QVERIFY(!access.isConnecting());

    HandshakeResult result;
    result.socket = -1;
    access.setLinkFactoryForTest([&result](const QStringList &paths) {
        auto *mock = new MockKPilotDeviceLink(paths);
        mock->setNextResultSuccess(result);
        return mock;
    });

    QSignalSpy complete(&access, &PalmDeviceAccess::connectionComplete);
    access.connectDevice({QStringLiteral("/dev/mock")});

    // Synchronously set in connectDevice() before the QueuedConnection bounce.
    QVERIFY(access.isConnecting());
    QVERIFY(!access.isConnected());

    // While connect is in flight (before QTimer::singleShot(0) fires),
    // isConnecting should briefly be true. Race-prone — mostly we care
    // about the post-state.
    QVERIFY(complete.wait(2000));
    QVERIFY(access.isConnected());
    QVERIFY(!access.isConnecting());

    access.disconnectDevice();
    QVERIFY(!access.isConnected());
    QVERIFY(!access.isConnecting());
}

void TstPalmDeviceAccessConnect::multiple_connect_calls_rejected()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;

    HandshakeResult result;
    result.socket = -1;
    int factoryCalls = 0;
    access.setLinkFactoryForTest([&](const QStringList &paths) {
        ++factoryCalls;
        auto *mock = new MockKPilotDeviceLink(paths);
        mock->setNextResultSuccess(result);
        return mock;
    });

    QSignalSpy started(&access, &PalmDeviceAccess::connectionStarted);
    QSignalSpy complete(&access, &PalmDeviceAccess::connectionComplete);

    access.connectDevice({QStringLiteral("/dev/mock")});
    access.connectDevice({QStringLiteral("/dev/mock2")});  // should be ignored
    QVERIFY(complete.wait(2000));

    QCOMPARE(started.count(), 1);
    QCOMPARE(complete.count(), 1);
    QCOMPARE(factoryCalls, 1);
}

QTEST_MAIN(TstPalmDeviceAccessConnect)
#include "tst_palm_device_access_connect.moc"
