#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

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
    void addProvider_returns_uuid_and_persists();
    void addProvider_refused_for_unsupported_kind();
    void removeProvider_drops_from_list_and_sidecar();
    void loadFromProfile_handlesUnreachableServer();
    void removeProvider_cascadesMappings();
    void mappingDescriptionsFor_returns_first_N();
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
        ac.addProvider("carddav", cfg);
    }

    QVERIFY(QFile::exists(QDir(dir.path())
        .filePath(".wildpalms.providers")));
    KConfig cfg(QDir(dir.path()).filePath(".wildpalms.providers"),
                KConfig::SimpleConfig);
    QVERIFY(cfg.hasGroup(QStringLiteral("Providers")));
}

void TstAccountController::addProvider_returns_uuid_and_persists()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.displayName = "TestServer";
    cfg.connectionParams["url"] = "https://nonresolvable.example/";
    cfg.connectionParams["username"] = "alice";

    QString uuid = ac.addProvider("carddav", cfg);
    QVERIFY(!uuid.isEmpty());
    QCOMPARE(ac.providers().size(), 1);

    QVERIFY(QFile::exists(QDir(dir.path()).filePath(".wildpalms.providers")));
    KConfig sc(QDir(dir.path()).filePath(".wildpalms.providers"),
               KConfig::SimpleConfig);
    QVERIFY(sc.hasGroup("Providers"));
    KConfigGroup root = sc.group("Providers");
    QVERIFY(root.hasGroup(uuid));
    QCOMPARE(root.group(uuid).readEntry("kind"),
             QStringLiteral("carddav"));
}

void TstAccountController::addProvider_refused_for_unsupported_kind()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams["url"] = "https://x/";
    QString uuid = ac.addProvider("unsupported-kind", cfg);
    QVERIFY(uuid.isEmpty());
    QCOMPARE(ac.providers().size(), 0);
}

void TstAccountController::removeProvider_drops_from_list_and_sidecar()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams["url"] = "https://x/";
    QString uuid = ac.addProvider("carddav", cfg);
    QCOMPARE(ac.providers().size(), 1);

    QVERIFY(ac.removeProvider(uuid));
    QCOMPARE(ac.providers().size(), 0);

    KConfig sc(QDir(dir.path()).filePath(".wildpalms.providers"),
               KConfig::SimpleConfig);
    QVERIFY(!sc.group("Providers").hasGroup(uuid));
}

void TstAccountController::loadFromProfile_handlesUnreachableServer()
{
    QTemporaryDir dir;
    {
        KConfig sc(QDir(dir.path()).filePath(".wildpalms.providers"),
                   KConfig::SimpleConfig);
        KConfigGroup g = sc.group("Providers").group("dead-uuid");
        g.writeEntry("kind", "carddav");
        g.writeEntry("url", "https://this-server-does-not-resolve.invalid/");
        g.writeEntry("username", "x");
        sc.sync();
    }
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QCOMPARE(ac.providers().size(), 1);
    QCOMPARE(ac.providers().first()->id(),
             QStringLiteral("dead-uuid"));
}

void TstAccountController::removeProvider_cascadesMappings()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams["url"] = "https://x/";
    const QString uuid = ac.addProvider("carddav", cfg);

    // Seed three mappings: 2 reference the provider, 1 doesn't.
    QJsonArray maps;
    maps.append(QJsonObject{
        {"id", "m1"}, {"sourceBackend", "palm:contact/0"},
        {"targetBackend", uuid + ":addrbook-1"},
        {"sourceCalendar", ""}, {"targetCalendar", ""}});
    maps.append(QJsonObject{
        {"id", "m2"}, {"sourceBackend", uuid + ":addrbook-2"},
        {"targetBackend", "palm:contact/1"},
        {"sourceCalendar", ""}, {"targetCalendar", ""}});
    maps.append(QJsonObject{
        {"id", "m3"}, {"sourceBackend", "palm:calendar/0"},
        {"targetBackend", "rawfiles-cal"},
        {"sourceCalendar", ""}, {"targetCalendar", ""}});
    profile.setSyncMappingsJson(maps);
    profile.save();

    QCOMPARE(ac.mappingCountFor(uuid), 2);

    QSignalSpy spy(&ac, &WildPalms::Runtime::AccountController::mappingsChanged);
    QVERIFY(ac.removeProvider(uuid));
    QCOMPARE(spy.count(), 1);

    const QJsonArray after = profile.syncMappingsJson();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().toObject().value("id").toString(),
             QStringLiteral("m3"));
}

void TstAccountController::mappingDescriptionsFor_returns_first_N()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams["url"] = "https://x/";
    const QString uuid = ac.addProvider("carddav", cfg);

    QJsonArray maps;
    for (int i = 0; i < 5; ++i) {
        maps.append(QJsonObject{
            {"id", QString("m%1").arg(i)},
            {"sourceBackend", "palm:contact/0"},
            {"targetBackend", uuid + QString(":book-%1").arg(i)},
            {"sourceCalendar", ""}, {"targetCalendar", ""}});
    }
    profile.setSyncMappingsJson(maps);

    auto descs = ac.mappingDescriptionsFor(uuid, 3);
    QCOMPARE(descs.size(), 3);
    QVERIFY(descs.first().startsWith("palm:contact/0"));
}

QTEST_MAIN(TstAccountController)
#include "tst_account_controller.moc"
