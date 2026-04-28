#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/ibackendplugin.h"
#include "core/synctypes.h"
#include "runtime/backendpluginmanager.h"
#include "runtime/syncrunner_wp.h"

#include "iblobbackend.h"
#include "mockblobbackend.h"

using WildPalms::BackendPluginManager;
using WildPalms::IBackendPlugin;
using WildPalms::Runtime::SyncRunner;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::MockBlobBackend;

namespace {

QString sha256Hex(const QByteArray &b)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

CollectionInfo makeCollection(const QString &id)
{
    CollectionInfo c;
    c.id   = id;
    c.name = id;
    c.type = QStringLiteral("memos");
    return c;
}

BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id           = id;
    r.data         = data;
    r.contentHash  = sha256Hex(data);
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

/// IBlobBackend wrapper that forwards every call to a foreign instance
/// owned outside the wrapper. Lets the harness expose persistent
/// MockBlobBackend stores across multiple SyncRunner.run() invocations
/// (each call asks the plugin / factory for a fresh blob pointer that
/// SyncRunner takes ownership of).
class ForwardingBlob : public IBlobBackend
{
public:
    explicit ForwardingBlob(MockBlobBackend *target) : m_t(target) {}

    QString backendId() const override   { return m_t->backendId(); }
    QString displayName() const override { return m_t->displayName(); }
    bool    isAvailable() const override { return m_t->isAvailable(); }
    QList<CollectionInfo> availableCollections() override
    { return m_t->availableCollections(); }
    CollectionInfo collectionInfo(const QString &c) override { return m_t->collectionInfo(c); }
    QString createCollection(const CollectionInfo &i) override { return m_t->createCollection(i); }
    QList<BackendRecord> loadRecords(const QString &c) override { return m_t->loadRecords(c); }
    std::optional<BackendRecord> loadRecord(const QString &r) override { return m_t->loadRecord(r); }
    QString createRecord(const QString &c, const BackendRecord &r) override
    { return m_t->createRecord(c, r); }
    bool updateRecord(const BackendRecord &r) override { return m_t->updateRecord(r); }
    bool deleteRecord(const QString &r) override { return m_t->deleteRecord(r); }
    QList<BackendRecord> modifiedSince(const QString &c, const QDateTime &t) override
    { return m_t->modifiedSince(c, t); }
    QStringList deletedSince(const QString &c, const QDateTime &t) override
    { return m_t->deletedSince(c, t); }
    bool supportsDeleteTracking() const override { return m_t->supportsDeleteTracking(); }
private:
    MockBlobBackend *m_t;
};

class StubPlugin : public QObject, public IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    StubPlugin(QString id, MockBlobBackend *blob)
        : m_id(std::move(id)), m_blob(blob) {}

    QString pluginId() const override    { return m_id; }
    QString displayName() const override { return m_id; }
    QString description() const override { return {}; }
    QString version() const override     { return QStringLiteral("1.0"); }
    QIcon   icon() const override        { return {}; }
    QStringList claimedDatabases() const override
    { return {QStringLiteral("FakeDB-") + m_id}; }

    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                     PalmDeviceConnection *) override
    {
        ProvidedBackends out;
        out.blob = new ForwardingBlob(m_blob);
        return out;
    }

private:
    QString          m_id;
    MockBlobBackend *m_blob;
};

class TestableBackendPluginManager : public BackendPluginManager
{
public:
    using BackendPluginManager::BackendPluginManager;
    using BackendPluginManager::registerInstanceForTest;
};

struct Harness {
    QTemporaryDir tmpSync;
    QTemporaryDir tmpState;
    MockBlobBackend palm;
    MockBlobBackend pcStore;
    StubPlugin   *plugin = nullptr; // owned by manager
    TestableBackendPluginManager *mgr = nullptr;
    std::unique_ptr<SyncRunner> runner;

    explicit Harness(const QString &pluginId = QStringLiteral("stub"))
    {
        Q_ASSERT(tmpSync.isValid());
        Q_ASSERT(tmpState.isValid());

        plugin = new StubPlugin(pluginId, &palm);
        mgr    = new TestableBackendPluginManager(nullptr, nullptr, nullptr);
        mgr->registerInstanceForTest(pluginId, plugin);

        runner = std::make_unique<SyncRunner>(
            mgr, /*device*/nullptr, /*host*/nullptr,
            tmpSync.path(), tmpState.path());

        MockBlobBackend *captured = &pcStore;
        runner->setLocalBackendFactory(
            [captured](const QString &, const QString &) {
                return std::unique_ptr<IBlobBackend>(new ForwardingBlob(captured));
            });
    }

    ~Harness() { runner.reset(); delete mgr; }

    void seedPalmCollection(const QString &cid, int count)
    {
        palm.createCollection(makeCollection(cid));
        pcStore.createCollection(makeCollection(cid));
        for (int i = 0; i < count; ++i) {
            const QString id   = QStringLiteral("palm:%1").arg(i);
            const QByteArray d = QStringLiteral("body-%1").arg(i).toUtf8();
            palm.createRecord(cid, makeRecord(id, d));
        }
    }
};

} // namespace

class TestSyncRunner : public QObject
{
    Q_OBJECT
private slots:
    void hotSyncFreshPropagatesPalmRecordsToLocal();
    void hotSyncSecondRunWithoutChangesIsNoop();
    void fullSyncClearsBaselineAndResyncs();
    void copyPalmToPCWithExistingLocalReplacesLocal();
    void copyPCToPalmWithExistingPalmReplacesPalm();
    void backupCopiesPalmToLocalAdditively();
    void restorePushesLocalRecordsToPalm();
    void cancelStopsLoopBetweenPlugins();
    void emptyEnabledListMeansAllPlugins();
    void unknownPluginIdsAreSkipped();
};

void TestSyncRunner::hotSyncFreshPropagatesPalmRecordsToLocal()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.seedPalmCollection(cid, 3);

    auto result = h.runner->run(Sync::SyncMode::HotSync);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    QCOMPARE(h.pcStore.loadRecords(cid).size(), 3);
}

void TestSyncRunner::hotSyncSecondRunWithoutChangesIsNoop()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.seedPalmCollection(cid, 2);

    QVERIFY(h.runner->run(Sync::SyncMode::HotSync).success);
    auto second = h.runner->run(Sync::SyncMode::HotSync);
    QVERIFY(second.success);
    QCOMPARE(h.pcStore.loadRecords(cid).size(), 2);
    QCOMPARE(second.palmStats.errors, 0);
    QCOMPARE(second.pcStats.errors,   0);
}

void TestSyncRunner::fullSyncClearsBaselineAndResyncs()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.seedPalmCollection(cid, 1);

    QVERIFY(h.runner->run(Sync::SyncMode::HotSync).success);

    // Update the Palm-side record so its content + hash differ.
    auto recs = h.palm.loadRecords(cid);
    QCOMPARE(recs.size(), 1);
    BackendRecord updated = recs.first();
    updated.data         = QByteArrayLiteral("changed");
    updated.contentHash  = sha256Hex(updated.data);
    updated.lastModified = QDateTime::currentDateTimeUtc().addSecs(60);
    QVERIFY(h.palm.updateRecord(updated));

    auto result = h.runner->run(Sync::SyncMode::FullSync);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    const auto pcRecs = h.pcStore.loadRecords(cid);
    QCOMPARE(pcRecs.size(), 1);
    QCOMPARE(pcRecs.first().data, QByteArrayLiteral("changed"));
}

void TestSyncRunner::copyPalmToPCWithExistingLocalReplacesLocal()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.seedPalmCollection(cid, 2);
    h.pcStore.createRecord(cid, makeRecord(QStringLiteral("orphan"), "stale"));

    auto result = h.runner->run(Sync::SyncMode::CopyPalmToPC);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    const auto pcRecs = h.pcStore.loadRecords(cid);
    QCOMPARE(pcRecs.size(), 2);
    for (const auto &r : pcRecs) QVERIFY(r.id != QStringLiteral("orphan"));
}

void TestSyncRunner::copyPCToPalmWithExistingPalmReplacesPalm()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.palm.createCollection(makeCollection(cid));
    h.pcStore.createCollection(makeCollection(cid));
    h.pcStore.createRecord(cid, makeRecord(QStringLiteral("local:1"), "alpha"));
    h.pcStore.createRecord(cid, makeRecord(QStringLiteral("local:2"), "beta"));
    h.palm.createRecord(cid, makeRecord(QStringLiteral("palm:orphan"), "stale"));

    auto result = h.runner->run(Sync::SyncMode::CopyPCToPalm);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    const auto palmRecs = h.palm.loadRecords(cid);
    QCOMPARE(palmRecs.size(), 2);
    for (const auto &r : palmRecs) QVERIFY(r.id != QStringLiteral("palm:orphan"));
}

void TestSyncRunner::backupCopiesPalmToLocalAdditively()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.seedPalmCollection(cid, 2);
    h.pcStore.createRecord(cid, makeRecord(QStringLiteral("legacy"), "from-old-backup"));

    auto result = h.runner->run(Sync::SyncMode::Backup);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    const auto pcRecs = h.pcStore.loadRecords(cid);
    QCOMPARE(pcRecs.size(), 3);
    bool sawLegacy = false;
    for (const auto &r : pcRecs) if (r.id == QStringLiteral("legacy")) sawLegacy = true;
    QVERIFY(sawLegacy);
}

void TestSyncRunner::restorePushesLocalRecordsToPalm()
{
    Harness h;
    const QString cid = QStringLiteral("palm:memo");
    h.palm.createCollection(makeCollection(cid));
    h.pcStore.createCollection(makeCollection(cid));
    h.pcStore.createRecord(cid, makeRecord(QStringLiteral("from-archive:1"), "a"));
    h.pcStore.createRecord(cid, makeRecord(QStringLiteral("from-archive:2"), "b"));
    h.palm.createRecord(cid, makeRecord(QStringLiteral("palm:stray"), "x"));

    auto result = h.runner->run(Sync::SyncMode::Restore);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    const auto palmRecs = h.palm.loadRecords(cid);
    QCOMPARE(palmRecs.size(), 2);
    for (const auto &r : palmRecs) QVERIFY(r.id != QStringLiteral("palm:stray"));
}

void TestSyncRunner::cancelStopsLoopBetweenPlugins()
{
    Harness h(QStringLiteral("a"));
    h.seedPalmCollection(QStringLiteral("c1"), 1);

    // Cancellation flag is reset at the top of run(), so requesting
    // cancel before run() returns false. Hook the started() signal
    // and request cancel from there — that fires *after* the reset
    // but *before* the per-plugin loop iterates.
    QObject::connect(h.runner.get(), &SyncRunner::started,
                     h.runner.get(), [&h](int) { h.runner->requestCancel(); });
    auto result = h.runner->run(Sync::SyncMode::HotSync);
    QVERIFY(!result.success);
    QCOMPARE(result.errorMessage, QStringLiteral("cancelled"));
}

void TestSyncRunner::emptyEnabledListMeansAllPlugins()
{
    Harness h;
    h.seedPalmCollection(QStringLiteral("palm:memo"), 1);

    auto result = h.runner->run(Sync::SyncMode::HotSync, {});
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    QCOMPARE(h.pcStore.loadRecords(QStringLiteral("palm:memo")).size(), 1);
}

void TestSyncRunner::unknownPluginIdsAreSkipped()
{
    Harness h;
    h.seedPalmCollection(QStringLiteral("palm:memo"), 1);

    auto result = h.runner->run(Sync::SyncMode::HotSync,
                                {QStringLiteral("does-not-exist")});
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    QCOMPARE(h.pcStore.loadRecords(QStringLiteral("palm:memo")).size(), 0);
}

QTEST_MAIN(TestSyncRunner)
#include "tst_syncrunner.moc"
