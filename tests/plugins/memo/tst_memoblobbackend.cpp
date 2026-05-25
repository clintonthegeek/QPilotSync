#include <QtTest/QtTest>

#include "plugins/memo/memoblobbackend.h"
#include "palm/codecs/memocodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"

using WildPalms::Memo::MemoBlobBackend;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

// Phase 5: MemoBlobBackend presents/consumes raw (note, palm) PalmRecord wire
// bytes. The Palm<->Markdown conversion moved into the shape-graph stages
// (palmnotetransformation), so these tests assert the wire-bytes contract and
// decode the payload to check fields — they no longer expect Markdown.

class TestMemoBlobBackend : public QObject {
    Q_OBJECT
private slots:
    void availableCollectionsExposesOnlyPalmMemo();
    void loadRecordsPresentsPalmWire();
    void createRecordConsumesPalmWirePreservingCategory();
    void updateRecordRoundTripsCategory();
    void deleteRecordPropagatesToDevice();
    void deleteRecord_usesMemoDBCanonicalName();
    void modifiedSinceDelegates();
    void privateFlagPreservedBothDirections();
    void categorySlotCarriedInWireBytes();

private:
    std::uint32_t seedMemo(MockPalmDatabaseAccess *dev,
                           const QString &text,
                           int category,
                           bool isPrivate);
};

std::uint32_t TestMemoBlobBackend::seedMemo(MockPalmDatabaseAccess *dev,
                                            const QString &text,
                                            int category,
                                            bool isPrivate)
{
    PalmRecord pr;
    pr.category = category;
    pr.data = WildPalms::PalmCodecs::encodeMemo({text, isPrivate});
    if (isPrivate) pr.attributes |= PalmRecord::AttrSecret;
    pr.lastModified = QDateTime::currentDateTimeUtc();
    return dev->createRecord("MemoDB", pr);
}

// --- collection exposure ---

void TestMemoBlobBackend::availableCollectionsExposesOnlyPalmMemo()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    dev.createDatabase("DatebookDB");   // other DBs should not surface here
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto cols = mb.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, QStringLiteral("palm:memo"));
    QCOMPARE(cols.first().type, QStringLiteral("memos"));
}

// --- read path ---

void TestMemoBlobBackend::loadRecordsPresentsPalmWire()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("hello\nworld"), 0, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto records = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().type, QStringLiteral("note"));

    const PalmRecord pr = PalmRecord::fromWireBytes(records.first().data);
    const auto pod = WildPalms::PalmCodecs::decodeMemo(pr.data);
    QVERIFY(pod.has_value());
    QCOMPARE(pod->text, QStringLiteral("hello\nworld"));

    // contentHash should match the Palm record's hash.
    const auto palmRec = dev.readAllRecords("MemoDB").first();
    QCOMPARE(records.first().contentHash, palmRec.contentHash());
}

// --- write path ---

void TestMemoBlobBackend::createRecordConsumesPalmWirePreservingCategory()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    // Synthesise a (note, palm) wire BackendRecord as it arrives from the engine
    // after the canon->palm stage. Memo carries category per-record (not per
    // collection slot), so the backend keeps the wire bytes' category.
    PalmRecord seed;
    seed.category = 4;
    seed.data = WildPalms::PalmCodecs::encodeMemo({QStringLiteral("a new memo"), false});

    Kalburator::Sync::BackendRecord br;
    br.type = QStringLiteral("note");
    br.data = seed.toWireBytes();
    const QString newId = mb.createRecord(QStringLiteral("palm:memo"), br);
    QVERIFY(!newId.isEmpty());

    const auto stored = dev.readAllRecords("MemoDB");
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().category, 4);
    const auto decoded = WildPalms::PalmCodecs::decodeMemo(stored.first().data);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QStringLiteral("a new memo"));
}

void TestMemoBlobBackend::updateRecordRoundTripsCategory()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    const auto id = seedMemo(&dev, QStringLiteral("initial"), 2, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    // Load wire bytes, mutate the decoded payload + category, re-serialise.
    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);

    PalmRecord pr = PalmRecord::fromWireBytes(recs.first().data);
    pr.data = WildPalms::PalmCodecs::encodeMemo({QStringLiteral("updated body"), false});
    pr.category = 6;

    Kalburator::Sync::BackendRecord updated = recs.first();
    updated.data = pr.toWireBytes();

    QVERIFY(mb.updateRecord(updated));

    const auto stored = dev.readRecord("MemoDB", id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->category, 6);
    const auto reDecoded = WildPalms::PalmCodecs::decodeMemo(stored->data);
    QVERIFY(reDecoded.has_value());
    QCOMPARE(reDecoded->text, QStringLiteral("updated body"));
}

void TestMemoBlobBackend::deleteRecordPropagatesToDevice()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    const auto id = seedMemo(&dev, QStringLiteral("doomed"), 0, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    QVERIFY(mb.deleteRecord(PalmBackend::encodeRecordId("MemoDB", id)));
    QVERIFY(!dev.readRecord("MemoDB", id).has_value());
}

void TestMemoBlobBackend::modifiedSinceDelegates()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("old"), 0, false);

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-1);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto mods = mb.modifiedSince(QStringLiteral("palm:memo"), cutoff);
    QCOMPARE(mods.size(), 1);
    const PalmRecord pr = PalmRecord::fromWireBytes(mods.first().data);
    const auto pod = WildPalms::PalmCodecs::decodeMemo(pr.data);
    QVERIFY(pod.has_value());
    QCOMPARE(pod->text, QStringLiteral("old"));
}

void TestMemoBlobBackend::privateFlagPreservedBothDirections()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("shh"), 0, /*isPrivate=*/true);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);
    const PalmRecord loaded = PalmRecord::fromWireBytes(recs.first().data);
    QVERIFY((loaded.attributes & PalmRecord::AttrSecret) != 0);

    // Round-trip the wire bytes back to Palm — private flag preserved.
    Kalburator::Sync::BackendRecord br = recs.first();
    br.data = recs.first().data;  // unchanged
    QVERIFY(mb.updateRecord(br));

    const auto stored = dev.readAllRecords("MemoDB");
    QCOMPARE(stored.size(), 1);
    QVERIFY((stored.first().attributes & PalmRecord::AttrSecret) != 0);
}

void TestMemoBlobBackend::categorySlotCarriedInWireBytes()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("work thing"), 3, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);
    const PalmRecord pr = PalmRecord::fromWireBytes(recs.first().data);
    QCOMPARE(static_cast<int>(pr.category), 3);
}

void TestMemoBlobBackend::deleteRecord_usesMemoDBCanonicalName()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    const auto id = seedMemo(&dev, QStringLiteral("memo body"), 0, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const QString encoded = PalmBackend::encodeRecordId(
        QStringLiteral("MemoDB"), id);
    QVERIFY(mb.deleteRecord(encoded));

    const auto remaining = dev.readAllRecords("MemoDB");
    QCOMPARE(remaining.size(), 0);
}

QTEST_MAIN(TestMemoBlobBackend)
#include "tst_memoblobbackend.moc"
