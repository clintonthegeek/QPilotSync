#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <iblobbackend.h>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/mockpalmfileinstaller.h"
#include "plugins/install/installactionplugin.h"
#include "runtime/backendpluginmanager.h"
#include "runtime/installsourcecollector.h"
#include "runtime/simpleactioncontext.h"

using namespace Kalburator::Sync;
using namespace WildPalms;
using namespace WildPalms::PalmSync;

namespace {

class StubBlobBackend : public QObject, public IBlobBackend
{
    Q_OBJECT
public:
    QString backendId() const override   { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
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

class StubBackendPlugin : public QObject, public IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    QString pluginId()    const override { return QStringLiteral("stubplucker"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QString description() const override { return {}; }
    QString version()     const override { return QStringLiteral("1.0"); }
    QIcon   icon()        const override { return {}; }
    QStringList claimedDatabases() const override { return {}; }

    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                     PalmDeviceConnection *) override
    {
        auto *b = new StubBlobBackend;
        CollectionInfo boot;
        boot.id   = QStringLiteral("stub:bootstrap");
        boot.name = QStringLiteral("Bootstrap");
        boot.type = QStringLiteral("plucker");
        CollectionInfo channels;
        channels.id   = QStringLiteral("stub:channels");
        channels.name = QStringLiteral("Channels");
        channels.type = QStringLiteral("plucker");
        b->m_cols = {boot, channels};

        BackendRecord syszlib;
        syszlib.id          = QStringLiteral("bootstrap:syszlib");
        syszlib.type        = QStringLiteral("plucker-bootstrap");
        syszlib.displayName = QStringLiteral("SysZLib.prc");
        syszlib.data        = QByteArray("SZLB");
        BackendRecord viewer;
        viewer.id          = QStringLiteral("bootstrap:viewer");
        viewer.type        = QStringLiteral("plucker-bootstrap");
        viewer.displayName = QStringLiteral("viewer_en.prc");
        viewer.data        = QByteArray("VIEW");
        BackendRecord ch;
        ch.id          = QStringLiteral("channel:bbc");
        ch.type        = QStringLiteral("plucker-pdb");
        ch.displayName = QStringLiteral("BBC");
        ch.data        = QByteArray("PDB:BBC");

        b->m_records[boot.id]     = {syszlib, viewer};
        b->m_records[channels.id] = {ch};

        return { b, nullptr };
    }
};

class StubPluginManager : public BackendPluginManager
{
public:
    StubPluginManager() : BackendPluginManager(nullptr, nullptr, nullptr, nullptr) {}
    bool inject(const QString &id, IBackendPlugin *p)
    { return registerInstanceForTest(id, p); }
};

} // namespace

class TestInstallV2E2E : public QObject
{
    Q_OBJECT

private slots:
    void e2e_folderAndPluginBlobs_drainCorrectly()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(folder.path()).filePath(QStringLiteral("user.prc")));

        StubPluginManager mgr;
        mgr.inject(QStringLiteral("stubplucker"), new StubBackendPlugin);

        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);

        InstallSourceCollector collector;
        const auto result = collector.collect(folder.path(), &mgr);

        QCOMPARE(result.files.size(), 4);

        QJsonArray filesArr;
        for (const auto &f : result.files) {
            QJsonObject o;
            o[QStringLiteral("path")]         = f.path;
            o[QStringLiteral("display_name")] = f.displayName;
            filesArr.append(o);
        }
        QJsonObject params;
        params[QStringLiteral("files")] = filesArr;

        InstallActionPlugin action;
        SimpleActionContext ctx;
        QVERIFY(action.execute(&ctx, &conn, params));

        QCOMPARE(inst.installedPaths().size(), 4);

        QStringList names;
        for (const auto &p : inst.installedPaths()) names << QFileInfo(p).fileName();
        QVERIFY(names.contains(QStringLiteral("user.prc")));

        const int bootIdx    = names.indexOf(QStringLiteral("SysZLib.prc"));
        const int viewerIdx  = names.indexOf(QStringLiteral("viewer_en.prc"));
        int channelIdx = names.indexOf(QStringLiteral("BBC.pdb"));
        if (channelIdx < 0) channelIdx = names.indexOf(QStringLiteral("BBC"));
        QVERIFY(bootIdx    >= 0);
        QVERIFY(viewerIdx  >= 0);
        QVERIFY(channelIdx >= 0);
        QVERIFY(bootIdx    < channelIdx);
        QVERIFY(viewerIdx  < channelIdx);

        InstallSourceCollector().moveSucceededToInstalled(
            result, inst.installedPaths());
        QVERIFY(QFile::exists(QDir(folder.path()).filePath(
            QStringLiteral("installed/user.prc"))));
    }
};

QTEST_MAIN(TestInstallV2E2E)
#include "tst_install_v2_e2e.moc"
