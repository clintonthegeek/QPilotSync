#include "targetpickerpage.h"
#include "targetpickerrow.h"
#include "wizardstate.h"
#include "newprofilewizard.h"

#include <QUuid>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

int TargetPickerPage::nextId() const
{
    if (!m_state) return NewProfileWizard::ReviewPageId;
    if (!m_state->accounts.isEmpty())
        return NewProfileWizard::AddAccountsPageId;
    for (const auto &m : m_state->mappings) {
        if (m.kind == TargetKind::Account)
            return NewProfileWizard::DiscoveryPageId;
    }
    return NewProfileWizard::ReviewPageId;
}

TargetPickerPage::TargetPickerPage(WizardState *state, QWidget *parent)
    : QWizardPage(parent)
    , m_state(state)
{
    setTitle(tr("Sync targets"));
    setSubTitle(tr("Pick a target for each Palm domain. Use 'Local files' "
                   "for the simple default or add a remote account."));
    buildRows();
}

void TargetPickerPage::buildRows()
{
    auto *layout = new QVBoxLayout(this);
    struct DomainSpec { QString pluginId; QStringList compatible; };
    const QList<DomainSpec> domains = {
        { QStringLiteral("calendar"),
          { QStringLiteral("rawfiles"),
            QStringLiteral("caldav"),
            QStringLiteral("akonadi") } },
        { QStringLiteral("contacts"),
          { QStringLiteral("rawfiles"),
            QStringLiteral("carddav"),
            QStringLiteral("akonadi") } },
        { QStringLiteral("memo"),
          { QStringLiteral("rawfiles") } },   // disabled in row
        { QStringLiteral("todo"),
          { QStringLiteral("rawfiles"),
            QStringLiteral("caldav"),
            QStringLiteral("akonadi") } },
    };
    for (const auto &d : domains) {
        auto *row = new TargetPickerRow(d.pluginId, d.compatible, m_state, this);
        layout->addWidget(row);
        m_rows.insert(d.pluginId, row);
        connect(row, &TargetPickerRow::addNewRequested, this,
                [this, pid = d.pluginId](const QString &kind) {
                    addNewAccount(pid, kind);
                });
        connect(row, &TargetPickerRow::existingSelected, this,
                [this, pid = d.pluginId](const QString &accountId) {
                    selectExistingAccount(pid, accountId);
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

void TargetPickerPage::addNewAccount(const QString &pluginId, const QString &kind)
{
    if (!m_state) return;
    WizardAccount acc;
    acc.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    acc.kind = kind;
    m_state->accounts.append(acc);

    const int mi = mappingIndex(pluginId);
    if (mi >= 0) {
        m_state->mappings[mi].kind        = TargetKind::Account;
        m_state->mappings[mi].accountRef  = acc.id;
        m_state->mappings[mi].collectionId.clear();
    }

    // Re-populate every row so the new account appears as a selectable
    // entry (other compatible rows may want to point at the same account).
    for (auto *row : m_rows.values()) row->rebuild();
}

void TargetPickerPage::selectExistingAccount(const QString &pluginId,
                                             const QString &accountId)
{
    if (!m_state) return;
    const int mi = mappingIndex(pluginId);
    if (mi < 0) return;
    if (accountId.isEmpty()) {
        m_state->mappings[mi].kind        = TargetKind::RawFiles;
        m_state->mappings[mi].accountRef.clear();
        m_state->mappings[mi].collectionId.clear();
    } else {
        m_state->mappings[mi].kind        = TargetKind::Account;
        m_state->mappings[mi].accountRef  = accountId;
        m_state->mappings[mi].collectionId.clear();
    }
}

}  // namespace WildPalms::Wizard
