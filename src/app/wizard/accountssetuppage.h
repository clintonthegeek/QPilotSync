#ifndef WILDPALMS_APP_WIZARD_ACCOUNTSSETUPPAGE_H
#define WILDPALMS_APP_WIZARD_ACCOUNTSSETUPPAGE_H

#include <QHash>
#include <QSet>
#include <QWizardPage>
#include <memory>
#include <unordered_map>

class QPushButton;
class QVBoxLayout;

namespace Kalburator::Sync {
    class BackendRegistry;
    class IProvider;
    struct BackendConfiguration;
}

namespace WildPalms::Wizard {

struct WizardState;

/// Page 2 — create the profile's accounts up front. Each account is
/// configured via AddAccountDialog, then its provider connect()s
/// immediately so collections are known before the Bindings page. The
/// page owns the transient providers (wizard lifetime); discovery
/// results are value-copied into WizardState::accounts. Skippable:
/// zero accounts == local-files-only profile.
class AccountsSetupPage : public QWizardPage {
    Q_OBJECT
public:
    AccountsSetupPage(Kalburator::Sync::BackendRegistry *registry,
                      WizardState *state,
                      QWidget *parent = nullptr);
    ~AccountsSetupPage() override;

    void initializePage() override;
    bool isComplete() const override;   // false only while a connect() is in flight

    // Programmatic seams: the Add/Edit/Remove buttons route through these;
    // tests call them directly to bypass the modal dialog.
    QString addAccountFromConfig(
        const QString &kind, const Kalburator::Sync::BackendConfiguration &cfg);
    void editAccountConfig(
        const QString &id, const QString &kind,
        const Kalburator::Sync::BackendConfiguration &cfg);
    void removeAccount(const QString &id);

private slots:
    void onAddClicked();

private:
    void onEditClicked(const QString &id);
    void connectAccount(const QString &id);
    void rebuildList();
    int  accountIndex(const QString &id) const;

    Kalburator::Sync::BackendRegistry *m_registry;
    WizardState *m_state;
    QVBoxLayout *m_listLayout {nullptr};
    QPushButton *m_addButton {nullptr};

    std::unordered_map<std::string, std::unique_ptr<Kalburator::Sync::IProvider>> m_providers;
    QHash<QString, QString> m_lastError;
    QSet<QString> m_inFlightIds;
};

}  // namespace WildPalms::Wizard

#endif
