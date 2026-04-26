#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "webcalfeed.h"

using WildPalms::WebcalPlugin::WebcalFeed;

class TestWebcalFeed : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip_preservesAllFields()
    {
        WebcalFeed f;
        f.name        = QStringLiteral("US Holidays");
        f.url         = QUrl(QStringLiteral("https://example.com/us.ics"));
        f.palmSlot    = 5;
        f.enabled     = true;
        f.fetchPolicy = QStringLiteral("daily");

        const auto obj = f.toJson();
        const auto g   = WebcalFeed::fromJson(obj);

        QCOMPARE(g.name, f.name);
        QCOMPARE(g.url, f.url);
        QCOMPARE(g.palmSlot, f.palmSlot);
        QCOMPARE(g.enabled, f.enabled);
        QCOMPARE(g.fetchPolicy, f.fetchPolicy);
    }

    void legacyCategoryField_disablesFeed()
    {
        QJsonObject obj;
        obj[QStringLiteral("name")]     = QStringLiteral("Old Format");
        obj[QStringLiteral("url")]      = QStringLiteral("https://example.com/x.ics");
        obj[QStringLiteral("category")] = QStringLiteral("Work");
        obj[QStringLiteral("enabled")]  = true;

        const auto f = WebcalFeed::fromJson(obj);
        QVERIFY(!f.enabled);
        QCOMPARE(f.palmSlot, -1);
    }

    void slotOutOfRange_invalid()
    {
        WebcalFeed f;
        f.name     = QStringLiteral("x");
        f.url      = QUrl(QStringLiteral("https://example.com/x.ics"));
        f.palmSlot = 0;
        QVERIFY(!f.isValid());
        f.palmSlot = 16;
        QVERIFY(!f.isValid());
        f.palmSlot = 1;
        QVERIFY(f.isValid());
        f.palmSlot = 15;
        QVERIFY(f.isValid());
    }

    void emptyName_invalid()
    {
        WebcalFeed f;
        f.url      = QUrl(QStringLiteral("https://example.com/x.ics"));
        f.palmSlot = 5;
        QVERIFY(!f.isValid());
    }

    void invalidUrl_invalid()
    {
        WebcalFeed f;
        f.name     = QStringLiteral("x");
        f.url      = QUrl();
        f.palmSlot = 5;
        QVERIFY(!f.isValid());
    }

    void defaultFetchPolicy_isWeekly()
    {
        QJsonObject obj;
        obj[QStringLiteral("name")] = QStringLiteral("x");
        obj[QStringLiteral("url")]  = QStringLiteral("https://example.com/x.ics");
        obj[QStringLiteral("palm_slot")] = 5;
        // No fetch_policy set.
        const auto f = WebcalFeed::fromJson(obj);
        QCOMPARE(f.fetchPolicy, QStringLiteral("weekly"));
    }
};

QTEST_GUILESS_MAIN(TestWebcalFeed)
#include "tst_webcalfeed.moc"
