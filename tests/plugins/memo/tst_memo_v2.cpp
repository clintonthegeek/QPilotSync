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

#include <QtTest/QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFuture>

#include <KPluginFactory>
#include <KPluginMetaData>

#include "core/ibackendplugin_v2.h"
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

// Loads wildpalms_memo_v2.so via KPluginFactory and returns a QObject
// pointer downcast to IBackendPluginV2*. The QObject is parented to
// `parent` so Qt cleanup handles it.
WildPalms::IBackendPluginV2 *loadMemoPluginV2(QObject *parent)
{
    const auto metaDatas = KPluginMetaData::findPlugins(
        QStringLiteral("wildpalms/plugins"),
        [](const KPluginMetaData &md) {
            return md.value(QStringLiteral("X-WildPalms-PluginType"))
                       == QStringLiteral("backend")
                && md.fileName().contains(QStringLiteral("memo"));
        });
    if (metaDatas.isEmpty()) return nullptr;

    auto factoryResult = KPluginFactory::loadFactory(metaDatas.first());
    if (!factoryResult) return nullptr;

    QObject *obj = factoryResult.plugin->create<QObject>(parent);
    return qobject_cast<WildPalms::IBackendPluginV2 *>(obj);
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
    // wildpalms_memo_v2.so installs at build/lib/wildpalms/plugins/.
    QCoreApplication::addLibraryPath(QStringLiteral(CMAKE_BINARY_DIR "/lib"));
}

void TestMemoV2::freshSync_palmMemos_arriveAsMarkdownOnPC()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    // 1. PalmRuntime + Palm-side mock device with three seeded memos.
    auto *plugin = loadMemoPluginV2(this);
    QVERIFY2(plugin != nullptr, "wildpalms_memo_v2.so failed to load as IBackendPluginV2");

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

    // 2. Wire the real V2 plugin into PalmRuntime — palm-side backend
    //    registered under plugin->pluginId() = "memo".
    runtime.registerPluginForTest(
        std::shared_ptr<WildPalms::IBackendPluginV2>(
            plugin, [](WildPalms::IBackendPluginV2 *) {
                // Qt parent owns the QObject; the shared_ptr is just a
                // handle. No-op deleter avoids double-free.
            }));

    // 3. PC-side: a MockBlobBackend for raw blob storage.
    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcBlobRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Memos");
    pcInfo.type = QStringLiteral("memos");
    pcBlob->createCollection(pcInfo);
    runtime.registerBlobBackendForTest(kPcBackendId, std::move(pcBlob));

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

    auto *plugin = loadMemoPluginV2(this);
    QVERIFY(plugin != nullptr);

    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    auto palmDb = std::make_unique<MockPalmDatabaseAccess>();
    seedPalm(palmDb.get(),
             {QStringLiteral("alpha"), QStringLiteral("beta")},
             {0, 0});
    runtime.setDeviceAccessForTest(
        std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(std::move(palmDb)));

    runtime.registerPluginForTest(
        std::shared_ptr<WildPalms::IBackendPluginV2>(
            plugin, [](WildPalms::IBackendPluginV2 *) {}));

    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Memos");
    pcInfo.type = QStringLiteral("memos");
    pcBlob->createCollection(pcInfo);
    runtime.registerBlobBackendForTest(kPcBackendId, std::move(pcBlob));

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
