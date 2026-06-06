#include <QtTest/QtTest>
#include "runtime/clobberdialog.h"

using namespace WildPalms::Runtime;

class TstClobberDialog : public QObject { Q_OBJECT
private slots:
    void initial_selection_empty()
    {
        ClobberDialog::DomainMappings input = {
            { "calendar", { "m-cal-1" } },
            { "contacts", { "m-con-1" } },
        };
        ClobberDialog dlg(input);
        QVERIFY(dlg.selectedMappingIds().isEmpty());
    }

    void select_one_domain_returns_its_mapping_ids()
    {
        ClobberDialog::DomainMappings input = {
            { "calendar", { "m-cal-1", "m-cal-2" } },
            { "contacts", { "m-con-1" } },
        };
        ClobberDialog dlg(input);
        dlg.setDomainChecked("calendar", true);
        const auto ids = dlg.selectedMappingIds();
        QCOMPARE(ids.size(), 2);
        QVERIFY(ids.contains("m-cal-1"));
        QVERIFY(ids.contains("m-cal-2"));
    }
};
QTEST_MAIN(TstClobberDialog)
#include "tst_clobber_dialog.moc"
