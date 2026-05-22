#include <QtTest/QtTest>

#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileRegistry : public QObject
{
    Q_OBJECT
private slots:
    void defaultRootIsUnderHome();
    void setDefaultRootOverrides();
    void allocateNewIdOnEmptyRegistry();
};

void TstProfileRegistry::defaultRootIsUnderHome()
{
    ProfileRegistry reg;
    QVERIFY(reg.defaultRoot().contains(QStringLiteral(".wildpalms")));
    QVERIFY(!reg.defaultRoot().isEmpty());
}

void TstProfileRegistry::setDefaultRootOverrides()
{
    ProfileRegistry reg;
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    reg.setDefaultRoot(tmp.path());
    QCOMPARE(reg.defaultRoot(), tmp.path());
}

void TstProfileRegistry::allocateNewIdOnEmptyRegistry()
{
    ProfileRegistry reg;
    // Empty registry — first allocation is profile1.
    QCOMPARE(reg.allocateNewId(), QStringLiteral("profile1"));
    QCOMPARE(reg.allocateNewId(), QStringLiteral("profile1"));  // pure; no side effect
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistry)
#include "tst_profileregistry.moc"
