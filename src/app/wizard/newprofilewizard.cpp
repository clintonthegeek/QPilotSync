#include "newprofilewizard.h"
#include "namepage.h"

#include "runtime/profileregistry.h"

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

    // Seed mappings with one RawFiles row per Palm plugin. Pages added in
    // T5–T10 will edit these in place as the user makes selections.
    for (const auto &pid : {
            QStringLiteral("calendar"),
            QStringLiteral("contacts"),
            QStringLiteral("memo"),
            QStringLiteral("todo") }) {
        MappingSpec s;
        s.pluginId = pid;
        s.kind     = TargetKind::RawFiles;
        m_state.mappings.append(s);
    }

    setPage(NamePageId, new NamePage(m_profileRegistry, &m_state, this));
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
