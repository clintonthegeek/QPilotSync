#ifndef WILDPALMS_APP_ACCOUNTS_ACCOUNTFORMWIDGET_H
#define WILDPALMS_APP_ACCOUNTS_ACCOUNTFORMWIDGET_H

#include <QWidget>
#include <memory>
#include <vector>

namespace Kalburator::Sync {
    class IProvider;
    class BackendRegistry;
    struct BackendConfiguration;
}
class QComboBox;
class QStackedWidget;
class QPushButton;
class QLabel;

namespace WildPalms::App::Accounts {

/// Reusable account form. Populates a kind combo from
/// BackendRegistry::contributions() and stacks each provider's
/// createConfigWidget(). Used by AddAccountDialog (Settings → Accounts
/// and the NewProfileWizard's AccountsSetupPage).
class AccountFormWidget : public QWidget {
    Q_OBJECT
public:
    explicit AccountFormWidget(Kalburator::Sync::BackendRegistry *registry,
                               QWidget *parent = nullptr);
    ~AccountFormWidget() override;

    QString selectedKind() const;
    Kalburator::Sync::BackendConfiguration configuration() const;
    bool isValid() const;

    /// Populate the form from a persisted account (Edit flows). Selects the
    /// kind matching cfg.type and forwards cfg to that kind's config widget.
    /// No-op when cfg.type isn't among the registered contributions.
    void setConfiguration(const Kalburator::Sync::BackendConfiguration &cfg);

private slots:
    void onKindChanged(int index);
    void onTestConnection();

private:
    void buildUi();

    Kalburator::Sync::BackendRegistry *m_registry {nullptr};
    QComboBox      *m_kindCombo {nullptr};
    QStackedWidget *m_configStack {nullptr};
    QPushButton    *m_testButton {nullptr};
    QLabel         *m_statusLabel {nullptr};

    QString                 m_lastTestError;
    QMetaObject::Connection m_errorConn;

    std::vector<std::unique_ptr<Kalburator::Sync::IProvider>> m_providers;
};

}  // namespace WildPalms::App::Accounts

#endif
