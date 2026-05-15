// M5c Task 4: post-V2 / post-runSyncFuture rewrite of tst_contacts_v2.
//
// Same migration as tst_memo_v2 (M5c Task 3): swap V1 IBackendPlugin +
// runBlobTwoWay for the V2 plugin loaded through PalmRuntime's test seams
// driving SyncEngine::runSyncFuture via runtime.hotSync().
//
// Phase Ia (palm-native pivot): the ContactsBlobBackend emits
// palm-native wire bytes on the read path; vCard transformation is
// owned by the registered TransformationStage at the engine edge.
// Phase Ia.5 unified the engine routing so dispatchSync compiles and
// runs the registered (palm -> vcard4) Pipeline at the edge — the PC
// mock therefore receives vCard 4.0 bytes after sync.
//
// This V2 form pins the load-bearing end-to-end smoke assertion: the
// real wildpalms_contacts_v2.so plumbed through PalmRuntime +
// SyncEngine::runSyncFuture produces a vCard 4.0 payload on the PC
// side for a seeded Palm contact. Per-slot routing and conflict-merge
// semantics live in the plugin's unit tests
// (tst_contactsblobbackend, tst_contactsconflicthandler) and
// libkalburator's contract suite.

#include <QtTest/QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFuture>
#include <cstring>

// K.8b T6: KPluginFactory / IBackendPluginV2 includes removed.
// ContactsBackendPlugin is now STATIC; PalmRuntime loads it in-process.
#include <pi-appinfo.h>

#include "palm/codecs/contactcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "plugins/contacts/palmcontactsbackend.h"
#include "runtime/palmdeviceaccess.h"
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"

#include "mockblobbackend.h"
#include "synctypes.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"
#include "pluginmanager.h"
#include "stock_plugins.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::ContactsPlugin::PalmContactsBackend;

namespace {

constexpr int kSyncTimeoutMs = 5000;
const QString kPcBackendId      = QStringLiteral("pc");
const QString kPcCollectionId   = QStringLiteral("pc-contacts-unfiled");
const QString kContactsPluginId = QStringLiteral("contacts");

// AddressDB AppInfo block naming four slots — the contacts plugin's
// populateFromAppInfo path uses CategoryAppInfo's name table to populate
// the per-slot CategoryMappingStore.
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

PalmRecord makeContactRecord(int slot,
                             const QString &lastName,
                             const QString &firstName)
{
    Contact c;
    c.lastName  = lastName;
    c.firstName = firstName;
    PalmRecord pr;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.data         = encodeContact(c);
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

Kalburator::Sync::SyncMapping makeUnfiledMapping()
{
    Kalburator::Sync::SyncMapping m;
    m.id              = QStringLiteral("contacts-unfiled-twoway");
    m.sourceBackend   = kContactsPluginId;
    // Source collection: the Unfiled slot ("palm:contact/0").
    m.sourceCalendar  = PalmContactsBackend::collectionIdForSlot(0);
    m.targetBackend   = kPcBackendId;
    m.targetCalendar  = kPcCollectionId;
    m.mode            = Kalburator::Sync::SyncMode::TwoWay;
    m.enabled         = true;
    return m;
}

} // namespace

class TestContactsV2 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void freshSync_seededPalmContact_arrivesOnPC();
};

void TestContactsV2::initTestCase()
{
    qApp->setApplicationName(QStringLiteral("tst_contacts_v2"));
    // K.8b T6: library path for stale .so files removed (plugins are STATIC).
    // Stock plugins seeded so dispatchSync finds the contacts domain definitions.
    // ContactsBackendPlugin constructor registers palm↔vcard4 edges when
    // PalmRuntime constructs it via registerPalmPlugins().
    Kalburator::PluginManager pm;
    Kalburator::registerStockPlugins(pm);
}

void TestContactsV2::freshSync_seededPalmContact_arrivesOnPC()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    // K.8b T6: ContactsBackendPlugin is STATIC; PalmRuntime loads it in-process.
    // setDeviceAccessForTest() calls finishConnect() which registers
    // PalmContactsBackend (shape: contacts/palm) under id "contacts".
    WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

    // Seed AddressDB with one contact in the Unfiled slot.
    auto palmDb = std::make_unique<MockPalmDatabaseAccess>();
    palmDb->createDatabase(QStringLiteral("AddressDB"));
    palmDb->setAppBlock(QStringLiteral("AddressDB"), buildAppInfo());
    palmDb->createRecord(QStringLiteral("AddressDB"),
                         makeContactRecord(0,
                                            QStringLiteral("Doe"),
                                            QStringLiteral("Jane")));

    runtime.setDeviceAccessForTest(
        std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(std::move(palmDb)));

    // PC side: a MockBlobBackend with an empty Unfiled-equivalent collection.
    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Contacts (Unfiled)");
    pcInfo.type = QStringLiteral("contacts");
    pcBlob->createCollection(pcInfo);
    // Phase Ia.5 Task 19: declare the PC adapter as (contacts, vcard4)
    // so the unified dispatchSync can compile the (contacts, palm) ->
    // (contacts, vcard4) Pipeline via the registered TransformationRegistry
    // edges. Without this, the adapter defaults to blob/blob and the
    // engine's path resolution fails with "no edge path".
    const Kalburator::Shape::Shape pcShape{
        Kalburator::Shape::DomainId{QStringLiteral("contacts")},
        Kalburator::Shape::EncodingId{QStringLiteral("vcard4")}
    };
    runtime.registerBlobBackendForTest(kPcBackendId, std::move(pcBlob), pcShape);

    runtime.setMappingsForTest({makeUnfiledMapping()});

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const auto run = future.resultAt(0);
    QVERIFY2(run.success, qUtf8Printable(run.errorMessage));

    // PC mock should now hold one record whose blob is the engine's
    // transformed output. Phase Ia.5 routes the BackendRecord through
    // the registered (palm -> vcard4) edge at the engine edge, so the
    // PC mock receives a vCard 4.0 payload, not palm wire-bytes.
    const auto pcRecs = pcRaw->loadRecords(kPcCollectionId);
    QCOMPARE(pcRecs.size(), 1);

    const QByteArray payload = pcRecs.first().data;
    QVERIFY(!payload.isEmpty());
    // Seeded contact's last/first names must survive the palm ->
    // vCard transcode (KContacts emits FN/N from the Addressee).
    QVERIFY2(payload.contains(QByteArrayLiteral("Doe")),
             qUtf8Printable(QStringLiteral("Expected 'Doe' in PC payload "
                                           "(payload size: %1)")
                                .arg(payload.size())));
    QVERIFY(payload.contains(QByteArrayLiteral("Jane")));
    // Phase Ia.5: engine routes through the registered palm -> vcard4
    // edge before push, so the target receives vCard 4.0.
    QVERIFY(payload.contains(QByteArrayLiteral("BEGIN:VCARD")));
    QVERIFY(payload.contains(QByteArrayLiteral("VERSION:4.0")));
}

QTEST_MAIN(TestContactsV2)
#include "tst_contacts_v2.moc"
