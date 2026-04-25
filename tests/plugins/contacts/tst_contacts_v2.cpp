#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>

#include <cstring>

#include <pi-appinfo.h>

#include "core/ibackendplugin.h"
#include "palm/codecs/contactcodec.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"
#include "plugins/contacts/contactsblobbackend.h"
#include "plugins/contacts/contactsvcardtranscoder.h"
#include "runtime/backendpluginmanager.h"

#include "blobsyncengine.h"
#include "blobbaselinestore.h"
#include "mockblobbackend.h"
#include "conflicthandlerregistry.h"
#include "conflictstore.h"
#include "conflictpolicy.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmCodecs::decodeContact;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::ContactsPlugin::ContactsBlobBackend;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRecord;

/// Phase E.12 Task 5 end-to-end test.
///
/// Loads the real wildpalms_contacts_v2.so via BackendPluginManager,
/// drives BlobSyncEngine::twoWayWithBaseline against a MockBlobBackend
/// target across several Address category slots. Mirrors
/// tst_todo_v2.cpp in structure.
///
/// MockBlobBackend stands in for LocalBlobBackend (cross id-space
/// mapping deferred to E.15+; the engine matches records by literal
/// id-string across both sides, which the mock tolerates).

namespace {

// Synthesise an AddressDB AppInfo block naming four slots
// (Unfiled + Personal + Business + QuickList). The Address block
// has phone/custom-label tables tacked on after CategoryAppInfo,
// but parseCategoryAppInfo only consumes the leading prefix, so
// just the CategoryAppInfo bytes suffice for the populateFromAppInfo
// path the contacts plugin uses.
QByteArray buildAppInfo()
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Business"),
                             QStringLiteral("QuickList") };
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

PalmRecord makeContactRecord(std::uint32_t recordId,
                             int slot,
                             const QString &lastName,
                             const QString &firstName = QStringLiteral("Test"),
                             const QString &company = {})
{
    Contact c;
    c.lastName  = lastName;
    c.firstName = firstName;
    c.company   = company;
    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeContact(c);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

QByteArray makeContactVcard(std::uint32_t recordId,
                            int slot,
                            const QString &lastName,
                            const QString &firstName = QStringLiteral("Test"),
                            const QString &company = {})
{
    return WildPalms::ContactsPlugin::encodePalmToVcard(
        makeContactRecord(recordId, slot, lastName, firstName, company));
}

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

CollectionInfo collectionForSlot(int slot)
{
    CollectionInfo info;
    info.id   = ContactsBlobBackend::collectionIdForSlot(slot);
    info.name = QStringLiteral("target-contact-%1").arg(slot);
    info.type = QStringLiteral("contacts");
    return info;
}

BackendRecord makeTargetRecord(const QString &id,
                               const QByteArray &vcardBytes)
{
    BackendRecord br;
    br.id           = id;
    br.data         = vcardBytes;
    br.type         = QStringLiteral("text/vcard");
    br.lastModified = QDateTime::currentDateTimeUtc();
    br.contentHash  = sha256Hex(vcardBytes);
    return br;
}

// KF6 derives the pluginId from the .so filename (stripping the .so
// suffix but NOT the "lib" prefix on Linux). Our cmake target is
// wildpalms_contacts_v2 -> file libwildpalms_contacts_v2.so ->
// pluginId "libwildpalms_contacts_v2". Same convention as memo (E.9),
// calendar (E.10), and todos (E.11).
const QString kContactsPluginId =
    QStringLiteral("libwildpalms_contacts_v2");

} // namespace

class TestContactsV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void loadsPlugin_andClaimsAddressDb();
    void pushSourceToTarget_singleSlot();
    void pushSourceToTarget_multiSlotRouting();
    void pullTargetToSource_assignsCategoryFromCollectionId();
    void conflictMerge_phoneSlotUnion();
    void deletionPropagatesSourceToTarget();

private:
    QString m_pluginSubdir;
};

void TestContactsV2::initTestCase()
{
    qApp->setApplicationName(QStringLiteral("tst_contacts_v2"));
    // The real contacts plugin installs under
    // ${CMAKE_BINARY_DIR}/lib/wildpalms/plugins/libwildpalms_contacts_v2.so.
    // Adding CMAKE_BINARY_DIR/lib to the library path makes
    // KPluginMetaData::findPlugins("wildpalms/plugins") pick it up.
    QCoreApplication::addLibraryPath(
        QStringLiteral(CMAKE_BINARY_DIR "/lib"));
    m_pluginSubdir = QStringLiteral("wildpalms/plugins");
}

void TestContactsV2::loadsPlugin_andClaimsAddressDb()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY2(mgr.loadPlugin(kContactsPluginId),
             "contacts plugin failed to load via BackendPluginManager - "
             "check build dir lib path / plugin metadata");
    auto *plugin = mgr.plugin(kContactsPluginId);
    QVERIFY(plugin != nullptr);

    // The catalogue entry must claim AddressDB.
    bool found = false;
    for (const auto &pi : mgr.catalogue()) {
        if (pi.claimedDatabases.contains(QStringLiteral("AddressDB"))) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "no plugin in catalogue claims AddressDB");

    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    delete provided.blob;
}

void TestContactsV2::pushSourceToTarget_singleSlot()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Palm seeded with one record in slot 0.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    dev.createRecord(QStringLiteral("AddressDB"),
        makeContactRecord(0, 0, QStringLiteral("Single"),
                          QStringLiteral("Solo")));
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kContactsPluginId));
    auto *plugin = mgr.plugin(kContactsPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(0));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    const QString cid       = ContactsBlobBackend::collectionIdForSlot(0);
    const QString mappingId = QStringLiteral("e12-single-0");
    auto result = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    auto recs = target.loadRecords(cid);
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().type, QStringLiteral("text/vcard"));
    // vCard 4.0: KContacts emits BEGIN:VCARD..VERSION:4.0..N:Single;Solo
    // (FN may be omitted when formattedName is empty since the codec
    // doesn't synthesize it). Verify the body is vCard-shaped and
    // carries the lastName.
    QVERIFY(recs.first().data.contains("BEGIN:VCARD"));
    QVERIFY(recs.first().data.contains("Single"));

    delete provided.blob;
}

void TestContactsV2::pushSourceToTarget_multiSlotRouting()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Palm seeded with records in slots 0 and 3 ("QuickList").
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    dev.createRecord(QStringLiteral("AddressDB"),
        makeContactRecord(0, 0, QStringLiteral("Unfiledman")));
    dev.createRecord(QStringLiteral("AddressDB"),
        makeContactRecord(0, 3, QStringLiteral("QuickGuy")));
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kContactsPluginId));
    auto *plugin = mgr.plugin(kContactsPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(0));
    target.createCollection(collectionForSlot(3));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    for (int slot : {0, 3}) {
        const QString cid       = ContactsBlobBackend::collectionIdForSlot(slot);
        const QString mappingId = QStringLiteral("e12-multi-%1").arg(slot);
        auto result = engine.twoWayWithBaseline(
            provided.blob, &target, cid, mappingId,
            &baseline, &registry, &conflicts, policy);
        QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    }

    auto unfiled = target.loadRecords(ContactsBlobBackend::collectionIdForSlot(0));
    auto quick   = target.loadRecords(ContactsBlobBackend::collectionIdForSlot(3));
    QCOMPARE(unfiled.size(), 1);
    QCOMPARE(quick.size(), 1);
    QVERIFY(unfiled.first().data.contains("Unfiledman"));
    QVERIFY(quick.first().data.contains("QuickGuy"));

    delete provided.blob;
}

void TestContactsV2::pullTargetToSource_assignsCategoryFromCollectionId()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Empty Palm side; AppInfo present so slot 2 ("Business") is populated.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kContactsPluginId));
    auto *plugin = mgr.plugin(kContactsPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(2));
    const QString cid = ContactsBlobBackend::collectionIdForSlot(2);

    target.createRecord(cid,
        makeTargetRecord(QStringLiteral("target:pulled-1"),
                         makeContactVcard(0, 2, QStringLiteral("Pulled"),
                                          QStringLiteral("Vee"))));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    auto result = engine.twoWayWithBaseline(
        provided.blob, &target, cid, QStringLiteral("e12-pull-2"),
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const auto palmRecs = dev.readAllRecords(QStringLiteral("AddressDB"));
    QCOMPARE(palmRecs.size(), 1);
    QCOMPARE(static_cast<int>(palmRecs.first().category), 2);
    auto contact = decodeContact(QByteArrayView(palmRecs.first().data));
    QVERIFY(contact.has_value());
    QCOMPARE(contact->lastName, QStringLiteral("Pulled"));

    delete provided.blob;
}

void TestContactsV2::conflictMerge_phoneSlotUnion()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Both sides start with the same record (slot 1, Doe/John, one
    // Work phone). After baseline:
    //   - Source-side adds a second phone (Mobile).
    //   - Target-side leaves the second phone empty (only the shared
    //     Work phone) but flips a custom field.
    // After vCard round-trip the source side ends up with phone[0]+
    // phone[1] populated, the target side has only phone[0]. The
    // ContactsConflictHandler's per-slot union must keep both phones,
    // mirroring tst_contactsconflicthandler's strategy.
    Contact baselineC;
    baselineC.lastName  = QStringLiteral("Doe");
    baselineC.firstName = QStringLiteral("John");
    baselineC.phone[0]  = QStringLiteral("555-0000");
    baselineC.phoneLabels = { QStringLiteral("Work") };

    PalmRecord seed;
    seed.recordId     = 0;
    seed.category     = 1;
    seed.data         = encodeContact(baselineC);
    seed.lastModified = QDateTime::currentDateTimeUtc();

    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    const auto seedId = dev.createRecord(QStringLiteral("AddressDB"), seed);
    QVERIFY(seedId != 0);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kContactsPluginId));
    auto *plugin = mgr.plugin(kContactsPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    // Register the plugin's conflict handler under "palm-contacts" -
    // the engine looks up by the source backend's id (provided.blob's
    // backendId() is "palm-contacts").
    auto *handler = plugin->createConflictHandler();
    QVERIFY2(handler != nullptr,
             "contacts plugin must produce a conflict handler");
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    registry.registerHandler(QStringLiteral("palm-contacts"), handler);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(1));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    const QString cid       = ContactsBlobBackend::collectionIdForSlot(1);
    const QString mappingId = QStringLiteral("e12-conflict-union");

    // First sync establishes the baseline + propagates Palm->target.
    auto r1 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    auto recsAfterFirst = target.loadRecords(cid);
    QCOMPARE(recsAfterFirst.size(), 1);
    BackendRecord targetRec = recsAfterFirst.first();

    // Modify target side: add custom[0]. Re-encode via the transcoder
    // so the bytes stay valid vCard.
    {
        auto decoded = WildPalms::ContactsPlugin::decodeVcardToPalm(
            targetRec.data, /*slotHint=*/1);
        QVERIFY(decoded.has_value());
        auto c = decodeContact(QByteArrayView(decoded->data));
        QVERIFY(c.has_value());
        c->custom[0] = QStringLiteral("hobby:woodworking");
        decoded->data = encodeContact(*c);
        targetRec.data         = WildPalms::ContactsPlugin::encodePalmToVcard(*decoded);
        targetRec.contentHash  = sha256Hex(targetRec.data);
        targetRec.lastModified = QDateTime::currentDateTimeUtc().addSecs(60);
    }
    QVERIFY(target.updateRecord(targetRec));

    // Modify Palm side: add a second phone (will round-trip into
    // phone[1] after vCard re-decode on the source side).
    {
        auto palmRecs = dev.readAllRecords(QStringLiteral("AddressDB"));
        QCOMPARE(palmRecs.size(), 1);
        PalmRecord pr = palmRecs.first();
        auto c = decodeContact(QByteArrayView(pr.data));
        QVERIFY(c.has_value());
        c->phone[1]    = QStringLiteral("555-2222");
        c->phoneLabels = { QStringLiteral("Work"), QStringLiteral("Mobile") };
        pr.data         = encodeContact(*c);
        pr.lastModified = QDateTime::currentDateTimeUtc().addSecs(120);
        QVERIFY(dev.updateRecord(QStringLiteral("AddressDB"), pr));
    }

    // Second sync - both sides have local edits since baseline ->
    // BothModified conflict -> handler fires.
    auto r2 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    // The engine routed through the registered ContactsConflictHandler.
    // The overlay produces ConflictDecision::Merge with a re-serialised
    // vCard carrying BOTH phones AND the target's custom[0]. The current
    // BlobSyncEngine does not auto-apply Merge - it adds the conflict
    // (with the populated mergedContent) to the store. Verify both: a
    // conflict landed, AND the merged content matches the overlay.
    QCOMPARE(conflicts.count(), 1);
    auto pending = conflicts.allConflicts();
    QCOMPARE(pending.size(), 1);
    const auto &conflict = pending.first();
    QVERIFY2(!conflict.mergedContent.isEmpty(),
             "ContactsConflictHandler did not populate mergedContent - "
             "phone-slot-union overlay was not invoked");

    auto merged = WildPalms::ContactsPlugin::decodeVcardToPalm(
        conflict.mergedContent, /*slotHint=*/1);
    QVERIFY(merged.has_value());
    auto mergedContact = decodeContact(QByteArrayView(merged->data));
    QVERIFY(mergedContact.has_value());
    QCOMPARE(mergedContact->phone[0], QStringLiteral("555-0000"));
    QCOMPARE(mergedContact->phone[1], QStringLiteral("555-2222"));
    QCOMPARE(mergedContact->custom[0], QStringLiteral("hobby:woodworking"));

    delete handler;
    delete provided.blob;
}

void TestContactsV2::deletionPropagatesSourceToTarget()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    const auto seedId = dev.createRecord(QStringLiteral("AddressDB"),
        makeContactRecord(0, 0, QStringLiteral("Doomed"),
                          QStringLiteral("Soon")));
    QVERIFY(seedId != 0);
    PalmDeviceConnection conn(&dev);

    WildPalms::BackendPluginManager mgr(nullptr, nullptr, nullptr);
    mgr.setPluginSubdir(m_pluginSubdir);
    mgr.discoverPlugins();
    QVERIFY(mgr.loadPlugin(kContactsPluginId));
    auto *plugin = mgr.plugin(kContactsPluginId);
    QVERIFY(plugin != nullptr);
    auto provided = plugin->createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    Kalburator::Sync::MockBlobBackend target;
    target.createCollection(collectionForSlot(0));

    Kalburator::Sync::BlobBaselineStore baseline(
        tmp.filePath(QStringLiteral(".wildpalms-sync.db")));
    QVERIFY(baseline.isOpen());
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    Kalburator::Sync::QSyncCore::ConflictStore conflicts;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    Kalburator::Sync::BlobSyncEngine engine;

    const QString cid       = ContactsBlobBackend::collectionIdForSlot(0);
    const QString mappingId = QStringLiteral("e12-delete");

    // First sync establishes the baseline + propagates the seed to target.
    auto r1 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QCOMPARE(target.loadRecords(cid).size(), 1);

    // Delete the record on the source after baseline.
    QVERIFY(dev.deleteRecord(QStringLiteral("AddressDB"), seedId));

    // Second sync - deletion propagates to target.
    auto r2 = engine.twoWayWithBaseline(
        provided.blob, &target, cid, mappingId,
        &baseline, &registry, &conflicts, policy);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    QCOMPARE(target.loadRecords(cid).size(), 0);

    delete provided.blob;
}

QTEST_MAIN(TestContactsV2)
#include "tst_contacts_v2.moc"
