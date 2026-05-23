#include "reviewpage.h"
#include "wizardstate.h"

#include <QLabel>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

ReviewPage::ReviewPage(WizardState *state, QWidget *parent)
    : QWizardPage(parent)
    , m_state(state)
{
    setTitle(tr("Review"));
    setSubTitle(tr("Confirm the new profile."));

    auto *layout = new QVBoxLayout(this);
    m_label = new QLabel(this);
    m_label->setTextFormat(Qt::RichText);
    m_label->setWordWrap(true);
    layout->addWidget(m_label);
}

void ReviewPage::initializePage()
{
    if (!m_state || !m_label) return;

    QString html;
    html += QStringLiteral("<p>You're about to create:</p>");
    html += QStringLiteral("<p style='margin-left:1em'><b>Profile:</b> \"%1\"</p>")
                .arg(m_state->profileName.toHtmlEscaped());

    html += QStringLiteral("<p><b>Sync mappings:</b></p><ul>");
    for (const auto &m : m_state->mappings) {
        QString line;
        if (m.kind == TargetKind::RawFiles) {
            line = tr("%1 → Local files").arg(m.pluginId.toHtmlEscaped());
        } else {
            // Look up the referenced PendingAccount.
            QString accountDisplay = m.accountRef;
            QString accountKind;
            for (const auto &a : m_state->pendingAccounts) {
                if (a.id == m.accountRef) {
                    accountDisplay = a.config.displayName.isEmpty()
                        ? a.id : a.config.displayName;
                    accountKind = a.kind;
                    break;
                }
            }
            line = tr("%1 → %2 / \"%3\" (%4)")
                       .arg(m.pluginId.toHtmlEscaped(),
                            accountDisplay.toHtmlEscaped(),
                            m.collectionId.toHtmlEscaped(),
                            accountKind.toHtmlEscaped());
        }
        html += QStringLiteral("<li>%1</li>").arg(line);
    }
    html += QStringLiteral("</ul>");

    if (!m_state->pendingAccounts.isEmpty()) {
        html += QStringLiteral("<p><b>New accounts to be created:</b></p><ul>");
        for (const auto &a : m_state->pendingAccounts) {
            const QString disp = a.config.displayName.isEmpty()
                ? a.id : a.config.displayName;
            html += QStringLiteral("<li>%1 (%2)</li>")
                        .arg(disp.toHtmlEscaped(), a.kind.toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    }

    m_label->setText(html);
}

}  // namespace WildPalms::Wizard
