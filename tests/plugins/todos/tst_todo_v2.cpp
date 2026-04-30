#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>

#include <cstring>

#include <pi-appinfo.h>

#include "core/ibackendplugin.h"
#include "palm/codecs/todocodec.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"
#include "plugins/todos/todoblobbackend.h"
#include "plugins/todos/todoicstranscoder.h"
#include "runtime/backendpluginmanager.h"

#include "syncengine.h"
#include "blobbaselinestore.h"
#include "mockblobbackend.h"
#include "conflicthandlerregistry.h"
#include "conflictstore.h"
#include "conflictpolicy.h"

using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::TodoPlugin::TodoBlobBackend;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRecord;

/// Phase E.11 Task 6 end-to-end test.
///
/// Loads the real wildpalms_todos_v2.so via BackendPluginManager,
/// drives BlobSyncEngine::twoWayWithBaseline against a MockBlobBackend
/// target across several ToDo category slots, and proves four
/// scenarios: fresh sync target→Palm, fresh sync Palm→target across
/// slots, completion-asymmetric merge via the registered handler, and
/// a cross-slot move from target.
///
/// MockBlobBackend stands in for LocalBlobBackend (cross id-space
/// mapping deferred to E.15+; the engine matches records by literal
/// id-string across both sides, which the mock tolerates).

namespace {

// Synthesise a ToDoDB AppInfo block naming four slots
// (Unfiled + Personal + Business + Errands). Mirrors
// tst_calendar_v2's buildAppInfo.
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

PalmRecord makeTodoRecord(std::uint32_t recordId,
                          int slot,
                          const QString &description,
                          const QString &note = {},
                          bool complete = false,
                          int priority = 1)
{
    Todo t;
    t.description = description;
    t.note = note;
    t.priority = priority;
    t.isComplete = complete;
    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeTodo(t);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

QByteArray makeTodoIcs(std::uint32_t recordId,
                       int slot,
                       const QString &description,
                       const QString &note = {},
                       bool complete = false,
                       int priority = 1)
{
    return WildPalms::TodoPlugin::encodePalmToIcs(
        makeTodoRecord(recordId, slot, description, note, complete, priority));
}

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

CollectionInfo collectionForSlot(int slot)
{
    CollectionInfo info;
    info.id   = TodoBlobBackend::collectionIdForSlot(slot);
    info.name = QStringLiteral("target-todo-%1").arg(slot);
    info.type = QStringLiteral("calendar");
    return info;
}

BackendRecord makeTargetRecord(const QString &id,
                               const QByteArray &icsBytes)
{
    BackendRecord br;
    br.id           = id;
    br.data         = icsBytes;
    br.type         = QStringLiteral("text/calendar");
    br.lastModified = QDateTime::currentDateTimeUtc();
    br.contentHash  = sha256Hex(icsBytes);
    return br;
}

// KF6 derives the pluginId from the .so filename (stripping the .so
// suffix but NOT the "lib" prefix on Linux). Our cmake target is
// wildpalms_todos_v2 → file libwildpalms_todos_v2.so → pluginId
// "libwildpalms_todos_v2". Same convention as memo (E.9) and
// calendar (E.10).
const QString kTodoPluginId =
    QStringLiteral("libwildpalms_todos_v2");

} // namespace

class TestTodoV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSyncMovesTargetRecordsToPalm();
    void palmRecordsLandAtTargetInCorrectSlots();
    void completionConflictMergesViaOverlay();
    void crossSlotMoveUpdatesPalmCategory();

private:
    QString m_pluginSubdir;
};

void TestTodoV2::initTestCase()
{
    qApp->setApplicationName(QStringLiteral("tst_todo_v2"));
    // The real todos plugin installs under
    // ${CMAKE_BINARY_DIR}/lib/wildpalms/plugins/libwildpalms_todos_v2.so.
    // Adding CMAKE_BINARY_DIR/lib to the library path makes
    // KPluginMetaData::findPlugins("wildpalms/plugins") pick it up.
    QCoreApplication::addLibraryPath(
        QStringLiteral(CMAKE_BINARY_DIR "/lib"));
    m_pluginSubdir = QStringLiteral("wildpalms/plugins");
}

void TestTodoV2::freshSyncMovesTargetRecordsToPalm()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Empty Palm side — only the AppInfo block so populated slots
    // resolve correctly.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("ToDoDB"));
    dev.setAppBlock(QStringLiteral("ToDoDB"), buildAppInfo());
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY2(mgr.loadPlugin(kTodoPluginId),
             "todo plugin failed to load via BackendPluginManager — "
             "check build dir lib path / plugin metadata");
    auto *plugin = mgr.plugin(kTodoPluginId);
    QVERIFY(plugin != nullptr);

    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    // Target side: one MockBlobBackend with a collection per slot we
    // intend to use. Seed two records: one in Unfiled, one in
    // Personal.
    Kalburator::Sync::MockBlobBackend target;
    for (int slot : {0, 1}) {
        target.createCollection(collectionForSlot(slot));
    }

    const QString unfiledCid  = TodoBlobBackend::collectionIdForSlot(0);
    const QString personalCid = TodoBlobBackend::collectionIdForSlot(1);

    target.createRecord(unfiledCid,
        makeTargetRecord(QStringLiteral("target:fresh-1"),
                         makeTodoIcs(0, 0, QStringLiteral("Buy milk"))));
    target.createRecord(personalCid,
        makeTargetRecord(QStringLiteral("target:fresh-2"),
                         makeTodoIcs(0, 1, QStringLiteral("Call mum"))));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    for (int slot : {0, 1}) {
        const QString cid = TodoBlobBackend::collectionIdForSlot(slot);
        const QString mappingId =
            QStringLiteral("e11-fresh-%1").arg(slot);
        auto result = engine.runBlobTwoWay(
            provided.blob, &target, cid, mappingId,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    }

    // Both target records should have landed on the device, each
    // with the matching category.
    const auto palmRecs = dev.readAllRecords(QStringLiteral("ToDoDB"));
    QCOMPARE(palmRecs.size(), 2);

    QSet<int> seenCategories;
    QSet<QString> seenDescriptions;
    for (const auto &pr : palmRecs) {
        seenCategories.insert(static_cast<int>(pr.category));
        auto t = decodeTodo(QByteArrayView(pr.data));
        QVERIFY(t.has_value());
        seenDescriptions.insert(t->description);
    }
    QVERIFY(seenCategories.contains(0));
    QVERIFY(seenCategories.contains(1));
    QVERIFY(seenDescriptions.contains(QStringLiteral("Buy milk")));
    QVERIFY(seenDescriptions.contains(QStringLiteral("Call mum")));

    delete provided.blob;
    // provided.calendar is null for the todo plugin (no typed
    // SyncBackend layer for VTODO); nothing to delete.
}

void TestTodoV2::palmRecordsLandAtTargetInCorrectSlots()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Palm side: 3 records spread across slots 0, 1, 2.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("ToDoDB"));
    dev.setAppBlock(QStringLiteral("ToDoDB"), buildAppInfo());
    dev.createRecord(QStringLiteral("ToDoDB"),
        makeTodoRecord(0, 0, QStringLiteral("File taxes")));
    dev.createRecord(QStringLiteral("ToDoDB"),
        makeTodoRecord(0, 1, QStringLiteral("Read book")));
    dev.createRecord(QStringLiteral("ToDoDB"),
        makeTodoRecord(0, 2, QStringLiteral("Submit invoice")));
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kTodoPluginId));
    auto *plugin = mgr.plugin(kTodoPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    // Target side: empty MockBlobBackend with collections for the
    // three slots we expect to populate.
    Kalburator::Sync::MockBlobBackend target;
    for (int slot : {0, 1, 2}) {
        target.createCollection(collectionForSlot(slot));
    }

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    for (int slot : {0, 1, 2}) {
        const QString cid = TodoBlobBackend::collectionIdForSlot(slot);
        const QString mappingId =
            QStringLiteral("e11-palm2target-%1").arg(slot);
        auto result = engine.runBlobTwoWay(
            provided.blob, &target, cid, mappingId,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    }

    QCOMPARE(target.loadRecords(TodoBlobBackend::collectionIdForSlot(0)).size(), 1);
    QCOMPARE(target.loadRecords(TodoBlobBackend::collectionIdForSlot(1)).size(), 1);
    QCOMPARE(target.loadRecords(TodoBlobBackend::collectionIdForSlot(2)).size(), 1);

    delete provided.blob;
    // provided.calendar is null for the todo plugin (no typed
    // SyncBackend layer for VTODO); nothing to delete.
}

void TestTodoV2::completionConflictMergesViaOverlay()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Both sides start with the same record (slot 1, "Email Bob").
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("ToDoDB"));
    dev.setAppBlock(QStringLiteral("ToDoDB"), buildAppInfo());
    dev.createRecord(QStringLiteral("ToDoDB"),
        makeTodoRecord(0, 1,
                       QStringLiteral("Email Bob"),
                       QStringLiteral("Original note")));
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kTodoPluginId));
    auto *plugin = mgr.plugin(kTodoPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    // Register the plugin's conflict handler under "palm-todo" — the
    // engine looks up by the source backend's id (provided.blob's
    // backendId() is "palm-todo").
    auto *handler = plugin->createConflictHandler();
    QVERIFY2(handler != nullptr,
             "todo plugin must produce a conflict handler");
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    registry.registerHandler(QStringLiteral("palm-todo"), handler);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(1));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    const QString cid       = TodoBlobBackend::collectionIdForSlot(1);
    const QString mappingId = QStringLiteral("e11-conflict");

    // First sync establishes the baseline + propagates Palm→target.
    auto r1 = engine.runBlobTwoWay(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    auto recsAfterFirst = target.loadRecords(cid);
    QCOMPARE(recsAfterFirst.size(), 1);
    BackendRecord targetRec = recsAfterFirst.first();

    // Modify target side: edit the description (note remains
    // "Original note"), keep complete=false. Re-encode via the
    // transcoder so the bytes stay valid VTODO.
    {
        auto decoded = WildPalms::TodoPlugin::decodeIcsToPalm(
            targetRec.data, /*slotHint=*/1);
        QVERIFY(decoded.has_value());
        auto t = decodeTodo(QByteArrayView(decoded->data));
        QVERIFY(t.has_value());
        t->description = QStringLiteral("Email Bob (target-edit)");
        // note unchanged → "Original note"
        // isComplete unchanged → false
        decoded->data = encodeTodo(*t);
        targetRec.data = WildPalms::TodoPlugin::encodePalmToIcs(*decoded);
        targetRec.contentHash  = sha256Hex(targetRec.data);
        targetRec.lastModified = QDateTime::currentDateTimeUtc().addSecs(60);
    }
    QVERIFY(target.updateRecord(targetRec));

    // Modify Palm side: flip isComplete to true on the same record.
    // Description and note remain at the original values.
    {
        auto palmRecs = dev.readAllRecords(QStringLiteral("ToDoDB"));
        QCOMPARE(palmRecs.size(), 1);
        PalmRecord pr = palmRecs.first();
        auto t = decodeTodo(QByteArrayView(pr.data));
        QVERIFY(t.has_value());
        t->isComplete  = true;
        // description / note unchanged.
        pr.data         = encodeTodo(*t);
        pr.lastModified = QDateTime::currentDateTimeUtc().addSecs(120);
        QVERIFY(dev.updateRecord(QStringLiteral("ToDoDB"), pr));
    }

    // Second sync — both sides have local edits since baseline →
    // BothModified conflict → handler fires.
    auto r2 = engine.runBlobTwoWay(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    // The engine routed through the registered TodoConflictHandler
    // (it's the only handler under "palm-todo"). The overlay
    // produces ConflictDecision::Merge with a re-serialised VTODO
    // carrying isComplete=true AND the peer's edited description.
    // The current BlobSyncEngine does not auto-apply Merge — it
    // adds the conflict (with the populated mergedContent) to the
    // store. Verify both: a conflict landed, AND the merged content
    // matches the overlay's contract.
    QCOMPARE(conflicts.count(), 1);
    auto pending = conflicts.allConflicts();
    QCOMPARE(pending.size(), 1);
    const auto &conflict = pending.first();
    QVERIFY2(!conflict.mergedContent.isEmpty(),
             "TodoConflictHandler did not populate mergedContent — "
             "completion-asymmetric overlay was not invoked");

    auto merged = WildPalms::TodoPlugin::decodeIcsToPalm(
        conflict.mergedContent, /*slotHint=*/1);
    QVERIFY(merged.has_value());
    auto mergedTodo = decodeTodo(QByteArrayView(merged->data));
    QVERIFY(mergedTodo.has_value());
    QVERIFY2(mergedTodo->isComplete,
             "merged content should carry isComplete=true (Palm flip)");
    QCOMPARE(mergedTodo->description,
             QStringLiteral("Email Bob (target-edit)"));

    delete handler;
    delete provided.blob;
    // provided.calendar is null for the todo plugin (no typed
    // SyncBackend layer for VTODO); nothing to delete.
}

void TestTodoV2::crossSlotMoveUpdatesPalmCategory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Palm seeded with one record in slot 1 ("Personal").
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("ToDoDB"));
    dev.setAppBlock(QStringLiteral("ToDoDB"), buildAppInfo());
    dev.createRecord(QStringLiteral("ToDoDB"),
        makeTodoRecord(0, 1, QStringLiteral("Plan trip")));
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kTodoPluginId));
    auto *plugin = mgr.plugin(kTodoPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(1));
    target.createCollection(collectionForSlot(2));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::SyncEngine engine(/*registry=*/nullptr, /*host=*/nullptr);

    const QString cid1      = TodoBlobBackend::collectionIdForSlot(1);
    const QString cid2      = TodoBlobBackend::collectionIdForSlot(2);
    const QString mappingId1 = QStringLiteral("e11-move-slot1");
    const QString mappingId2 = QStringLiteral("e11-move-slot2");

    // First sync — slot 1 record propagates Palm→target.
    {
        auto r = engine.runBlobTwoWay(
            provided.blob, &target, cid1, mappingId1,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
        // And run slot-2 sync once to establish baseline (both empty).
        auto r2 = engine.runBlobTwoWay(
            provided.blob, &target, cid2, mappingId2,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    }

    auto inSlot1 = target.loadRecords(cid1);
    QCOMPARE(inSlot1.size(), 1);

    // Target re-categorises: delete the record from slot 1's
    // collection, recreate it in slot 2's collection. This is the
    // MockBlobBackend-side equivalent of moving the .ics file
    // between calendar dirs.
    const QByteArray icsBytes = inSlot1.first().data;
    QVERIFY(target.deleteRecord(inSlot1.first().id));

    // Re-encode for slot 2 so the X-WP-PALM-CATEGORY-SLOT property
    // matches the destination collection (the plugin's
    // createRecord uses the collection-id slot regardless, but
    // round-tripping via the transcoder keeps the body honest).
    auto decoded = WildPalms::TodoPlugin::decodeIcsToPalm(icsBytes, /*slotHint=*/2);
    QVERIFY(decoded.has_value());
    const QByteArray icsForSlot2 =
        WildPalms::TodoPlugin::encodePalmToIcs(*decoded);

    BackendRecord newRec;
    newRec.id           = QStringLiteral("target:moved-1");
    newRec.data         = icsForSlot2;
    newRec.type         = QStringLiteral("text/calendar");
    newRec.lastModified = QDateTime::currentDateTimeUtc().addSecs(60);
    newRec.contentHash  = sha256Hex(newRec.data);
    target.createRecord(cid2, newRec);

    // Re-sync both slots. Slot 1 propagates the deletion to Palm;
    // slot 2 propagates the new record to Palm.
    {
        auto r1 = engine.runBlobTwoWay(
            provided.blob, &target, cid1, mappingId1,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
        auto r2 = engine.runBlobTwoWay(
            provided.blob, &target, cid2, mappingId2,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    }

    // The device should now hold a single live record with
    // category=2; the old slot-1 record is deleted.
    int liveSlot2 = 0;
    int liveSlot1 = 0;
    for (const auto &pr : dev.readAllRecords(QStringLiteral("ToDoDB"))) {
        if (pr.isDeleted()) continue;
        if (pr.category == 1) ++liveSlot1;
        if (pr.category == 2) ++liveSlot2;
    }
    QCOMPARE(liveSlot1, 0);
    QCOMPARE(liveSlot2, 1);

    delete provided.blob;
    // provided.calendar is null for the todo plugin (no typed
    // SyncBackend layer for VTODO); nothing to delete.
}

QTEST_MAIN(TestTodoV2)
#include "tst_todo_v2.moc"
