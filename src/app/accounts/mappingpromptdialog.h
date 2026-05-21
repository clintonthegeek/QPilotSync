#ifndef WILDPALMS_APP_ACCOUNTS_MAPPINGPROMPTDIALOG_H
#define WILDPALMS_APP_ACCOUNTS_MAPPINGPROMPTDIALOG_H

#include <QDialog>

namespace WildPalms::Runtime { class AccountController; }
class QTableWidget;

namespace WildPalms::App::Accounts {

/// Modal opened after a successful AccountController::addProvider().
/// Shows discovered collections × Palm slot picker. Save writes new
/// SyncMappings to Profile via AccountController::appendMappings. Skip
/// is harmless — user can bind via MappingEditor later.
class MappingPromptDialog : public QDialog {
    Q_OBJECT
public:
    MappingPromptDialog(WildPalms::Runtime::AccountController *accounts,
                        const QString &providerId,
                        QWidget *parent = nullptr);

private slots:
    void onSave();

private:
    void buildUi();

    WildPalms::Runtime::AccountController *m_accounts;
    QString                                m_providerId;
    QTableWidget                          *m_table {nullptr};
};

}  // namespace WildPalms::App::Accounts

#endif
