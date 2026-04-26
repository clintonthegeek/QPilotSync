#include <QCoreApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTest>
#include <QUrl>

#include <blobsyncengine.h>
#include <icsfeedfetcher.h>
#include <mockblobbackend.h>

#include "webcalbackendplugin.h"
#include "webcalblobbackend.h"
#include "webcalfeed.h"

using namespace Kalburator::Sync;
using WildPalms::WebcalPlugin::WebcalBackendPlugin;
using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

namespace {
QUrl fixtureUrl(const QString &name)
{
    const QString fixtureDir = QStringLiteral(WEBCAL_FIXTURE_DIR);
    return QUrl::fromLocalFile(QDir(fixtureDir).absoluteFilePath(name));
}

WebcalFeed makeFeed(int slot, const QString &fixtureName,
                     const QString &name = QStringLiteral("Feed"))
{
    WebcalFeed f;
    f.name      = name;
    f.url       = fixtureUrl(fixtureName);
    f.palmSlot  = slot;
    f.enabled   = true;
    return f;
}
} // namespace

class TestWebcalV2E2E : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_network = new QNetworkAccessManager(this);
        m_fetcher = new IcsFeedFetcher(m_network, this);
    }

    void mirror_emptyTargetGainsAllSourceRecords()
    {
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("three_events.ics"))},
                                m_fetcher);
        MockBlobBackend dst;
        CollectionInfo c;
        c.id   = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        // Drive a fetch first to populate cache + lastFetchSucceeded.
        src.loadRecords(QStringLiteral("palm:calendar/5"));
        QVERIFY(src.lastFetchSucceeded(5));

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));

        const auto records = dst.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 3);
    }

    void mirror_staleTargetLosesAndGainsCorrectly()
    {
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("three_events.ics"))},
                                m_fetcher);

        // Pre-populate target with 2 stale records that are NOT in source.
        MockBlobBackend dst;
        CollectionInfo c;
        c.id = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        BackendRecord s1; s1.id = QStringLiteral("stale-001@example.com");
        s1.type = QStringLiteral("event"); s1.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
        s1.contentHash = QStringLiteral("aa");
        BackendRecord s2; s2.id = QStringLiteral("stale-002@example.com");
        s2.type = QStringLiteral("event"); s2.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
        s2.contentHash = QStringLiteral("bb");

        dst.createRecord(QStringLiteral("palm:calendar/5"), s1);
        dst.createRecord(QStringLiteral("palm:calendar/5"), s2);
        QCOMPARE(dst.loadRecords(QStringLiteral("palm:calendar/5")).size(), 2);

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));

        const auto records = dst.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 3);
        for (const auto &r : records) {
            QVERIFY(r.id != QStringLiteral("stale-001@example.com"));
            QVERIFY(r.id != QStringLiteral("stale-002@example.com"));
        }
    }

    void mirror_identicalContentNoOp()
    {
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("three_events.ics"))},
                                m_fetcher);
        MockBlobBackend dst;
        CollectionInfo c;
        c.id = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        const auto first = dst.loadRecords(QStringLiteral("palm:calendar/5"));

        // Second mirror against identical source → target unchanged.
        engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        const auto second = dst.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(second.size(), first.size());
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(second[i].contentHash, first[i].contentHash);
        }
    }

    void mirror_firstFetchFailureGate_skipsMirror()
    {
        // Configure feed with a non-existent URL.
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("does_not_exist.ics"))},
                                m_fetcher);
        MockBlobBackend dst;
        CollectionInfo c;
        c.id = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        // Pre-populate target with a record. If the gate is honoured,
        // the target is preserved; if violated (caller naively calls
        // mirror), the empty-source-deletes-target footgun fires.
        BackendRecord r;
        r.id = QStringLiteral("preserved-001@example.com");
        r.type = QStringLiteral("event");
        r.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
        r.contentHash = QStringLiteral("cc");
        dst.createRecord(QStringLiteral("palm:calendar/5"), r);
        QCOMPARE(dst.loadRecords(QStringLiteral("palm:calendar/5")).size(), 1);

        // Drive a fetch (populates lastFetchOk for slot 5, will be false).
        src.loadRecords(QStringLiteral("palm:calendar/5"));
        QVERIFY(!src.lastFetchSucceeded(5));

        // Caller (this test, standing in for the runtime) honours the
        // gate by skipping the mirror call.
        if (src.lastFetchSucceeded(5)) {
            BlobSyncEngine engine;
            engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        }

        // Target preserved.
        QCOMPARE(dst.loadRecords(QStringLiteral("palm:calendar/5")).size(), 1);
    }

private:
    QNetworkAccessManager *m_network = nullptr;
    IcsFeedFetcher *m_fetcher = nullptr;
};

QTEST_MAIN(TestWebcalV2E2E)
#include "tst_webcal_v2_e2e.moc"
