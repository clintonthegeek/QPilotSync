#include "discoverypage.h"
#include "discoveryrow.h"
#include "wizardstate.h"

#include <QLabel>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

DiscoveryPage::DiscoveryPage(Kalburator::Sync::BackendRegistry *registry,
                             WizardState *state,
                             QWidget *parent)
    : QWizardPage(parent)
    , m_registry(registry)
    , m_state(state)
{
    setTitle(tr("Discover collections"));
    setSubTitle(tr("Choose a collection on each remote account."));

    m_container = new QWidget(this);
    auto *outer = new QVBoxLayout(this);
    outer->addWidget(m_container);
    new QVBoxLayout(m_container);
}

void DiscoveryPage::initializePage()
{
    qDeleteAll(m_rows);
    m_rows.clear();
    m_mappingIndexForRow.clear();

    auto *layout = qobject_cast<QVBoxLayout*>(m_container->layout());
    if (!layout) return;
    while (auto *item = layout->takeAt(0)) {
        if (auto *w = item->widget()) w->deleteLater();
        delete item;
    }
    if (!m_state) return;

    for (int i = 0; i < m_state->mappings.size(); ++i) {
        const auto &m = m_state->mappings[i];
        if (m.kind != TargetKind::Account) continue;

        // Find the WizardAccount for accountRef.
        const WizardAccount *acc = nullptr;
        for (const auto &a : m_state->accounts) {
            if (a.id == m.accountRef) { acc = &a; break; }
        }
        if (!acc) continue;

        auto *row = new DiscoveryRow(m_registry, *acc, m_container);
        connect(row, &DiscoveryRow::readinessChanged, this,
                [this]() { emit completeChanged(); });
        layout->addWidget(row);
        m_rows.append(row);
        m_mappingIndexForRow.append(i);
    }
    layout->addStretch();
    emit completeChanged();
}

bool DiscoveryPage::isComplete() const
{
    if (m_rows.isEmpty()) return true;   // defensive
    for (auto *r : m_rows) {
        if (!r || r->state() != DiscoveryRow::Chosen) return false;
    }
    return true;
}

bool DiscoveryPage::validatePage()
{
    if (!isComplete()) return false;
    if (!m_state) return true;
    for (int k = 0; k < m_rows.size(); ++k) {
        const int mi = m_mappingIndexForRow[k];
        if (mi >= 0 && mi < m_state->mappings.size()) {
            m_state->mappings[mi].collectionId = m_rows[k]->chosenCollectionId();
        }
    }
    return true;
}

}  // namespace WildPalms::Wizard
