#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QListWidget>
#include <QPushButton>

#include "app/accounts/accountspage.h"
#include "app/accounts/addaccountdialog.h"
#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include <backendconfiguration.h>

class TstAccountsPage : public QObject {
    Q_OBJECT
private slots:
    void emptyState_addEnabled();
    void afterAdd_listShowsProvider();
    void interlock_disablesAddRemoveDuringRun();
};

void TstAccountsPage::emptyState_addEnabled()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    WildPalms::App::Accounts::AccountsPage page(&ac, &rt);
    page.show();
    QTest::qWait(50);

    auto *list   = page.findChild<QListWidget*>();
    auto buttons = page.findChildren<QPushButton*>();
    QVERIFY(list);
    QVERIFY(!buttons.isEmpty());
    QCOMPARE(list->count(), 0);
    QVERIFY(buttons.at(0)->isEnabled());
}

void TstAccountsPage::afterAdd_listShowsProvider()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.displayName = "Server X";
    cfg.connectionParams["url"] = "https://x/";
    ac.addProvider("carddav", cfg);

    WildPalms::App::Accounts::AccountsPage page(&ac, &rt);
    page.show();
    QTest::qWait(50);

    auto *list = page.findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), QStringLiteral("Server X"));
}

void TstAccountsPage::interlock_disablesAddRemoveDuringRun()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);
    WildPalms::App::Accounts::AccountsPage page(&ac, &rt);
    page.show();

    // Emit runStarted directly via test-emit pattern. If PalmRuntime
    // doesn't allow external signal emission, fall back to checking
    // updateInterlock via setRunningForTest if exposed; otherwise QSKIP.
    emit rt.runStarted("test");
    QTest::qWait(50);

    auto buttons = page.findChildren<QPushButton*>();
    QVERIFY(!buttons.isEmpty());
    // Add button is disabled during sync.
    QVERIFY(!buttons.at(0)->isEnabled());

    emit rt.runFinished({});
    QTest::qWait(50);
    QVERIFY(buttons.at(0)->isEnabled());
}

QTEST_MAIN(TstAccountsPage)
#include "tst_accounts_page.moc"
