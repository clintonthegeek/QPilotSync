#include <QtTest/QtTest>

#include <QIcon>
#include <QPointer>
#include <QSignalSpy>

#include "core/ibackendplugin.h"
#include "runtime/backendpluginmanager.h"

using WildPalms::BackendPluginManager;

namespace {

class FakeBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    QString pluginId() const override    { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QIcon   icon() const override        { return {}; }
    QString description() const override { return {}; }
    QString version() const override     { return QStringLiteral("1.0"); }
    QStringList claimedDatabases() const override
    {
        return {QStringLiteral("FakeDB")};
    }
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                     PalmDeviceConnection *) override
    {
        return {}; // both pointers nullptr; this test doesn't run the engine.
    }
};

class TestableBackendPluginManager : public WildPalms::BackendPluginManager
{
public:
    using BackendPluginManager::BackendPluginManager;
    using BackendPluginManager::registerInstanceForTest;
};

} // namespace

class TestBackendPluginManager : public QObject
{
    Q_OBJECT
private slots:
    void discoverWithEmptyPathYieldsEmptyCatalogue();
    void registerInjectedPluginShowsUpInQueries();
    void unloadInjectedPluginClearsInstance();
    void pluginForDatabaseMatchesClaim();
    void destructorUnloadsAll();
};

void TestBackendPluginManager::discoverWithEmptyPathYieldsEmptyCatalogue()
{
    BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms_e8_nonexistent"));
    mgr.discoverPlugins();

    QCOMPARE(mgr.catalogue().size(), 0);
    QCOMPARE(mgr.plugins().size(), 0);
}

void TestBackendPluginManager::registerInjectedPluginShowsUpInQueries()
{
    TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
    auto *fake = new FakeBackendPlugin();
    QVERIFY(mgr.registerInstanceForTest(QStringLiteral("fake"), fake));

    QCOMPARE(mgr.plugins().size(), 1);
    QCOMPARE(mgr.plugin(QStringLiteral("fake")), fake);
    QCOMPARE(mgr.catalogue().size(), 1);
}

void TestBackendPluginManager::unloadInjectedPluginClearsInstance()
{
    TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
    auto *fake = new FakeBackendPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake"), fake);

    QSignalSpy unloadSpy(&mgr,
        &WildPalms::BackendPluginManager::pluginUnloading);

    mgr.unloadPlugin(QStringLiteral("fake"));

    QCOMPARE(unloadSpy.count(), 1);
    QCOMPARE(mgr.plugins().size(), 0);
    QCOMPARE(mgr.plugin(QStringLiteral("fake")), nullptr);
}

void TestBackendPluginManager::pluginForDatabaseMatchesClaim()
{
    TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
    auto *fake = new FakeBackendPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake"), fake);

    QCOMPARE(mgr.pluginForDatabase(QStringLiteral("FakeDB")), fake);
    QCOMPARE(mgr.pluginForDatabase(QStringLiteral("OtherDB")), nullptr);
}

void TestBackendPluginManager::destructorUnloadsAll()
{
    QPointer<FakeBackendPlugin> fakeGuard;
    {
        TestableBackendPluginManager mgr(nullptr, nullptr, nullptr);
        auto *fake = new FakeBackendPlugin();
        fakeGuard = fake;
        mgr.registerInstanceForTest(QStringLiteral("fake"), fake);
        QVERIFY(!fakeGuard.isNull());
    }
    // Manager went out of scope; destructor should have deleted fake.
    QVERIFY(fakeGuard.isNull());
}

QTEST_MAIN(TestBackendPluginManager)
#include "tst_backendpluginmanager.moc"
