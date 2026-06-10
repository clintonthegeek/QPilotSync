#include "discoveryrow.h"
#include "wizardstate.h"

#include <backendregistry.h>
#include <backendcontribution.h>
#include <iprovider.h>
#include <collectioninfo.h>

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

DiscoveryRow::DiscoveryRow(Kalburator::Sync::BackendRegistry *registry,
                           const WizardAccount &account,
                           QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_displayName(account.config.displayName.isEmpty()
                       ? account.id
                       : account.config.displayName)
{
    auto *layout = new QVBoxLayout(this);

    m_status = new QLabel(this);
    layout->addWidget(m_status);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVisible(false);
    layout->addWidget(m_list);

    m_retry = new QPushButton(tr("Retry"), this);
    m_retry->setObjectName(QStringLiteral("retry"));
    m_retry->setVisible(false);
    layout->addWidget(m_retry);

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &DiscoveryRow::onSelectionChanged);
    connect(m_retry, &QPushButton::clicked, this, [this]() { runDiscovery(); });

    connect(&m_watcher, &QFutureWatcher<bool>::finished,
            this, &DiscoveryRow::onConnectFinished);

    // Build the provider from the account config.
    if (m_registry) {
        auto *contribution = m_registry->contributionFor(account.kind);
        if (contribution) {
            m_provider = contribution->createProvider(this);
            if (m_provider) m_provider->load(account.config);
        }
    }

    if (!m_provider) {
        setState(Failed);
        m_status->setText(tr("No provider for account kind: %1").arg(account.kind));
        return;
    }

    runDiscovery();
}

DiscoveryRow::~DiscoveryRow()
{
    if (m_watcher.isRunning()) m_watcher.cancel();
}

void DiscoveryRow::setState(State s)
{
    m_state = s;
    m_list->setVisible(s == Loaded || s == Chosen);
    m_retry->setVisible(s == Empty || s == Failed);
    emit readinessChanged();
}

void DiscoveryRow::runDiscovery()
{
    if (!m_provider) return;
    setState(Loading);
    m_status->setText(tr("Discovering on %1…").arg(m_displayName));
    m_list->clear();
    m_chosenCollectionId.clear();
    m_watcher.setFuture(m_provider->connect());
}

void DiscoveryRow::onConnectFinished()
{
    if (!m_provider) { setState(Failed); return; }
    bool ok = false;
    if (m_watcher.future().resultCount() > 0)
        ok = m_watcher.future().result();
    if (!ok) {
        setState(Failed);
        m_status->setText(tr("Couldn't reach %1.").arg(m_displayName));
        return;
    }

    const auto cols = m_provider->collections();
    if (cols.isEmpty()) {
        setState(Empty);
        m_status->setText(tr("No collections found on %1.").arg(m_displayName));
        return;
    }

    m_list->clear();
    for (const auto &c : cols) {
        auto *item = new QListWidgetItem(c.name);
        item->setData(Qt::UserRole, c.id);
        m_list->addItem(item);
    }
    setState(Loaded);
    m_status->setText(tr("Pick a collection on %1.").arg(m_displayName));
}

void DiscoveryRow::onSelectionChanged()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    m_chosenCollectionId = item->data(Qt::UserRole).toString();
    setState(Chosen);
}

}  // namespace WildPalms::Wizard
