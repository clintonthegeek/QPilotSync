// M5c Task 2: smoke test — IBackendPluginV2 hasMainView()/createMainView() contract.
//
// KF6MainWindow's onBackendPluginLoaded slot was built against the V1
// IBackendPlugin interface, but post-M4 the real plugins implement
// IBackendPluginV2. PalmRuntime::connectDevice does the V2 discovery /
// qobject_cast / load loop. This test mirrors that loop, then asserts that
// every loaded V2 plugin claiming hasMainView()=true returns a non-null
// QWidget from createMainView() with a non-empty mainViewName(). That is
// the contract the per-plugin KPageWidget wiring depends on regardless
// of which manager (V1 BackendPluginManager or V2 PalmRuntime) ends up
// hosting it after M6.
//
// See FINDINGS.md for the V1-vs-V2 plumbing gap discovered during M5c.

#include <QtTest/QtTest>
#include <QApplication>
#include <QWidget>

#include <KPluginFactory>
#include <KPluginMetaData>

#include "core/ibackendplugin_v2.h"

class TstMainWindowPluginPagesPopulated : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void v2_plugins_with_main_view_create_non_null_widgets();
};

void TstMainWindowPluginPagesPopulated::initTestCase()
{
    // Real WildPalms plugins install at
    // ${CMAKE_BINARY_DIR}/lib/wildpalms/plugins/. Adding CMAKE_BINARY_DIR/lib
    // to the application's library paths makes
    // KPluginMetaData::findPlugins("wildpalms/plugins") pick them up.
    QCoreApplication::addLibraryPath(QStringLiteral(CMAKE_BINARY_DIR "/lib"));
}

void TstMainWindowPluginPagesPopulated::v2_plugins_with_main_view_create_non_null_widgets()
{
    // Mirror the discovery loop in PalmRuntime::connectDevice
    // (palmruntime.cpp), filtered to V2-migrated plugins only. The plucker
    // plugin is still V1 and would fail the qobject_cast below (and its
    // QObject destructor's interaction with the test parent triggers a
    // double-free at process exit).
    const auto metaDatas = KPluginMetaData::findPlugins(
        QStringLiteral("wildpalms/plugins"),
        [](const KPluginMetaData &md) {
            if (md.value(QStringLiteral("X-WildPalms-PluginType"))
                != QStringLiteral("backend"))
                return false;
            // Plucker has not yet been migrated to IBackendPluginV2.
            return !md.fileName().contains(QStringLiteral("plucker"));
        });

    QVERIFY2(!metaDatas.isEmpty(),
             "No backend plugins discovered — check that "
             "build/lib/wildpalms/plugins/*.so artifacts exist.");

    QStringList viewPluginIds;
    QStringList nonViewPluginIds;
    // Single parent QWidget owns both the plugin QObjects and the views
    // they create. Reverse-of-insertion deletion order means views are
    // destroyed before the plugins that wired them, avoiding use-after-
    // free in the plugin's view-shutdown path.
    QWidget parent;

    for (const KPluginMetaData &meta : metaDatas) {
        auto factoryResult = KPluginFactory::loadFactory(meta);
        QVERIFY2(factoryResult.plugin != nullptr,
                 qPrintable(QStringLiteral("Factory load failed for %1: %2")
                                .arg(meta.pluginId(), factoryResult.errorString)));

        QObject *obj = factoryResult.plugin->create<QObject>(&parent);
        QVERIFY2(obj != nullptr,
                 qPrintable(QStringLiteral("Factory returned nullptr for %1")
                                .arg(meta.pluginId())));

        auto *plugin = qobject_cast<WildPalms::IBackendPluginV2 *>(obj);
        QVERIFY2(plugin != nullptr,
                 qPrintable(QStringLiteral("Plugin %1 does not implement "
                                            "IBackendPluginV2 — post-M4 contract.")
                                .arg(meta.pluginId())));

        if (plugin->hasMainView()) {
            QWidget *view = plugin->createMainView(&parent);
            QVERIFY2(view != nullptr,
                qPrintable(QStringLiteral(
                    "Plugin '%1' claims hasMainView()=true but "
                    "createMainView() returned nullptr — any per-plugin "
                    "KPageWidget wiring would silently skip it.")
                    .arg(plugin->pluginId())));
            QVERIFY2(!plugin->mainViewName().isEmpty(),
                qPrintable(QStringLiteral(
                    "Plugin '%1' claims hasMainView()=true but "
                    "mainViewName() is empty — page header would be blank.")
                    .arg(plugin->pluginId())));
            viewPluginIds << plugin->pluginId();
        } else {
            nonViewPluginIds << plugin->pluginId();
        }
        // obj is parented to `this`; Qt cleans it up when the test object
        // destructs. A manual `delete obj` here would double-free.
    }

    QVERIFY2(!viewPluginIds.isEmpty(),
             "Expected at least one V2 plugin with hasMainView()=true; "
             "any per-plugin page area would be empty.");

    qInfo() << "V2 plugins with main view:" << viewPluginIds;
    qInfo() << "V2 plugins without main view:" << nonViewPluginIds;
}

// Custom main: QTEST_MAIN runs std::exit() at the end, which triggers
// static destructors in the loaded plugin .so files. Some plugins have
// known-buggy global destructors (e.g. QNetworkAccessManager interactions
// inside the webcalendar plugin) that abort the process even after all
// assertions pass. Use _exit() to skip global teardown — the test logic's
// pass/fail signal is already in QTest::qExec's return value.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TstMainWindowPluginPagesPopulated tc;
    const int rc = QTest::qExec(&tc, argc, argv);
    _exit(rc);
}
#include "tst_main_window_plugin_pages_populated.moc"
