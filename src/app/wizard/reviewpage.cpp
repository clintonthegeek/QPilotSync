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
            // Look up the referenced WizardAccount.
            QString accountDisplay = m.accountRef;
            QString accountKind;
            QString collectionLabel = m.collectionId;
            if (const auto *a = m_state->accountById(m.accountRef)) {
                accountDisplay = a->config.displayName.isEmpty()
                    ? a->id : a->config.displayName;
                accountKind = a->kind;
                for (const auto &c : a->collections)
                    if (c.id == m.collectionId) { collectionLabel = c.name; break; }
            }
            line = tr("%1 → %2 / \"%3\" (%4)")
                       .arg(m.pluginId.toHtmlEscaped(),
                            accountDisplay.toHtmlEscaped(),
                            collectionLabel.toHtmlEscaped(),
                            accountKind.toHtmlEscaped());
        }
        html += QStringLiteral("<li>%1</li>").arg(line);
    }
    html += QStringLiteral("</ul>");

    if (!m_state->accounts.isEmpty()) {
        html += QStringLiteral("<p><b>New accounts to be created:</b></p><ul>");
        for (const auto &a : m_state->accounts) {
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
