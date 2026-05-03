#include <QtTest/QtTest>

#include "mappingrowdialog.h"
#include "synctypes.h"

class TstMappingRowDialog : public QObject
{
    Q_OBJECT
private slots:
    void round_trips_mapping();
    void produces_unique_id_for_new_mapping();
};

void TstMappingRowDialog::round_trips_mapping()
{
    Kalburator::Sync::SyncMapping in;
    in.id = QStringLiteral("test-1");
    in.sourceBackend = QStringLiteral("calendar-palm");
    in.sourceCalendar = QStringLiteral("cal-A");
    in.targetBackend = QStringLiteral("rawfiles-cal");
    in.targetCalendar = QStringLiteral("cal-A-out");
    in.mode = Kalburator::Sync::SyncMode::TwoWay;
    in.conflictPolicy = Kalburator::Sync::ConflictResolution::AskUser;
    in.lossPolicy = Kalburator::Sync::WhenLossWouldOccur::Warn;
    in.enabled = true;

    MappingRowDialog dlg;
    dlg.setSourceBackends({QStringLiteral("calendar-palm"),
                           QStringLiteral("memo-palm")});
    dlg.setMapping(in);

    auto out = dlg.mapping();
    QCOMPARE(out.id, in.id);
    QCOMPARE(out.sourceBackend, in.sourceBackend);
    QCOMPARE(out.sourceCalendar, in.sourceCalendar);
    QCOMPARE(out.targetCalendar, in.targetCalendar);
    QCOMPARE(out.enabled, in.enabled);
    QCOMPARE(static_cast<int>(out.mode),
             static_cast<int>(in.mode));
    QCOMPARE(static_cast<int>(out.conflictPolicy),
             static_cast<int>(in.conflictPolicy));
}

void TstMappingRowDialog::produces_unique_id_for_new_mapping()
{
    MappingRowDialog dlg;
    dlg.setSourceBackends({QStringLiteral("calendar-palm")});
    // No setMapping() call → "Add" mode. The dialog must seed a
    // non-empty id.
    auto out = dlg.mapping();
    QVERIFY(!out.id.isEmpty());
}

QTEST_MAIN(TstMappingRowDialog)
#include "tst_mapping_row_dialog.moc"
