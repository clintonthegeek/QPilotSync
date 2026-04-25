#include <QtTest/QtTest>

#include "plugins/calendar/calendarblobbackend.h"
#include "plugins/calendar/icstranscoder.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/calendar/datebookcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

#include <KCalendarCore/Event>

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using WildPalms::CalendarPlugin::CalendarBlobBackend;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord eventRecord(const QString &uid, int slot)
{
    KCalendarCore::Event::Ptr e(new KCalendarCore::Event);
    e->setUid(uid);
    e->setSummary(QStringLiteral("Event ") + uid);
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(10, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(11, 0)));
    auto pr = DatebookCodec::encode(e, slot);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace

class TestCalendarBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void backendIdAndDisplayName();
    void availableCollectionsReflectsStore();
    void availableCollectionsAlwaysIncludesUnfiled();
    void loadRecordsFiltersBySlot();
    void loadRecordsReturnsIcsContentType();
    void createRecordRoutesToSlot();
    void updateRecordPreservesSlot();
    void deleteRecordForwards();
    void deletedSinceReturnsAllSlots();
    void slotParsingHelpers();
};

void TestCalendarBlobBackend::backendIdAndDisplayName()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    CalendarBlobBackend backend(&pb, &store);
    QCOMPARE(backend.backendId(), QStringLiteral("calendar"));
    QVERIFY(!backend.displayName().isEmpty());
    QVERIFY(backend.isAvailable());
}

void TestCalendarBlobBackend::availableCollectionsReflectsStore()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Personal"));
    CalendarBlobBackend backend(&pb, &store);

    auto cols = backend.availableCollections();
    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    QCOMPARE(ids.size(), 3);          // 0 + 1 + 3
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/3")));
}

void TestCalendarBlobBackend::availableCollectionsAlwaysIncludesUnfiled()
{
    MockPalmDatabaseAccess dev;
    PalmBackend pb(&dev);
    CategoryMappingStore store;       // empty
    CalendarBlobBackend backend(&pb, &store);

    auto cols = backend.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, QStringLiteral("palm:calendar/0"));
}

void TestCalendarBlobBackend::loadRecordsFiltersBySlot()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("a", 0));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("b", 1));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("c", 1));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("d", 2));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 2, QStringLiteral("Personal"));
    CalendarBlobBackend backend(&pb, &store);

    QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/0")).size(), 1);
    QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/1")).size(), 2);
    QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/2")).size(), 1);
}

void TestCalendarBlobBackend::loadRecordsReturnsIcsContentType()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("a", 0));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    CalendarBlobBackend backend(&pb, &store);

    auto recs = backend.loadRecords(QStringLiteral("palm:calendar/0"));
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().type, QStringLiteral("text/calendar"));
    QVERIFY(!recs.first().data.isEmpty());
    QVERIFY(recs.first().data.contains("BEGIN:VEVENT"));
}

void TestCalendarBlobBackend::createRecordRoutesToSlot()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("Travel"));
    CalendarBlobBackend backend(&pb, &store);

    // Build an ICS payload from a slot-7 event ... but write into
    // collection palm:calendar/5. The collection slot wins.
    auto seedPr = eventRecord("new-uid", 7);
    BackendRecord br;
    br.id   = QString();
    br.data = WildPalms::CalendarPlugin::encodePalmToIcs(seedPr);
    br.type = QStringLiteral("text/calendar");

    QString newId = backend.createRecord(
        QStringLiteral("palm:calendar/5"), br);
    QVERIFY(!newId.isEmpty());

    auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 5);
}

void TestCalendarBlobBackend::updateRecordPreservesSlot()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    auto seedId = dev.createRecord(QStringLiteral("DatebookDB"),
                                   eventRecord("u-1", 4));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 4, QStringLiteral("Volunteering"));
    CalendarBlobBackend backend(&pb, &store);

    auto recs = backend.loadRecords(QStringLiteral("palm:calendar/4"));
    QCOMPARE(recs.size(), 1);
    auto br = recs.first();
    // Mutate summary by string-replacement (cheap, sufficient).
    br.data.replace("SUMMARY:Event u-1", "SUMMARY:Event u-1 (revised)");
    QVERIFY(backend.updateRecord(br));

    auto stored = dev.readRecord(QStringLiteral("DatebookDB"), seedId);
    QVERIFY(stored.has_value());
    QCOMPARE(static_cast<int>(stored->category), 4);
}

void TestCalendarBlobBackend::deleteRecordForwards()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    auto seedId = dev.createRecord(QStringLiteral("DatebookDB"),
                                   eventRecord("d-1", 0));
    PalmBackend pb(&dev);
    CategoryMappingStore store;
    CalendarBlobBackend backend(&pb, &store);

    auto recs = backend.loadRecords(QStringLiteral("palm:calendar/0"));
    QCOMPARE(recs.size(), 1);
    QVERIFY(backend.deleteRecord(recs.first().id));
    QVERIFY(!dev.readRecord(QStringLiteral("DatebookDB"), seedId).has_value());
}

void TestCalendarBlobBackend::deletedSinceReturnsAllSlots()
{
    // Documents the current intentional limitation: deletedSince does
    // not filter by collection slot because the deleted record's
    // category is not retained. BlobSyncEngine tolerates over-broad
    // returns. See KNOWN LIMITATION comment in calendarblobbackend.cpp.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    auto idA = dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("a", 0));
    auto idB = dev.createRecord(QStringLiteral("DatebookDB"), eventRecord("b", 1));

    const QDateTime t0 = QDateTime::currentDateTimeUtc().addSecs(-5);
    QVERIFY(dev.deleteRecord(QStringLiteral("DatebookDB"), idA));
    QVERIFY(dev.deleteRecord(QStringLiteral("DatebookDB"), idB));

    PalmBackend pb(&dev);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    CalendarBlobBackend backend(&pb, &store);

    // Both ids appear regardless of which collection we ask about.
    auto fromUnfiled = backend.deletedSince(QStringLiteral("palm:calendar/0"), t0);
    auto fromWork    = backend.deletedSince(QStringLiteral("palm:calendar/1"), t0);
    QCOMPARE(fromUnfiled.size(), 2);
    QCOMPARE(fromWork.size(),    2);
    QCOMPARE(fromUnfiled, fromWork);
}

void TestCalendarBlobBackend::slotParsingHelpers()
{
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:calendar/0")), 0);
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:calendar/15")), 15);
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:calendar/16")), -1);
    QCOMPARE(CalendarBlobBackend::slotFromCollectionId(
        QStringLiteral("not-a-calendar-id")), -1);
    QCOMPARE(CalendarBlobBackend::collectionIdForSlot(7),
             QStringLiteral("palm:calendar/7"));
}

QTEST_MAIN(TestCalendarBlobBackend)
#include "tst_calendarblobbackend.moc"
