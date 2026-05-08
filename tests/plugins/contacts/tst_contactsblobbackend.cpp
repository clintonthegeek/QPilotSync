#include <QtTest/QtTest>

#include "plugins/contacts/contactsblobbackend.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/contactcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

using Kalburator::Sync::BackendRecord;
using WildPalms::ContactsPlugin::ContactsBlobBackend;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord makeContact(std::uint32_t recordId,
                       int slot,
                       const QString &lastName,
                       const QString &firstName = QStringLiteral("Test"))
{
    Contact c;
    c.lastName  = lastName;
    c.firstName = firstName;
    PalmRecord pr;
    pr.recordId     = recordId;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.data         = encodeContact(c);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace

class TestContactsBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void slotFromCollectionId_validRange();
    void slotFromCollectionId_outOfRangeReturnsMinusOne();
    void availableCollections_includesUnfiledAlways();
    void availableCollections_includesPopulatedSlots();
    void loadRecords_filtersBySlot();
    void loadRecords_skipsDeletedRecords();
    void loadRecord_byId_roundTrips();
    void createRecord_assignsCategory();
    void updateRecord_preservesSlotFromExisting();
    void deleteRecord_forwardsToPalmBackend();
    void modifiedSince_filtersByTimestampAndSlot();
    void loadRecordsEmitsPalmNativeBytes();
    void createRecordAcceptsPalmNativeBytes();
};

void TestContactsBlobBackend::slotFromCollectionId_validRange()
{
    for (int slot = 0; slot <= 15; ++slot) {
        const QString cid = ContactsBlobBackend::collectionIdForSlot(slot);
        QCOMPARE(cid, QStringLiteral("palm:contact/%1").arg(slot));
        QCOMPARE(ContactsBlobBackend::slotFromCollectionId(cid), slot);
    }
    // Bad prefix
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:todo/1")), -1);
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(QString()), -1);
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("garbage")), -1);
}

void TestContactsBlobBackend::slotFromCollectionId_outOfRangeReturnsMinusOne()
{
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/-1")), -1);
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/16")), -1);
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/100")), -1);
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/abc")), -1);
    QCOMPARE(ContactsBlobBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/")), -1);
}

void TestContactsBlobBackend::availableCollections_includesUnfiledAlways()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    ContactsBlobBackend be(&palm, &store);

    auto cols = be.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols[0].id,   QStringLiteral("palm:contact/0"));
    QCOMPARE(cols[0].name, QStringLiteral("Unfiled"));
    QCOMPARE(cols[0].type, QStringLiteral("contacts"));
}

void TestContactsBlobBackend::availableCollections_includesPopulatedSlots()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 1, QStringLiteral("Personal"));
    store.setSlotName(QStringLiteral("AddressDB"), 4, QStringLiteral("Business"));

    ContactsBlobBackend be(&palm, &store);
    auto cols = be.availableCollections();

    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    QCOMPARE(cols.size(), 3);
    QVERIFY(ids.contains(QStringLiteral("palm:contact/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:contact/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:contact/4")));
    for (const auto &c : cols) {
        if (c.id == QStringLiteral("palm:contact/1"))
            QCOMPARE(c.name, QStringLiteral("Personal"));
        if (c.id == QStringLiteral("palm:contact/4"))
            QCOMPARE(c.name, QStringLiteral("Business"));
    }
}

void TestContactsBlobBackend::loadRecords_filtersBySlot()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    device.createRecord(QStringLiteral("AddressDB"),
                        makeContact(0, 0, QStringLiteral("Unfiledton")));
    device.createRecord(QStringLiteral("AddressDB"),
                        makeContact(0, 1, QStringLiteral("Personalov")));
    device.createRecord(QStringLiteral("AddressDB"),
                        makeContact(0, 1, QStringLiteral("Personalez")));
    device.createRecord(QStringLiteral("AddressDB"),
                        makeContact(0, 2, QStringLiteral("Businesski")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 1, QStringLiteral("Personal"));
    store.setSlotName(QStringLiteral("AddressDB"), 2, QStringLiteral("Business"));
    ContactsBlobBackend be(&palm, &store);

    QCOMPARE(be.loadRecords(QStringLiteral("palm:contact/0")).size(), 1);
    auto personal = be.loadRecords(QStringLiteral("palm:contact/1"));
    QCOMPARE(personal.size(), 2);
    QCOMPARE(be.loadRecords(QStringLiteral("palm:contact/2")).size(), 1);

    // type stamped correctly (Phase Ia: backend emits palm-native bytes;
    // the engine's TransformationStage promotes to vcard4 at the edge).
    QCOMPARE(personal[0].type, QStringLiteral("contacts"));
    // Both slot-1 records present (scan, order is implementation-defined).
    QByteArray combined = personal[0].data + personal[1].data;
    QVERIFY(combined.contains("Personalov"));
    QVERIFY(combined.contains("Personalez"));
}

void TestContactsBlobBackend::loadRecords_skipsDeletedRecords()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));

    auto live    = makeContact(0, 0, QStringLiteral("Liveman"));
    auto doomed  = makeContact(0, 0, QStringLiteral("Goneman"));
    doomed.attributes |= PalmRecord::AttrDeleted;
    device.createRecord(QStringLiteral("AddressDB"), live);
    device.createRecord(QStringLiteral("AddressDB"), doomed);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    ContactsBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QCOMPARE(records.size(), 1);
    QVERIFY(records[0].data.contains("Liveman"));
    QVERIFY(!records[0].data.contains("Goneman"));
}

void TestContactsBlobBackend::loadRecord_byId_roundTrips()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 0, QStringLiteral("Lookuper")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    ContactsBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QCOMPARE(records.size(), 1);
    const QString id = records.first().id;

    auto loaded = be.loadRecord(id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->id, id);
    QVERIFY(loaded->data.contains("Lookuper"));
    // Phase Ia: backend emits palm-native bytes; type tag is "contacts".
    QCOMPARE(loaded->type, QStringLiteral("contacts"));

    // Bad id returns nullopt.
    QVERIFY(!be.loadRecord(QStringLiteral("not-a-real-id")).has_value());
}

void TestContactsBlobBackend::createRecord_assignsCategory()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 4, QStringLiteral("Quad"));
    ContactsBlobBackend be(&palm, &store);

    // Phase Ia: hand the backend palm-native wire bytes (the engine
    // demotes through the registered edge before reaching us). The
    // record's own category byte is irrelevant — createRecord uses the
    // collectionId slot. Verify by writing a slot-0 record into
    // palm:contact/4 and confirming category is overwritten.
    PalmRecord seed = makeContact(0, 0, QStringLiteral("Createman"));
    BackendRecord br;
    br.id   = QString();
    br.data = seed.toWireBytes();
    br.type = QStringLiteral("contacts");

    QString newId = be.createRecord(QStringLiteral("palm:contact/4"), br);
    QVERIFY(!newId.isEmpty());

    const auto stored = device.readAllRecords(QStringLiteral("AddressDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 4);
    // Sanity: the record actually round-tripped through fromWireBytes
    // (i.e. the test isn't passing because empty bytes silently no-oped).
    QVERIFY(stored.first().data.contains("Createman"));
}

void TestContactsBlobBackend::updateRecord_preservesSlotFromExisting()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 3, QStringLiteral("Original")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 3, QStringLiteral("Three"));
    ContactsBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/3"));
    QCOMPARE(records.size(), 1);
    BackendRecord br = records.first();

    // Phase Ia: the engine hands the backend palm-native bytes after
    // demoting through the registered edge. Replace lastName by
    // constructing a new PalmRecord and re-serializing. Note: the input
    // PalmRecord's category is intentionally 0 here — updateRecord must
    // recover the actual slot (3) from the existing record on the device.
    PalmRecord modified = makeContact(seedId, 0, QStringLiteral("Edited"));
    br.data = modified.toWireBytes();
    br.type = QStringLiteral("contacts");
    QVERIFY(be.updateRecord(br));

    const auto stored = device.readRecord(QStringLiteral("AddressDB"), seedId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->recordId, seedId);
    // Slot must still be 3 (recovered from existing record).
    QCOMPARE(static_cast<int>(stored->category), 3);
    // Sanity: the new lastName actually landed (i.e. updateRecord
    // applied the wire bytes, didn't silently no-op).
    QVERIFY(stored->data.contains("Edited"));
    QVERIFY(!stored->data.contains("Original"));
}

void TestContactsBlobBackend::deleteRecord_forwardsToPalmBackend()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 0, QStringLiteral("Doomed")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    ContactsBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QCOMPARE(records.size(), 1);
    QVERIFY(be.deleteRecord(records.first().id));

    QVERIFY(!device.readRecord(QStringLiteral("AddressDB"), seedId).has_value());
    QCOMPARE(be.loadRecords(QStringLiteral("palm:contact/0")).size(), 0);
}

void TestContactsBlobBackend::modifiedSince_filtersByTimestampAndSlot()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));

    const QDateTime t0 = QDateTime::fromString(
        QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    const QDateTime t1 = t0.addSecs(60);
    const QDateTime t2 = t0.addSecs(120);

    // Two records in slot 1; one in slot 2. Mix old/new timestamps.
    auto oldOne   = makeContact(0, 1, QStringLiteral("Oldslot1"));
    oldOne.lastModified = t0;
    auto newOne   = makeContact(0, 1, QStringLiteral("Newslot1"));
    newOne.lastModified = t2;
    auto wrongSlot = makeContact(0, 2, QStringLiteral("Slot2man"));
    wrongSlot.lastModified = t2;
    device.createRecord(QStringLiteral("AddressDB"), oldOne);
    device.createRecord(QStringLiteral("AddressDB"), newOne);
    device.createRecord(QStringLiteral("AddressDB"), wrongSlot);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 1, QStringLiteral("One"));
    store.setSlotName(QStringLiteral("AddressDB"), 2, QStringLiteral("Two"));
    ContactsBlobBackend be(&palm, &store);

    auto changed = be.modifiedSince(QStringLiteral("palm:contact/1"), t1);
    QCOMPARE(changed.size(), 1);
    QVERIFY(changed[0].data.contains("Newslot1"));
    QVERIFY(!changed[0].data.contains("Oldslot1"));
    QVERIFY(!changed[0].data.contains("Slot2man"));
}

void TestContactsBlobBackend::loadRecordsEmitsPalmNativeBytes()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 0, QStringLiteral("Wireman")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    ContactsBlobBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QVERIFY(!records.isEmpty());
    const auto &r = records.first();
    // Palm wire bytes are NOT vCard. Verify the negative:
    QVERIFY2(!r.data.startsWith("BEGIN:VCARD"),
             qPrintable("expected palm-native bytes; got vCard:\n"
                        + QString::fromUtf8(r.data.left(80))));
    // Verify the positive: round-trip via PalmRecord.
    const auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(r.data);
    QVERIFY(pr.recordId != 0);
    // br.type should now be "contacts", not "text/vcard".
    QCOMPARE(r.type, QStringLiteral("contacts"));
}

void TestContactsBlobBackend::createRecordAcceptsPalmNativeBytes()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    PalmBackend palm(&device);
    CategoryMappingStore store;
    ContactsBlobBackend be(&palm, &store);

    // Construct a PalmRecord and wire-serialize it (palm-native bytes,
    // not vCard). After Phase Ia, the engine demotes through the
    // registered edge before reaching the backend, so the backend gets
    // palm wire bytes directly.
    PalmRecord pr = makeContact(0, 0, QStringLiteral("Wirecreator"));
    pr.category = 0;

    BackendRecord br;
    br.data = pr.toWireBytes();
    br.type = QStringLiteral("contacts");

    const auto newId = be.createRecord(QStringLiteral("palm:contact/0"), br);
    QVERIFY(!newId.isEmpty());

    const auto stored = device.readAllRecords(QStringLiteral("AddressDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 0);
    // Verify the contact data round-tripped (it should contain "Wirecreator").
    QVERIFY(stored.first().data.contains("Wirecreator"));
}

QTEST_MAIN(TestContactsBlobBackend)
#include "tst_contactsblobbackend.moc"
