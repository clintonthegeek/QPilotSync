#ifndef WILDPALMS_APP_WIZARD_NEWPROFILEWIZARD_H
#define WILDPALMS_APP_WIZARD_NEWPROFILEWIZARD_H

#include <QWizard>
#include <memory>
#include <vector>
#include "wizardstate.h"

namespace Kalburator::Sync { class BackendRegistry; }
namespace WildPalms::Runtime { class ProfileRegistry; }
namespace WildPalms::Plugins { class PimPlugin; }

namespace WildPalms::Wizard {

struct Result {
    WizardState state;
};

class NewProfileWizard : public QWizard {
    Q_OBJECT
public:
    NewProfileWizard(WildPalms::Runtime::ProfileRegistry *registry,
                     Kalburator::Sync::BackendRegistry *backendRegistry,
                     QWidget *parent = nullptr);
    ~NewProfileWizard() override;

    WildPalms::Runtime::ProfileRegistry *profileRegistry() const { return m_profileRegistry; }
    Kalburator::Sync::BackendRegistry *backendRegistry() const   { return m_backendRegistry; }

    WizardState *state() { return &m_state; }
    Result result() const;

    /// Substrate A1: transient conduit descriptors the wizard enumerates to
    /// seed mappings and drive the Bindings page's per-conduit rows. Owned
    /// here; borrowed (by the TargetPickerPage) for descriptor queries only.
    const std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> &
        conduits() const { return m_conduits; }

    // Page ids; flow is strictly sequential (QWizard default ordering).
    enum PageId {
        NamePageId = 0,
        AccountsPageId,
        TargetPickerPageId,
        ReviewPageId,
    };

private:
    WildPalms::Runtime::ProfileRegistry *m_profileRegistry;
    Kalburator::Sync::BackendRegistry   *m_backendRegistry;
    WizardState                          m_state;
    std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> m_conduits;
};

}  // namespace WildPalms::Wizard

#endif
