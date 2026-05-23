// tests/runtime/tst_addaccountspage.cpp
#include <QtTest/QtTest>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/addaccountspage.h"
#include "app/wizard/wizardstate.h"
#include "app/accounts/accountformwidget.h"

#include <backendregistry.h>

using WildPalms::Wizard::AddAccountsPage;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::PendingAccount;
using WildPalms::App::Accounts::AccountFormWidget;
using Kalburator::Sync::BackendRegistry;

class TstAddAccountsPage : public QObject {
    Q_OBJECT
private slots:
    void emptyPendingAccountsRendersNoForms();
    void pendingAccountsRenderStackedForms();
    void incompleteWhenNoPendingAccounts();
};

void TstAddAccountsPage::emptyPendingAccountsRendersNoForms()
{
    BackendRegistry reg;
    WizardState s;
    AddAccountsPage page(&reg, &s);
    page.initializePage();
    // No PendingAccounts → no AccountFormWidgets.
    const auto forms = page.findChildren<AccountFormWidget*>();
    QCOMPARE(forms.size(), 0);
}

void TstAddAccountsPage::pendingAccountsRenderStackedForms()
{
    BackendRegistry reg;
    WizardState s;
    PendingAccount a;
    a.id = QStringLiteral("a-id"); a.kind = QStringLiteral("caldav");
    PendingAccount b;
    b.id = QStringLiteral("b-id"); b.kind = QStringLiteral("carddav");
    s.pendingAccounts.append(a);
    s.pendingAccounts.append(b);

    AddAccountsPage page(&reg, &s);
    page.initializePage();
    const auto forms = page.findChildren<AccountFormWidget*>();
    QCOMPARE(forms.size(), 2);
}

void TstAddAccountsPage::incompleteWhenNoPendingAccounts()
{
    // Defensive: page should never be reached if pendingAccounts is empty,
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
