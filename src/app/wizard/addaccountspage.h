#ifndef WILDPALMS_APP_WIZARD_ADDACCOUNTSPAGE_H
#define WILDPALMS_APP_WIZARD_ADDACCOUNTSPAGE_H

#include <QWizardPage>
#include <QList>

namespace Kalburator::Sync { class BackendRegistry; }
namespace WildPalms::App::Accounts { class AccountFormWidget; }

namespace WildPalms::Wizard {

struct WizardState;

/// Page 3 (conditional, shown only if WizardState::pendingAccounts is
/// non-empty). Stacks one AccountFormWidget per PendingAccount with the
/// kind locked. validatePage() writes each widget's configuration() back
/// into the matching PendingAccount.config.
class AddAccountsPage : public QWizardPage {
    Q_OBJECT
public:
    AddAccountsPage(Kalburator::Sync::BackendRegistry *registry,
                    WizardState *state,
                    QWidget *parent = nullptr);

    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override;
    int  nextId() const override;

private:
    Kalburator::Sync::BackendRegistry *m_registry;
    WizardState *m_state;
    QList<WildPalms::App::Accounts::AccountFormWidget*> m_forms;
    QWidget *m_container {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
