#ifndef WILDPALMS_APP_ACCOUNTS_ADDACCOUNTDIALOG_H
#define WILDPALMS_APP_ACCOUNTS_ADDACCOUNTDIALOG_H

#include <QDialog>
#include <memory>

namespace Kalburator::Sync {
    class IProvider;
    struct BackendConfiguration;
}
class QComboBox;
class QStackedWidget;
class QPushButton;
class QLabel;

namespace WildPalms::App::Accounts {

/// Modal: pick CalDAV or CardDAV → fill provider's createConfigWidget →
/// Test Connection → Save. On accept, configuration() returns the populated
/// BackendConfiguration; selectedKind() returns "caldav" or "carddav".
class AddAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddAccountDialog(QWidget *parent = nullptr);
    ~AddAccountDialog() override;

    QString selectedKind() const;
    Kalburator::Sync::BackendConfiguration configuration() const;

private slots:
    void onKindChanged(int index);
    void onTestConnection();

private:
    void buildUi();

    QComboBox      *m_kindCombo {nullptr};
    QStackedWidget *m_configStack {nullptr};
    QPushButton    *m_testButton {nullptr};
    QLabel         *m_statusLabel {nullptr};

    // One provider instance per kind, kept alive so its config widget
    // (parented into the stack) stays valid.
    std::unique_ptr<Kalburator::Sync::IProvider> m_calDavProvider;
    std::unique_ptr<Kalburator::Sync::IProvider> m_cardDavProvider;
};

}  // namespace WildPalms::App::Accounts

#endif
