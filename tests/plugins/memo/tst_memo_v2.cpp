// M5c Task 3: post-V2 / post-runSyncFuture rewrite of tst_memo_v2.
//
// The original V1 test used the deleted F1-facade SyncEngine::runBlobTwoWay
// against backends produced by IBackendPlugin::createBackends. Both APIs
// are gone:
//   - IBackendPlugin (V1) → migrated to IBackendPluginV2 in M4.
//   - SyncEngine::runBlobTwoWay → deleted in F1; replaced by
//     SyncEngine::runSyncFuture(profile, mappings, override).
//
// This rewrite drives the same end-to-end behavior (Palm memos transcoded
// to Markdown, round-tripping through the sync engine) but plumbs the
// real wildpalms_memo_v2.so through PalmRuntime — the same path the
// production HotSync code uses. PalmRuntime's test seams inject:
//   - a MockPalmDatabaseAccess (seeded with three palm memos)
//   - the V2 plugin's IBlobBackend (palm side, real transcoding)
//   - a MockBlobBackend (pc side, raw byte storage)
//   - a TwoWay SyncMapping
// then runtime.hotSync() executes the full SyncEngine::runSyncFuture path.

// K.8b T6: KPluginFactory / IBackendPluginV2 includes removed.
// The stale wildpalms_memo_v2.so no longer exists (plugins are now STATIC).
// PalmRuntime::registerPalmPlugins() loads the backend in-process;
// setDeviceAccessForTest() calls finishConnect() which wires the backend.
#include <QtTest/QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFuture>

#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/codecs/memocodec.h"
#include "plugins/memo/memomarkdown.h"
#include "runtime/palmdeviceaccess.h"
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"

#include "mockblobbackend.h"
#include "synctypes.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "pluginmanager.h"
#include "stock_plugins.h"
// K.8b T7: BlobBackendAdapter deleted; inject via BlobSyncBackendWrapper.
#include "../../blobsyncbackendwrapper.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

namespace {

constexpr int kSyncTimeoutMs = 5000;
const QString kPalmCollectionId = QStringLiteral("palm:memo");
const QString kPcBackendId      = QStringLiteral("pc");
const QString kPcCollectionId   = QStringLiteral("pc-memo");
const QString kMemoPluginId     = QStringLiteral("memo");  // V2 IBackendPluginV2::pluginId()

void seedPalm(MockPalmDatabaseAccess *dev,
              const QStringList     &bodies,
              const QList<int>      &cats)
{
    dev->createDatabase(QStringLiteral("MemoDB"));
    for (int i = 0; i < bodies.size(); ++i) {
        PalmRecord pr;
        pr.category     = static_cast<std::uint8_t>(cats.value(i, 0));
        pr.data         = WildPalms::PalmCodecs::encodeMemo({bodies[i], false});
        pr.lastModified = QDateTime::currentDateTimeUtc();
        dev->createRecord(QStringLiteral("MemoDB"), pr);
    }
}

Kalburator::Sync::SyncMapping makeTwoWayMapping()
{
    Kalburator::Sync::SyncMapping m;
    m.id              = QStringLiteral("memo-twoway");
    m.sourceBackend   = kMemoPluginId;
    m.sourceCalendar  = kPalmCollectionId;
    m.targetBackend   = kPcBackendId;
    m.targetCalendar  = kPcCollectionId;
    m.mode            = Kalburator::Sync::SyncMode::TwoWay;
    m.enabled         = true;
    return m;
}

} // namespace

class TestMemoV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSync_palmMemos_arriveAsMarkdownOnPC();
    void idempotent_secondSync_isNoop();
};

void TestMemoV2::initTestCase()
{
    // K.8b T6: library path for stale .so files removed (plugins are now STATIC).
    // Stock plugins seeded so dispatchSync finds the memo/blob domain definitions.
    Kalburator::PluginManager pm;
    Kalburator::registerStockPlugins(pm);
}

void TestMemoV2::freshSync_palmMemos_arriveAsMarkdownOnPC()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    // K.8b T6: PalmRuntime::registerPalmPlugins() loads the memo backend
    // in-process at construction time; setDeviceAccessForTest() calls
    // finishConnect() which wires it.  No KPluginFactory loading needed.

    // 1. PalmRuntime + Palm-side mock device with three seeded memos.
    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    auto palmDb = std::make_unique<MockPalmDatabaseAccess>();
    auto *palmDbRaw = palmDb.get();
    const QStringList seededBodies = {
        QStringLiteral("first memo"),
        QStringLiteral("second memo"),
        QStringLiteral("third memo"),
    };
    seedPalm(palmDbRaw, seededBodies, {0, 0, 2});

    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(palmDb));
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    // 2. PC-side: a MockBlobBackend for raw blob storage.
    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcBlobRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Memos");
    pcInfo.type = QStringLiteral("memos");
    pcBlob->createCollection(pcInfo);
    runtime.registerBackendInstanceForTest(kPcBackendId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(pcBlob), kPcBackendId));

    // 4. Two-way mapping memo → pc, then HotSync.
    runtime.setMappingsForTest({makeTwoWayMapping()});

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());
    const auto run = future.resultAt(0);
    QVERIFY2(run.success, qUtf8Printable(run.errorMessage));

    // 5. PC mock should now hold three records, each decoding to one of
    //    the seeded memo bodies — proving the plugin's MemoBlobBackend
    //    transcoded Palm memos into Markdown blobs end-to-end.
    const auto pcRecs = pcBlobRaw->loadRecords(kPcCollectionId);
    QCOMPARE(pcRecs.size(), 3);

    QStringList foundBodies;
    for (const auto &br : pcRecs) {
        const auto m = WildPalms::Memo::decode(QString::fromUtf8(br.data));
        foundBodies << m.content.text.trimmed();
    }
    std::sort(foundBodies.begin(), foundBodies.end());
    QStringList expected = seededBodies;
    std::sort(expected.begin(), expected.end());
    QCOMPARE(foundBodies, expected);
}

void TestMemoV2::idempotent_secondSync_isNoop()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    // K.8b T6: plugin loaded in-process; no KPluginFactory needed.
    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    auto palmDb = std::make_unique<MockPalmDatabaseAccess>();
    seedPalm(palmDb.get(),
             {QStringLiteral("alpha"), QStringLiteral("beta")},
             {0, 0});
    runtime.setDeviceAccessForTest(
        std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(std::move(palmDb)));

    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Memos");
    pcInfo.type = QStringLiteral("memos");
    pcBlob->createCollection(pcInfo);
    runtime.registerBackendInstanceForTest(kPcBackendId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(pcBlob), kPcBackendId));

    runtime.setMappingsForTest({makeTwoWayMapping()});

    // First sync: PC gets 2 markdown blobs.
    auto f1 = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(f1.isFinished(), kSyncTimeoutMs);
    QVERIFY2(f1.resultAt(0).success, qUtf8Printable(f1.resultAt(0).errorMessage));
    QCOMPARE(pcRaw->loadRecords(kPcCollectionId).size(), 2);

    // Snapshot record contents before the second sync.
    const auto before = pcRaw->loadRecords(kPcCollectionId);
    QStringList beforeBodies;
    for (const auto &r : before) beforeBodies << QString::fromUtf8(r.data);
    std::sort(beforeBodies.begin(), beforeBodies.end());

    // Second sync: should be a no-op — same record count + content.
    auto f2 = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(f2.isFinished(), kSyncTimeoutMs);
    QVERIFY2(f2.resultAt(0).success, qUtf8Printable(f2.resultAt(0).errorMessage));

    const auto after = pcRaw->loadRecords(kPcCollectionId);
    QCOMPARE(after.size(), 2);
    QStringList afterBodies;
    for (const auto &r : after) afterBodies << QString::fromUtf8(r.data);
    std::sort(afterBodies.begin(), afterBodies.end());
    QCOMPARE(afterBodies, beforeBodies);
}

QTEST_MAIN(TestMemoV2)
#include "tst_memo_v2.moc"
