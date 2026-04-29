#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "webcalbackendplugin.h"
#include "webcalblobbackend.h"
#include "webcalfeed.h"

using WildPalms::WebcalPlugin::WebcalBackendPlugin;
using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

class TestWebcalBackendPlugin : public QObject
{
    Q_OBJECT

private slots:
    void metadata_correct()
    {
        WebcalBackendPlugin p;
        QCOMPARE(p.pluginId(), QStringLiteral("webcalendar"));
        QCOMPARE(p.claimedDatabases().size(), 0);
        QVERIFY(!p.hasMainView());
        QVERIFY(!p.createConflictHandler());
        QCOMPARE(p.version(), QStringLiteral("2.0.0"));
    }

    void createBackends_returnsBlobNullCalendar()
    {
        WebcalBackendPlugin p;
        const auto out = p.createBackends(nullptr, nullptr);
        QVERIFY(out.blob != nullptr);
        QVERIFY(out.calendar == nullptr);
        QVERIFY(dynamic_cast<WebcalBlobBackend *>(out.blob) != nullptr);
        delete out.blob;
    }

    void loadSettings_parsesFeeds()
    {
        QJsonArray feeds;
        QJsonObject f1;
        f1[QStringLiteral("name")]      = QStringLiteral("US");
        f1[QStringLiteral("url")]       = QStringLiteral("https://x.com/us.ics");
        f1[QStringLiteral("palm_slot")] = 5;
        f1[QStringLiteral("enabled")]   = true;
        feeds.append(f1);
        QJsonObject f2;
        f2[QStringLiteral("name")]      = QStringLiteral("UK");
        f2[QStringLiteral("url")]       = QStringLiteral("https://x.com/uk.ics");
        f2[QStringLiteral("palm_slot")] = 6;
        f2[QStringLiteral("enabled")]   = true;
        feeds.append(f2);

        QJsonObject settings;
        settings[QStringLiteral("feeds")] = feeds;

        WebcalBackendPlugin p;
        p.loadSettings(settings);
        QCOMPARE(p.feeds().size(), 2);
        QCOMPARE(p.feeds()[0].palmSlot, 5);
        QCOMPARE(p.feeds()[1].palmSlot, 6);
    }

    void slotCollision_disablesBothFeeds()
    {
        QJsonArray feeds;
        QJsonObject f1;
        f1[QStringLiteral("name")]      = QStringLiteral("A");
        f1[QStringLiteral("url")]       = QStringLiteral("https://x.com/a.ics");
        f1[QStringLiteral("palm_slot")] = 5;
        f1[QStringLiteral("enabled")]   = true;
        feeds.append(f1);
        QJsonObject f2 = f1;
        f2[QStringLiteral("name")] = QStringLiteral("B");
        feeds.append(f2);

        QJsonObject settings;
        settings[QStringLiteral("feeds")] = feeds;

        WebcalBackendPlugin p;
        p.loadSettings(settings);
        QCOMPARE(p.feeds().size(), 2);
        QVERIFY(!p.feeds()[0].enabled);
        QVERIFY(!p.feeds()[1].enabled);
    }

    void saveSettings_roundTrip()
    {
        QJsonArray feeds;
        QJsonObject f1;
        f1[QStringLiteral("name")]      = QStringLiteral("US");
        f1[QStringLiteral("url")]       = QStringLiteral("https://x.com/us.ics");
        f1[QStringLiteral("palm_slot")] = 5;
        f1[QStringLiteral("enabled")]   = true;
        f1[QStringLiteral("fetch_policy")] = QStringLiteral("daily");
        feeds.append(f1);
        QJsonObject in;
        in[QStringLiteral("feeds")] = feeds;

        WebcalBackendPlugin p;
        p.loadSettings(in);
        const auto out = p.saveSettings();
        const auto outFeeds = out.value(QStringLiteral("feeds")).toArray();
        QCOMPARE(outFeeds.size(), 1);
        QCOMPARE(outFeeds[0].toObject().value("palm_slot").toInt(), 5);
        QCOMPARE(outFeeds[0].toObject().value("fetch_policy").toString(),
                 QStringLiteral("daily"));
    }
};

QTEST_GUILESS_MAIN(TestWebcalBackendPlugin)
#include "tst_webcalbackendplugin.moc"
