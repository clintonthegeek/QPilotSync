#include <QtTest/QtTest>

#include <cstring>

#include <pi-appinfo.h>

#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/contacts/contactsblobbackend.h"
#include "plugins/contacts/contactsconflicthandler.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

#include "core/ibackendplugin.h"

// Complete-type includes for libkalburator pointers (delete on a forward
// decl is UB; ConflictHandler is needed for dynamic_cast).
#include "iblobbackend.h"
#include "conflictpolicy.h"
#include "conflictrecord.h"

using WildPalms::ContactsPlugin::ContactsBackendPlugin;
using WildPalms::ContactsPlugin::ContactsBlobBackend;
using WildPalms::ContactsPlugin::ContactsConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

namespace {

// Build an AddressDB AppInfo block with the named slots populated.
// Names must fit in pi-appinfo's 16-byte name field (15 chars + NUL).
QByteArray buildAddressDbAppInfo(const QStringList &slotNames)
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const int n = static_cast<int>(std::min<qsizetype>(slotNames.size(), 16));
    for (int i = 0; i < n; ++i) {
        const QByteArray utf = slotNames[i].toUtf8().left(15);
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

} // namespace

class TestContactsBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void pluginIdentity();
    void createBackends_returnsBlobOnly();
    void createBackends_populatesCategoryStoreFromAppInfo();
    void createConflictHandler_requiresPriorCreateBackends();
    void createConflictHandler_returnsContactsConflictHandler();
    void enrichConflictSnapshot_extractsFnFromVcard();
    void formatConflictRecordHtml_includesTitleAndPre();
};

void TestContactsBackendPlugin::pluginIdentity()
{
    ContactsBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("contacts"));
    QCOMPARE(p.version(),  QStringLiteral("2.0"));
    auto claims = p.claimedDatabases();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims[0], QStringLiteral("AddressDB"));
    QVERIFY(!p.hasMainView());
}

void TestContactsBackendPlugin::createBackends_returnsBlobOnly()
{
    MockPalmDatabaseAccess device;
    PalmDeviceConnection conn(&device);

    ContactsBackendPlugin p;
    auto provided = p.createBackends(/*host=*/nullptr, &conn);
    QVERIFY(provided.blob != nullptr);
    QVERIFY(provided.calendar == nullptr);

    delete provided.blob;
}

void TestContactsBackendPlugin::createBackends_populatesCategoryStoreFromAppInfo()
{
    // Slots 0 ("Unfiled"), 1 ("Family"), 5 ("Customers") populated;
    // intermediate slots (2..4) blank — must NOT show as collections.
    QStringList names;
    for (int i = 0; i < 16; ++i) names << QString();
    names[0] = QStringLiteral("Unfiled");
    names[1] = QStringLiteral("Family");
    names[5] = QStringLiteral("Customers");

    MockPalmDatabaseAccess device;
    device.setAppBlock(QStringLiteral("AddressDB"), buildAddressDbAppInfo(names));

    PalmDeviceConnection conn(&device);

    ContactsBackendPlugin p;
    auto provided = p.createBackends(/*host=*/nullptr, &conn);
    QVERIFY(provided.blob != nullptr);

    auto *blob = static_cast<ContactsBlobBackend *>(provided.blob);
    QVERIFY(blob);
    auto cols = blob->availableCollections();
    QCOMPARE(cols.size(), 3);  // Unfiled (slot 0) + Family (slot 1) + Customers (slot 5)

    delete provided.blob;
}

void TestContactsBackendPlugin::createConflictHandler_requiresPriorCreateBackends()
{
    ContactsBackendPlugin p;
    auto *handler = p.createConflictHandler();
    QVERIFY(handler == nullptr);
}

void TestContactsBackendPlugin::createConflictHandler_returnsContactsConflictHandler()
{
    MockPalmDatabaseAccess device;
    PalmDeviceConnection conn(&device);

    ContactsBackendPlugin p;
    auto provided = p.createBackends(nullptr, &conn);   // primes m_device
    delete provided.blob;

    auto *handler = p.createConflictHandler();
    QVERIFY(handler != nullptr);
    QVERIFY(dynamic_cast<ContactsConflictHandler *>(handler) != nullptr);
    delete handler;
}

void TestContactsBackendPlugin::enrichConflictSnapshot_extractsFnFromVcard()
{
    ContactsBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    snap.content = QByteArrayLiteral(
        "BEGIN:VCARD\r\n"
        "VERSION:4.0\r\n"
        "FN:Jane Doe\r\n"
        "N:Doe;Jane;;;\r\n"
        "END:VCARD\r\n");

    p.enrichConflictSnapshot(snap, /*isSourceSide=*/false);

    QCOMPARE(snap.metadata.value(QStringLiteral("title")).toString(),
             QStringLiteral("Jane Doe"));
    QCOMPARE(snap.contentType, QStringLiteral("text/vcard"));
}

void TestContactsBackendPlugin::formatConflictRecordHtml_includesTitleAndPre()
{
    ContactsBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    snap.content = QByteArrayLiteral("BEGIN:VCARD\nFN:Jane Doe\nEND:VCARD\n");
    snap.metadata[QStringLiteral("title")] = QStringLiteral("Jane Doe");

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<h3>Jane Doe</h3>")));
    QVERIFY(html.contains(QStringLiteral("<pre>")));
}

QTEST_MAIN(TestContactsBackendPlugin)
#include "tst_contactsbackendplugin.moc"
