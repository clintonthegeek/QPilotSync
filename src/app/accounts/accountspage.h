#ifndef WILDPALMS_APP_ACCOUNTS_ACCOUNTSPAGE_H
#define WILDPALMS_APP_ACCOUNTS_ACCOUNTSPAGE_H

#include <QWidget>

namespace WildPalms::Runtime {
    class AccountController;
    class PalmRuntime;
}
namespace Kalburator::Ui {
    class AccountsListWidget;
}

namespace WildPalms::App::Accounts {

/// KPageWidget item content for the SettingsDialog "Accounts" page.
/// Hosts Kalburator::Ui::AccountsListWidget; wires its signals to
/// AccountController. PalmRuntime interlock: widget disabled while sync runs.
class AccountsPage : public QWidget {
    Q_OBJECT
public:
    AccountsPage(WildPalms::Runtime::AccountController *accounts,
                 WildPalms::Runtime::PalmRuntime *palmRuntime,
                 QWidget *parent = nullptr);

private slots:
    void onAddClicked();
    void onPalmRunStarted();
    void onPalmRunFinished();
    void refreshList();

private:
    void buildUi();

    WildPalms::Runtime::AccountController *m_accounts;
    WildPalms::Runtime::PalmRuntime       *m_palmRuntime;

    Kalburator::Ui::AccountsListWidget *m_listWidget {nullptr};
};

}  // namespace WildPalms::App::Accounts

#endif
