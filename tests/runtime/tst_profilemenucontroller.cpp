// tests/runtime/tst_profilemenucontroller.cpp
#include <QtTest/QtTest>

#include "../../src/kf6/profilemenucontroller.h"
#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KSharedConfig>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileMenuController : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegistryDisablesBothMenus();
    void nonEmptyEnablesMenus();
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

WILDPALMS_QTEST_MAIN(TstProfileMenuController)
#include "tst_profilemenucontroller.moc"
