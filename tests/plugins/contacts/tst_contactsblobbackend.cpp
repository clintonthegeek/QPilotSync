#include <QtTest/QtTest>

#include "plugins/contacts/palmcontactsbackend.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/contactcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using WildPalms::ContactsPlugin::PalmContactsBackend;
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

class TestPalmContactsBackend : public QObject
{
    Q_OBJECT
private slots:
    void nativeShapes_returnsContactsPalm();
    void backendType_returnsPalmContacts();
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
    // C: domain-level collection
    void domainCollection_availableCollectionsIncludesDomainId();
    void domainCollection_loadRecordsReturnsAllCategories();
    void domainCollection_nativeShapeIsContactsPalm();
};

void TestPalmContactsBackend::nativeShapes_returnsContactsPalm()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

    auto shapes = be.nativeShapes();
    QCOMPARE(shapes.size(), 1);
    QCOMPARE(shapes.first().domain.toString(), QStringLiteral("contacts"));
    QCOMPARE(shapes.first().encoding.toString(), QStringLiteral("palm"));
}

void TestPalmContactsBackend::backendType_returnsPalmContacts()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);
    QCOMPARE(be.backendType(), QStringLiteral("palm-contacts"));
}

void TestPalmContactsBackend::slotFromCollectionId_validRange()
{
    for (int slot = 0; slot <= 15; ++slot) {
        const QString cid = PalmContactsBackend::collectionIdForSlot(slot);
        QCOMPARE(cid, QStringLiteral("palm:contact/%1").arg(slot));
        QCOMPARE(PalmContactsBackend::slotFromCollectionId(cid), slot);
    }
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("palm:todo/1")), -1);
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(QString()), -1);
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("garbage")), -1);
}

void TestPalmContactsBackend::slotFromCollectionId_outOfRangeReturnsMinusOne()
{
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/-1")), -1);
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/16")), -1);
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/100")), -1);
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/abc")), -1);
    QCOMPARE(PalmContactsBackend::slotFromCollectionId(
        QStringLiteral("palm:contact/")), -1);
}

void TestPalmContactsBackend::availableCollections_includesUnfiledAlways()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

    auto cols = be.availableCollections();
    // Always includes domain-level + unfiled
    QCOMPARE(cols.size(), 2);
    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    QVERIFY(ids.contains(QStringLiteral("palm:contacts")));
    QVERIFY(ids.contains(QStringLiteral("palm:contact/0")));
    for (const auto &c : cols) {
        if (c.id == QStringLiteral("palm:contact/0")) {
            QCOMPARE(c.name, QStringLiteral("Unfiled"));
            QCOMPARE(c.type, QStringLiteral("contacts"));
        }
    }
}

void TestPalmContactsBackend::availableCollections_includesPopulatedSlots()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 1, QStringLiteral("Personal"));
    store.setSlotName(QStringLiteral("AddressDB"), 4, QStringLiteral("Business"));

    PalmContactsBackend be(&palm, &store);
    auto cols = be.availableCollections();

    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    // domain-level + unfiled + 2 named slots
    QCOMPARE(cols.size(), 4);
    QVERIFY(ids.contains(QStringLiteral("palm:contacts")));
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

void TestPalmContactsBackend::loadRecords_filtersBySlot()
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
    PalmContactsBackend be(&palm, &store);

    QCOMPARE(be.loadRecords(QStringLiteral("palm:contact/0")).size(), 1);
    auto personal = be.loadRecords(QStringLiteral("palm:contact/1"));
    QCOMPARE(personal.size(), 2);
    QCOMPARE(be.loadRecords(QStringLiteral("palm:contact/2")).size(), 1);

    QCOMPARE(personal[0].type, QStringLiteral("contacts"));
    QByteArray combined = personal[0].data + personal[1].data;
    QVERIFY(combined.contains("Personalov"));
    QVERIFY(combined.contains("Personalez"));
}

void TestPalmContactsBackend::loadRecords_skipsDeletedRecords()
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
    PalmContactsBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QCOMPARE(records.size(), 1);
    QVERIFY(records[0].data.contains("Liveman"));
    QVERIFY(!records[0].data.contains("Goneman"));
}

void TestPalmContactsBackend::loadRecord_byId_roundTrips()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 0, QStringLiteral("Lookuper")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QCOMPARE(records.size(), 1);
    const QString id = records.first().id;

    auto loaded = be.loadRecord(id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->id, id);
    QVERIFY(loaded->data.contains("Lookuper"));
    QCOMPARE(loaded->type, QStringLiteral("contacts"));

    QVERIFY(!be.loadRecord(QStringLiteral("not-a-real-id")).has_value());
}

void TestPalmContactsBackend::createRecord_assignsCategory()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 4, QStringLiteral("Quad"));
    PalmContactsBackend be(&palm, &store);

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
    QVERIFY(stored.first().data.contains("Createman"));
}

void TestPalmContactsBackend::updateRecord_preservesSlotFromExisting()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 3, QStringLiteral("Original")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 3, QStringLiteral("Three"));
    PalmContactsBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/3"));
    QCOMPARE(records.size(), 1);
    BackendRecord br = records.first();

    PalmRecord modified = makeContact(seedId, 0, QStringLiteral("Edited"));
    br.data = modified.toWireBytes();
    br.type = QStringLiteral("contacts");
    QVERIFY(be.updateRecord(br));

    const auto stored = device.readRecord(QStringLiteral("AddressDB"), seedId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->recordId, seedId);
    QCOMPARE(static_cast<int>(stored->category), 3);
    QVERIFY(stored->data.contains("Edited"));
    QVERIFY(!stored->data.contains("Original"));
}

void TestPalmContactsBackend::deleteRecord_forwardsToPalmBackend()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 0, QStringLiteral("Doomed")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QCOMPARE(records.size(), 1);
    QVERIFY(be.deleteRecord(records.first().id));

    QVERIFY(!device.readRecord(QStringLiteral("AddressDB"), seedId).has_value());
    QCOMPARE(be.loadRecords(QStringLiteral("palm:contact/0")).size(), 0);
}

void TestPalmContactsBackend::modifiedSince_filtersByTimestampAndSlot()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));

    const QDateTime t0 = QDateTime::fromString(
        QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    const QDateTime t1 = t0.addSecs(60);
    const QDateTime t2 = t0.addSecs(120);

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
    PalmContactsBackend be(&palm, &store);

    auto changed = be.modifiedSince(QStringLiteral("palm:contact/1"), t1);
    QCOMPARE(changed.size(), 1);
    QVERIFY(changed[0].data.contains("Newslot1"));
    QVERIFY(!changed[0].data.contains("Oldslot1"));
    QVERIFY(!changed[0].data.contains("Slot2man"));
}

void TestPalmContactsBackend::loadRecordsEmitsPalmNativeBytes()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    auto seedId = device.createRecord(QStringLiteral("AddressDB"),
        makeContact(0, 0, QStringLiteral("Wireman")));
    QVERIFY(seedId != 0);

    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

    auto records = be.loadRecords(QStringLiteral("palm:contact/0"));
    QVERIFY(!records.isEmpty());
    const auto &r = records.first();
    QVERIFY2(!r.data.startsWith("BEGIN:VCARD"),
             qPrintable("expected palm-native bytes; got vCard:\n"
                        + QString::fromUtf8(r.data.left(80))));
    const auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(r.data);
    QVERIFY(pr.recordId != 0);
    QCOMPARE(r.type, QStringLiteral("contacts"));
}

void TestPalmContactsBackend::createRecordAcceptsPalmNativeBytes()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

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
    QVERIFY(stored.first().data.contains("Wirecreator"));
}

// C: domain-level collection tests

void TestPalmContactsBackend::domainCollection_availableCollectionsIncludesDomainId()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 1, QStringLiteral("Personal"));
    PalmContactsBackend be(&palm, &store);

    auto cols = be.availableCollections();
    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    QVERIFY2(ids.contains(QStringLiteral("palm:contacts")),
             qPrintable("domain-level id missing; collections: " + ids.join(", ")));
}

void TestPalmContactsBackend::domainCollection_loadRecordsReturnsAllCategories()
{
    MockPalmDatabaseAccess device;
    device.createDatabase(QStringLiteral("AddressDB"));
    device.createRecord(QStringLiteral("AddressDB"),
                        makeContact(0, 0, QStringLiteral("Alpha")));
    device.createRecord(QStringLiteral("AddressDB"),
                        makeContact(0, 3, QStringLiteral("Beta")));

    PalmBackend palm(&device);
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("AddressDB"), 3, QStringLiteral("Work"));
    PalmContactsBackend be(&palm, &store);

    auto all = be.loadRecords(QStringLiteral("palm:contacts"));
    QCOMPARE(all.size(), 2);
    for (const auto &br : all) {
        QCOMPARE(br.type, QStringLiteral("contacts"));
        QVERIFY(!br.data.isEmpty());
    }
}

void TestPalmContactsBackend::domainCollection_nativeShapeIsContactsPalm()
{
    MockPalmDatabaseAccess device;
    PalmBackend palm(&device);
    CategoryMappingStore store;
    PalmContactsBackend be(&palm, &store);

    auto shapes = be.nativeShapes();
    QVERIFY(!shapes.isEmpty());
    QCOMPARE(shapes.first().domain.toString(), QStringLiteral("contacts"));
    QCOMPARE(shapes.first().encoding.toString(), QStringLiteral("palm"));
}

QTEST_MAIN(TestPalmContactsBackend)
#include "tst_contactsblobbackend.moc"
