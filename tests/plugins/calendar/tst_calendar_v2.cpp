#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>

#include <cstring>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <pi-appinfo.h>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/calendar/datebookcodec.h"
#include "plugins/calendar/icstranscoder.h"
#include "plugins/calendar/calendarblobbackend.h"
#include "runtime/backendpluginmanager.h"

#include "blobsyncengine.h"
#include "blobbaselinestore.h"
#include "mockblobbackend.h"
#include "conflicthandlerregistry.h"
#include "conflictstore.h"
#include "conflictpolicy.h"
#include "syncbackend.h"   // complete-type for delete provided.calendar

using Kalburator::Sync::CollectionInfo;
using WildPalms::CalendarPlugin::CalendarBlobBackend;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

/// Phase E.10 Task 7 end-to-end test.
///
/// Loads the real wildpalms_calendar_v2.so via BackendPluginManager,
/// drives BlobSyncEngine::twoWayWithBaseline against a MockBlobBackend
/// target across four DateBk6-style category slots, and proves four
/// scenarios: fresh sync, target-side modify propagation, Palm-side
/// delete propagation, and idempotent noop.
///
/// MockBlobBackend stands in for LocalBlobBackend (cross id-space
/// mapping deferred to E.15+; the engine matches records by literal
/// id-string across both sides, which the mock tolerates).

namespace {

// Synthesise a Datebook AppInfo block naming four slots (Unfiled +
// Work / Personal / Travel). pisock on this system exposes only
// CategoryAppInfo_t / pack_CategoryAppInfo (no AppInfo_t wrapper);
// the field layout is at the top level of the struct (see
// tst_categoryappinforeader.cpp:buildAppInfoBytes).
QByteArray buildAppInfo()
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Work"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Travel") };
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

PalmRecord makeEvent(const QString &uid, int slot, int hourOfDay)
{
    KCalendarCore::Event::Ptr e(new KCalendarCore::Event);
    e->setUid(uid);
    e->setSummary(QStringLiteral("Event ") + uid);
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(hourOfDay, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(hourOfDay + 1, 0)));
    auto pr = DatebookCodec::encode(e, slot);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

CollectionInfo collectionForSlot(int slot)
{
    CollectionInfo info;
    info.id   = CalendarBlobBackend::collectionIdForSlot(slot);
    info.name = QStringLiteral("target-%1").arg(slot);
    info.type = QStringLiteral("calendar");
    return info;
}

// KF6 derives the pluginId from the .so filename (stripping the .so
// suffix but NOT the "lib" prefix on Linux). Our cmake target is
// wildpalms_calendar_v2 → file libwildpalms_calendar_v2.so → pluginId
// "libwildpalms_calendar_v2". Same convention as memo (Phase E.9).
const QString kCalendarPluginId =
    QStringLiteral("libwildpalms_calendar_v2");

} // namespace

class TestCalendarV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSyncCreatesPerCategoryRecords();
    void modifyTargetPropagatesToPalm();
    void deletePalmRemovesTargetRecord();
    void idempotentNoopSyncChangesNothing();

private:
    void seedPalm(MockPalmDatabaseAccess *dev) const;
    QString m_pluginSubdir;
};

void TestCalendarV2::initTestCase()
{
    // The real calendar plugin installs under
    // ${CMAKE_BINARY_DIR}/lib/wildpalms/plugins/libwildpalms_calendar_v2.so.
    // Adding CMAKE_BINARY_DIR/lib to the library path makes
    // KPluginMetaData::findPlugins("wildpalms/plugins") pick it up.
    QCoreApplication::addLibraryPath(
        QStringLiteral(CMAKE_BINARY_DIR "/lib"));
    m_pluginSubdir = QStringLiteral("wildpalms/plugins");
}

void TestCalendarV2::seedPalm(MockPalmDatabaseAccess *dev) const
{
    dev->createDatabase(QStringLiteral("DatebookDB"));
    dev->setAppBlock(QStringLiteral("DatebookDB"), buildAppInfo());

    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e1", 0, 9));   // Unfiled
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e2", 1, 10));  // Work
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e3", 1, 11));  // Work
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e4", 2, 12));  // Personal
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e5", 2, 13));  // Personal
    dev->createRecord(QStringLiteral("DatebookDB"), makeEvent("e6", 3, 14));  // Travel
}

void TestCalendarV2::freshSyncCreatesPerCategoryRecords()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY2(mgr.loadPlugin(kCalendarPluginId),
             "calendar plugin failed to load via BackendPluginManager — "
             "check build dir lib path / plugin metadata");
    auto *plugin = mgr.plugin(kCalendarPluginId);
    QVERIFY(plugin != nullptr);

    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    // Target side: one MockBlobBackend with a collection per slot.
    Kalburator::Sync::MockBlobBackend target;
    for (int slot : {0, 1, 2, 3}) {
        target.createCollection(collectionForSlot(slot));
    }

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    for (int slot : {0, 1, 2, 3}) {
        const QString cid = CalendarBlobBackend::collectionIdForSlot(slot);
        const QString mappingId =
            QStringLiteral("e10-fresh-%1").arg(slot);
        auto result = engine.twoWayWithBaseline(
            provided.blob, &target, cid, mappingId,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    }

    // Per-collection counts on the target: 1 / 2 / 2 / 1.
    QCOMPARE(target.loadRecords(CalendarBlobBackend::collectionIdForSlot(0)).size(), 1);  // e1
    QCOMPARE(target.loadRecords(CalendarBlobBackend::collectionIdForSlot(1)).size(), 2);  // e2,e3
    QCOMPARE(target.loadRecords(CalendarBlobBackend::collectionIdForSlot(2)).size(), 2);  // e4,e5
    QCOMPARE(target.loadRecords(CalendarBlobBackend::collectionIdForSlot(3)).size(), 1);  // e6

    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarV2::modifyTargetPropagatesToPalm()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kCalendarPluginId));
    auto *plugin = mgr.plugin(kCalendarPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(1));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    const QString cid = CalendarBlobBackend::collectionIdForSlot(1);
    const QString mappingId = QStringLiteral("e10-mod");

    // First sync.
    auto r1 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    // Mutate target side: rename e2's summary by patching the iCal text
    // and recomputing the content hash so the engine's 3-way diff
    // actually notices the change.
    auto recs = target.loadRecords(cid);
    QVERIFY(!recs.isEmpty());
    Kalburator::Sync::BackendRecord mutated;
    bool foundE2 = false;
    for (const auto &r : recs) {
        if (r.data.contains("SUMMARY:Event e2")) {
            mutated  = r;
            foundE2  = true;
            break;
        }
    }
    QVERIFY2(foundE2, "e2 not found on target after first sync");
    mutated.data.replace("SUMMARY:Event e2",
                         "SUMMARY:Event e2 (target-edit)");
    mutated.contentHash = sha256Hex(mutated.data);
    mutated.lastModified = QDateTime::currentDateTimeUtc().addSecs(60);
    QVERIFY(target.updateRecord(mutated));

    // Re-sync; expect Palm side to pick up the change.
    auto r2 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    auto palmRecs = dev.readAllRecords(QStringLiteral("DatebookDB"));
    bool found = false;
    for (const auto &pr : palmRecs) {
        if (pr.category != 1) continue;
        auto decoded = DatebookCodec::decode(pr);
        if (!decoded.isValid()) continue;
        if (decoded.event->summary().contains(QStringLiteral("target-edit"))) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "target-side edit did not propagate to Palm");

    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarV2::deletePalmRemovesTargetRecord()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kCalendarPluginId));
    auto *plugin = mgr.plugin(kCalendarPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(2));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    const QString cid = CalendarBlobBackend::collectionIdForSlot(2);
    const QString mappingId = QStringLiteral("e10-del");

    auto r1 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    QCOMPARE(target.loadRecords(cid).size(), 2);

    // Delete one Personal-slot record on Palm side.
    auto palmRecs = dev.readAllRecords(QStringLiteral("DatebookDB"));
    bool deleted = false;
    for (const auto &pr : palmRecs) {
        if (pr.category == 2) {
            QVERIFY(dev.deleteRecord(QStringLiteral("DatebookDB"), pr.recordId));
            deleted = true;
            break;
        }
    }
    QVERIFY(deleted);

    auto r2 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    QCOMPARE(target.loadRecords(cid).size(), 1);

    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarV2::idempotentNoopSyncChangesNothing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    seedPalm(&dev);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kCalendarPluginId));
    auto *plugin = mgr.plugin(kCalendarPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(1));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    const QString cid = CalendarBlobBackend::collectionIdForSlot(1);
    const QString mappingId = QStringLiteral("e10-noop");

    // First sync.
    auto r1 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    auto recsAfterFirst = target.loadRecords(cid);

    // Capture content hashes for the target side after the first sync.
    QStringList hashesAfterFirst;
    for (const auto &r : recsAfterFirst) hashesAfterFirst << sha256Hex(r.data);
    std::sort(hashesAfterFirst.begin(), hashesAfterFirst.end());

    // Second sync should be a noop.
    auto r2 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QCOMPARE(r2.sourceStats.created, 0);
    QCOMPARE(r2.sourceStats.updated, 0);
    QCOMPARE(r2.sourceStats.deleted, 0);
    QCOMPARE(r2.targetStats.created, 0);
    QCOMPARE(r2.targetStats.updated, 0);
    QCOMPARE(r2.targetStats.deleted, 0);

    auto recsAfterSecond = target.loadRecords(cid);
    QStringList hashesAfterSecond;
    for (const auto &r : recsAfterSecond) hashesAfterSecond << sha256Hex(r.data);
    std::sort(hashesAfterSecond.begin(), hashesAfterSecond.end());

    QCOMPARE(recsAfterSecond.size(), recsAfterFirst.size());
    QCOMPARE(hashesAfterSecond, hashesAfterFirst);

    delete provided.blob;
    delete provided.calendar;
}

QTEST_MAIN(TestCalendarV2)
#include "tst_calendar_v2.moc"
