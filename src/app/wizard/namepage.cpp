#include "namepage.h"
#include "wizardstate.h"

#include "runtime/profileregistry.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace WildPalms::Wizard {

NamePage::NamePage(WildPalms::Runtime::ProfileRegistry *registry,
                   WizardState *state,
                   QWidget *parent)
    : QWizardPage(parent)
    , m_registry(registry)
    , m_state(state)
{
    setTitle(tr("Profile name"));
    setSubTitle(tr("Choose a name for the new sync profile."));

    auto *layout = new QFormLayout(this);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("e.g. Palm m505"));
    layout->addRow(tr("Name:"), m_edit);

    m_warning = new QLabel(this);
    m_warning->setStyleSheet(QStringLiteral("color: #c00"));
    m_warning->setVisible(false);
    layout->addRow(QString(), m_warning);

    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString &t) {
        const QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !isUnique(trimmed)) {
            m_warning->setText(tr("A profile with this name already exists."));
            m_warning->setVisible(true);
        } else {
            m_warning->setVisible(false);
        }
        emit completeChanged();
    });

    registerField(QStringLiteral("name*"), m_edit);
}

bool NamePage::isUnique(const QString &name) const
{
    if (!m_registry) return true;
    for (const auto &e : m_registry->entries()) {
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) return false;
    }
    return true;
}

bool NamePage::isComplete() const
{
    if (!m_edit) return false;
    const QString trimmed = m_edit->text().trimmed();
    if (trimmed.isEmpty()) return false;
    return isUnique(trimmed);
}

bool NamePage::validatePage()
{
    if (!isComplete()) return false;
    if (m_state) m_state->profileName = m_edit->text().trimmed();
    return true;
}

}  // namespace WildPalms::Wizard
