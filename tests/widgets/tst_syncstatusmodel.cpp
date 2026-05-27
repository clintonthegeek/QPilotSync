#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QDateTime>

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
    void repeatedFailureUpdatesErrorText();
    void seedingCreatesPendingChips();
    void mappingStartedMarksActiveAndPreviousDone();
    void mappingProgressUpdatesCounts();
    void mappingFinishedFillsCountsAndState();
    void unplugMidSyncInterruptsActiveChip();
    void runProgressUpdatesProgressFields();
    void runFinishedBuildsDigestAndReturnsToConnected();
    void runFinishedAfterUnplugStaysDisconnected();
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

void TestSyncStatusModel::repeatedFailureUpdatesErrorText()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(false, QStringLiteral("port busy"));
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Listening);
    QSignalSpy spy(&m, &SyncStatusModel::changed);
    m.onConnectionComplete(false, QStringLiteral("permission denied"));
    QCOMPARE(m.errorText(), QStringLiteral("permission denied"));
    QVERIFY(spy.count() >= 1);
}

static QVector<SyncStatusModel::ConduitSeed> twoSeeds()
{
    return {
        { QStringLiteral("m-cal"), QStringLiteral("Calendar"),  QStringLiteral("office-calendar") },
        { QStringLiteral("m-con"), QStringLiteral("Contacts"),  QStringLiteral("contact-new") },
    };
}

void TestSyncStatusModel::seedingCreatesPendingChips()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    QCOMPARE(m.conduits().size(), 2);
    QCOMPARE(m.conduits()[0].label, QStringLiteral("Calendar"));
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Pending);
}

void TestSyncStatusModel::mappingStartedMarksActiveAndPreviousDone()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Active);
    m.onMappingSyncStarted(QStringLiteral("m-con"), QStringLiteral("Contacts"), QStringLiteral("contact-new"));
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Done);   // previous auto-done
    QCOMPARE(m.conduits()[1].state, SyncStatusModel::ChipState::Active);
}

void TestSyncStatusModel::mappingProgressUpdatesCounts()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    m.onMappingSyncProgress(QStringLiteral("m-cal"), 0, 12, 45);
    QCOMPARE(m.conduits()[0].current, 12);
    QCOMPARE(m.conduits()[0].total, 45);
}

void TestSyncStatusModel::mappingFinishedFillsCountsAndState()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    m.onMappingSyncFinished(QStringLiteral("m-cal"), 3, 2, 1, true);
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Done);
    QCOMPARE(m.conduits()[0].created, 3);
    QCOMPARE(m.conduits()[0].modified, 2);
    QCOMPARE(m.conduits()[0].deleted, 1);
    SyncStatusModel m2;
    m2.seedConduits(twoSeeds());
    m2.onRunStarted(QStringLiteral("HotSync"));
    m2.onMappingSyncFinished(QStringLiteral("m-con"), 0, 0, 0, false);
    QCOMPARE(m2.conduits()[1].state, SyncStatusModel::ChipState::Error);
}

void TestSyncStatusModel::unplugMidSyncInterruptsActiveChip()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    m.onDeviceLost();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Disconnected);
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Interrupted);
}

void TestSyncStatusModel::runProgressUpdatesProgressFields()
{
    SyncStatusModel m;
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onRunProgress(2, 6, QStringLiteral("Syncing Contacts"));
    QCOMPARE(m.progressCurrent(), 2);
    QCOMPARE(m.progressTotal(), 6);
    QCOMPARE(m.progressMessage(), QStringLiteral("Syncing Contacts"));
    QCOMPARE(m.headline(), QStringLiteral("Syncing Contacts"));
}

void TestSyncStatusModel::runFinishedBuildsDigestAndReturnsToConnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncFinished(QStringLiteral("m-cal"), 3, 2, 1, true);
    m.onMappingSyncFinished(QStringLiteral("m-con"), 1, 0, 0, true);

    WildPalms::Runtime::PalmRunResult r;
    r.success = true;
    r.startTime = QDateTime::currentDateTime().addSecs(-8);
    r.endTime = QDateTime::currentDateTime();
    m.onRunFinished(r);

    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Connected);
    QVERIFY(m.lastDigest().valid);
    QCOMPARE(m.lastDigest().totalChanges, 7);          // 3+2+1 + 1
    QVERIFY(m.lastDigest().success);
}

void TestSyncStatusModel::runFinishedAfterUnplugStaysDisconnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onDeviceLost();                                   // unplugged mid-sync
    WildPalms::Runtime::PalmRunResult r;
    r.success = false;
    r.errorMessage = QStringLiteral("link lost");
    m.onRunFinished(r);
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Disconnected);
}

QTEST_GUILESS_MAIN(TestSyncStatusModel)
#include "tst_syncstatusmodel.moc"
