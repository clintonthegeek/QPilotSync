#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include <KConfig>
#include <KConfigGroup>

#include <providermanager.h>
#include <iprovider.h>
#include <carddavprovider.h>
#include <backendconfiguration.h>

class TstAccountController : public QObject {
    Q_OBJECT
private slots:
    void constructs_and_destructs_cleanly();
    void loadFromProfile_reads_existing_sidecar();
    void persist_writes_sidecar();
};

void TstAccountController::constructs_and_destructs_cleanly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Profile profile(dir.path());
    profile.initialize();

    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    using AC = WildPalms::Runtime::AccountController;
    AC ac(dir.path(),
          &rt.backendRegistry(),
          &profile,
          &rt);

    QCOMPARE(ac.providers().size(), 0);
    QCOMPARE(ac.mappingCountFor("nonexistent"), 0);
}

void TstAccountController::loadFromProfile_reads_existing_sidecar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Hand-write a sidecar with one CardDAV provider.
    {
        KConfig cfg(QDir(dir.path()).filePath(".wildpalms.providers"),
                    KConfig::SimpleConfig);
        KConfigGroup root = cfg.group(QStringLiteral("Providers"));
        KConfigGroup sub  = root.group(QStringLiteral("test-uuid-1"));
        sub.writeEntry("kind", "carddav");
        sub.writeEntry("displayName", "Personal CardDAV");
        sub.writeEntry("url", "https://nonresolvable.example/");
        sub.writeEntry("username", "alice");
        cfg.sync();
    }

    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QCOMPARE(ac.providers().size(), 1);
    QCOMPARE(ac.providers().first()->id(),
             QStringLiteral("test-uuid-1"));
    QCOMPARE(ac.providers().first()->kind(),
             QStringLiteral("carddav"));
    QCOMPARE(ac.providers().first()->displayName(),
             QStringLiteral("Personal CardDAV"));
}

void TstAccountController::persist_writes_sidecar()
{
    // Persistence trigger lands in Task 5 (addProvider calls persist()).
    // For Task 4 this is a placeholder; un-skip in Task 5.
    QSKIP("Persistence trigger lands in Task 5", SkipAll);

    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    {
        WildPalms::Runtime::AccountController ac(dir.path(),
            &rt.backendRegistry(), &profile, &rt);

        Kalburator::Sync::BackendConfiguration cfg;
        cfg.id = "manual-uuid";
        cfg.displayName = "Manual";
        cfg.connectionParams["url"] = "https://example.test/";
        auto provider = std::make_unique<Kalburator::Sync::CardDavProvider>();
        provider->load(cfg);
        ac.providerManager()->addProvider(std::move(provider));
    }

    QVERIFY(QFile::exists(QDir(dir.path())
        .filePath(".wildpalms.providers")));
    KConfig cfg(QDir(dir.path()).filePath(".wildpalms.providers"),
                KConfig::SimpleConfig);
    QVERIFY(cfg.hasGroup(QStringLiteral("Providers")));
}

QTEST_MAIN(TstAccountController)
#include "tst_account_controller.moc"
