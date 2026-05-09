#include <QtTest/QtTest>

#include "mappingrowdialog.h"
#include "synctypes.h"

class TstMappingRowDialog : public QObject
{
    Q_OBJECT
private slots:
    void round_trips_mapping();
    void produces_unique_id_for_new_mapping();
    void target_combo_round_trips_existing_value();
    void setTargetBackends_populates_combo();
    void provider_bound_target_round_trips();
    void default_add_uses_rawfiles_when_target_combo_empty();
};

void TstMappingRowDialog::round_trips_mapping()
{
    Kalburator::Sync::SyncMapping in;
    in.id = QStringLiteral("test-1");
    in.sourceBackend = QStringLiteral("calendar-palm");
    in.sourceCalendar = QStringLiteral("cal-A");
    in.targetBackend = QStringLiteral("provider-uuid:addressbook-A");
    in.targetCalendar = QStringLiteral("cal-A-out");
    in.mode = Kalburator::Sync::SyncMode::TwoWay;
    in.conflictPolicy = Kalburator::Sync::ConflictResolution::AskUser;
    in.lossPolicy = Kalburator::Sync::WhenLossWouldOccur::Warn;
    in.enabled = true;

    MappingRowDialog dlg;
    dlg.setSourceBackends({QStringLiteral("calendar-palm"),
                           QStringLiteral("memo-palm")});
    dlg.setTargetBackends({QStringLiteral("rawfiles-cal"),
                           QStringLiteral("provider-uuid:addressbook-A")});
    dlg.setMapping(in);

    auto out = dlg.mapping();
    QCOMPARE(out.id, in.id);
    QCOMPARE(out.sourceBackend, in.sourceBackend);
    QCOMPARE(out.sourceCalendar, in.sourceCalendar);
    QCOMPARE(out.targetBackend, in.targetBackend);
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

void TstMappingRowDialog::target_combo_round_trips_existing_value()
{
    Kalburator::Sync::SyncMapping in;
    in.id = "t1";
    in.sourceBackend = "palm:contact/0";
    in.targetBackend = "uuid-X:addressbook-1";
    in.targetCalendar = "addressbook-1";

    MappingRowDialog dlg;
    dlg.setSourceBackends({"palm:contact/0"});
    dlg.setTargetBackends({"rawfiles-cal", "uuid-X:addressbook-1",
                           "uuid-X:addressbook-2"});
    dlg.setMapping(in);

    auto out = dlg.mapping();
    QCOMPARE(out.targetBackend, in.targetBackend);
}

void TstMappingRowDialog::setTargetBackends_populates_combo()
{
    MappingRowDialog dlg;
    dlg.setTargetBackends({"a", "b", "c"});
    // Trigger via mapping() — no setMapping call → first item selected.
    auto out = dlg.mapping();
    QCOMPARE(out.targetBackend, QStringLiteral("a"));
}

void TstMappingRowDialog::provider_bound_target_round_trips()
{
    Kalburator::Sync::SyncMapping in;
    in.id = "t2";
    in.sourceBackend = "palm:contact/0";
    in.targetBackend = "00000000-1111-2222-3333-444444444444:abc";
    in.targetCalendar = "abc";

    MappingRowDialog dlg;
    dlg.setSourceBackends({"palm:contact/0"});
    // target NOT seeded — combo round-trips via findText/addItem fallback.
    dlg.setMapping(in);

    auto out = dlg.mapping();
    QCOMPARE(out.targetBackend, in.targetBackend);
}

void TstMappingRowDialog::default_add_uses_rawfiles_when_target_combo_empty()
{
    MappingRowDialog dlg;  // no setTargetBackends → seeded with rawfiles-cal
    auto out = dlg.mapping();
    QCOMPARE(out.targetBackend, QStringLiteral("rawfiles-cal"));
}

QTEST_MAIN(TstMappingRowDialog)
#include "tst_mapping_row_dialog.moc"
