#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/codecs/memocodec.h"
#include "plugins/memo/memomarkdown.h"
#include "runtime/backendpluginmanager.h"

#include <QCryptographicHash>

#include "syncengine.h"
#include "blobbaselinestore.h"
#include "mockblobbackend.h"
#include "conflicthandlerregistry.h"
#include "conflictstore.h"
#include "conflictpolicy.h"

namespace {
QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
}

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

/// Phase E.9 end-to-end test: loads the real wildpalms_memo_v2.so via
/// BackendPluginManager, wires the plugin-produced MemoBlobBackend
/// into BlobSyncEngine::twoWayWithBaseline, and verifies fresh sync,
/// modify + resync, delete + resync, and idempotent no-op behaviours.
///
/// The target side uses MockBlobBackend (not LocalBlobBackend)
/// because the engine matches records by literal id-string across
/// both backends; LocalBlobBackend rewrites ids to absolute file
/// paths which breaks matching against PalmBackend's "MemoDB:N"
/// ids. Production wiring uses an IDMappingStore layer for
/// cross-space matching; that's outside the E.9 exit-gate scope.
/// The mock still proves the plugin round-trips real Palm memos
/// into Markdown bytes and back.

class TestMemoV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSyncCreatesLocalFiles();
    void modifyLocalPropagatesToPalm();
    void deletePalmRemovesLocalFile();
    void idempotentNoopSyncChangesNothing();

private:
    void seedPalmMemos(MockPalmDatabaseAccess *dev) const;
    static Kalburator::Sync::CollectionInfo memoCollection();
    QString m_pluginSubdir;
};

void TestMemoV2::initTestCase()
{
    // The real memo plugin installs under
    // ${CMAKE_BINARY_DIR}/lib/wildpalms/plugins/libwildpalms_memo_v2.so.
    // Adding CMAKE_BINARY_DIR/lib to the library path makes
    // KPluginMetaData::findPlugins("wildpalms/plugins") pick it up.
    QCoreApplication::addLibraryPath(
        QStringLiteral(CMAKE_BINARY_DIR "/lib"));
    m_pluginSubdir = QStringLiteral("wildpalms/plugins");
}

void TestMemoV2::seedPalmMemos(MockPalmDatabaseAccess *dev) const
{
    dev->createDatabase(QStringLiteral("MemoDB"));
    const QStringList bodies = {
        QStringLiteral("first memo"),
        QStringLiteral("second memo"),
        QStringLiteral("third memo"),
    };
    const QList<int> cats = {0, 0, 2};
    for (int i = 0; i < bodies.size(); ++i) {
        PalmRecord pr;
        pr.category     = static_cast<std::uint8_t>(cats[i]);
        pr.data         = WildPalms::PalmCodecs::encodeMemo({bodies[i], false});
        pr.lastModified = QDateTime::currentDateTimeUtc();
        dev->createRecord(QStringLiteral("MemoDB"), pr);
    }
}

Kalburator::Sync::CollectionInfo TestMemoV2::memoCollection()
{
    Kalburator::Sync::CollectionInfo info;
    info.id   = QStringLiteral("palm:memo");
    info.name = QStringLiteral("Memos");
    info.type = QStringLiteral("memos");
    return info;
}

// KF6 derives the pluginId from the .so filename (stripping the .so
// suffix but NOT the "lib" prefix on Linux). Our cmake target is
// wildpalms_memo_v2, so the file is "libwildpalms_memo_v2.so" →
// pluginId "libwildpalms_memo_v2". The "Id" field in the manifest
// emits a kf.coreaddons warning and is ignored.
static const QString kMemoPluginId =
    QStringLiteral("libwildpalms_memo_v2");

void TestMemoV2::freshSyncCreatesLocalFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY2(mgr.loadPlugin(kMemoPluginId),
             "failed to load memo plugin via BackendPluginManager");
    auto *plugin = mgr.plugin(kMemoPluginId);
    QVERIFY(plugin != nullptr);

    auto backends = plugin->createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);
    auto *memoBackend = backends.blob;

    Kalburator::Sync::MockBlobBackend mock;
    mock.createCollection(memoCollection());

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;

    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);
    auto result = engine.runBlobTwoWay(
        memoBackend, &mock,
        QStringLiteral("palm:memo"),
        QStringLiteral("e9-fresh"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Three memos should have flowed plugin → mock as Markdown bytes.
    const auto mockRecs = mock.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(mockRecs.size(), 3);

    // Prove the plugin really transcoded Palm memos into Markdown:
    // every mock record should decode as a MarkdownMemo with a body
    // matching one of the seeded memo bodies.
    QStringList expectedBodies = {
        QStringLiteral("first memo"),
        QStringLiteral("second memo"),
        QStringLiteral("third memo"),
    };
    QStringList foundBodies;
    for (const auto &br : mockRecs) {
        const auto m = WildPalms::Memo::decode(QString::fromUtf8(br.data));
        foundBodies << m.content.text.trimmed();
    }
    std::sort(foundBodies.begin(), foundBodies.end());
    std::sort(expectedBodies.begin(), expectedBodies.end());
    QCOMPARE(foundBodies, expectedBodies);

    delete memoBackend;
}

void TestMemoV2::modifyLocalPropagatesToPalm()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kMemoPluginId));
    auto backends = mgr.plugin(kMemoPluginId)
        ->createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);

    Kalburator::Sync::MockBlobBackend mock;
    mock.createCollection(memoCollection());

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    auto r1 = engine.runBlobTwoWay(
        backends.blob, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e9-mod"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    // Rewrite a mock-side record with a modified body (simulating
    // the user editing the .md file on disk).
    const auto mockRecs = mock.loadRecords(QStringLiteral("palm:memo"));
    QVERIFY(!mockRecs.isEmpty());

    Kalburator::Sync::BackendRecord mutated = mockRecs.first();
    auto md = WildPalms::Memo::decode(QString::fromUtf8(mutated.data));
    md.content.text = QStringLiteral("edited on local side");
    mutated.data = WildPalms::Memo::encode(md).toUtf8();
    // MockBlobBackend stores records verbatim; recompute the hash so
    // the engine's 3-way diff actually notices the change.
    mutated.contentHash = sha256Hex(mutated.data);
    // Force a later lastModified so naive time-wins fallbacks also
    // select the mock side if the policy is ever changed. For the
    // 3-way baseline path a hash change is enough.
    mutated.lastModified = QDateTime::currentDateTimeUtc().addSecs(60);
    QVERIFY(mock.updateRecord(mutated));

    auto r2 = engine.runBlobTwoWay(
        backends.blob, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e9-mod"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    bool sawEdit = false;
    for (const auto &pr : dev.readAllRecords(QStringLiteral("MemoDB"))) {
        const auto decoded = WildPalms::PalmCodecs::decodeMemo(pr.data);
        if (decoded && decoded->text.contains(QStringLiteral("edited on local side"))) {
            sawEdit = true;
            break;
        }
    }
    QVERIFY(sawEdit);

    delete backends.blob;
}

void TestMemoV2::deletePalmRemovesLocalFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kMemoPluginId));
    auto backends = mgr.plugin(kMemoPluginId)
        ->createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);

    Kalburator::Sync::MockBlobBackend mock;
    mock.createCollection(memoCollection());

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    auto firstSync = engine.runBlobTwoWay(
        backends.blob, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e9-del"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(firstSync.success, qUtf8Printable(firstSync.errorMessage));

    const auto all = dev.readAllRecords(QStringLiteral("MemoDB"));
    QVERIFY(!all.isEmpty());
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), all.first().recordId));

    QCOMPARE(mock.loadRecords(QStringLiteral("palm:memo")).size(), 3);

    auto r = engine.runBlobTwoWay(
        backends.blob, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e9-del"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(r.targetStats.deleted, 1);
    QCOMPARE(mock.loadRecords(QStringLiteral("palm:memo")).size(), 2);

    delete backends.blob;
}

void TestMemoV2::idempotentNoopSyncChangesNothing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    MockPalmDatabaseAccess dev;
    seedPalmMemos(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kMemoPluginId));
    auto backends = mgr.plugin(kMemoPluginId)
        ->createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);

    Kalburator::Sync::MockBlobBackend mock;
    mock.createCollection(memoCollection());

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    auto r1 = engine.runBlobTwoWay(
        backends.blob, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e9-noop"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    auto r2 = engine.runBlobTwoWay(
        backends.blob, &mock,
        QStringLiteral("palm:memo"), QStringLiteral("e9-noop"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QCOMPARE(r2.sourceStats.created, 0);
    QCOMPARE(r2.sourceStats.updated, 0);
    QCOMPARE(r2.sourceStats.deleted, 0);
    QCOMPARE(r2.targetStats.created, 0);
    QCOMPARE(r2.targetStats.updated, 0);
    QCOMPARE(r2.targetStats.deleted, 0);

    delete backends.blob;
}

QTEST_MAIN(TestMemoV2)
#include "tst_memo_v2.moc"
