#include <QTest>

#include "pluckerblobbackend.h"
#include "pluckerchannel.h"
#include "pluckerfetcher.h"

using namespace Kalburator::Sync;
using namespace WildPalms::PluckerPlugin;

namespace {

class FakePluckerFetcher : public PluckerFetcher
{
public:
    using PluckerFetcher::PluckerFetcher;

    Result fetch(const PluckerChannel &channel, int /*timeoutMs*/ = 0) override
    {
        ++m_calls;
        m_lastChannelId = channel.id;
        Result r;
        r.docFile = pluckerSanitizeDocFile(channel.name);
        if (m_failNext) {
            r.success = false;
            r.errorMessage = QStringLiteral("simulated");
            m_failNext = false;
        } else {
            r.success  = true;
            r.pdbBytes = QStringLiteral("PDB:%1").arg(channel.id).toUtf8();
        }
        return r;
    }

    int      callCount()         const { return m_calls; }
    QString  lastChannelId()     const { return m_lastChannelId; }
    void     setFailNext(bool f)       { m_failNext = f; }

private:
    int     m_calls = 0;
    bool    m_failNext = false;
    QString m_lastChannelId;
};

PluckerChannel makeChannel(const QString &id, const QString &name,
                            bool enabled = true,
                            const QDateTime &lastFetched = {})
{
    PluckerChannel c;
    c.id            = id;
    c.name          = name;
    c.homeUrl       = QStringLiteral("https://example.com/") + id;
    c.updateEnabled = enabled;
    c.lastFetched   = lastFetched;
    return c;
}
} // namespace

class TestPluckerBlobBackend : public QObject
{
    Q_OBJECT

private slots:
    void availableCollections_listsChannelsAndBootstrap()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, /*device=*/nullptr,
                                    {}, {});
        const auto cols = backend.availableCollections();
        QCOMPARE(cols.size(), 2);

        QStringList ids;
        for (const auto &c : cols) ids << c.id;
        std::sort(ids.begin(), ids.end());
        QCOMPARE(ids, (QStringList{
            QStringLiteral("plucker:bootstrap"),
            QStringLiteral("plucker:channels")}));
    }

    void loadRecords_channels_emitsOnePerDueChannel()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha")),
            makeChannel(QStringLiteral("b"), QStringLiteral("Bravo"))
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QCOMPARE(records.size(), 2);
        QCOMPARE(fetcher.callCount(), 2);
        for (const auto &r : records) {
            QCOMPARE(r.type, QStringLiteral("plucker-pdb"));
            QVERIFY(r.id.startsWith(QStringLiteral("channel:")));
            QVERIFY(!r.data.isEmpty());
        }
    }

    void loadRecords_channels_skipsDisabled()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha"),
                        /*enabled=*/false),
            makeChannel(QStringLiteral("b"), QStringLiteral("Bravo"))
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QCOMPARE(records.size(), 1);
        QCOMPARE(fetcher.callCount(), 1);
        QCOMPARE(fetcher.lastChannelId(), QStringLiteral("b"));
    }

    void loadRecords_channels_skipsRecentlyFetched()
    {
        const QDateTime recent = QDateTime::currentDateTime().addSecs(-3600);
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha"),
                        /*enabled=*/true, recent)
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QVERIFY(records.isEmpty());
        QCOMPARE(fetcher.callCount(), 0);
    }

    void loadRecords_channels_failedFetchEmitsNothing()
    {
        FakePluckerFetcher fetcher;
        fetcher.setFailNext(true);
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha"))
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QVERIFY(records.isEmpty());
        QCOMPARE(fetcher.callCount(), 1);
    }

    void writeOps_areReadOnly()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, nullptr, {}, {});
        BackendRecord r;
        r.id   = QStringLiteral("x");
        r.type = QStringLiteral("plucker-pdb");
        QVERIFY(backend.createRecord(QStringLiteral("plucker:channels"), r).isEmpty());
        QVERIFY(!backend.updateRecord(r));
        QVERIFY(!backend.deleteRecord(QStringLiteral("x")));
    }

    void loadRecords_unknownCollection_returnsEmpty()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, nullptr, {}, {});
        QVERIFY(backend.loadRecords(QStringLiteral("nope")).isEmpty());
    }
};

QTEST_MAIN(TestPluckerBlobBackend)
#include "tst_pluckerblobbackend.moc"
