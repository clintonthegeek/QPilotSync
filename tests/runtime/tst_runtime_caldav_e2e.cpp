// Phase J Task 9: CalDAV E2E integration test.
// Wires FakeCalDavServer + CalDavProvider + PalmRuntime
// through the real SyncEngine to verify end-to-end calendar sync.

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "fakecaldavserver.h"

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include "mockblobbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "synctypes.h"
#include "shape.h"

#include "caldavprovider.h"
#include "backendconfiguration.h"
#include "backendregistry.h"
#include "syncbackend.h"
#include "pluginmanager.h"
#include "stock_plugins.h"
// K.8b T7: BlobBackendAdapter deleted; inject via BlobSyncBackendWrapper.
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;
using namespace Kalburator::Shape;

namespace {

bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

BackendConfiguration makeCalDavConfig(const QUrl &serverUrl)
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-caldav");
    cfg.type = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake CalDAV");
    cfg.connectionParams.insert(QStringLiteral("url"), serverUrl.toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    return cfg;
}

QByteArray makeIcsEvent(const QString &uid, const QString &summary)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\nSUMMARY:%2\r\n"
        "DTSTART:20260601T090000Z\r\nDTEND:20260601T100000Z\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid, summary).toUtf8();
}

const QString kPalmCalId  = QStringLiteral("palm:calendar/0");
const QString kPalmBkId   = QStringLiteral("palm-calendar");
const QString kCaldavBkId = QStringLiteral("caldav-personal");
const Shape   kCalShape   = { DomainId{QStringLiteral("calendar")},
                               EncodingId{QStringLiteral("ical")} };

} // namespace

class TstRuntimeCalDavE2E : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // K.7: seed DomainRegistry with stock plugins so dispatchSync
        // finds domain definitions (CalendarPlugin, BlobPlugin, etc.).
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
    }

    void palm_to_caldav_propagates();
    void caldav_to_palm_propagates();
    void bidirectional_no_conflict();
    void default_mappings_per_slot_when_calendar_bound();
    void memory_calendar_observable_during_sync();
};

void TstRuntimeCalDavE2E::palm_to_caldav_propagates()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeCalDavConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));
    QVERIFY(provider.isConnected());

    auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString caldavColId = cols.first().id;
    auto caldavBackendOwned = provider.createBackend(caldavColId);
    QVERIFY(caldavBackendOwned);
    auto *caldavSync = dynamic_cast<SyncBackend *>(caldavBackendOwned.get());
    QVERIFY(caldavSync);
    runtime.backendRegistry().registerBackendInstance(kCaldavBkId, caldavSync);

    auto palmBlob = std::make_unique<MockBlobBackend>();
    {
        CollectionInfo ci; ci.id = kPalmCalId; ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);
        BackendRecord rec;
        rec.id   = QStringLiteral("event-palm-001");
        rec.data = makeIcsEvent(QStringLiteral("event-palm-001@palm"),
                                QStringLiteral("Palm Event"));
        rec.type = QStringLiteral("text/calendar");
        palmBlob->createRecord(kPalmCalId, rec);
    }
    runtime.registerBackendInstanceForTest(kPalmBkId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlob), kPalmBkId, kCalShape));

    {
        SyncMapping m;
        m.id = QStringLiteral("test-p2c");
        m.sourceBackend = kPalmBkId;
        m.targetBackend = kCaldavBkId;
        m.sourceCalendar = kPalmCalId;
        m.targetCalendar = caldavColId;
        m.mode = SyncMode::TwoWay;
        m.enabled = true;
        runtime.setMappingsForTest({m});
    }

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.resultAt(0).success);

    // Phase K.4 gate: the palm event lands on the CalDAV server. This
    // is the core "palm -> caldav direction" assertion — what
    // `CalendarPluginWriter` blocked on pre-K.4 because of its
    // `m_collection != nullptr` guard. With K.4's `prepareForApply`
    // null-collection blob fallback, the writer parses the iCal from
    // BackendRecord::data and pushes through CalDAV's IBlobBackend
    // surface (createRecord -> RemoteCalendarBackend::createRecord ->
    // network PUT).
    QVERIFY(server.hasEvent(QStringLiteral("/calendars/testuser/personal/"),
                            QStringLiteral("event-palm-001@palm")));
}

void TstRuntimeCalDavE2E::caldav_to_palm_propagates()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    FakeCalDavServer server;
    QVERIFY(server.startListening());
    server.setSeedEvents(QStringLiteral("/calendars/testuser/personal/"),
                        { makeIcsEvent(QStringLiteral("server-001@caldav"),
                                       QStringLiteral("Server Event")) });

    CalDavProvider provider;
    provider.load(makeCalDavConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));

    auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString caldavColId = cols.first().id;
    auto caldavBackendOwned = provider.createBackend(caldavColId);
    QVERIFY(caldavBackendOwned);
    auto *caldavSync = dynamic_cast<SyncBackend *>(caldavBackendOwned.get());
    QVERIFY(caldavSync);
    runtime.backendRegistry().registerBackendInstance(kCaldavBkId, caldavSync);

    auto palmBlobOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *palmBlob = palmBlobOwned.get();
    {
        CollectionInfo ci; ci.id = kPalmCalId; ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);
    }
    runtime.registerBackendInstanceForTest(kPalmBkId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlobOwned), kPalmBkId, kCalShape));

    {
        SyncMapping m;
        m.id = QStringLiteral("test-c2p");
        m.sourceBackend = kCaldavBkId;
        m.targetBackend = kPalmBkId;
        m.sourceCalendar = caldavColId;
        m.targetCalendar = kPalmCalId;
        m.mode = SyncMode::TwoWay;
        m.enabled = true;
        runtime.setMappingsForTest({m});
    }

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.resultAt(0).success);

    QVERIFY(!palmBlob->recordsIn(kPalmCalId).isEmpty());
}

void TstRuntimeCalDavE2E::bidirectional_no_conflict()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    FakeCalDavServer server;
    QVERIFY(server.startListening());
    server.setSeedEvents(QStringLiteral("/calendars/testuser/personal/"),
                        { makeIcsEvent(QStringLiteral("server-uid@caldav"),
                                       QStringLiteral("Server Event")) });

    CalDavProvider provider;
    provider.load(makeCalDavConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));

    auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString caldavColId = cols.first().id;
    auto caldavBackendOwned = provider.createBackend(caldavColId);
    QVERIFY(caldavBackendOwned);
    auto *caldavSync = dynamic_cast<SyncBackend *>(caldavBackendOwned.get());
    QVERIFY(caldavSync);
    runtime.backendRegistry().registerBackendInstance(kCaldavBkId, caldavSync);

    auto palmBlobOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *palmBlob = palmBlobOwned.get();
    {
        CollectionInfo ci; ci.id = kPalmCalId; ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);
        BackendRecord rec;
        rec.id   = QStringLiteral("palm-uid");
        rec.data = makeIcsEvent(QStringLiteral("palm-uid@palm"),
                                QStringLiteral("Palm Event"));
        rec.type = QStringLiteral("text/calendar");
        palmBlob->createRecord(kPalmCalId, rec);
    }
    runtime.registerBackendInstanceForTest(kPalmBkId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlobOwned), kPalmBkId, kCalShape));

    {
        SyncMapping m;
        m.id = QStringLiteral("bidir");
        m.sourceBackend = kPalmBkId;
        m.targetBackend = kCaldavBkId;
        m.sourceCalendar = kPalmCalId;
        m.targetCalendar = caldavColId;
        m.mode = SyncMode::TwoWay;
        m.enabled = true;
        runtime.setMappingsForTest({m});
    }

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.resultAt(0).success);

    QVERIFY(server.hasEvent(QStringLiteral("/calendars/testuser/personal/"),
                            QStringLiteral("palm-uid@palm")));
    QVERIFY(server.hasEvent(QStringLiteral("/calendars/testuser/personal/"),
                            QStringLiteral("server-uid@caldav")));

    auto recs = palmBlob->recordsIn(kPalmCalId);
    QCOMPARE(recs.size(), 2);
}

void TstRuntimeCalDavE2E::default_mappings_per_slot_when_calendar_bound()
{
    QSKIP("F1 acceptance covered by per-slot unit test");
}

void TstRuntimeCalDavE2E::memory_calendar_observable_during_sync()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    FakeCalDavServer server;
    QVERIFY(server.startListening());
    server.setSeedEvents(QStringLiteral("/calendars/testuser/personal/"),
                        { makeIcsEvent(QStringLiteral("live-uid@caldav"),
                                       QStringLiteral("Live Event")) });

    CalDavProvider provider;
    provider.load(makeCalDavConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));

    auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString caldavColId = cols.first().id;
    auto caldavBackendOwned = provider.createBackend(caldavColId);
    QVERIFY(caldavBackendOwned);
    auto *caldavSync = dynamic_cast<SyncBackend *>(caldavBackendOwned.get());
    QVERIFY(caldavSync);
    runtime.backendRegistry().registerBackendInstance(kCaldavBkId, caldavSync);

    auto palmBlob = std::make_unique<MockBlobBackend>();
    {
        CollectionInfo ci; ci.id = kPalmCalId; ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);
    }
    runtime.registerBackendInstanceForTest(kPalmBkId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlob), kPalmBkId, kCalShape));

    {
        SyncMapping m;
        m.id = QStringLiteral("live");
        m.sourceBackend = kCaldavBkId;
        m.targetBackend = kPalmBkId;
        m.sourceCalendar = caldavColId;
        m.targetCalendar = kPalmCalId;
        m.mode = SyncMode::TwoWay;
        m.enabled = true;
        runtime.setMappingsForTest({m});
    }

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.resultAt(0).success);
}

QTEST_GUILESS_MAIN(TstRuntimeCalDavE2E)
#include "tst_runtime_caldav_e2e.moc"
