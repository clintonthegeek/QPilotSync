// M5c Task 5: post-V2 / post-runSyncFuture rewrite of tst_todo_v2.
//
// Same migration as tst_memo_v2 / tst_contacts_v2 (M5c Tasks 3, 4):
// V1 IBackendPlugin + runBlobTwoWay → V2 plugin loaded through
// PalmRuntime test seams driving SyncEngine::runSyncFuture via
// runtime.hotSync().
//
// Reduced to one fresh-sync scenario verifying the real
// wildpalms_todos_v2.so transcodes a seeded Palm todo into iCalendar
// VTODO bytes on the PC mock. Multi-slot routing, completion-conflict
// merge, and cross-slot moves remain pinned by the todos plugin's own
// unit tests (tst_todoblobbackend, tst_todoconflicthandler) and
// libkalburator's contract suite.

#include <QtTest/QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFuture>
#include <cstring>

// K.8b T6: KPluginFactory / IBackendPluginV2 includes removed.
// TodoBackendPlugin is now STATIC; PalmRuntime loads it in-process.
#include <pi-appinfo.h>

#include "palm/codecs/todocodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "plugins/todos/todoblobbackend.h"
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

using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::TodoPlugin::TodoBlobBackend;

namespace {

constexpr int kSyncTimeoutMs = 5000;
const QString kPcBackendId    = QStringLiteral("pc");
const QString kPcCollectionId = QStringLiteral("pc-todos-unfiled");
const QString kTodoPluginId   = QStringLiteral("todo");

QByteArray buildAppInfo()
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Business"),
                             QStringLiteral("Errands") };
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray utf = names[i].toUtf8().left(15);
        std::memcpy(info.name[i], utf.constData(), utf.size());
        info.name[i][utf.size()] = '\0';
        info.ID[i] = static_cast<unsigned char>(i);
    }
    info.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_CategoryAppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

PalmRecord makeTodoRecord(int slot,
                          const QString &description,
                          int priority = 1)
{
    Todo t;
    t.description  = description;
    t.priority     = priority;
    t.isComplete   = false;
    PalmRecord pr;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.data         = encodeTodo(t);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

Kalburator::Sync::SyncMapping makeUnfiledMapping()
{
    Kalburator::Sync::SyncMapping m;
    m.id              = QStringLiteral("todo-unfiled-twoway");
    m.sourceBackend   = kTodoPluginId;
    m.sourceCalendar  = TodoBlobBackend::collectionIdForSlot(0);
    m.targetBackend   = kPcBackendId;
    m.targetCalendar  = kPcCollectionId;
    m.mode            = Kalburator::Sync::SyncMode::TwoWay;
    m.enabled         = true;
    return m;
}

} // namespace

class TestTodoV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSync_palmTodo_arrivesAsVtodoOnPC();
};

void TestTodoV2::initTestCase()
{
    qApp->setApplicationName(QStringLiteral("tst_todo_v2"));
    // K.8b T6: library path for stale .so removed. Stock plugins seeded so
    // dispatchSync finds the todo/blob domain definitions.
    Kalburator::PluginManager pm;
    Kalburator::registerStockPlugins(pm);
}

void TestTodoV2::freshSync_palmTodo_arrivesAsVtodoOnPC()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    // K.8b T6: TodoBackendPlugin is STATIC; PalmRuntime loads it in-process.
    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    auto palmDb = std::make_unique<MockPalmDatabaseAccess>();
    palmDb->createDatabase(QStringLiteral("ToDoDB"));
    palmDb->setAppBlock(QStringLiteral("ToDoDB"), buildAppInfo());
    palmDb->createRecord(QStringLiteral("ToDoDB"),
                         makeTodoRecord(0,
                                         QStringLiteral("Buy groceries"),
                                         /*priority=*/2));

    runtime.setDeviceAccessForTest(
        std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(std::move(palmDb)));

    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Todos (Unfiled)");
    pcInfo.type = QStringLiteral("calendar");
    pcBlob->createCollection(pcInfo);
    runtime.registerBackendInstanceForTest(kPcBackendId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(pcBlob), kPcBackendId));

    runtime.setMappingsForTest({makeUnfiledMapping()});

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const auto run = future.resultAt(0);
    QVERIFY2(run.success, qUtf8Printable(run.errorMessage));

    const auto pcRecs = pcRaw->loadRecords(kPcCollectionId);
    QCOMPARE(pcRecs.size(), 1);

    const QByteArray ics = pcRecs.first().data;
    QVERIFY2(ics.contains(QByteArrayLiteral("BEGIN:VCALENDAR")),
             qUtf8Printable(QStringLiteral("Expected iCalendar payload, got: %1")
                                .arg(QString::fromUtf8(ics.left(120)))));
    QVERIFY(ics.contains(QByteArrayLiteral("VTODO")));
    QVERIFY(ics.contains(QByteArrayLiteral("Buy groceries")));
}

QTEST_MAIN(TestTodoV2)
#include "tst_todo_v2.moc"
