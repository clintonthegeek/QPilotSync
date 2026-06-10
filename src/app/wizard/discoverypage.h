#ifndef WILDPALMS_APP_WIZARD_DISCOVERYPAGE_H
#define WILDPALMS_APP_WIZARD_DISCOVERYPAGE_H

#include <QWizardPage>
#include <QList>

namespace Kalburator::Sync { class BackendRegistry; }

namespace WildPalms::Wizard {

struct WizardState;
class DiscoveryRow;

/// Page 4 (conditional, shown only if any mapping has kind Account).
/// One DiscoveryRow per remote mapping. Block-finish: Next is disabled
/// until every row reaches Chosen state. (F.1c brainstorm Q5.)
class DiscoveryPage : public QWizardPage {
    Q_OBJECT
public:
    DiscoveryPage(Kalburator::Sync::BackendRegistry *registry,
                  WizardState *state,
                  QWidget *parent = nullptr);

    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override;

private:
    Kalburator::Sync::BackendRegistry *m_registry;
    WizardState *m_state;
    QWidget *m_container {nullptr};
    QList<DiscoveryRow*> m_rows;
    QList<int> m_mappingIndexForRow;   // parallel: row[i] writes mappings[m_mappingIndexForRow[i]]
};

}  // namespace WildPalms::Wizard

#endif
