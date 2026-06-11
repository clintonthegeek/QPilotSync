#include "targetpickerpage.h"
#include "targetpickerrow.h"
#include "wizardstate.h"

#include "plugins/pimplugin.h"

#include <QVBoxLayout>

namespace WildPalms::Wizard {

TargetPickerPage::TargetPickerPage(
    WizardState *state,
    const std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> *conduits,
    QWidget *parent)
    : QWizardPage(parent)
    , m_state(state)
    , m_conduits(conduits)
{
    setTitle(tr("Sync targets"));
    setSubTitle(tr("Pick where each Palm domain syncs. Go back to the "
                   "Accounts page if a collection you expect is missing."));
    buildRows();
}

void TargetPickerPage::buildRows()
{
    auto *layout = new QVBoxLayout(this);
    if (!m_conduits) return;
    // Substrate A1: one row per conduit descriptor (not a hardcoded id list).
    for (const auto &c : *m_conduits) {
        const QString pid = c->conduitId();
        auto *row = new TargetPickerRow(c.get(), m_state, this);
        layout->addWidget(row);
        m_rows.insert(pid, row);
        connect(row, &TargetPickerRow::bindingSelected, this,
                [this, pid](const QString &accountId, const QString &collectionId) {
                    selectBinding(pid, accountId, collectionId);
                });
    }
}

void TargetPickerPage::initializePage()
{
    for (auto *row : m_rows.values())
        row->rebuild();
}

int TargetPickerPage::mappingIndex(const QString &pluginId) const
{
    if (!m_state) return -1;
    for (int i = 0; i < m_state->mappings.size(); ++i)
        if (m_state->mappings[i].pluginId == pluginId) return i;
    return -1;
}

void TargetPickerPage::selectBinding(const QString &pluginId,
                                     const QString &accountId,
                                     const QString &collectionId)
{
    const int mi = mappingIndex(pluginId);
    if (mi < 0) return;
    if (accountId.isEmpty()) {
        m_state->mappings[mi].kind = TargetKind::RawFiles;
        m_state->mappings[mi].accountRef.clear();
        m_state->mappings[mi].collectionId.clear();
    } else {
        m_state->mappings[mi].kind         = TargetKind::Account;
        m_state->mappings[mi].accountRef   = accountId;
        m_state->mappings[mi].collectionId = collectionId;
    }
}

}  // namespace WildPalms::Wizard
