// tests/runtime/tst_targetpickerpage.cpp
#include <QtTest/QtTest>
#include <QComboBox>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/targetpickerpage.h"
#include "app/wizard/targetpickerrow.h"
#include "app/wizard/wizardstate.h"

using WildPalms::Wizard::TargetPickerPage;
using WildPalms::Wizard::TargetPickerRow;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::PendingAccount;
using WildPalms::Wizard::TargetKind;

class TstTargetPickerPage : public QObject {
    Q_OBJECT
private slots:
    void seedsRawFilesByDefault();
    void memoRowDropdownDisabled();
    void addNewAppendsPendingAccount();
    void selectingExistingAccountUpdatesMappingRef();
};

namespace {
WizardState seedState() {
    WizardState s;
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        MappingSpec m;
        m.pluginId = pid;
        m.kind     = TargetKind::RawFiles;
        s.mappings.append(m);
    }
    return s;
}
} // namespace

void TstTargetPickerPage::seedsRawFilesByDefault()
{
    auto s = seedState();
    TargetPickerPage page(&s);
    page.initializePage();
    // All four rows default to RawFiles.
    QCOMPARE(s.mappings.size(), 4);
    for (const auto &m : s.mappings)
        QCOMPARE(m.kind, TargetKind::RawFiles);
}

void TstTargetPickerPage::memoRowDropdownDisabled()
{
    auto s = seedState();
    TargetPickerPage page(&s);
    page.initializePage();

    // The memo row exposes a QComboBox; in the memo row's case it's disabled.
    TargetPickerRow *memoRow = nullptr;
    for (auto *r : page.findChildren<TargetPickerRow*>()) {
        if (r->pluginId() == QStringLiteral("memo")) { memoRow = r; break; }
    }
    QVERIFY(memoRow);
    auto *combo = memoRow->findChild<QComboBox*>();
    QVERIFY(combo);
    QVERIFY(!combo->isEnabled());
}

void TstTargetPickerPage::addNewAppendsPendingAccount()
{
    auto s = seedState();
    TargetPickerPage page(&s);
    page.initializePage();

    // Simulate the "Add new caldav" selection on the Calendar row by invoking
    // the page's slot directly (the row would emit this on dropdown change).
    page.addNewAccount(QStringLiteral("calendar"), QStringLiteral("caldav"));

    QCOMPARE(s.pendingAccounts.size(), 1);
    QCOMPARE(s.pendingAccounts.first().kind, QStringLiteral("caldav"));
    QVERIFY(!s.pendingAccounts.first().id.isEmpty());

    // The calendar mapping now references the new pending account.
    QCOMPARE(s.mappings[0].kind, TargetKind::RemoteNew);
    QCOMPARE(s.mappings[0].accountRef, s.pendingAccounts.first().id);
}

void TstTargetPickerPage::selectingExistingAccountUpdatesMappingRef()
{
    auto s = seedState();
    PendingAccount existing;
    existing.id   = QStringLiteral("preset-caldav");
    existing.kind = QStringLiteral("caldav");
    existing.config.displayName = QStringLiteral("Preset");
    s.pendingAccounts.append(existing);

    TargetPickerPage page(&s);
    page.initializePage();

    page.selectExistingAccount(QStringLiteral("calendar"), existing.id);
    QCOMPARE(s.mappings[0].kind, TargetKind::RemoteNew);
    QCOMPARE(s.mappings[0].accountRef, existing.id);
}

WILDPALMS_QTEST_MAIN(TstTargetPickerPage)
#include "tst_targetpickerpage.moc"
