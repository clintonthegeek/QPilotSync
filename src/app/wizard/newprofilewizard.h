#ifndef WILDPALMS_APP_WIZARD_NEWPROFILEWIZARD_H
#define WILDPALMS_APP_WIZARD_NEWPROFILEWIZARD_H

#include <QWizard>
#include "wizardstate.h"

namespace Kalburator::Sync { class BackendRegistry; }
namespace WildPalms::Runtime { class ProfileRegistry; }

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

    // Page ids used for QWizardPage::nextId() skip logic.
    enum PageId {
        NamePageId = 0,
        TargetPickerPageId,
        AddAccountsPageId,
        DiscoveryPageId,
        ReviewPageId,
    };

private:
    WildPalms::Runtime::ProfileRegistry *m_profileRegistry;
    Kalburator::Sync::BackendRegistry   *m_backendRegistry;
    WizardState                          m_state;
};

}  // namespace WildPalms::Wizard

#endif
