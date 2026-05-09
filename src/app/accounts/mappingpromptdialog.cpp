#include "mappingpromptdialog.h"

#include "runtime/accountcontroller.h"

#include <iprovider.h>
#include <providermanager.h>
#include <collectioninfo.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using AC = WildPalms::Runtime::AccountController;

namespace {
constexpr int kCardDavSlotChoices = 16;  // Unfiled (0) + 15 named slots
QStringList palmContactSlots() {
    QStringList out{QStringLiteral("(skip)"),
                    QStringLiteral("Unfiled (palm:contact/0)")};
    for (int i = 1; i < kCardDavSlotChoices; ++i) {
        out << QStringLiteral("palm:contact/%1").arg(i);
    }
    return out;
}
QString slotForIndex(int idx) {
    // 0 = (skip); 1 = Unfiled (palm:contact/0); 2..16 = palm:contact/1..15
    if (idx <= 0) return QString();
    return QStringLiteral("palm:contact/%1").arg(idx - 1);
}
}  // namespace

MappingPromptDialog::MappingPromptDialog(AC *accounts,
                                         const QString &providerId,
                                         QWidget *parent)
    : QDialog(parent), m_accounts(accounts), m_providerId(providerId)
{
    setWindowTitle(tr("Bind collections"));
    setModal(true);
    buildUi();
}

void MappingPromptDialog::buildUi() {
    auto *outer = new QVBoxLayout(this);

    outer->addWidget(new QLabel(
        tr("Bind discovered collections to Palm slots. You can revisit "
           "this later in Mappings."), this));

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Collection"), tr("Kind"), tr("Bind to")});
    m_table->horizontalHeader()->setStretchLastSection(true);

    auto *p = m_accounts->providerManager()->providerById(m_providerId);
    if (p) {
        const auto cols = p->collections();
        m_table->setRowCount(cols.size());
        const QString providerKind = p->kind();
        for (int i = 0; i < cols.size(); ++i) {
            const auto &c = cols.at(i);
            m_table->setItem(i, 0, new QTableWidgetItem(c.name));
            m_table->setItem(i, 1, new QTableWidgetItem(providerKind));

            if (providerKind == QStringLiteral("carddav")) {
                auto *combo = new QComboBox(this);
                combo->addItems(palmContactSlots());
                m_table->setCellWidget(i, 2, combo);
            } else {
                m_table->setCellWidget(i, 2,
                    new QLabel(tr("Bound (Phase J wires this)"), this));
            }
        }
    }
    outer->addWidget(m_table);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &MappingPromptDialog::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

void MappingPromptDialog::onSave() {
    auto *p = m_accounts->providerManager()->providerById(m_providerId);
    if (!p) { reject(); return; }

    QJsonArray adds;
    const auto cols = p->collections();
    for (int i = 0; i < cols.size(); ++i) {
        auto *combo = qobject_cast<QComboBox*>(m_table->cellWidget(i, 2));
        if (!combo) continue;
        const QString slot = slotForIndex(combo->currentIndex());
        if (slot.isEmpty()) continue;

        QJsonObject row;
        row["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        row["sourceBackend"]  = slot;
        row["sourceCalendar"] = QString();
        row["targetBackend"]  = m_providerId + QStringLiteral(":") + cols.at(i).id;
        row["targetCalendar"] = cols.at(i).id;
        row["mode"] = QStringLiteral("TwoWay");
        row["conflictPolicy"] = QStringLiteral("AskUser");
        row["enabled"] = true;
        adds.append(row);
    }

    if (!adds.isEmpty()) {
        m_accounts->appendMappings(adds);
    }
    accept();
}

}  // namespace WildPalms::App::Accounts
