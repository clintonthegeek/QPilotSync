#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "canonseed.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::DeviceE2E;

class TestCanonCalendarSeed : public QObject
{
    Q_OBJECT
private slots:
    void buildsValidCanonEnvelope()
    {
        CanonCalendarEventSpec spec; // defaults
        const QByteArray bytes = buildCanonCalendarEvent(spec);
        QVERIFY(!bytes.isEmpty());

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        const QJsonObject obj = doc.object();

        QCOMPARE(obj.value(QStringLiteral("_canon")).toObject().value(QStringLiteral("domain")).toString(),
                 QStringLiteral("calendar"));
        QCOMPARE(obj.value(QStringLiteral("uid")).toString(), QStringLiteral("seed-event-001@wildpalms"));
        QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("Seeded Event"));
        QCOMPARE(obj.value(QStringLiteral("description")).toString(), QStringLiteral("Note body text"));
        QCOMPARE(obj.value(QStringLiteral("allDay")).toBool(), false);
        QCOMPARE(obj.value(QStringLiteral("start")).toObject().value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-07-01T09:00:00Z"));
        QCOMPARE(obj.value(QStringLiteral("end")).toObject().value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-07-01T10:00:00Z"));
        const auto alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms.at(0).toObject().value(QStringLiteral("offset")).toInt(), -600);
        QVERIFY(!obj.contains(QStringLiteral("categories"))); // Unfiled by design
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestCanonCalendarSeed)
#include "tst_canon_calendar_seed.moc"
