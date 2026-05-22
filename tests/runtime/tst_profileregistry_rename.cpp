#include <QtTest/QtTest>

#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <KSharedConfig>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileRegistryRename : public QObject
{
    Q_OBJECT
private slots:
    void renameUpdatesEntryName();
    void renamePersistsToRegistryFile();
    void renamePersistsToProfileConf();
    void renameEmitsEntryUpdated();
    void renameEmptyReturnsFalse();
    void renameWhitespaceOnlyReturnsFalse();
    void renameUnknownIdReturnsFalse();
};

namespace {
ProfileEntry registerOne(ProfileRegistry &reg, const QString &name)
{
    return reg.registerNew(name);
}
} // namespace

void TstProfileRegistryRename::renameUpdatesEntryName()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());

    const auto e = registerOne(reg, QStringLiteral("Alpha"));
    QVERIFY(e.isValid());

    QVERIFY(reg.rename(e.id, QStringLiteral("Bravo")));
    QCOMPARE(reg.entry(e.id).name, QStringLiteral("Bravo"));
}

void TstProfileRegistryRename::renamePersistsToRegistryFile()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    const QString cfgPath = tmp.path() + QStringLiteral("/wprc");
    QString id;
    {
        auto cfg = KSharedConfig::openConfig(cfgPath);
        ProfileRegistry reg(cfg);
        reg.setDefaultRoot(tmp.path());
        const auto e = registerOne(reg, QStringLiteral("Alpha"));
        id = e.id;
        QVERIFY(reg.rename(id, QStringLiteral("Bravo")));
    }
    // Reopen the registry from disk.
    auto cfg2 = KSharedConfig::openConfig(cfgPath);
    ProfileRegistry reg2(cfg2);
    QCOMPARE(reg2.entry(id).name, QStringLiteral("Bravo"));
}

void TstProfileRegistryRename::renamePersistsToProfileConf()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QVERIFY(reg.rename(e.id, QStringLiteral("Bravo")));

    QSettings s(e.path + QStringLiteral("/profile.conf"), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("profile"));
    QCOMPARE(s.value(QStringLiteral("name")).toString(), QStringLiteral("Bravo"));
}

void TstProfileRegistryRename::renameEmitsEntryUpdated()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QSignalSpy spy(&reg, &ProfileRegistry::entryUpdated);
    QVERIFY(reg.rename(e.id, QStringLiteral("Bravo")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), e.id);
}

void TstProfileRegistryRename::renameEmptyReturnsFalse()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QVERIFY(!reg.rename(e.id, QString()));
    QCOMPARE(reg.entry(e.id).name, QStringLiteral("Alpha"));
}

void TstProfileRegistryRename::renameWhitespaceOnlyReturnsFalse()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QVERIFY(!reg.rename(e.id, QStringLiteral("   ")));
    QCOMPARE(reg.entry(e.id).name, QStringLiteral("Alpha"));
}

void TstProfileRegistryRename::renameUnknownIdReturnsFalse()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());

    QVERIFY(!reg.rename(QStringLiteral("nonexistent"), QStringLiteral("X")));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistryRename)
#include "tst_profileregistry_rename.moc"
