#include "targetpickerrow.h"
#include "wizardstate.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>

namespace WildPalms::Wizard {

namespace {
QString domainLabel(const QString &pluginId) {
    if (pluginId == QStringLiteral("calendar")) return QObject::tr("Calendar");
    if (pluginId == QStringLiteral("contacts")) return QObject::tr("Contacts");
    if (pluginId == QStringLiteral("memo"))     return QObject::tr("Memo");
    if (pluginId == QStringLiteral("todo"))     return QObject::tr("To-do");
    return pluginId;
}

QString kindFriendly(const QString &kind) {
    if (kind == QStringLiteral("caldav"))  return QStringLiteral("CalDAV");
    if (kind == QStringLiteral("carddav")) return QStringLiteral("CardDAV");
    if (kind == QStringLiteral("akonadi")) return QStringLiteral("Akonadi");
    return kind.toUpper();
}
} // namespace

TargetPickerRow::TargetPickerRow(const QString &pluginId,
                                 const QStringList &compatibleKinds,
                                 WizardState *state,
                                 QWidget *parent)
    : QWidget(parent)
    , m_pluginId(pluginId)
    , m_compatibleKinds(compatibleKinds)
    , m_state(state)
{
    auto *layout = new QHBoxLayout(this);
    auto *label = new QLabel(domainLabel(pluginId), this);
    label->setMinimumWidth(120);
    m_combo = new QComboBox(this);
    layout->addWidget(label);
    layout->addWidget(m_combo, /*stretch=*/1);

    if (m_compatibleKinds.isEmpty() ||
        (m_compatibleKinds.size() == 1 &&
         m_compatibleKinds.first() == QStringLiteral("rawfiles"))) {
        // Memo or any other rawfiles-only domain.
        m_combo->setEnabled(false);
    }

    rebuild();
    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TargetPickerRow::onCurrentIndexChanged);
}

void TargetPickerRow::rebuild()
{
    if (!m_combo || !m_state) return;

    QSignalBlocker block(m_combo);
    m_combo->clear();

    // 1. Always present: Local files (RawFiles).
    m_combo->addItem(tr("Local files (default)"),
                     QVariant::fromValue(QString()));   // empty id == rawfiles

    // 2. Existing pending accounts compatible with this row's kinds.
    for (const auto &acc : m_state->accounts) {
        if (!m_compatibleKinds.contains(acc.kind)) continue;
        const QString label = QStringLiteral("%1 (%2)")
            .arg(acc.config.displayName.isEmpty()
                    ? acc.id
                    : acc.config.displayName,
                 kindFriendly(acc.kind));
        m_combo->addItem(label, QVariant::fromValue(acc.id));
    }

    // 3. "Add new <kind>…" entries, one per compatible kind.
    for (const auto &kind : m_compatibleKinds) {
        if (kind == QStringLiteral("rawfiles")) continue;
        m_combo->addItem(tr("Add new %1 account…").arg(kindFriendly(kind)),
                         QVariant::fromValue(
                             QStringLiteral("__add_new__:%1").arg(kind)));
    }
}

void TargetPickerRow::onCurrentIndexChanged(int idx)
{
    if (idx < 0 || !m_combo) return;
    const QString tag = m_combo->itemData(idx).toString();
    if (tag.startsWith(QStringLiteral("__add_new__:"))) {
        const QString kind = tag.section(QLatin1Char(':'), 1);
        emit addNewRequested(kind);
        return;
    }
    emit existingSelected(tag);   // empty == rawfiles
}

}  // namespace WildPalms::Wizard
