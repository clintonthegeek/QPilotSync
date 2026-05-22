// tests/runtime/tst_profilemenucontroller.cpp
#include <QtTest/QtTest>

#include "../../src/kf6/profilemenucontroller.h"
#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KSharedConfig>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileMenuController : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegistryDisablesBothMenus();
    void nonEmptyEnablesMenus();
    void submenusPopulatedSorted();
    void activeProfileCheckedAndDisabledInSwitch();
    void activeProfileDisabledInForget();
    void clearingActiveIdReenablesAll();
};

namespace {
struct Fixture {
    QTemporaryDir tmp;
    KSharedConfig::Ptr cfg;
    ProfileRegistry registry;
    KActionCollection actions;

    Fixture()
        : cfg(KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc")))
        , registry(cfg)
        , actions(static_cast<QObject *>(nullptr)) {
        registry.setDefaultRoot(tmp.path());
    }
};
} // namespace

void TstProfileMenuController::emptyRegistryDisablesBothMenus()
{
    Fixture f;
    ProfileMenuController ctrl(&f.registry, &f.actions);

    QVERIFY(ctrl.switchMenu() != nullptr);
    QVERIFY(ctrl.forgetMenu() != nullptr);
    QVERIFY(!ctrl.switchMenu()->isEnabled());
    QVERIFY(!ctrl.forgetMenu()->isEnabled());
}

void TstProfileMenuController::nonEmptyEnablesMenus()
{
    Fixture f;
    f.registry.registerNew(QStringLiteral("Alpha"));
    ProfileMenuController ctrl(&f.registry, &f.actions);

    QVERIFY(ctrl.switchMenu()->isEnabled());
    QVERIFY(ctrl.forgetMenu()->isEnabled());
}

void TstProfileMenuController::submenusPopulatedSorted()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));
    const auto c = f.registry.registerNew(QStringLiteral("Charlie"));
    // Bump lastOpened so the desired order is Bravo (most recent) > Charlie > Alpha.
    f.registry.setLastActive(a.id);
    f.registry.setLastActive(c.id);
    f.registry.setLastActive(b.id);

    ProfileMenuController ctrl(&f.registry, &f.actions);

    auto swActs = ctrl.switchMenu()->menu()->actions();
    QCOMPARE(swActs.size(), 3);
    QCOMPARE(swActs[0]->text(), QStringLiteral("Bravo"));
    QCOMPARE(swActs[1]->text(), QStringLiteral("Charlie"));
    QCOMPARE(swActs[2]->text(), QStringLiteral("Alpha"));

    auto fgActs = ctrl.forgetMenu()->menu()->actions();
    QCOMPARE(fgActs.size(), 3);
    QCOMPARE(fgActs[0]->text(), QStringLiteral("Bravo"));
}

void TstProfileMenuController::activeProfileCheckedAndDisabledInSwitch()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    ctrl.setActiveProfileId(b.id);

    for (auto *act : ctrl.switchMenu()->menu()->actions()) {
        const bool isB = (act->data().toString() == b.id);
        QCOMPARE(act->isChecked(), isB);
        QCOMPARE(act->isEnabled(), !isB);
    }
    Q_UNUSED(a);
}

void TstProfileMenuController::activeProfileDisabledInForget()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    ctrl.setActiveProfileId(b.id);

    for (auto *act : ctrl.forgetMenu()->menu()->actions()) {
        const bool isB = (act->data().toString() == b.id);
        QCOMPARE(act->isEnabled(), !isB);
    }
    Q_UNUSED(a);
}

void TstProfileMenuController::clearingActiveIdReenablesAll()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    ctrl.setActiveProfileId(b.id);
    ctrl.setActiveProfileId(QString());

    for (auto *act : ctrl.switchMenu()->menu()->actions()) {
        QVERIFY(!act->isChecked());
        QVERIFY(act->isEnabled());
    }
    for (auto *act : ctrl.forgetMenu()->menu()->actions()) {
        QVERIFY(act->isEnabled());
    }
    Q_UNUSED(a);
}

WILDPALMS_QTEST_MAIN(TstProfileMenuController)
#include "tst_profilemenucontroller.moc"
