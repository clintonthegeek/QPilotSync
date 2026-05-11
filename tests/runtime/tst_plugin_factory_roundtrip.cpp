#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QSignalSpy>

#include "core/ibackendplugin_v2.h"
#include "core/ipluginaction.h"
#include "runtime/backendpluginmanager.h"
#include "runtime/pluginactionmanager.h"
#include "runtime/simpleactioncontext.h"

/// Phase E.8 round-trip: real .so plugins built under tests/plugins/ get
/// discovered, loaded, queried, and unloaded through the new-ABI managers.

class TestPluginFactoryRoundtrip : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void backendManagerLoadsAndUnloadsDummy();
    void actionManagerLoadsAndExecutesDummy();

private:
    QString m_libraryPathAdded;
};

void TestPluginFactoryRoundtrip::initTestCase()
{
    // The dummy plugins install under ${CMAKE_BINARY_DIR}/lib/wildpalms_test/plugins/.
    // Qt looks up plugins relative to library paths. Adding the lib/ dir
    // makes KPluginMetaData::findPlugins("wildpalms_test/plugins") work.
    const QString binDir = QCoreApplication::applicationDirPath();
    // Walk up until we find a directory containing "wildpalms_test/plugins"
    // under either "lib" or "bin".
    QDir d(binDir);
    QString candidate;
    for (int i = 0; i < 6; ++i) {
        if (d.exists(QStringLiteral("wildpalms_test/plugins"))) {
            candidate = d.absolutePath();
            break;
        }
        if (d.exists(QStringLiteral("lib/wildpalms_test/plugins"))) {
            candidate = d.absoluteFilePath(QStringLiteral("lib"));
            break;
        }
        if (d.exists(QStringLiteral("bin/wildpalms_test/plugins"))) {
            candidate = d.absoluteFilePath(QStringLiteral("bin"));
            break;
        }
        if (!d.cdUp()) break;
    }
    QVERIFY2(!candidate.isEmpty(),
             "Could not locate wildpalms_test/plugins under the build tree. "
             "Run cmake --build . --target dummy_backend_plugin dummy_action_plugin first.");

    m_libraryPathAdded = candidate;
    QCoreApplication::addLibraryPath(m_libraryPathAdded);
}

void TestPluginFactoryRoundtrip::backendManagerLoadsAndUnloadsDummy()
{
    // KF6 derives the pluginId from the .so filename (stripping the .so suffix
    // but NOT the "lib" prefix on Linux). The cmake target is "dummy_backend"
    // so the file is "libdummy_backend.so" → pluginId "libdummy_backend".
    static const QString kBackendId = QStringLiteral("libdummy_backend");

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms_test/plugins"));
    mgr.discoverPlugins();

    const auto cat = mgr.catalogue();
    QVERIFY2(std::any_of(cat.begin(), cat.end(),
                         [](const auto &info){
                             return info.metaData.pluginId() == QStringLiteral("libdummy_backend");
                         }),
             "libdummy_backend not found in catalogue");

    QSignalSpy loadSpy(&mgr, &WildPalms::BackendPluginManager::pluginLoaded);
    QVERIFY(mgr.loadPlugin(kBackendId));
    QCOMPARE(loadSpy.count(), 1);

    WildPalms::IBackendPluginV2 *plug = mgr.plugin(kBackendId);
    QVERIFY(plug != nullptr);
    QCOMPARE(plug->pluginId(), QStringLiteral("dummy_backend"));
    QCOMPARE(plug->claimedDatabases(), (QStringList{QStringLiteral("DummyDB")}));
    QCOMPARE(mgr.pluginForDatabase(QStringLiteral("DummyDB")), plug);

    QSignalSpy unloadSpy(&mgr, &WildPalms::BackendPluginManager::pluginUnloading);
    mgr.unloadPlugin(kBackendId);
    QCOMPARE(unloadSpy.count(), 1);
    QCOMPARE(mgr.plugin(kBackendId), nullptr);
}

void TestPluginFactoryRoundtrip::actionManagerLoadsAndExecutesDummy()
{
    // KF6 derives the pluginId from the .so filename (stripping the .so suffix
    // but NOT the "lib" prefix on Linux). The cmake target is "dummy_action"
    // so the file is "libdummy_action.so" → pluginId "libdummy_action".
    static const QString kActionId = QStringLiteral("libdummy_action");

    WildPalms::PluginActionManager mgr(nullptr);
    mgr.setPluginSubdir(QStringLiteral("wildpalms_test/plugins"));
    mgr.discoverActions();

    QVERIFY(mgr.loadAction(kActionId));

    WildPalms::IPluginAction *action = mgr.action(kActionId);
    QVERIFY(action != nullptr);
    QCOMPARE(action->pluginId(), QStringLiteral("dummy_action"));
    QVERIFY(!action->preconditions().requiresDeviceConnection);

    WildPalms::SimpleActionContext ctx;
    QSignalSpy messageSpy(&ctx, &WildPalms::SimpleActionContext::message);
    QVERIFY(action->execute(&ctx, nullptr, QJsonObject()));
    QCOMPARE(ctx.total(), 1);
    QCOMPARE(ctx.current(), 1);
    QCOMPARE(messageSpy.count(), 1);

    mgr.unloadAction(kActionId);
    QCOMPARE(mgr.action(kActionId), nullptr);
}

QTEST_MAIN(TestPluginFactoryRoundtrip)
#include "tst_plugin_factory_roundtrip.moc"
