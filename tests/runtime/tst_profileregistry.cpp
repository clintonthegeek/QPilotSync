#include <QtTest/QtTest>

#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <KConfigGroup>
#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileRegistry : public QObject
{
    Q_OBJECT
private slots:
    void defaultRootIsUnderHome();
    void setDefaultRootOverrides();
    void allocateNewIdOnEmptyRegistry();
    void emptyRegistry();
    void persistenceRoundTrip();
    void registerNewNoPath();
    void registerNewWritesStubProfileConf();
    void registerNewCustomPath();
    void registerNewCustomPathInvalidId();
    void registerNewSecondAllocation();
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

void TstProfileRegistry::emptyRegistry()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/test-wprc"));
    ProfileRegistry reg(cfg);

    QVERIFY(reg.entries().isEmpty());
    QCOMPARE(reg.lastActiveId(), QString());
    QVERIFY(!reg.entry(QStringLiteral("profile1")).isValid());
}

void TstProfileRegistry::persistenceRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString cfgPath = tmp.path() + QStringLiteral("/test-wprc");

    // Write directly into the KConfig file as if a prior session had
    // registered two profiles.
    {
        auto cfg = KSharedConfig::openConfig(cfgPath);
        KConfigGroup g1(cfg, QStringLiteral("profile-profile1"));
        g1.writeEntry("name", "Alpha");
        g1.writeEntry("path", "/tmp/alpha");
        g1.writeEntry("lastOpened",
                      QDateTime::fromString(QStringLiteral("2026-05-21T10:00:00Z"),
                                            Qt::ISODate));
        KConfigGroup g2(cfg, QStringLiteral("profile-profile2"));
        g2.writeEntry("name", "Beta");
        g2.writeEntry("path", "/tmp/beta");
        g2.writeEntry("lastOpened",
                      QDateTime::fromString(QStringLiteral("2026-05-21T12:00:00Z"),
                                            Qt::ISODate));
        KConfigGroup gen(cfg, QStringLiteral("General"));
        gen.writeEntry("lastActiveProfileId", "profile2");
        cfg->sync();
    }

    // Load into a fresh registry.
    auto cfg = KSharedConfig::openConfig(cfgPath);
    ProfileRegistry reg(cfg);

    const auto entries = reg.entries();
    QCOMPARE(entries.size(), 2);
    // Sorted by lastOpened desc: profile2 (12:00) before profile1 (10:00).
    QCOMPARE(entries.at(0).id, QStringLiteral("profile2"));
    QCOMPARE(entries.at(0).name, QStringLiteral("Beta"));
    QCOMPARE(entries.at(0).path, QStringLiteral("/tmp/beta"));
    QCOMPARE(entries.at(1).id, QStringLiteral("profile1"));
    QCOMPARE(reg.lastActiveId(), QStringLiteral("profile2"));
    QVERIFY(reg.entry(QStringLiteral("profile1")).isValid());
    QVERIFY(!reg.entry(QStringLiteral("nope")).isValid());
}

void TstProfileRegistry::registerNewNoPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto e = reg.registerNew(QStringLiteral("Test Profile"));
    QVERIFY(e.isValid());
    QCOMPARE(e.id, QStringLiteral("profile1"));
    QCOMPARE(e.name, QStringLiteral("Test Profile"));
    QCOMPARE(e.path, tmp.path() + QStringLiteral("/wp-root/profile1"));
    QVERIFY(QDir(e.path).exists());
}

void TstProfileRegistry::registerNewWritesStubProfileConf()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto e = reg.registerNew(QStringLiteral("Stub Test"));
    QVERIFY(e.isValid());

    const QString confPath = e.path + QStringLiteral("/profile.conf");
    QVERIFY2(QFile::exists(confPath),
             qPrintable(QStringLiteral("Expected stub profile.conf at: ") + confPath));

    QSettings s(confPath, QSettings::IniFormat);
    QCOMPARE(s.value("meta/schemaVersion").toInt(), 1);
    QCOMPARE(s.value("profile/id").toString(), e.id);
    QCOMPARE(s.value("profile/name").toString(), QStringLiteral("Stub Test"));
}

void TstProfileRegistry::registerNewCustomPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const QString customPath = tmp.path() + QStringLiteral("/my-special-profile");
    const auto e = reg.registerNew(QStringLiteral("Special"), customPath);
    QVERIFY(e.isValid());
    QCOMPARE(e.id, QStringLiteral("my-special-profile"));  // basename
    QCOMPARE(e.path, customPath);
}

void TstProfileRegistry::registerNewCustomPathInvalidId()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    // Basename contains a dot; not in [A-Za-z0-9_-]+.
    const QString bad = tmp.path() + QStringLiteral("/bad.name");
    const auto e = reg.registerNew(QStringLiteral("X"), bad);
    QVERIFY(!e.isValid());
}

void TstProfileRegistry::registerNewSecondAllocation()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto a = reg.registerNew(QStringLiteral("A"));
    const auto b = reg.registerNew(QStringLiteral("B"));
    QVERIFY(a.isValid());
    QVERIFY(b.isValid());
    QCOMPARE(a.id, QStringLiteral("profile1"));
    QCOMPARE(b.id, QStringLiteral("profile2"));
    QCOMPARE(reg.entries().size(), 2);
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistry)
#include "tst_profileregistry.moc"
