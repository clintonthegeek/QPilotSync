#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H

#include <QHash>
#include <QWizardPage>
#include <memory>
#include <vector>

namespace WildPalms::Plugins { class PimPlugin; }

namespace WildPalms::Wizard {

struct WizardState;
class TargetPickerRow;

/// Page 3 — Bindings. One row per conduit descriptor (substrate A1); each
/// picks (account, collection) from the accounts created on the
/// AccountsSetupPage, or Local files. The conduit set is borrowed from the
/// wizard (descriptor queries only) and must outlive this page.
class TargetPickerPage : public QWizardPage {
    Q_OBJECT
public:
    TargetPickerPage(WizardState *state,
                     const std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> *conduits,
                     QWidget *parent = nullptr);

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
    const std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> *m_conduits;
    QHash<QString, TargetPickerRow*> m_rows;   // conduitId -> row
};

}  // namespace WildPalms::Wizard

#endif
