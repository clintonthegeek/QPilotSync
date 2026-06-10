#ifndef WILDPALMS_APP_ACCOUNTS_ADDACCOUNTDIALOG_H
#define WILDPALMS_APP_ACCOUNTS_ADDACCOUNTDIALOG_H

#include <QDialog>

namespace Kalburator::Sync {
    class BackendRegistry;
    struct BackendConfiguration;
}

namespace WildPalms::App::Accounts {

class AccountFormWidget;

/// Modal wrapper around AccountFormWidget. Used by Settings → Accounts.
/// (F.1c.1's NewProfileWizard embeds AccountFormWidget directly instead.)
class AddAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddAccountDialog(Kalburator::Sync::BackendRegistry *registry,
                              QWidget *parent = nullptr);
    ~AddAccountDialog() override;

    QString selectedKind() const;
    Kalburator::Sync::BackendConfiguration configuration() const;

    /// Pre-fill the form from an existing account (Edit flows).
    void setConfiguration(const Kalburator::Sync::BackendConfiguration &cfg);

private:
    AccountFormWidget *m_form {nullptr};
};

}  // namespace WildPalms::App::Accounts

#endif
