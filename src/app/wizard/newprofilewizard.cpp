#include "newprofilewizard.h"
#include "namepage.h"
#include "accountssetuppage.h"
#include "targetpickerpage.h"
#include "reviewpage.h"

#include "runtime/profileregistry.h"
#include "runtime/conduitcatalog.h"
#include "plugins/pimplugin.h"

namespace WildPalms::Wizard {

NewProfileWizard::NewProfileWizard(WildPalms::Runtime::ProfileRegistry *registry,
                                   Kalburator::Sync::BackendRegistry *backendRegistry,
                                   QWidget *parent)
    : QWizard(parent)
    , m_profileRegistry(registry)
    , m_backendRegistry(backendRegistry)
{
    setWindowTitle(tr("New Wild Palms Profile"));
    setWizardStyle(QWizard::ModernStyle);

    // Substrate A1: enumerate the stock conduit descriptors instead of a
    // hardcoded id list. Seed one RawFiles mapping per conduit; the Accounts
    // and Bindings pages edit these in place as the user makes selections.
    m_conduits = WildPalms::Runtime::createStockConduits();
    for (const auto &c : m_conduits) {
        MappingSpec s;
        s.pluginId = c->conduitId();
        s.kind     = TargetKind::RawFiles;
        m_state.mappings.append(s);
    }

    setPage(NamePageId, new NamePage(m_profileRegistry, &m_state, this));
    setPage(AccountsPageId,
            new AccountsSetupPage(m_backendRegistry, &m_state, this));
    setPage(TargetPickerPageId, new TargetPickerPage(&m_state, &m_conduits, this));
    setPage(ReviewPageId, new ReviewPage(&m_state, this));
    setStartId(NamePageId);
}

NewProfileWizard::~NewProfileWizard() = default;

Result NewProfileWizard::result() const
{
    Result r;
    r.state = m_state;
    return r;
}

}  // namespace WildPalms::Wizard
