#include "accountspage.h"
#include "addaccountdialog.h"
// TODO(Task 10): #include "mappingpromptdialog.h"

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"

#include <iprovider.h>
#include <backendconfiguration.h>

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using AC = WildPalms::Runtime::AccountController;

AccountsPage::AccountsPage(AC *accounts,
                           WildPalms::Runtime::PalmRuntime *palmRuntime,
                           QWidget *parent)
    : QWidget(parent)
    , m_accounts(accounts)
    , m_palmRuntime(palmRuntime)
{
    buildUi();
    refreshList();

    connect(m_accounts, &AC::providersChanged,
            this, &AccountsPage::refreshList);
    connect(m_accounts, &AC::mappingsChanged,
            this, &AccountsPage::refreshList);
    connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runStarted,
            this, &AccountsPage::onPalmRunStarted);
    connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runFinished,
            this, &AccountsPage::onPalmRunFinished);
}

void AccountsPage::buildUi() {
    auto *outer = new QHBoxLayout(this);

    auto *leftCol = new QVBoxLayout();
    m_list = new QListWidget(this);
    leftCol->addWidget(m_list, 1);

    auto *btnRow = new QHBoxLayout();
    m_addBtn    = new QPushButton(tr("Add..."), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_removeBtn->setEnabled(false);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    leftCol->addLayout(btnRow);
    outer->addLayout(leftCol, 1);

    m_rightPane = new QStackedWidget(this);
    outer->addWidget(m_rightPane, 2);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &AccountsPage::onProviderRowChanged);
    connect(m_addBtn, &QPushButton::clicked,
            this, &AccountsPage::onAddClicked);
    connect(m_removeBtn, &QPushButton::clicked,
            this, &AccountsPage::onRemoveClicked);

    updateInterlock();
}

void AccountsPage::refreshList() {
    const QString currentId = (m_list->currentRow() >= 0 && m_list->currentItem())
        ? m_list->currentItem()->data(Qt::UserRole).toString()
        : QString();

    m_list->clear();
    while (m_rightPane->count() > 0) {
        QWidget *w = m_rightPane->widget(0);
        m_rightPane->removeWidget(w);
        w->deleteLater();
    }

    int restoreRow = -1;
    int row = 0;
    for (auto *p : m_accounts->providers()) {
        auto *item = new QListWidgetItem(p->displayName());
        item->setData(Qt::UserRole, p->id());
        m_list->addItem(item);
        m_rightPane->addWidget(p->createConfigWidget(m_rightPane));
        if (p->id() == currentId) restoreRow = row;
        ++row;
    }
    if (restoreRow >= 0) m_list->setCurrentRow(restoreRow);
    else if (m_list->count() > 0) m_list->setCurrentRow(0);

    updateInterlock();
}

void AccountsPage::onProviderRowChanged(int row) {
    if (row < 0) {
        m_removeBtn->setEnabled(false);
        return;
    }
    m_rightPane->setCurrentIndex(row);
    m_removeBtn->setEnabled(!m_palmRuntime->isRunning());
}

void AccountsPage::onAddClicked() {
    AddAccountDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString uuid = m_accounts->addProvider(
        dlg.selectedKind(), dlg.configuration());
    if (uuid.isEmpty()) {
        QMessageBox::warning(this, tr("Add Account"),
            tr("Couldn't add account (sync may be running, "
               "or the kind isn't supported)."));
        return;
    }

    // TODO(Task 10): un-comment after MappingPromptDialog lands.
    // MappingPromptDialog prompt(m_accounts, uuid, this);
    // prompt.exec();
}

void AccountsPage::onRemoveClicked() {
    if (m_list->currentRow() < 0 || !m_list->currentItem()) return;
    const QString id = m_list->currentItem()->data(Qt::UserRole).toString();
    const int n = m_accounts->mappingCountFor(id);
    const QStringList sample = m_accounts->mappingDescriptionsFor(id, 3);

    QString body = tr("Remove account?");
    if (n > 0) {
        body = tr("Remove account? This will delete %1 sync mapping(s):\n\n%2")
            .arg(n).arg(sample.join("\n"));
        if (n > sample.size()) body += tr("\n... and %1 more.")
            .arg(n - sample.size());
    }

    if (QMessageBox::question(this, tr("Remove Account"), body)
        != QMessageBox::Yes) return;

    if (!m_accounts->removeProvider(id)) {
        QMessageBox::warning(this, tr("Remove Account"),
            tr("Couldn't remove (sync may be running)."));
    }
}

void AccountsPage::onPalmRunStarted()  { updateInterlock(); }
void AccountsPage::onPalmRunFinished() { updateInterlock(); }

void AccountsPage::updateInterlock() {
    const bool busy = m_palmRuntime->isRunning();
    m_addBtn->setEnabled(!busy);
    m_removeBtn->setEnabled(!busy && m_list->currentRow() >= 0
                            && m_list->currentItem() != nullptr);
    const QString tip = busy ? tr("Sync in progress.") : QString();
    m_addBtn->setToolTip(tip);
    m_removeBtn->setToolTip(tip);
}

}  // namespace WildPalms::App::Accounts
