#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include <blobsyncengine.h>
#include <mockblobbackend.h>

#include "palm/sync/mockpalmdatabaseaccess.h"
#include "pluckerbackendplugin.h"
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
    Result fetch(const PluckerChannel &channel, int = 0) override
    {
        Result r;
        r.success = true;
        r.docFile = pluckerSanitizeDocFile(channel.name);
        r.pdbBytes = QStringLiteral("PDB:%1").arg(channel.id).toUtf8();
        return r;
    }
};

PluckerChannel makeChannel(const QString &id, const QString &name,
                            const QDateTime &lastFetched = {})
{
    PluckerChannel c;
    c.id            = id;
    c.name          = name;
    c.homeUrl       = QStringLiteral("https://example.com/") + id;
    c.updateEnabled = true;
    c.lastFetched   = lastFetched;
    return c;
}
} // namespace

class TestPluckerV2E2E : public QObject
{
    Q_OBJECT

private slots:
    void mirror_dueChannelProducesRecord_nonDueDoesNot()
    {
        FakePluckerFetcher fetcher;
        WildPalms::PalmSync::MockPalmDatabaseAccess device;

        const QDateTime fresh = QDateTime::currentDateTime().addSecs(-3600);
        PluckerBlobBackend src({
            makeChannel(QStringLiteral("due"),    QStringLiteral("Due Channel")),
            makeChannel(QStringLiteral("recent"), QStringLiteral("Recent Channel"), fresh)
        }, &fetcher, &device, QByteArray("SZLB"), QByteArray("VIEW"));

        MockBlobBackend dst;
        CollectionInfo c;
        c.id   = QStringLiteral("plucker:channels");
        c.name = QStringLiteral("Plucker Channels");
        c.type = QStringLiteral("plucker");
        dst.createCollection(c);

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("plucker:channels"));

        const auto records = dst.loadRecords(QStringLiteral("plucker:channels"));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].id, QStringLiteral("channel:due"));
    }

    void mirror_bootstrapEmittedOnFirstRun_absentOnSecond()
    {
        FakePluckerFetcher fetcher;
        WildPalms::PalmSync::MockPalmDatabaseAccess device;
        PluckerBlobBackend src({}, &fetcher, &device,
                                QByteArray("SZLB"), QByteArray("VIEW"));

        MockBlobBackend dst;
        CollectionInfo c;
        c.id   = QStringLiteral("plucker:bootstrap");
        c.name = QStringLiteral("Plucker Bootstrap");
        c.type = QStringLiteral("plucker");
        dst.createCollection(c);

        BlobSyncEngine engine;

        // First run — device lacks Plucker DB.
        engine.mirror(&src, &dst, QStringLiteral("plucker:bootstrap"));
        const auto first = dst.loadRecords(QStringLiteral("plucker:bootstrap"));
        QCOMPARE(first.size(), 2);

        // Simulate Install action having run.
        device.createDatabase(QStringLiteral("Plucker"));

        // Second run — bootstrap collection becomes empty; mirror deletes
        // the now-absent records from dst.
        engine.mirror(&src, &dst, QStringLiteral("plucker:bootstrap"));
        const auto second = dst.loadRecords(QStringLiteral("plucker:bootstrap"));
        QCOMPARE(second.size(), 0);
    }

    void plugin_settingsRoundTripsLastFetched()
    {
        QJsonObject settings;
        QJsonArray channels;
        QJsonObject ch;
        ch[QStringLiteral("id")]            = QStringLiteral("c1");
        ch[QStringLiteral("name")]          = QStringLiteral("Alpha");
        ch[QStringLiteral("home_url")]      = QStringLiteral("https://example.com/");
        ch[QStringLiteral("last_fetched")]  = QStringLiteral("2026-04-26T10:30:00");
        channels.append(ch);
        settings[QStringLiteral("channels")] = channels;

        PluckerBackendPlugin plugin;
        plugin.loadSettings(settings);

        const QJsonObject out = plugin.saveSettings();
        const QJsonArray outChannels = out[QStringLiteral("channels")].toArray();
        QCOMPARE(outChannels.size(), 1);
        QCOMPARE(outChannels[0].toObject()[QStringLiteral("last_fetched")].toString(),
                  QStringLiteral("2026-04-26T10:30:00"));
    }
};

QTEST_MAIN(TestPluckerV2E2E)
#include "tst_plucker_v2_e2e.moc"
