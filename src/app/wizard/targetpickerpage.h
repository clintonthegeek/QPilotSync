#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H

#include <QHash>
#include <QWizardPage>

namespace WildPalms::Wizard {

struct WizardState;
class TargetPickerRow;

/// Page 3 — Bindings. Four domain rows; each picks (account, collection)
/// from the accounts created on the AccountsSetupPage, or Local files.
class TargetPickerPage : public QWizardPage {
    Q_OBJECT
public:
    explicit TargetPickerPage(WizardState *state, QWidget *parent = nullptr);

    void initializePage() override;
    bool isComplete() const override { return true; }

    // Called by rows (and exposed for tests). Empty accountId == Local files.
    void selectBinding(const QString &pluginId,
                       const QString &accountId,
                       const QString &collectionId);

private:
    void buildRows();
    int  mappingIndex(const QString &pluginId) const;

    WizardState *m_state;
    QHash<QString, TargetPickerRow*> m_rows;   // pluginId -> row
};

}  // namespace WildPalms::Wizard

#endif
