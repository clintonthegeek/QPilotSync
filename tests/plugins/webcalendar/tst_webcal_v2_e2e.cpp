// M5c Task 6: post-runSyncFuture rewrite of tst_webcal_v2_e2e.
//
// The original test used SyncEngine::runBlobMirror — a deleted F1 facade.
// The replacement drives the same one-way feed→target mirror through
// PalmRuntime::copyPalmToPC, which internally calls
// SyncEngine::runSyncFuture(mappingId, ExecutionOverride{Direction::MirrorAToB}).
// "Palm" in the API name is incidental here — the source side is the
// webcal feed, the target is a MockBlobBackend; MirrorAToB == src→target
// regardless of what the sides represent.
//
// Webcal is a read-only feed plugin; this test injects a directly-
// constructed WebcalBlobBackend rather than loading the .so via the
// plugin manager (the plugin's V2 createPalmBackend ignores its
// PalmDeviceAccess and simply returns a WebcalBlobBackend over its
// JSON-loaded feed list, so wiring the feed list into the plugin via
// loadSettings + a .so load is strictly more setup with no extra
// coverage).

#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QUrl>

#include <icsfeedfetcher.h>
#include <mockblobbackend.h>

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"

#include "webcalblobbackend.h"
#include "webcalfeed.h"

#include "synctypes.h"
#include "pluginmanager.h"
#include "stock_plugins.h"

using namespace Kalburator::Sync;
using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

namespace {

constexpr int kSyncTimeoutMs = 5000;
const QString kSrcBackendId    = QStringLiteral("webcal-src");
const QString kPcBackendId     = QStringLiteral("pc");
const QString kSlot5Collection = QStringLiteral("palm:calendar/5");

QUrl fixtureUrl(const QString &name)
{
    return QUrl::fromLocalFile(
        QDir(QStringLiteral(WEBCAL_FIXTURE_DIR)).absoluteFilePath(name));
}

WebcalFeed makeFeed(int slot, const QString &fixtureName)
{
    WebcalFeed f;
    f.name     = QStringLiteral("Feed");
    f.url      = fixtureUrl(fixtureName);
    f.palmSlot = slot;
    f.enabled  = true;
    return f;
}

SyncMapping makeMirrorMapping()
{
    SyncMapping m;
    m.id              = QStringLiteral("webcal-mirror");
    m.sourceBackend   = kSrcBackendId;
    m.sourceCalendar  = kSlot5Collection;
    m.targetBackend   = kPcBackendId;
    m.targetCalendar  = kSlot5Collection;
    // copyPalmToPC overrides direction to MirrorAToB; mode here is just
    // a placeholder.
    m.mode            = SyncMode::TwoWay;
    m.enabled         = true;
    return m;
}

} // namespace

class TestWebcalV2E2E : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void mirror_emptyTargetGainsAllSourceRecords();
    void mirror_staleTargetLosesAndGainsCorrectly();

private:
    QNetworkAccessManager *m_network = nullptr;
    IcsFeedFetcher       *m_fetcher = nullptr;
};

void TestWebcalV2E2E::initTestCase()
{
    m_network = new QNetworkAccessManager(this);
    m_fetcher = new IcsFeedFetcher(m_network, this);
    // K.7: seed DomainRegistry with stock plugins so dispatchSync finds
    // the blob domain definition (BlobPlugin) used by BlobBackendAdapter.
    Kalburator::PluginManager pm;
    Kalburator::registerStockPlugins(pm);
}

void TestWebcalV2E2E::mirror_emptyTargetGainsAllSourceRecords()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    // Source: webcal feed (3 events fixture).
    auto src = std::make_unique<WebcalBlobBackend>(
        QList<WebcalFeed>{makeFeed(5, QStringLiteral("three_events.ics"))},
        m_fetcher);
    // Drive a fetch first to populate cache + lastFetchSucceeded gate.
    src->loadRecords(kSlot5Collection);
    QVERIFY(src->lastFetchSucceeded(5));
    runtime.registerBlobBackendForTest(kSrcBackendId, std::move(src));

    // Target: empty MockBlobBackend.
    auto pc = std::make_unique<MockBlobBackend>();
    auto *pcRaw = pc.get();
    CollectionInfo c;
    c.id   = kSlot5Collection;
    c.name = QStringLiteral("Slot 5");
    c.type = QStringLiteral("calendar");
    pc->createCollection(c);
    runtime.registerBlobBackendForTest(kPcBackendId, std::move(pc));

    runtime.setMappingsForTest({makeMirrorMapping()});

    auto future = runtime.copyPalmToPC();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.resultAt(0).success);

    QCOMPARE(pcRaw->loadRecords(kSlot5Collection).size(), 3);
}

void TestWebcalV2E2E::mirror_staleTargetLosesAndGainsCorrectly()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    auto src = std::make_unique<WebcalBlobBackend>(
        QList<WebcalFeed>{makeFeed(5, QStringLiteral("three_events.ics"))},
        m_fetcher);
    src->loadRecords(kSlot5Collection);
    QVERIFY(src->lastFetchSucceeded(5));
    runtime.registerBlobBackendForTest(kSrcBackendId, std::move(src));

    auto pc = std::make_unique<MockBlobBackend>();
    auto *pcRaw = pc.get();
    CollectionInfo c;
    c.id   = kSlot5Collection;
    c.name = QStringLiteral("Slot 5");
    c.type = QStringLiteral("calendar");
    pc->createCollection(c);

    // Pre-populate target with 2 stale records that are NOT in source.
    BackendRecord s1; s1.id = QStringLiteral("stale-001@example.com");
    s1.type = QStringLiteral("event");
    s1.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
    s1.contentHash = QStringLiteral("aa");
    BackendRecord s2; s2.id = QStringLiteral("stale-002@example.com");
    s2.type = QStringLiteral("event");
    s2.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
    s2.contentHash = QStringLiteral("bb");
    pc->createRecord(kSlot5Collection, s1);
    pc->createRecord(kSlot5Collection, s2);
    QCOMPARE(pcRaw->loadRecords(kSlot5Collection).size(), 2);

    runtime.registerBlobBackendForTest(kPcBackendId, std::move(pc));
    runtime.setMappingsForTest({makeMirrorMapping()});

    auto future = runtime.copyPalmToPC();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.resultAt(0).success);

    const auto records = pcRaw->loadRecords(kSlot5Collection);
    QCOMPARE(records.size(), 3);
    for (const auto &r : records) {
        QVERIFY(r.id != QStringLiteral("stale-001@example.com"));
        QVERIFY(r.id != QStringLiteral("stale-002@example.com"));
    }
}

QTEST_MAIN(TestWebcalV2E2E)
#include "tst_webcal_v2_e2e.moc"
