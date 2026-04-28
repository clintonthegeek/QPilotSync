#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTest>

#include <KPluginFactory>
#include <KPluginMetaData>

#include "pluckerbackendplugin.h"

using namespace WildPalms::PluckerPlugin;

class TestPluckerBackendPlugin : public QObject
{
    Q_OBJECT

private slots:
    void identity_metadata()
    {
        PluckerBackendPlugin plugin;
        QCOMPARE(plugin.pluginId(),    QStringLiteral("plucker"));
        QCOMPARE(plugin.displayName(), QStringLiteral("Plucker"));
        QCOMPARE(plugin.version(),     QStringLiteral("2.0.0"));
        QVERIFY(plugin.claimedDatabases().isEmpty());
        QVERIFY(!plugin.hasMainView());
        QVERIFY(plugin.hasSettings());
    }

    void runAfter_listsAllPriorPlugins()
    {
        PluckerBackendPlugin plugin;
        const QStringList ra = plugin.runAfter();
        QVERIFY(ra.contains(QStringLiteral("memo")));
        QVERIFY(ra.contains(QStringLiteral("calendar")));
        QVERIFY(ra.contains(QStringLiteral("todo")));
        QVERIFY(ra.contains(QStringLiteral("contacts")));
        QVERIFY(ra.contains(QStringLiteral("webcalendar")));
    }

    void settings_roundTripJson()
    {
        QJsonObject in;
        QJsonArray channels;
        QJsonObject ch;
        ch[QStringLiteral("id")]       = QStringLiteral("c1");
        ch[QStringLiteral("name")]     = QStringLiteral("Alpha");
        ch[QStringLiteral("home_url")] = QStringLiteral("https://a.example/");
        ch[QStringLiteral("max_depth")] = 4;
        channels.append(ch);
        in[QStringLiteral("channels")] = channels;

        PluckerBackendPlugin plugin;
        plugin.loadSettings(in);
        const QJsonObject out = plugin.saveSettings();

        QVERIFY(out.contains(QStringLiteral("channels")));
        const QJsonArray outChannels = out[QStringLiteral("channels")].toArray();
        QCOMPARE(outChannels.size(), 1);
        const QJsonObject ch0 = outChannels[0].toObject();
        QCOMPARE(ch0[QStringLiteral("id")].toString(),       QStringLiteral("c1"));
        QCOMPARE(ch0[QStringLiteral("name")].toString(),     QStringLiteral("Alpha"));
        QCOMPARE(ch0[QStringLiteral("max_depth")].toInt(),   4);
    }

    void settings_emptyJsonYieldsNoChannels()
    {
        PluckerBackendPlugin plugin;
        plugin.loadSettings(QJsonObject{});
        QCOMPARE(plugin.channels().size(), 0);
    }

    void factory_soLoadsAndExposesKPluginFactory()
    {
        // We only test the .so loads cleanly and exposes a KPluginFactory.
        // Cross-binary qobject_cast back to PluckerBackendPlugin would fail
        // because the test executable links its own copy of the plugin
        // sources and thus has a distinct QMetaObject. Cross-binary
        // factory→PluckerBackendPlugin instantiation is exercised via
        // tests/plugins/dummy_backend/tst_plugin_factory_roundtrip.
        const QString pluginSo = QStringLiteral(PLUCKER_PLUGIN_SO_PATH);
        QPluginLoader loader(pluginSo);
        QObject *root = loader.instance();
        QVERIFY2(root, qPrintable(loader.errorString()));
        QVERIFY(qobject_cast<KPluginFactory*>(root) != nullptr);
    }

    void createBackends_returnsBlobNullCalendar()
    {
        PluckerBackendPlugin p;
        const auto out = p.createBackends(nullptr, nullptr);
        QVERIFY(out.blob != nullptr);
        QVERIFY(out.calendar == nullptr);
    }
};

QTEST_MAIN(TestPluckerBackendPlugin)
#include "tst_pluckerbackendplugin.moc"
