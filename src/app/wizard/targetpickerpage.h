#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H

#include <QWizardPage>
#include <QHash>

namespace WildPalms::Wizard {

struct WizardState;
class TargetPickerRow;

class TargetPickerPage : public QWizardPage {
    Q_OBJECT
public:
    explicit TargetPickerPage(WizardState *state, QWidget *parent = nullptr);

    void initializePage() override;
    bool isComplete() const override { return true; }

    // Called by rows (and exposed for tests).
    void addNewAccount(const QString &pluginId, const QString &kind);
    void selectExistingAccount(const QString &pluginId, const QString &accountId);

private:
    void buildRows();
    int  mappingIndex(const QString &pluginId) const;

    WizardState *m_state;
    QHash<QString, TargetPickerRow*> m_rows;   // pluginId -> row
};

}  // namespace WildPalms::Wizard

#endif
