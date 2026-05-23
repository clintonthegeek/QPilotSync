#include "accountformwidget.h"

#include <backendregistry.h>
#include <backendcontribution.h>
#include <iprovider.h>
#include <backendconfiguration.h>

#include <QComboBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendConfiguration;

AccountFormWidget::AccountFormWidget(BackendRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
{
    buildUi(QString());
}

AccountFormWidget::AccountFormWidget(BackendRegistry *registry,
                                     const QString &lockedKind,
                                     QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
{
    buildUi(lockedKind);
}

AccountFormWidget::~AccountFormWidget() = default;

void AccountFormWidget::buildUi(const QString &lockedKind) {
    auto *outer = new QVBoxLayout(this);

    m_kindCombo  = new QComboBox(this);
    m_configStack = new QStackedWidget(this);

    int lockedIndex = -1;
    int i = 0;
    for (auto *contribution : m_registry->contributions()) {
        QString label = contribution->backendType();
        if (label == QStringLiteral("caldav"))
            label = tr("CalDAV (calendar)");
        else if (label == QStringLiteral("carddav"))
            label = tr("CardDAV (contacts)");
        else if (label == QStringLiteral("multiproto-dav"))
            label = tr("Multi-protocol DAV (calendar + contacts)");
        else {
            label[0] = label[0].toUpper();
        }
        m_kindCombo->addItem(label, contribution->backendType());

        auto provider = contribution->createProvider(this);
        QWidget *cfg = provider ? provider->createConfigWidget(m_configStack) : nullptr;
        if (!cfg) cfg = new QWidget(m_configStack);
        m_configStack->addWidget(cfg);
        m_providers.push_back(std::move(provider));

        if (!lockedKind.isEmpty() && contribution->backendType() == lockedKind)
            lockedIndex = i;
        ++i;
    }

    outer->addWidget(m_kindCombo);
    outer->addWidget(m_configStack);

    m_testButton  = new QPushButton(tr("Test Connection"), this);
    m_statusLabel = new QLabel(this);
    auto *testRow = new QHBoxLayout();
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_statusLabel, 1);
    outer->addLayout(testRow);

    connect(m_kindCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AccountFormWidget::onKindChanged);
    connect(m_testButton, &QPushButton::clicked,
            this, &AccountFormWidget::onTestConnection);

    const bool hasItems = m_kindCombo->count() > 0;
    m_testButton->setEnabled(hasItems);

    if (!lockedKind.isEmpty() && lockedIndex >= 0) {
        m_kindCombo->setCurrentIndex(lockedIndex);
        m_kindCombo->setVisible(false);
        onKindChanged(lockedIndex);
    } else if (hasItems) {
        onKindChanged(0);
    }
}

QString AccountFormWidget::selectedKind() const {
    return m_kindCombo->currentData().toString();
}

void AccountFormWidget::onKindChanged(int index) {
    m_configStack->setCurrentIndex(index);
    m_statusLabel->clear();
}

void AccountFormWidget::onTestConnection() {
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

BackendConfiguration AccountFormWidget::configuration() const {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return {};
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return {};
    return p->save();
}

bool AccountFormWidget::isValid() const {
    const auto cfg = configuration();
    return cfg.isValid() && !cfg.displayName.isEmpty();
}

}  // namespace WildPalms::App::Accounts
