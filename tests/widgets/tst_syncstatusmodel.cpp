#include <QtTest/QtTest>
#include <QSignalSpy>

#include "syncstatusmodel.h"

class TestSyncStatusModel : public QObject
{
    Q_OBJECT
private slots:
    void initialStateIsListening();
    void detectThenHandshakeThenConnected();
    void connectionFailureReturnsToListening();
    void unplugFromConnectedGoesDisconnected();
    void primaryActionLabelTracksState();
    void changedSignalFiresOnTransition();
};

void TestSyncStatusModel::initialStateIsListening()
{
    SyncStatusModel m;
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Listening);
    QVERIFY(!m.primaryActionVisible());
}

void TestSyncStatusModel::detectThenHandshakeThenConnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Detected);
    m.onConnectionStarted();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Handshaking);
    m.onConnectionComplete(true, QString());
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Connected);
    QVERIFY(m.errorText().isEmpty());
}

void TestSyncStatusModel::connectionFailureReturnsToListening()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(false, QStringLiteral("port busy"));
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Listening);
    QCOMPARE(m.errorText(), QStringLiteral("port busy"));
}

void TestSyncStatusModel::unplugFromConnectedGoesDisconnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.onDeviceLost();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Disconnected);
}

void TestSyncStatusModel::primaryActionLabelTracksState()
{
    SyncStatusModel m;
    QVERIFY(m.primaryActionLabel().isEmpty());          // Listening
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    QCOMPARE(m.primaryActionLabel(), QStringLiteral("Sync Now"));   // Connected
    m.onRunStarted(QStringLiteral("HotSync"));
    QCOMPARE(m.primaryActionLabel(), QStringLiteral("Cancel"));     // Syncing
}

void TestSyncStatusModel::changedSignalFiresOnTransition()
{
    SyncStatusModel m;
    QSignalSpy spy(&m, &SyncStatusModel::changed);
    m.onDeviceDetected();
    QVERIFY(spy.count() >= 1);
}

QTEST_GUILESS_MAIN(TestSyncStatusModel)
#include "tst_syncstatusmodel.moc"
