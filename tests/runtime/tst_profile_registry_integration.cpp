#include <QtTest/QtTest>
#include <QSettings>
#include <QTemporaryDir>

#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::Runtime;

class TstProfileRegistryIntegration : public QObject
{
    Q_OBJECT
private slots:
    void registerNewThenLoadProfile();
    void registerExistingPicksUpPreexistingDir();
};

void TstProfileRegistryIntegration::registerNewThenLoadProfile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto entry = reg.registerNew(QStringLiteral("Integration"));
    QVERIFY(entry.isValid());

    // Stub profile.conf was written; Profile::load should succeed.
    Profile p;
    p.setSyncFolderPath(entry.path);
    QVERIFY(p.load());
    QCOMPARE(p.id(), entry.id);
    QCOMPARE(p.name(), QStringLiteral("Integration"));

    // Save additional state, reload, assert.
    p.setBaudRate(QStringLiteral("38400"));
    QVERIFY(p.save());

    Profile p2;
    p2.setSyncFolderPath(entry.path);
    QVERIFY(p2.load());
    QCOMPARE(p2.id(), entry.id);
    QCOMPARE(p2.name(), QStringLiteral("Integration"));
    QCOMPARE(p2.baudRate(), QStringLiteral("38400"));
}

void TstProfileRegistryIntegration::registerExistingPicksUpPreexistingDir()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Create a saved profile directly (no registry yet). Profile::save()'s
    // id-fallback writes the directory basename as profile/id when m_id is
    // empty (which it always is for a never-loaded profile), giving us
    // exactly what registerExisting requires (id == basename).
    const QString dir = tmp.path() + QStringLiteral("/preexisting");
    {
        Profile p;
        p.setSyncFolderPath(dir);
        p.setName(QStringLiteral("Pre-existing"));
        QVERIFY(p.save());
    }

    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const auto entry = reg.registerExisting(dir);
    QVERIFY(entry.isValid());
    QCOMPARE(entry.id, QStringLiteral("preexisting"));
    QCOMPARE(entry.name, QStringLiteral("Pre-existing"));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistryIntegration)
#include "tst_profile_registry_integration.moc"
