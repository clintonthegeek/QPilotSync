#include <QtTest/QtTest>
#include <QWidget>

#include "plugins/memo/memobackendplugin.h"
#include "plugins/memo/memoblobbackend.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/codecs/memocodec.h"

#include "conflictrecord.h"   // Kalburator::Sync::QSyncCore::RecordSnapshot

using WildPalms::Memo::MemoBackendPlugin;
using WildPalms::Memo::MemoBlobBackend;

class TestMemoBackendPlugin : public QObject {
    Q_OBJECT
private slots:
    void metadataStatics();
    void claimsMemoDB();
    void createBackendsReturnsMemoBlobBackendOverPalmBackend();
    void viewHooksReportMemoSurface();
    void conflictHtmlRendersMemoTitleAndBody();
    void enrichSnapshotDecodesPalmBytesOnSourceSide();
};

void TestMemoBackendPlugin::metadataStatics()
{
    MemoBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("memo"));
    QCOMPARE(p.displayName(), QStringLiteral("Memos"));
    QCOMPARE(p.description(),
             QStringLiteral("Synchronizes Palm MemoDB with Markdown files"));
    QCOMPARE(p.version(), QStringLiteral("2.0"));
}

void TestMemoBackendPlugin::claimsMemoDB()
{
    MemoBackendPlugin p;
    QCOMPARE(p.claimedDatabases(), QStringList{QStringLiteral("MemoDB")});
}

void TestMemoBackendPlugin::createBackendsReturnsMemoBlobBackendOverPalmBackend()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);
    MemoBackendPlugin p;

    auto backends = p.createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);
    QCOMPARE(backends.blob->backendId(), QStringLiteral("palm-memo"));
    QCOMPARE(backends.calendar, static_cast<Kalburator::Sync::SyncBackend *>(nullptr));

    // Clean up: the plugin returns a raw pointer without parenting.
    delete backends.blob;
}

void TestMemoBackendPlugin::viewHooksReportMemoSurface()
{
    MemoBackendPlugin p;
    QCOMPARE(p.hasMainView(), true);
    QCOMPARE(p.mainViewName(), QStringLiteral("Memos"));

    QWidget parent;
    QWidget *view = p.createMainView(&parent);
    QVERIFY(view != nullptr);
    delete view;
}

void TestMemoBackendPlugin::conflictHtmlRendersMemoTitleAndBody()
{
    MemoBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    snap.content = QByteArrayLiteral("Grocery list\n- milk\n- eggs");
    snap.metadata[QStringLiteral("title")] = QStringLiteral("Grocery list");
    snap.contentType = QStringLiteral("text/plain");

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<h3>Grocery list</h3>")));
    QVERIFY(html.contains(QStringLiteral("- milk")));
}

void TestMemoBackendPlugin::enrichSnapshotDecodesPalmBytesOnSourceSide()
{
    MemoBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    const auto palm = WildPalms::PalmCodecs::encodeMemo(
        {QStringLiteral("Shopping list\nfirst line extends"), false});
    snap.content = palm;

    p.enrichConflictSnapshot(snap, /*isSourceSide=*/true);
    QCOMPARE(QString::fromUtf8(snap.content),
             QStringLiteral("Shopping list\nfirst line extends"));
    QCOMPARE(snap.contentType, QStringLiteral("text/plain"));
    QCOMPARE(snap.metadata.value(QStringLiteral("title")).toString(),
             QStringLiteral("Shopping list"));
}

QTEST_MAIN(TestMemoBackendPlugin)
#include "tst_memobackendplugin.moc"
