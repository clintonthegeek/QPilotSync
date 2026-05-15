#include "addaccountdialog.h"

#include <backendregistry.h>
#include <backendcontribution.h>
#include <iprovider.h>
#include <backendconfiguration.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendContribution;
using Kalburator::Sync::BackendConfiguration;

AddAccountDialog::AddAccountDialog(BackendRegistry *registry, QWidget *parent)
    : QDialog(parent)
    , m_registry(registry)
{
    setWindowTitle(tr("Add Account"));
    setModal(true);
    buildUi();
}

AddAccountDialog::~AddAccountDialog() = default;

void AddAccountDialog::buildUi() {
    auto *outer = new QVBoxLayout(this);

    m_kindCombo = new QComboBox(this);
    m_configStack = new QStackedWidget(this);

    for (auto *contribution : m_registry->contributions()) {
        // Use the backendType() as the label; format it for display.
        // e.g. "caldav" → "CalDAV", "carddav" → "CardDAV"
        // For unknown types, just capitalise the first letter.
        QString label = contribution->backendType();
        if (label == QStringLiteral("caldav"))
            label = tr("CalDAV (calendar)");
        else if (label == QStringLiteral("carddav"))
            label = tr("CardDAV (contacts)");
        else {
            label[0] = label[0].toUpper();
        }
        m_kindCombo->addItem(label, contribution->backendType());

        auto provider = contribution->createProvider(this);
        QWidget *cfg = provider ? provider->createConfigWidget(m_configStack) : nullptr;
        if (!cfg) cfg = new QWidget(m_configStack);
        m_configStack->addWidget(cfg);
        m_providers.push_back(std::move(provider));
    }

    outer->addWidget(m_kindCombo);
    outer->addWidget(m_configStack);

    m_testButton  = new QPushButton(tr("Test Connection"), this);
    m_statusLabel = new QLabel(this);
    auto *testRow = new QHBoxLayout();
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_statusLabel, 1);
    outer->addLayout(testRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(m_kindCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AddAccountDialog::onKindChanged);
    connect(m_testButton, &QPushButton::clicked,
            this, &AddAccountDialog::onTestConnection);

    // Disable OK / Test if no contributions registered.
    const bool hasItems = m_kindCombo->count() > 0;
    m_testButton->setEnabled(hasItems);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(hasItems);

    if (hasItems) onKindChanged(0);
}

QString AddAccountDialog::selectedKind() const {
    return m_kindCombo->currentData().toString();
}

void AddAccountDialog::onKindChanged(int index) {
    m_configStack->setCurrentIndex(index);
    m_statusLabel->clear();
}

void AddAccountDialog::onTestConnection() {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return;
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return;

    m_statusLabel->setText(tr("Testing..."));
    auto fut = p->connect();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, p]() {
        const bool ok = w->result();
        m_statusLabel->setText(ok ? tr("Connected") : tr("Failed"));
        p->disconnect();
        w->deleteLater();
    });
    w->setFuture(fut);
}

BackendConfiguration AddAccountDialog::configuration() const {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return {};
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return {};
    return p->save();
}

}  // namespace WildPalms::App::Accounts
