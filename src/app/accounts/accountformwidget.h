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

/// Reusable credential form. Populates a kind combo from
/// BackendRegistry::contributions() and stacks each provider's
/// createConfigWidget(). Used by AddAccountDialog and (F.1c.1)
/// the NewProfileWizard's AddAccountsPage.
///
/// Two construction modes:
///   - kind-selectable (default ctor): combo visible, user picks
///   - kind-locked (lockedKind ctor): combo hidden; only the
///     locked kind's config widget shown
class AccountFormWidget : public QWidget {
    Q_OBJECT
public:
    explicit AccountFormWidget(Kalburator::Sync::BackendRegistry *registry,
                               QWidget *parent = nullptr);
    AccountFormWidget(Kalburator::Sync::BackendRegistry *registry,
                      const QString &lockedKind,
                      QWidget *parent = nullptr);
    ~AccountFormWidget() override;

    QString selectedKind() const;
    Kalburator::Sync::BackendConfiguration configuration() const;
    bool isValid() const;

private slots:
    void onKindChanged(int index);
    void onTestConnection();

private:
    void buildUi(const QString &lockedKind);

    Kalburator::Sync::BackendRegistry *m_registry {nullptr};
    QComboBox      *m_kindCombo {nullptr};
    QStackedWidget *m_configStack {nullptr};
    QPushButton    *m_testButton {nullptr};
    QLabel         *m_statusLabel {nullptr};

    std::vector<std::unique_ptr<Kalburator::Sync::IProvider>> m_providers;
};

}  // namespace WildPalms::App::Accounts

#endif
