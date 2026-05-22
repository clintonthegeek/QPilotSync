#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QDateTime>

#include <KConfigGroup>

#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

class TestableMainWindow : public KF6MainWindow {
public:
    using KF6MainWindow::KF6MainWindow;

    int     stopgapInvocations = 0;
    QString stopgapReturn;

protected:
    QString showProfilePickerStopgap() override {
        ++stopgapInvocations;
        return stopgapReturn;
    }
};

class TstKf6MainWindowStartup : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegistryInvokesStopgap();
    void validLastActiveAutoLoads();
    void staleLastActiveFallsBack();
};

void TstKf6MainWindowStartup::emptyRegistryInvokesStopgap()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);

    TestableMainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.stopgapReturn = QString();   // user clicks Cancel on stopgap

    const QString picked = w.runStartupForTest();

    QCOMPARE(w.stopgapInvocations, 1);
    QVERIFY(picked.isEmpty());
}

void TstKf6MainWindowStartup::validLastActiveAutoLoads()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto entry = reg->registerNew(QStringLiteral("AutoLoad"));
    QVERIFY(entry.isValid());
    reg->setLastActive(entry.id);
    QVERIFY(QDir(entry.path).exists());

    TestableMainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.stopgapReturn = QString();   // stopgap should never be invoked

    const QString picked = w.runStartupForTest();

    QCOMPARE(w.stopgapInvocations, 0);
    QCOMPARE(picked, entry.path);
}

void TstKf6MainWindowStartup::staleLastActiveFallsBack()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));

    // Pre-write a registry entry whose path does NOT exist.
    {
        KConfigGroup g(cfg, QStringLiteral("profile-profile1"));
        g.writeEntry("name", "Ghost");
        g.writeEntry("path", "/nonexistent-path/profile1");
        g.writeEntry("lastOpened", QDateTime::currentDateTimeUtc());
        KConfigGroup gen(cfg, QStringLiteral("General"));
        gen.writeEntry("lastActiveProfileId", "profile1");
        cfg->sync();
    }

    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    QCOMPARE(reg->lastActiveId(), QStringLiteral("profile1"));

    TestableMainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.stopgapReturn = QString();

    const QString picked = w.runStartupForTest();

    QCOMPARE(w.stopgapInvocations, 1);   // last-active stale, picker fired
    QVERIFY(picked.isEmpty());
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowStartup)
#include "tst_kf6mainwindow_startup.moc"
