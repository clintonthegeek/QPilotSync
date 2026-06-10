// tests/runtime/tst_addaccountspage.cpp
#include <QtTest/QtTest>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/addaccountspage.h"
#include "app/wizard/wizardstate.h"
#include "app/accounts/accountformwidget.h"

#include <backendregistry.h>

using WildPalms::Wizard::AddAccountsPage;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::WizardAccount;
using WildPalms::App::Accounts::AccountFormWidget;
using Kalburator::Sync::BackendRegistry;

class TstAddAccountsPage : public QObject {
    Q_OBJECT
private slots:
    void emptyWizardAccountsRendersNoForms();
    void accountsRenderStackedForms();
    void incompleteWhenNoWizardAccounts();
};

void TstAddAccountsPage::emptyWizardAccountsRendersNoForms()
{
    BackendRegistry reg;
    WizardState s;
    AddAccountsPage page(&reg, &s);
    page.initializePage();
    // No WizardAccounts → no AccountFormWidgets.
    const auto forms = page.findChildren<AccountFormWidget*>();
    QCOMPARE(forms.size(), 0);
}

void TstAddAccountsPage::accountsRenderStackedForms()
{
    BackendRegistry reg;
    WizardState s;
    WizardAccount a;
    a.id = QStringLiteral("a-id"); a.kind = QStringLiteral("caldav");
    WizardAccount b;
    b.id = QStringLiteral("b-id"); b.kind = QStringLiteral("carddav");
    s.accounts.append(a);
    s.accounts.append(b);

    AddAccountsPage page(&reg, &s);
    page.initializePage();
    const auto forms = page.findChildren<AccountFormWidget*>();
    QCOMPARE(forms.size(), 2);
}

void TstAddAccountsPage::incompleteWhenNoWizardAccounts()
{
    // Defensive: page should never be reached if accounts is empty,
    // but if it is reached, isComplete() returns true (no requirements to
    // satisfy). The wizard's nextId() skip logic handles the normal case.
    BackendRegistry reg;
    WizardState s;
    AddAccountsPage page(&reg, &s);
    page.initializePage();
    QVERIFY(page.isComplete());
}

WILDPALMS_QTEST_MAIN(TstAddAccountsPage)
#include "tst_addaccountspage.moc"
