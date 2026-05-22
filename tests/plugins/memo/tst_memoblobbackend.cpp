#include <QtTest/QtTest>

#include "plugins/memo/memoblobbackend.h"
#include "plugins/memo/memomarkdown.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/memocodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"

using WildPalms::Memo::MemoBlobBackend;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

class TestMemoBlobBackend : public QObject {
    Q_OBJECT
private slots:
    void availableCollectionsExposesOnlyPalmMemo();
    void loadRecordsDecodesPalmToMarkdown();
    void createRecordEncodesMarkdownToPalmPreservingCategory();
    void updateRecordRoundTripsCategory();
    void deleteRecordPropagatesToDevice();
    void modifiedSinceDelegates();
    void privateFlagPreservedBothDirections();
    void categoryNameResolvedFromStoreWhenPresent();

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

void TestMemoBlobBackend::loadRecordsDecodesPalmToMarkdown()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("hello\nworld"), 0, false);
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    const auto records = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(records.size(), 1);
    const QString md = QString::fromUtf8(records.first().data);
    QVERIFY(md.contains(QStringLiteral("hello\nworld")));
    QVERIFY(md.startsWith(QStringLiteral("---\n")));
    // contentHash should match the Palm record's hash, not the Markdown bytes
    const auto palmRec = dev.readAllRecords("MemoDB").first();
    QCOMPARE(records.first().contentHash, palmRec.contentHash());
}

// --- write path ---

void TestMemoBlobBackend::createRecordEncodesMarkdownToPalmPreservingCategory()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    PalmBackend pb(&dev);
    MemoBlobBackend mb(&pb);

    // Synthesise a Markdown BackendRecord as if coming from LocalBlobBackend.
    WildPalms::Memo::MarkdownMemo m;
    m.content.text = QStringLiteral("a new memo");
    m.categorySlot = 4;
    const QByteArray mdBytes = WildPalms::Memo::encode(m).toUtf8();

    Kalburator::Sync::BackendRecord br;
    br.type = QStringLiteral("memos");
    br.data = mdBytes;
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

    // Load, mutate via markdown, write back.
    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);

    auto decoded = WildPalms::Memo::decode(QString::fromUtf8(recs.first().data));
    decoded.content.text = QStringLiteral("updated body");
    decoded.categorySlot = 6;

    Kalburator::Sync::BackendRecord updated = recs.first();
    updated.data = WildPalms::Memo::encode(decoded).toUtf8();

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
    QVERIFY(QString::fromUtf8(mods.first().data).contains(QStringLiteral("old")));
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
    QVERIFY(QString::fromUtf8(recs.first().data).contains(QStringLiteral("private: true")));

    // Round-trip the markdown back to Palm — private flag preserved.
    Kalburator::Sync::BackendRecord br = recs.first();
    br.data = recs.first().data;  // unchanged
    QVERIFY(mb.updateRecord(br));

    const auto stored = dev.readAllRecords("MemoDB");
    QCOMPARE(stored.size(), 1);
    QVERIFY((stored.first().attributes & PalmRecord::AttrSecret) != 0);
}

void TestMemoBlobBackend::categoryNameResolvedFromStoreWhenPresent()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("MemoDB");
    seedMemo(&dev, QStringLiteral("work thing"), 3, false);
    PalmBackend pb(&dev);

    WildPalms::PalmCalendar::CategoryMappingStore store;
    store.setSlotName(QStringLiteral("MemoDB"), 3, QStringLiteral("Work"));
    MemoBlobBackend mb(&pb, &store);

    const auto recs = mb.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(recs.size(), 1);
    const QString md = QString::fromUtf8(recs.first().data);
    QVERIFY(md.contains(QStringLiteral("category: 3")));
    QVERIFY(md.contains(QStringLiteral("categoryName: Work")));
}

QTEST_MAIN(TestMemoBlobBackend)
#include "tst_memoblobbackend.moc"
