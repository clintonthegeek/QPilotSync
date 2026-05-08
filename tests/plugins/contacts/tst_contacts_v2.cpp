// M5c Task 4: post-V2 / post-runSyncFuture rewrite of tst_contacts_v2.
//
// Same migration as tst_memo_v2 (M5c Task 3): swap V1 IBackendPlugin +
// runBlobTwoWay for the V2 plugin loaded through PalmRuntime's test seams
// driving SyncEngine::runSyncFuture via runtime.hotSync().
//
// Phase Ia (palm-native pivot): the ContactsBlobBackend now emits
// palm-native wire bytes on the read path; vCard transformation is
// owned by the registered TransformationStage at the engine edge.
// Until Phase Ia Task 19 wires the engine to invoke the registered
// edge, the engine passes BackendRecord bytes through verbatim — so
// the PC mock receives palm-native bytes, not vCard.
//
// The reduced V2 form preserves the load-bearing smoke assertion:
// the real wildpalms_contacts_v2.so plumbed through the new engine
// path produces *something matching the source* on the PC side for a
// seeded Palm contact. The vCard-shape contract is pinned in
// tst_contactsvcardtranscoder (PalmToVCardStage round-trip); the
// engine-level palm->vcard4 transform will be pinned by Task 19.
// Per-slot routing and conflict-merge semantics live in the plugin's
// unit tests (tst_contactsblobbackend, tst_contactsconflicthandler)
// and libkalburator's contract suite.

#include <QtTest/QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFuture>
#include <cstring>

#include <KPluginFactory>
#include <KPluginMetaData>
#include <pi-appinfo.h>

#include "core/ibackendplugin_v2.h"
#include "palm/codecs/contactcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "plugins/contacts/contactsblobbackend.h"
#include "runtime/palmdeviceaccess.h"
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"

#include "mockblobbackend.h"
#include "synctypes.h"
#include "collectioninfo.h"
#include "backendrecord.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::ContactsPlugin::ContactsBlobBackend;

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
    m.sourceCalendar  = ContactsBlobBackend::collectionIdForSlot(0);
    m.targetBackend   = kPcBackendId;
    m.targetCalendar  = kPcCollectionId;
    m.mode            = Kalburator::Sync::SyncMode::TwoWay;
    m.enabled         = true;
    return m;
}

WildPalms::IBackendPluginV2 *loadContactsPluginV2(QObject *parent)
{
    const auto metaDatas = KPluginMetaData::findPlugins(
        QStringLiteral("wildpalms/plugins"),
        [](const KPluginMetaData &md) {
            return md.value(QStringLiteral("X-WildPalms-PluginType"))
                       == QStringLiteral("backend")
                && md.fileName().contains(QStringLiteral("contacts"));
        });
    if (metaDatas.isEmpty()) return nullptr;
    auto factoryResult = KPluginFactory::loadFactory(metaDatas.first());
    if (!factoryResult) return nullptr;
    QObject *obj = factoryResult.plugin->create<QObject>(parent);
    return qobject_cast<WildPalms::IBackendPluginV2 *>(obj);
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
    QCoreApplication::addLibraryPath(QStringLiteral(CMAKE_BINARY_DIR "/lib"));
}

void TestContactsV2::freshSync_seededPalmContact_arrivesOnPC()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());

    auto *plugin = loadContactsPluginV2(this);
    QVERIFY2(plugin != nullptr, "wildpalms_contacts_v2.so failed to load as IBackendPluginV2");

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

    // Wire the V2 plugin in — palm-side backend registered under
    // plugin->pluginId() = "contacts".
    runtime.registerPluginForTest(
        std::shared_ptr<WildPalms::IBackendPluginV2>(
            plugin, [](WildPalms::IBackendPluginV2 *) {}));

    // PC side: a MockBlobBackend with an empty Unfiled-equivalent collection.
    auto pcBlob = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    auto *pcRaw = pcBlob.get();
    Kalburator::Sync::CollectionInfo pcInfo;
    pcInfo.id   = kPcCollectionId;
    pcInfo.name = QStringLiteral("PC Contacts (Unfiled)");
    pcInfo.type = QStringLiteral("contacts");
    pcBlob->createCollection(pcInfo);
    runtime.registerBlobBackendForTest(kPcBackendId, std::move(pcBlob));

    runtime.setMappingsForTest({makeUnfiledMapping()});

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const auto run = future.resultAt(0);
    QVERIFY2(run.success, qUtf8Printable(run.errorMessage));

    // PC mock should now hold one record whose blob is the seeded Palm
    // contact's payload. Phase Ia (read-path palm-native pivot) means
    // the BackendRecord travelling source -> engine -> target carries
    // palm-native wire bytes. The "vCard arrives on PC" claim moves to
    // Task 19's engine-level palm->vcard4 integration test once the
    // engine is wired to invoke the registered TransformationStage at
    // the edge. Until then this test pins the smoke assertion that
    // *something matching the source* lands on PC.
    const auto pcRecs = pcRaw->loadRecords(kPcCollectionId);
    QCOMPARE(pcRecs.size(), 1);

    const QByteArray payload = pcRecs.first().data;
    QVERIFY(!payload.isEmpty());
    // Seeded contact's last/first names must survive the palm-native
    // round-trip (palm wire bytes embed Pilot-Link's Address packed
    // form, which keeps these as plain UTF-8 substrings).
    QVERIFY2(payload.contains(QByteArrayLiteral("Doe")),
             qUtf8Printable(QStringLiteral("Expected 'Doe' in PC payload "
                                           "(payload size: %1)")
                                .arg(payload.size())));
    QVERIFY(payload.contains(QByteArrayLiteral("Jane")));
    // Negative: BEGIN:VCARD must NOT appear — engine doesn't yet
    // transform palm-native -> vCard. When Task 19 lands, this
    // assertion flips and a vCard-shape assertion replaces it (likely
    // in a sibling test, leaving this one as a pure smoke check).
    QVERIFY(!payload.contains(QByteArrayLiteral("BEGIN:VCARD")));
}

QTEST_MAIN(TestContactsV2)
#include "tst_contacts_v2.moc"
