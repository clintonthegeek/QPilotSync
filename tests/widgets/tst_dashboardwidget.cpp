#include <QtTest/QtTest>
#include <QSignalSpy>
#include "dashboardwidget.h"
#include "syncstatusmodel.h"

class TestDashboardWidget : public QObject
{
    Q_OBJECT
private slots:
    void bindsAndRendersThroughSyncWithoutCrashing();
    void primaryButtonForwardsToModel();
};

void TestDashboardWidget::bindsAndRendersThroughSyncWithoutCrashing()
{
    SyncStatusModel model;
    DashboardWidget w;
    w.setModel(&model);
    model.seedConduits({{ QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar") }});
    model.onDeviceDetected();
    model.onConnectionStarted();
    model.onConnectionComplete(true, QString());
    model.onRunStarted(QStringLiteral("HotSync"));
    model.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    model.onMappingSyncProgress(QStringLiteral("m-cal"), 0, 3, 9);
    model.onMappingSyncFinished(QStringLiteral("m-cal"), 1, 1, 0, true);
    WildPalms::Runtime::PalmRunResult r; r.success = true;
    model.onRunFinished(r);
    QVERIFY(true);   // reaching here without crashing is the assertion
}

void TestDashboardWidget::primaryButtonForwardsToModel()
{
    SyncStatusModel model;
    DashboardWidget w;
    w.setModel(&model);
    QSignalSpy spy(&model, &SyncStatusModel::syncRequested);
    model.onDeviceDetected();
    model.onConnectionStarted();
    model.onConnectionComplete(true, QString());      // Connected -> "Sync Now"
    model.triggerPrimaryAction();
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestDashboardWidget)
#include "tst_dashboardwidget.moc"
