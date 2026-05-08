// Phase Ia Task 19: engine-level palm <-> vcard4 sync integration test.
//
// This is the **diagnostic** integration test the entire phase was
// designed to deliver. It pressure-tests the SyncEngine's behaviour
// when the source backend declares native shape (contacts, palm) and
// the target backend declares native shape (contacts, vcard4) — i.e.
// a cross-shape mapping for a non-calendar domain, with a registered
// transformation edge already present in TransformationRegistry.
//
// What this test asserts (and why)
// ================================
//
// Setup:
//   - Source SyncBackend: nativeShapes() = (contacts, palm), holds a
//     single record whose `data` is the **wire-bytes** payload of a
//     known PalmRecord (created via PalmRecord::toWireBytes — the
//     contract pinned by tst_palmrecord and tst_palmtovcardtransformation).
//   - Target SyncBackend: nativeShapes() = (contacts, vcard4), starts
//     empty.
//   - ContactsDomainExtension::registerWith(...) is called in
//     initTestCase so the palm <-> vcard4 edges are present.
//   - SyncMapping: source -> target, OneWayUpload (forces dispatch to
//     target without bidirectional baseline complications).
//
// The test asserts the **architectural reality of the engine as of
// Phase Ia Task 18**:
//
//   1. The engine routes through dispatchBlobSync because
//      sourceBackend->nativeShapes().first().domain != "calendar"
//      (syncengine.cpp:1457-1465).
//
//   2. dispatchBlobSync fetches source bytes, fetches target bytes,
//      diffs them by content hash, and applies the resulting changes
//      to the destination backend **verbatim** (no transformation
//      stage is invoked between source.read and target.write).
//
//   3. Therefore the target receives the exact source byte-payload —
//      the palm-wire-bytes blob — NOT a vCard 4.0 representation,
//      even though the registered TransformationRegistry contains a
//      palm -> vcard4 edge that, if invoked, would produce
//      "BEGIN:VCARD\nVERSION:4.0\n…\nFN:<name>\nEND:VCARD\n".
//
//   4. The TransformationRegistry IS consulted: the engine calls
//      `registry.inspect(srcShape, tgtShape)` and forwards the
//      resulting LossProfile to the host's syncStarted notification
//      (syncengine.cpp:1441-1451). This proves the engine knows the
//      mapping is cross-shape — it just doesn't act on that knowledge
//      to invoke a Pipeline.
//
// This test pins that gap. When Phase Ib lands (engine actually
// invokes Pipeline at the edge), the assertions in
// `enginePassesPalmBytesThroughVerbatim_diagnostic` flip to assert
// the target receives vcard4 bytes (FN:, VERSION:4.0, etc). The
// flip is the "fix landed" signal.
//
// Why we can't do better today (without a libkalburator-side fix)
// ===============================================================
//
// SyncEngineWorker::dispatchBlobSync (syncengine.cpp:1775-1926)
// hardwires:
//
//     sourceRecords = srcBlob->loadRecords(srcColId);   // raw bytes
//     ...
//     for each rec in toWrite:
//         tgtBlob->createRecord(tgtColId, rec);          // raw bytes
//
// There is no `compile()` against the registry, no `Pipeline::run()`.
// Adding that is the Phase Ib delta. Hacking around it from inside a
// test would not pressure-test what we want to pressure-test.
//
// Per the task spec: "if the engine routing requires production-code
// changes to actually invoke the registered edge ... surface
// DONE_WITH_CONCERNS" — the resolution is to land this test as the
// pinned diagnostic of the gap, plus document the finding in the
// Phase Ia status doc's Discoveries section.

#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QFutureWatcher>

#include "backendrecord.h"
#include "backendregistry.h"
#include "blobbaselinestore.h"
#include "syncbackend.h"
#include "collectioninfo.h"
#include "domainregistry.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "shape.h"
#include "syncengine.h"
#include "synctypes.h"
#include "transformationregistry.h"

#include "palm/sync/palmrecord.h"
#include "contactsdomainextension.h"

#include <KCalendarCore/MemoryCalendar>

#include <QTemporaryDir>

#include <memory>

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BlobBaselineStore;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncEngine;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;
using Kalburator::Shape::TransformationRegistry;
using WildPalms::ContactsPlugin::ContactsDomainExtension;
using WildPalms::PalmSync::PalmRecord;

namespace {

constexpr int kSyncTimeoutMs = 5000;

// ──────────────────────────────────────────────────────────────────────────────
// ShapedTestBackend
// ──────────────────────────────────────────────────────────────────────────────
// SyncBackend subclass that stores a fixed list of BackendRecords in a
// single collection. The shape is configurable per-instance so the
// test can declare (contacts, palm) and (contacts, vcard4) sides
// against the same implementation.
//
// Mirrors libkalburator's MockBlobBackend in spirit, but is itself a
// SyncBackend (which MockBlobBackend is NOT — MockBlobBackend is only
// an IBlobBackend; the engine's dispatchBlobSync gates on
// nativeShapes() which only SyncBackend exposes).
//
// Calendar pure-virtuals are stubs — never invoked via dispatchBlobSync,
// matching the BlobBackendAdapter pattern in WP's PalmRuntime.
// ──────────────────────────────────────────────────────────────────────────────
class ShapedTestBackend final : public SyncBackend
{
    Q_OBJECT
public:
    ShapedTestBackend(const QString &id,
                      Shape shape,
                      const QString &collectionId,
                      QObject *parent = nullptr)
        : SyncBackend(parent)
        , m_id(id)
        , m_shape(shape)
        , m_collectionId(collectionId)
    {
        CollectionInfo info;
        info.id   = collectionId;
        info.name = collectionId;
        info.type = QStringLiteral("contacts");
        m_collections.append(info);
    }

    // SyncBackend identity
    QString backendType() const override { return m_id; }
    QList<Shape> nativeShapes() const override { return { m_shape }; }

    // IBlobBackend identity
    QString backendId()   const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool    isAvailable() const override { return true; }

    // Collections
    QList<CollectionInfo> availableCollections() override { return m_collections; }
    CollectionInfo collectionInfo(const QString &id) override
    {
        for (const auto &c : m_collections)
            if (c.id == id) return c;
        return {};
    }
    QString createCollection(const CollectionInfo &info) override
    {
        m_collections.append(info);
        return info.id;
    }

    // Records
    QList<BackendRecord> loadRecords(const QString &colId) override
    {
        Q_UNUSED(colId);
        ++m_loadRecordsCalls;
        return m_records;
    }
    std::optional<BackendRecord> loadRecord(const QString &id) override
    {
        for (const auto &r : m_records)
            if (r.id == id) return r;
        return std::nullopt;
    }
    QString createRecord(const QString &colId,
                         const BackendRecord &record) override
    {
        Q_UNUSED(colId);
        ++m_createRecordCalls;
        m_lastCreated = record;
        m_records.append(record);
        return record.id;
    }
    bool updateRecord(const BackendRecord &record) override
    {
        ++m_updateRecordCalls;
        for (auto &r : m_records) {
            if (r.id == record.id) { r = record; return true; }
        }
        return false;
    }
    bool deleteRecord(const QString &id) override
    {
        for (int i = 0; i < m_records.size(); ++i) {
            if (m_records[i].id == id) { m_records.removeAt(i); return true; }
        }
        return false;
    }

    QList<BackendRecord> modifiedSince(const QString &, const QDateTime &) override { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }
    bool supportsDeleteTracking() const override { return true; }

    // Calendar pure-virtuals — stubs; dispatchBlobSync never calls these.
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &,
        const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }

    // Test seams
    void seedRecord(const BackendRecord &record) { m_records.append(record); }
    QList<BackendRecord> snapshot() const { return m_records; }
    int createRecordCalls() const { return m_createRecordCalls; }
    int updateRecordCalls() const { return m_updateRecordCalls; }
    int loadRecordsCalls() const { return m_loadRecordsCalls; }
    BackendRecord lastCreated() const { return m_lastCreated; }

private:
    QString m_id;
    Shape m_shape;
    QString m_collectionId;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord> m_records;
    int m_createRecordCalls = 0;
    int m_updateRecordCalls = 0;
    int m_loadRecordsCalls = 0;
    BackendRecord m_lastCreated;
};

// ──────────────────────────────────────────────────────────────────────────────
// TestSyncHost
// ──────────────────────────────────────────────────────────────────────────────
// Minimal ISyncHost backed by BackendRegistry. Mirrors PalmRuntime's
// PalmSyncHost. recordChanged is a no-op; this test asserts on the
// target backend's createRecord call count instead.
//
// Captures the LossProfile passed to syncStarted — proving the engine
// consulted the registry for the cross-shape mapping.
// ──────────────────────────────────────────────────────────────────────────────
class TestSyncHost final : public ISyncHost
{
public:
    explicit TestSyncHost(BackendRegistry *registry) : m_registry(registry) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? m_registry->backendInstance(id) : nullptr;
    }
    QHash<QString, SyncBackend *> backends() override
    {
        QHash<QString, SyncBackend *> out;
        if (!m_registry) return out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, m_registry->backendInstance(id));
        return out;
    }
    ISyncConfigStore *configStore() override { return nullptr; }

    void syncStarted(const QString &mappingId, const LossProfile &loss) override
    {
        m_lastSyncStartedMappingId = mappingId;
        m_lastLossProfile = loss;
        m_syncStartedCount++;
    }

    void recordChanged(const QString &, const QString &, ChangeKind) override {}

    int syncStartedCount() const { return m_syncStartedCount; }
    LossProfile lastLossProfile() const { return m_lastLossProfile; }

private:
    BackendRegistry *m_registry = nullptr;
    int m_syncStartedCount = 0;
    LossProfile m_lastLossProfile;
    QString m_lastSyncStartedMappingId;
};

PalmRecord makeKnownPalmRecord()
{
    PalmRecord pr;
    pr.recordId    = 0xC0FFEE;
    pr.category    = 0;
    pr.attributes  = 0;
    // Palm AddressDB encodes records with pi-address; the
    // PalmToVCardStage path decodes via decodeContact then encodes via
    // KContacts::VCardConverter v4. For Task 19 we don't need a fully
    // round-trippable AddressDB blob — we just need a payload whose
    // wire-bytes round-trip through PalmRecord::fromWireBytes (the
    // engine path doesn't decode .data, only diffs by content hash).
    // The diagnostic assertion that matters is the byte equality at
    // the target side, which is satisfied by any non-empty payload.
    //
    // We embed a recognizable substring so the negative assertion
    // ("BEGIN:VCARD" must NOT appear in target.data") has teeth.
    pr.data = QByteArrayLiteral(
        "PalmAddressDB-Contact-Doe-Jane-PHASE-IA-TASK-19");
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return pr;
}

BackendRecord wrapPalmRecordForBackend(const PalmRecord &pr,
                                        const QString &recordId)
{
    BackendRecord rec;
    rec.id          = recordId;
    rec.type        = QStringLiteral("contact");
    rec.displayName = QStringLiteral("Doe, Jane");
    rec.data        = pr.toWireBytes();
    // SHA-1 hash over the wire bytes, matching how production blob
    // backends populate contentHash. The blob adapter diffs by hash;
    // an empty hash would compare equal across all records.
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha1).toHex());
    rec.isDeleted   = false;
    return rec;
}

} // namespace

class TestContactsPalmEngineSync : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    // Diagnostic test pinning the architectural gap (see file-level
    // comment). On Phase Ib landing, the assertions flip to verify
    // the target receives transformed vcard4 bytes.
    void enginePassesPalmBytesThroughVerbatim_diagnostic();
};

void TestContactsPalmEngineSync::initTestCase()
{
    // Register palm <-> vcard4 edges in the process-wide registry
    // BEFORE any SyncEngine is constructed for this test. The engine's
    // shape-resolution path (syncengine.cpp:1441-1451) consults this
    // registry to compute the LossProfile passed to ISyncHost::syncStarted.
    auto &reg = TransformationRegistry::instance();
    // Pre-register the vcard4 endpoint that ContactsDomainExtension
    // depends on (matches tst_contactsdomainextension's pattern: in
    // WP-only test envs, libkalburator's KalburatorDomainContacts
    // static registrar may not have run, so we stub vcard4 ourselves).
    const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
    reg.registerShape(v4, {});
    ContactsDomainExtension::registerWith(reg);
}

void TestContactsPalmEngineSync::cleanup()
{
    // Process-wide registry must be reset between test classes that
    // touch it; the contacts plugin tests in this directory share
    // process state via QTEST_MAIN. Phase F2 FINDINGS: registry is a
    // process-wide singleton and leaks across tests otherwise.
    TransformationRegistry::instance().clear();
    Kalburator::Shape::DomainRegistry::instance().clear();
}

void TestContactsPalmEngineSync::enginePassesPalmBytesThroughVerbatim_diagnostic()
{
    // ── Arrange ───────────────────────────────────────────────────────
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    BackendRegistry registry;

    const QString sourceBackendId  = QStringLiteral("palm-source");
    const QString targetBackendId  = QStringLiteral("vcard4-target");
    const QString sourceCollection = QStringLiteral("palm-contacts");
    const QString targetCollection = QStringLiteral("pc-contacts-vcard4");
    const QString mappingId        = QStringLiteral("ia-task19-mapping");

    const Shape palmShape { DomainId{"contacts"}, EncodingId{"palm"}   };
    const Shape v4Shape   { DomainId{"contacts"}, EncodingId{"vcard4"} };

    auto source = std::make_unique<ShapedTestBackend>(
        sourceBackendId, palmShape, sourceCollection);
    auto target = std::make_unique<ShapedTestBackend>(
        targetBackendId, v4Shape, targetCollection);

    // Seed the source with one known PalmRecord wire-bytes payload.
    const PalmRecord pr = makeKnownPalmRecord();
    const BackendRecord seeded = wrapPalmRecordForBackend(
        pr, QStringLiteral("rec-1"));
    source->seedRecord(seeded);

    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    TestSyncHost host(&registry);

    SyncEngine engine(&registry, &host);
    // Wire BlobBaselineStore so dispatchBlobSync's baseline-write path
    // doesn't no-op silently. Path is per-test; cleaned by tmpDir.
    BlobBaselineStore baselines(
        tmpDir.filePath(QStringLiteral("baselines.db")));
    engine.setBlobBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id              = mappingId;
    mapping.sourceBackend   = sourceBackendId;
    mapping.sourceCalendar  = sourceCollection;
    mapping.targetBackend   = targetBackendId;
    mapping.targetCalendar  = targetCollection;
    // OneWayUpload pushes source -> target only, simplifying
    // assertions. (TwoWay would also drive the same path here, but
    // OneWayUpload eliminates the bidirectional baseline noise.)
    mapping.mode            = SyncMode::OneWayUpload;
    mapping.conflictPolicy  = ConflictResolution::SourceWins;
    mapping.enabled         = true;
    engine.setSyncMappings({ mapping });

    // ── Act ───────────────────────────────────────────────────────────
    auto future = engine.runSyncFuture(
        mappingId, SyncEngine::SyncBehavior::Unmonitored);

    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY2(future.isFinished(),
             "SyncEngine::runSyncFuture did not complete within timeout");
    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // ── Assert: engine consulted TransformationRegistry ───────────────
    // The host's syncStarted received a LossProfile computed via
    // TransformationRegistry::inspect(palm, vcard4). Per
    // palmToVCardLoss() in palmtovcardtransformation.cpp, this is
    // Lossless (palm -> vcard4 is the lossless direction). Asserting
    // we receive a Lossless profile (rather than the default
    // unregistered "no path" sentinel) proves the engine consulted
    // the registry and found our edge.
    QCOMPARE(host.syncStartedCount(), 1);
    const LossProfile loss = host.lastLossProfile();
    QCOMPARE(loss.level, Kalburator::Shape::LossLevel::Lossless);

    // ── Assert: dispatch route is dispatchBlobSync ────────────────────
    // Implicit — sourceBackend->nativeShapes().first().domain ==
    // "contacts" != "calendar" forces dispatchBlobSync. The behaviour
    // below (hash-only diff, raw byte push) is the dispatchBlobSync
    // signature; if the engine ever routes contacts via the calendar
    // adapter, the loadRecords + createRecord call counts below would
    // diverge.

    // ── Assert: target received exactly one createRecord ──────────────
    QCOMPARE(target->createRecordCalls(), 1);
    QCOMPARE(target->updateRecordCalls(), 0);
    const QList<BackendRecord> targetSnapshot = target->snapshot();
    QCOMPARE(targetSnapshot.size(), 1);

    // ── Assert: target record bytes are PalmRecord wire-bytes (NOT vcard4) ──
    //
    // **This is the diagnostic assertion.** With the current engine,
    // dispatchBlobSync pushes source.data verbatim. The target
    // therefore sees PalmRecord::toWireBytes output, not a vCard 4.0
    // payload.
    //
    // When Phase Ib lands (engine compiles & runs Pipeline at the
    // edge), this assertion flips to assert "BEGIN:VCARD" present and
    // "VERSION:4.0" present. The flip is the "fix landed" signal.
    const BackendRecord arrived = targetSnapshot.first();
    QCOMPARE(arrived.id, seeded.id);

    // Source bytes round-trip through PalmRecord::fromWireBytes —
    // proves the test seed is genuinely wire-bytes, not just any
    // arbitrary blob.
    const PalmRecord roundTripped =
        PalmRecord::fromWireBytes(seeded.data);
    QCOMPARE(roundTripped.recordId, pr.recordId);
    QCOMPARE(roundTripped.data, pr.data);

    // Bytes pushed to target are byte-equal to the source's wire
    // bytes — no transformation occurred.
    QCOMPARE(arrived.data, seeded.data);

    // Target bytes do NOT contain vCard markers. (They're a
    // QDataStream blob + an opaque payload, neither of which contains
    // "BEGIN:VCARD" or "VERSION:4.0".)
    QVERIFY2(!arrived.data.contains(QByteArrayLiteral("BEGIN:VCARD")),
             "Target received vCard bytes — engine has been "
             "upgraded to invoke transformations. Flip this test's "
             "diagnostic assertions to assert vcard4 shape and "
             "remove this guard.");
    QVERIFY2(!arrived.data.contains(QByteArrayLiteral("VERSION:4.0")),
             "Target received vCard 4.0 bytes — see comment above.");

    // ── Assert: target bytes round-trip through PalmRecord ────────────
    // (Confirming the gap: the target backend is supposedly a vcard4
    // store but is being fed palm wire-bytes. A real vcard4 backend
    // would reject these on the next sync; this test pins that the
    // current engine cannot tell the difference.)
    const PalmRecord targetRoundTrip =
        PalmRecord::fromWireBytes(arrived.data);
    QCOMPARE(targetRoundTrip.recordId, pr.recordId);
    QCOMPARE(targetRoundTrip.data, pr.data);
}

QTEST_MAIN(TestContactsPalmEngineSync)
#include "tst_contacts_palm_engine_sync.moc"
