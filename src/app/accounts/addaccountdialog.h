#ifndef WILDPALMS_APP_ACCOUNTS_ADDACCOUNTDIALOG_H
#define WILDPALMS_APP_ACCOUNTS_ADDACCOUNTDIALOG_H

#include <QDialog>
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

/// Modal: pick a provider kind from BackendRegistry → fill createConfigWidget
/// → Test Connection → Save. On accept, configuration() returns the populated
/// BackendConfiguration; selectedKind() returns the contribution's backendType().
///
/// Provider-generic: the combo is populated from BackendRegistry::contributions()
/// at construction time; no CalDAV/CardDAV hard-coding.
class AddAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddAccountDialog(Kalburator::Sync::BackendRegistry *registry,
                              QWidget *parent = nullptr);
    ~AddAccountDialog() override;

    QString selectedKind() const;
    Kalburator::Sync::BackendConfiguration configuration() const;

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

    // One provider instance per registered contribution, kept alive so its
    // config widget (parented into the stack) stays valid.
    std::vector<std::unique_ptr<Kalburator::Sync::IProvider>> m_providers;
};

}  // namespace WildPalms::App::Accounts

#endif
