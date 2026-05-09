#include "addaccountdialog.h"

#include <iprovider.h>
#include <caldavprovider.h>
#include <carddavprovider.h>
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
using Kalburator::Sync::CalDavProvider;
using Kalburator::Sync::CardDavProvider;
using Kalburator::Sync::BackendConfiguration;

AddAccountDialog::AddAccountDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Add Account"));
    setModal(true);
    buildUi();
}

AddAccountDialog::~AddAccountDialog() = default;

void AddAccountDialog::buildUi() {
    auto *outer = new QVBoxLayout(this);

    m_kindCombo = new QComboBox(this);
    m_kindCombo->addItem(tr("CalDAV (calendar)"),  QStringLiteral("caldav"));
    m_kindCombo->addItem(tr("CardDAV (contacts)"), QStringLiteral("carddav"));
    outer->addWidget(m_kindCombo);

    m_configStack = new QStackedWidget(this);
    outer->addWidget(m_configStack);

    m_calDavProvider  = std::make_unique<CalDavProvider>();
    m_cardDavProvider = std::make_unique<CardDavProvider>();
    m_configStack->addWidget(m_calDavProvider->createConfigWidget(this));
    m_configStack->addWidget(m_cardDavProvider->createConfigWidget(this));

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

    onKindChanged(0);
}

QString AddAccountDialog::selectedKind() const {
    return m_kindCombo->currentData().toString();
}

void AddAccountDialog::onKindChanged(int index) {
    m_configStack->setCurrentIndex(index);
    m_statusLabel->clear();
}

void AddAccountDialog::onTestConnection() {
    m_statusLabel->setText(tr("Testing..."));
    IProvider *p = (selectedKind() == QStringLiteral("caldav"))
        ? m_calDavProvider.get() : m_cardDavProvider.get();

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
    IProvider *p = (selectedKind() == QStringLiteral("caldav"))
        ? m_calDavProvider.get() : m_cardDavProvider.get();
    return p->save();
}

}  // namespace WildPalms::App::Accounts
