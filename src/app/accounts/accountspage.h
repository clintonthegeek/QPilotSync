#ifndef WILDPALMS_APP_ACCOUNTS_ACCOUNTSPAGE_H
#define WILDPALMS_APP_ACCOUNTS_ACCOUNTSPAGE_H

#include <QWidget>

namespace WildPalms::Runtime {
    class AccountController;
    class PalmRuntime;
}
class QListWidget;
class QStackedWidget;
class QPushButton;

namespace WildPalms::App::Accounts {

/// KPageWidget item content for the SettingsDialog "Accounts" page.
/// Left: provider list. Right: selected provider's createConfigWidget().
/// Buttons: Add (opens AddAccountDialog → AC::addProvider →
/// MappingPromptDialog (Task 10)), Remove (confirm → AC::removeProvider).
/// Off-state interlock: Add/Remove disabled while
/// PalmRuntime::isRunning().
class AccountsPage : public QWidget {
    Q_OBJECT
public:
    AccountsPage(WildPalms::Runtime::AccountController *accounts,
                 WildPalms::Runtime::PalmRuntime *palmRuntime,
                 QWidget *parent = nullptr);

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onProviderRowChanged(int row);
    void onPalmRunStarted();
    void onPalmRunFinished();
    void refreshList();

private:
    void buildUi();
    void updateInterlock();

    WildPalms::Runtime::AccountController *m_accounts;
    WildPalms::Runtime::PalmRuntime       *m_palmRuntime;

    QListWidget    *m_list {nullptr};
    QStackedWidget *m_rightPane {nullptr};
    QPushButton    *m_addBtn {nullptr};
    QPushButton    *m_removeBtn {nullptr};
};

}  // namespace WildPalms::App::Accounts

#endif
