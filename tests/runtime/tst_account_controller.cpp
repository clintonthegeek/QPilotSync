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

#include <iprovider.h>
#include <backendconfiguration.h>

class TstAccountController : public QObject {
    Q_OBJECT
private slots:
    void constructs_and_destructs_cleanly();
    void loadFromProfile_reads_existing_accounts();
    void addProvider_returns_uuid_and_persists_to_profile();
    void addProvider_refused_for_unsupported_kind();
    void removeProvider_drops_from_list_and_profile();
    void loadFromProfile_handlesUnreachableServer();
    void removeProvider_cascadesMappings();
    void mappingDescriptionsFor_returns_first_N();
    void ac_lifetime_matches_profile_switch();
    void appendMappings_writes_and_persists();
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

void TstAccountController::loadFromProfile_reads_existing_accounts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Seed Profile with one CardDAV account before constructing AC.
    {
        Profile p(dir.path());
        p.initialize();
        Kalburator::Sync::BackendConfiguration cfg;
        cfg.id = QStringLiteral("test-uuid-1");
        cfg.type = QStringLiteral("carddav");
        cfg.displayName = QStringLiteral("Personal CardDAV");
        cfg.connectionParams[QStringLiteral("url")] =
            QStringLiteral("https://nonresolvable.example/");
        cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");
        p.saveAccount(cfg);
        QVERIFY(p.save());
    }

    Profile profile(dir.path());
    QVERIFY(profile.load());
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

void TstAccountController::addProvider_returns_uuid_and_persists_to_profile()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.id = QStringLiteral("manual-uuid");
    cfg.displayName = QStringLiteral("TestServer");
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://nonresolvable.example/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");

    QString uuid = ac.addProvider(QStringLiteral("carddav"), cfg);
    QVERIFY(!uuid.isEmpty());
    QCOMPARE(uuid, QStringLiteral("manual-uuid"));
    QCOMPARE(ac.providers().size(), 1);

    // Verify account was persisted to Profile, not a sidecar.
    const auto accts = profile.accounts();
    QCOMPARE(accts.size(), 1);
    QCOMPARE(accts.first().id, QStringLiteral("manual-uuid"));
    QCOMPARE(accts.first().type, QStringLiteral("carddav"));
}

void TstAccountController::addProvider_refused_for_unsupported_kind()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    QString uuid = ac.addProvider(QStringLiteral("unsupported-kind"), cfg);
    QVERIFY(uuid.isEmpty());
    QCOMPARE(ac.providers().size(), 0);
}

void TstAccountController::removeProvider_drops_from_list_and_profile()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    QString uuid = ac.addProvider(QStringLiteral("carddav"), cfg);
    QCOMPARE(ac.providers().size(), 1);
    QCOMPARE(profile.accounts().size(), 1);

    QVERIFY(ac.removeProvider(uuid));
    QCOMPARE(ac.providers().size(), 0);

    // Account must be gone from Profile, not just from the in-memory list.
    QCOMPARE(profile.accounts().size(), 0);
}

void TstAccountController::loadFromProfile_handlesUnreachableServer()
{
    QTemporaryDir dir;

    // Seed Profile with an unreachable CardDAV account.
    {
        Profile p(dir.path());
        p.initialize();
        Kalburator::Sync::BackendConfiguration cfg;
        cfg.id = QStringLiteral("dead-uuid");
        cfg.type = QStringLiteral("carddav");
        cfg.connectionParams[QStringLiteral("url")] =
            QStringLiteral("https://this-server-does-not-resolve.invalid/");
        cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("x");
        p.saveAccount(cfg);
        QVERIFY(p.save());
    }

    Profile profile(dir.path());
    QVERIFY(profile.load());
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
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    const QString uuid = ac.addProvider(QStringLiteral("carddav"), cfg);

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
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    const QString uuid = ac.addProvider(QStringLiteral("carddav"), cfg);

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

void TstAccountController::ac_lifetime_matches_profile_switch()
{
    // Simulates the loadProfile teardown order: AC first (releases borrowed
    // registry), then PalmRuntime, then Profile. Inverted order would
    // crash (registry destroyed while AC's ProviderManager still holds it).
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    auto rt = std::make_unique<WildPalms::Runtime::PalmRuntime>(
        dir.path() + "/state");

    auto ac = std::make_unique<WildPalms::Runtime::AccountController>(
        dir.path(),
        &rt->backendRegistry(),
        &profile,
        rt.get());

    ac.reset();
    rt.reset();
    QVERIFY(true);  // Reaching here means no use-after-free.
}

void TstAccountController::appendMappings_writes_and_persists()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QJsonArray rows;
    rows.append(QJsonObject{
        {"id", "new-1"},
        {"sourceBackend", "palm:contact/0"},
        {"targetBackend", "fake-uuid:abc"},
        {"sourceCalendar", ""}, {"targetCalendar", "abc"},
        {"mode", "TwoWay"}, {"conflictPolicy", "AskUser"},
        {"enabled", true}});

    QSignalSpy spy(&ac, &WildPalms::Runtime::AccountController::mappingsChanged);
    ac.appendMappings(rows);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(profile.syncMappingsJson().size(), 1);
}

QTEST_MAIN(TstAccountController)
#include "tst_account_controller.moc"
