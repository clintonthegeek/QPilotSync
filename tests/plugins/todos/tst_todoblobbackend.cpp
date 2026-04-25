#include <QtTest/QtTest>

#include "plugins/todos/todoblobbackend.h"
#include "plugins/todos/todoicstranscoder.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/todocodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::TodoPlugin::TodoBlobBackend;

namespace {

PalmRecord makeTodo(std::uint32_t recordId,
                    int slot,
                    const QString &description)
{
    Todo t;
    t.description = description;
    t.priority    = 1;
    PalmRecord pr;
    pr.recordId     = recordId;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.data         = encodeTodo(t);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace

class TestTodoBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void backendIdentity();
    void emptyStoreEmitsUnfiledOnly();
    void populatedStoreEmitsNamedSlots();
    void slotZeroNamedDoesNotShadowUnfiled();
    void loadRecordsRoutesByCategory();
    void loadRecordsMixedCategoryDataset();
    void createRecordStampsSlotFromCollectionId();
    void updateRecordPreservesRecordId();
    void deleteRecordForwardsToPalmBackend();
};

void TestTodoBlobBackend::backendIdentity()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    TodoBlobBackend be(&palm, &store);
    QCOMPARE(be.backendId(), QStringLiteral("palm-todo"));
    QVERIFY(!be.displayName().isEmpty());
}

void TestTodoBlobBackend::emptyStoreEmitsUnfiledOnly()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    TodoBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols[0].id, QStringLiteral("palm:todo/0"));
    QCOMPARE(cols[0].name, QStringLiteral("Unfiled"));
}

void TestTodoBlobBackend::populatedStoreEmitsNamedSlots()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("Personal"));
    store.setSlotName(QStringLiteral("ToDoDB"), 2, QStringLiteral("Business"));

    TodoBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();
    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    QCOMPARE(cols.size(), 3);
    QVERIFY(ids.contains(QStringLiteral("palm:todo/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:todo/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:todo/2")));
    // Names: scan rather than rely on order.
    for (const auto &c : cols) {
        if (c.id == QStringLiteral("palm:todo/1"))
            QCOMPARE(c.name, QStringLiteral("Personal"));
        if (c.id == QStringLiteral("palm:todo/2"))
            QCOMPARE(c.name, QStringLiteral("Business"));
    }
}

void TestTodoBlobBackend::slotZeroNamedDoesNotShadowUnfiled()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    // CategoryMappingStore::setSlotName ignores non-"Unfiled" names for slot 0.
    store.setSlotName(QStringLiteral("ToDoDB"), 0, QStringLiteral("Renamed"));

    TodoBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols[0].name, QStringLiteral("Unfiled"));
}

void TestTodoBlobBackend::loadRecordsRoutesByCategory()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("ToDoDB"));
    device.createRecord(QStringLiteral("ToDoDB"), makeTodo(0, 0, QStringLiteral("Anything")));
    device.createRecord(QStringLiteral("ToDoDB"), makeTodo(0, 1, QStringLiteral("Personal one")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("Personal"));
    TodoBlobBackend be(&palm, &store);

    auto unfiled  = be.loadRecords(QStringLiteral("palm:todo/0"));
    auto personal = be.loadRecords(QStringLiteral("palm:todo/1"));
    QCOMPARE(unfiled.size(), 1);
    QCOMPARE(personal.size(), 1);
    QVERIFY(unfiled[0].data.contains("Anything"));
    QVERIFY(personal[0].data.contains("Personal one"));
    QCOMPARE(unfiled[0].type, QStringLiteral("text/calendar"));
}

void TestTodoBlobBackend::loadRecordsMixedCategoryDataset()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("ToDoDB"));
    device.createRecord(QStringLiteral("ToDoDB"), makeTodo(0, 1, QStringLiteral("A1")));
    device.createRecord(QStringLiteral("ToDoDB"), makeTodo(0, 1, QStringLiteral("A2")));
    device.createRecord(QStringLiteral("ToDoDB"), makeTodo(0, 2, QStringLiteral("B1")));
    device.createRecord(QStringLiteral("ToDoDB"), makeTodo(0, 0, QStringLiteral("U1")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("Slot1"));
    store.setSlotName(QStringLiteral("ToDoDB"), 2, QStringLiteral("Slot2"));
    TodoBlobBackend be(&palm, &store);

    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/0")).size(), 1);
    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/1")).size(), 2);
    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/2")).size(), 1);
}

void TestTodoBlobBackend::createRecordStampsSlotFromCollectionId()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 5, QStringLiteral("Five"));
    TodoBlobBackend be(&palm, &store);

    // Build an ICS payload from a slot-0 todo, then write into
    // collection palm:todo/5. Collection slot must win.
    PalmRecord seed = makeTodo(0, 0, QStringLiteral("Created"));
    BackendRecord br;
    br.id   = QString();
    br.data = WildPalms::TodoPlugin::encodePalmToIcs(seed);
    br.type = QStringLiteral("text/calendar");

    QString newId = be.createRecord(QStringLiteral("palm:todo/5"), br);
    QVERIFY(!newId.isEmpty());

    const auto stored = device.readAllRecords(QStringLiteral("ToDoDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 5);
}

void TestTodoBlobBackend::updateRecordPreservesRecordId()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("ToDoDB"));
    auto seedId = device.createRecord(QStringLiteral("ToDoDB"),
                                      makeTodo(0, 1, QStringLiteral("Original")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("ToDoDB"), 1, QStringLiteral("One"));
    TodoBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:todo/1"));
    QCOMPARE(records.size(), 1);
    BackendRecord br = records.first();

    // Replace the description and round-trip through the transcoder.
    PalmRecord modified = makeTodo(seedId, 1, QStringLiteral("Edited"));
    br.data = WildPalms::TodoPlugin::encodePalmToIcs(modified);
    QVERIFY(be.updateRecord(br));

    const auto stored = device.readRecord(QStringLiteral("ToDoDB"), seedId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->recordId, seedId);
    QCOMPARE(static_cast<int>(stored->category), 1);
}

void TestTodoBlobBackend::deleteRecordForwardsToPalmBackend()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("ToDoDB"));
    auto seedId = device.createRecord(QStringLiteral("ToDoDB"),
                                      makeTodo(0, 0, QStringLiteral("Doomed")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    TodoBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:todo/0"));
    QCOMPARE(records.size(), 1);
    QVERIFY(be.deleteRecord(records.first().id));

    // Mock semantics: deletion removes the record outright.
    QVERIFY(!device.readRecord(QStringLiteral("ToDoDB"), seedId).has_value());
    QCOMPARE(be.loadRecords(QStringLiteral("palm:todo/0")).size(), 0);
}

QTEST_MAIN(TestTodoBlobBackend)
#include "tst_todoblobbackend.moc"
