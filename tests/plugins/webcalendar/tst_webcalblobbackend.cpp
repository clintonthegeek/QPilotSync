#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>

#include <icsfeedfetcher.h>

#include "webcalblobbackend.h"
#include "webcalfeed.h"

using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

namespace {
QUrl fixtureUrl(const QString &name)
{
    const QString fixtureDir = QStringLiteral(WEBCAL_FIXTURE_DIR);
    return QUrl::fromLocalFile(QDir(fixtureDir).absoluteFilePath(name));
}
} // namespace

class TestWebcalBlobBackend : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_network = new QNetworkAccessManager(this);
        m_fetcher = new Kalburator::Sync::IcsFeedFetcher(m_network, this);
    }

    void availableCollections_oneEntryPerEnabledValidFeed()
    {
        WebcalFeed a;
        a.name = QStringLiteral("A"); a.url = fixtureUrl("three_events.ics");
        a.palmSlot = 5; a.enabled = true;
        WebcalFeed b;
        b.name = QStringLiteral("B"); b.url = fixtureUrl("two_events.ics");
        b.palmSlot = 6; b.enabled = true;
        WebcalFeed c;
        c.name = QStringLiteral("Disabled"); c.url = fixtureUrl("two_events.ics");
        c.palmSlot = 7; c.enabled = false;
        WebcalFeed d;
        d.name = QStringLiteral("BadSlot"); d.url = fixtureUrl("two_events.ics");
        d.palmSlot = 0; d.enabled = true;

        WebcalBlobBackend backend({a, b, c, d}, m_fetcher);
        const auto cols = backend.availableCollections();
        QCOMPARE(cols.size(), 2);
        QCOMPARE(cols[0].id, QStringLiteral("palm:calendar/5"));
        QCOMPARE(cols[1].id, QStringLiteral("palm:calendar/6"));
    }

    void loadRecords_returnsOneRecordPerVEVENT()
    {
        WebcalFeed f;
        f.name = QStringLiteral("Three"); f.url = fixtureUrl("three_events.ics");
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        const auto records = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 3);
        for (const auto &r : records) {
            QCOMPARE(r.type, QStringLiteral("event"));
            QVERIFY(r.data.contains("BEGIN:VEVENT"));
            QVERIFY(!r.contentHash.isEmpty());
        }
        QVERIFY(backend.lastFetchSucceeded(5));
    }

    void loadRecords_independentSlotsHitIndependentFeeds()
    {
        WebcalFeed a, b;
        a.name = QStringLiteral("Three"); a.url = fixtureUrl("three_events.ics");
        a.palmSlot = 5; a.enabled = true;
        b.name = QStringLiteral("Two"); b.url = fixtureUrl("two_events.ics");
        b.palmSlot = 6; b.enabled = true;

        WebcalBlobBackend backend({a, b}, m_fetcher);
        QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/5")).size(), 3);
        QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/6")).size(), 2);
    }

    void loadRecords_firstCallFailureReturnsEmpty()
    {
        WebcalFeed f;
        f.name = QStringLiteral("Missing");
        f.url = fixtureUrl("does_not_exist.ics");
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        const auto records = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 0);
        QVERIFY(!backend.lastFetchSucceeded(5));
    }

    void loadRecords_failureWithCacheReturnsCachedList()
    {
        // Copy a fixture to a temp file, load successfully, then delete
        // the temp file and load again — the second load should fail
        // (file::// to nonexistent path) but return the cached list
        // because of cache-on-failure semantics.
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.close();
        const QString tmpPath = tmp.fileName();
        QFile fixtureFile(QDir(QStringLiteral(WEBCAL_FIXTURE_DIR))
                              .absoluteFilePath(QStringLiteral("three_events.ics")));
        QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        const QByteArray bytes = fixtureFile.readAll();
        QFile out(tmpPath);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        out.close();

        WebcalFeed f;
        f.name = QStringLiteral("Temp");
        f.url = QUrl::fromLocalFile(tmpPath);
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        const auto first = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(first.size(), 3);
        QVERIFY(backend.lastFetchSucceeded(5));

        // Delete the temp file → next fetch fails → cache returned.
        QVERIFY(QFile::remove(tmpPath));
        const auto second = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QVERIFY(!backend.lastFetchSucceeded(5));
        QCOMPARE(second.size(), 3); // cache preserved
        QCOMPARE(second[0].id, first[0].id);
    }

    void writeMethods_areNoOpsOrFalse()
    {
        WebcalFeed f;
        f.name = QStringLiteral("Three"); f.url = fixtureUrl("three_events.ics");
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        Kalburator::Sync::BackendRecord rec;
        rec.id = QStringLiteral("x");
        QVERIFY(backend.createRecord(QStringLiteral("palm:calendar/5"), rec).isEmpty());
        QVERIFY(!backend.updateRecord(rec));
        QVERIFY(!backend.deleteRecord(QStringLiteral("x")));
        QVERIFY(backend.createCollection({}).isEmpty());
    }

private:
    QNetworkAccessManager *m_network = nullptr;
    Kalburator::Sync::IcsFeedFetcher *m_fetcher = nullptr;
};

QTEST_MAIN(TestWebcalBlobBackend)
#include "tst_webcalblobbackend.moc"
