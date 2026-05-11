#include <QDir>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <iblobbackend.h>

#include "core/ibackendplugin_v2.h"
#include "runtime/backendpluginmanager.h"

#include <memory>
#include "runtime/installsourcecollector.h"

using WildPalms::InstallSourceCollector;

namespace {

using namespace Kalburator::Sync;

class FakeBlobBackend : public QObject, public IBlobBackend
{
    Q_OBJECT
public:
    QString backendId() const override   { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    bool    isAvailable() const override { return true; }

    QList<CollectionInfo> availableCollections() override { return m_cols; }
    CollectionInfo collectionInfo(const QString &id) override
    {
        for (const auto &c : m_cols) if (c.id == id) return c;
        return {};
    }
    QString createCollection(const CollectionInfo &) override { return {}; }

    QList<BackendRecord> loadRecords(const QString &id) override
    { return m_records.value(id); }
    std::optional<BackendRecord> loadRecord(const QString &) override { return std::nullopt; }
    QString createRecord(const QString &, const BackendRecord &) override { return {}; }
    bool    updateRecord(const BackendRecord &) override { return false; }
    bool    deleteRecord(const QString &)        override { return false; }
    QList<BackendRecord> modifiedSince(const QString &id, const QDateTime &) override
    { return loadRecords(id); }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }

    QList<CollectionInfo>                          m_cols;
    QHash<QString, QList<BackendRecord>>           m_records;

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);
};

class FakeBackendPlugin : public QObject, public WildPalms::IBackendPluginV2
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)
public:
    QString pluginId()    const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QString description() const override { return {}; }
    QString version()     const override { return QStringLiteral("1.0"); }
    QIcon   icon()        const override { return {}; }
    QStringList claimedDatabases() const override { return {}; }

    std::unique_ptr<Kalburator::Sync::IBlobBackend>
    createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    {
        auto *b = new FakeBlobBackend;
        b->m_cols = {m_col};
        b->m_records[m_col.id] = m_records;
        return std::unique_ptr<Kalburator::Sync::IBlobBackend>(b);
    }

    Kalburator::Sync::CollectionInfo                m_col;
    QList<Kalburator::Sync::BackendRecord>          m_records;
};

class FakePluginManager : public WildPalms::BackendPluginManager
{
public:
    FakePluginManager() : WildPalms::BackendPluginManager(nullptr, nullptr, nullptr, nullptr) {}
    bool injectPlugin(const QString &id, WildPalms::IBackendPluginV2 *p)
    { return registerInstanceForTest(id, p); }
};

} // namespace

class TestInstallSourceCollector : public QObject
{
    Q_OBJECT

private slots:
    void collect_emptyFolderAndNullManager_returnsEmpty()
    {
        InstallSourceCollector c;
        const auto r = c.collect(QString(), nullptr);
        QVERIFY(r.files.isEmpty());
        QVERIFY(r.folderSourcedPaths.isEmpty());
    }

    void collect_folder_picksUpPrcAndPdb()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString folder = tmp.path();
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(folder).filePath(QStringLiteral("foo.prc")));
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.pdb"),
                    QDir(folder).filePath(QStringLiteral("bar.pdb")));
        QFile junk(QDir(folder).filePath(QStringLiteral("junk.txt")));
        junk.open(QIODevice::WriteOnly);
        junk.write("xx");
        junk.close();

        InstallSourceCollector c;
        const auto r = c.collect(folder, nullptr);

        QCOMPARE(r.files.size(), 2);
        QCOMPARE(r.folderSourcedPaths.size(), 2);
        QStringList names;
        for (const auto &f : r.files) names << f.displayName;
        std::sort(names.begin(), names.end());
        QCOMPARE(names, (QStringList{QStringLiteral("bar.pdb"),
                                       QStringLiteral("foo.prc")}));
    }

    void collect_folder_caseInsensitive()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(tmp.path()).filePath(QStringLiteral("UPPER.PRC")));

        InstallSourceCollector c;
        const auto r = c.collect(tmp.path(), nullptr);
        QCOMPARE(r.files.size(), 1);
        QCOMPARE(r.files[0].displayName, QStringLiteral("UPPER.PRC"));
    }

    void collect_nonexistentFolder_returnsEmpty()
    {
        InstallSourceCollector c;
        const auto r = c.collect(QStringLiteral("/no/such/dir"), nullptr);
        QVERIFY(r.files.isEmpty());
    }

    void moveSucceededToInstalled_movesOnlyMatchingPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString folder = tmp.path();
        const QString a = QDir(folder).filePath(QStringLiteral("a.prc"));
        const QString b = QDir(folder).filePath(QStringLiteral("b.pdb"));
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"), a);
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.pdb"), b);

        InstallSourceCollector c;
        const auto r = c.collect(folder, nullptr);
        QCOMPARE(r.files.size(), 2);

        c.moveSucceededToInstalled(r, QStringList{a});
        QVERIFY(!QFile::exists(a));
        QVERIFY(QFile::exists(QDir(folder).filePath(
            QStringLiteral("installed/a.prc"))));
        QVERIFY(QFile::exists(b));
    }

    void collect_pluginBlobs_writesRecordsToTempDir()
    {
        auto *plugin = new FakeBackendPlugin;
        plugin->m_col.id   = QStringLiteral("plucker:bootstrap");
        plugin->m_col.name = QStringLiteral("Bootstrap");
        plugin->m_col.type = QStringLiteral("plucker");

        Kalburator::Sync::BackendRecord r;
        r.id          = QStringLiteral("bootstrap:syszlib");
        r.type        = QStringLiteral("plucker-bootstrap");
        r.displayName = QStringLiteral("SysZLib.prc");
        r.data        = QByteArray("SZLB");
        plugin->m_records = {r};

        FakePluginManager mgr;
        mgr.injectPlugin(QStringLiteral("fake"), plugin);

        InstallSourceCollector c;
        const auto result = c.collect(QString(), &mgr);

        QCOMPARE(result.files.size(), 1);
        QCOMPARE(result.files[0].displayName, QStringLiteral("SysZLib.prc"));
        QVERIFY(result.tempDir);
        QVERIFY(QFile::exists(result.files[0].path));
        QFile f(result.files[0].path);
        f.open(QIODevice::ReadOnly);
        QCOMPARE(f.readAll(), QByteArray("SZLB"));
    }

    void collect_pluginBlobs_skipsNonInstallableTypes()
    {
        auto *plugin = new FakeBackendPlugin;
        plugin->m_col.id   = QStringLiteral("memo:notes");
        plugin->m_col.type = QStringLiteral("memo");

        Kalburator::Sync::BackendRecord r;
        r.id   = QStringLiteral("note:1");
        r.type = QStringLiteral("memo-text");
        r.data = QByteArray("# Hello");
        plugin->m_records = {r};

        FakePluginManager mgr;
        mgr.injectPlugin(QStringLiteral("fake"), plugin);

        InstallSourceCollector c;
        const auto result = c.collect(QString(), &mgr);
        QVERIFY(result.files.isEmpty());
    }

    void collect_pluginBlobs_emptyCollectionEmitsNothing()
    {
        auto *plugin = new FakeBackendPlugin;
        plugin->m_col.id   = QStringLiteral("plucker:channels");
        plugin->m_col.type = QStringLiteral("plucker");
        plugin->m_records = {};

        FakePluginManager mgr;
        mgr.injectPlugin(QStringLiteral("fake"), plugin);

        InstallSourceCollector c;
        const auto result = c.collect(QString(), &mgr);
        QVERIFY(result.files.isEmpty());
    }
};

QTEST_MAIN(TestInstallSourceCollector)
#include "tst_installsourcecollector.moc"
