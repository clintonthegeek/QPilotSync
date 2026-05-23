#include "addaccountspage.h"
#include "wizardstate.h"

#include "app/accounts/accountformwidget.h"

#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

using WildPalms::App::Accounts::AccountFormWidget;

AddAccountsPage::AddAccountsPage(Kalburator::Sync::BackendRegistry *registry,
                                 WizardState *state,
                                 QWidget *parent)
    : QWizardPage(parent)
    , m_registry(registry)
    , m_state(state)
{
    setTitle(tr("Add accounts"));
    setSubTitle(tr("Fill in credentials for each new remote account."));

    m_container = new QWidget(this);
    auto *outer = new QVBoxLayout(this);
    outer->addWidget(m_container);
    new QVBoxLayout(m_container);
}

void AddAccountsPage::initializePage()
{
    // Tear down any prior forms (page may be re-entered).
    qDeleteAll(m_forms);
    m_forms.clear();

    auto *layout = qobject_cast<QVBoxLayout*>(m_container->layout());
    if (!layout) return;
    // Clear residual widgets.
    while (auto *item = layout->takeAt(0)) {
        if (auto *w = item->widget()) w->deleteLater();
        delete item;
    }

    if (!m_state) return;

    for (auto &pa : m_state->pendingAccounts) {
        auto *box = new QGroupBox(
            tr("New %1 account").arg(pa.kind.toUpper()), m_container);
        auto *boxLayout = new QVBoxLayout(box);

        auto *form = new AccountFormWidget(m_registry, pa.kind, box);
        boxLayout->addWidget(form);
        m_forms.append(form);

        layout->addWidget(box);
    }
    layout->addStretch();

    emit completeChanged();
}

bool AddAccountsPage::isComplete() const
{
    // No forms (empty pendingAccounts, defensive — wizard's nextId() skips
    // this page in that case): the page is trivially complete.
    if (m_forms.isEmpty()) return true;
    for (auto *f : m_forms) {
        if (!f || !f->isValid()) return false;
    }
    return true;
}

bool AddAccountsPage::validatePage()
{
    if (!isComplete()) return false;
    if (!m_state) return true;
    for (int i = 0; i < m_forms.size() && i < m_state->pendingAccounts.size(); ++i) {
        if (m_forms[i]) {
            m_state->pendingAccounts[i].config = m_forms[i]->configuration();
        }
    }
    return true;
}

}  // namespace WildPalms::Wizard
