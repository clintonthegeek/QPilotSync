#include <QtTest/QtTest>

#include <KCalendarCore/Event>

#include "datebookcodec.h"
#include "palmrecord.h"

using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::PalmRecord;

class TestDatebookCodec : public QObject
{
    Q_OBJECT
private slots:
    void deletedRecordDecodesToFailure();
    void emptyBytesDecodeToFailure();
    void recordIdPreservedThroughDecode();
    void categorySlotPreservedThroughDecode();
    void encodeRespectsSlotParameter();
    void encodeClampsOutOfRangeSlot();
    void encodeReadsRecordIdFromProperty();
    void roundTripMinimalEventPreservesRecordId();
};

namespace {

/// Build a minimal PalmRecord with known bytes by encoding a trivial event.
/// This gives us a starting byte sequence for decode tests.
PalmRecord makeMinimalRecord(std::uint32_t recordId,
                             std::uint8_t category = 0,
                             std::uint8_t attributes = 0)
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("minimal"));
    auto rec = DatebookCodec::encode(ev, category);
    rec.recordId   = recordId;
    rec.attributes = attributes;
    return rec;
}

} // namespace

void TestDatebookCodec::deletedRecordDecodesToFailure()
{
    PalmRecord rec = makeMinimalRecord(1, 0, PalmRecord::AttrDeleted);
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(!result.isValid());
    QCOMPARE(result.failureReason, QStringLiteral("deleted"));
}

void TestDatebookCodec::emptyBytesDecodeToFailure()
{
    PalmRecord rec;
    rec.recordId = 1;
    rec.data.clear();
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(!result.isValid());
    QCOMPARE(result.failureReason, QStringLiteral("empty-record"));
}

void TestDatebookCodec::recordIdPreservedThroughDecode()
{
    PalmRecord rec = makeMinimalRecord(42);
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(result.isValid());
    QCOMPARE(result.event->customProperty(
                 "KCalendarCore",
                 QByteArray(DatebookCodec::RecordIdProperty)),
             QStringLiteral("42"));
}

void TestDatebookCodec::categorySlotPreservedThroughDecode()
{
    PalmRecord rec = makeMinimalRecord(1, /*category=*/7);
    const auto result = DatebookCodec::decode(rec);
    QVERIFY(result.isValid());
    QCOMPARE(result.slot, 7);
    QCOMPARE(result.event->customProperty(
                 "KCalendarCore",
                 QByteArray(DatebookCodec::CategorySlotProperty)),
             QStringLiteral("7"));
}

void TestDatebookCodec::encodeRespectsSlotParameter()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    const auto rec = DatebookCodec::encode(ev, /*slot=*/11);
    QCOMPARE(static_cast<int>(rec.category), 11);
}

void TestDatebookCodec::encodeClampsOutOfRangeSlot()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    QCOMPARE(static_cast<int>(DatebookCodec::encode(ev, -5).category), 0);
    QCOMPARE(static_cast<int>(DatebookCodec::encode(ev, 99).category), 15);
}

void TestDatebookCodec::encodeReadsRecordIdFromProperty()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setCustomProperty("KCalendarCore",
                          QByteArray(DatebookCodec::RecordIdProperty),
                          QStringLiteral("99"));
    const auto rec = DatebookCodec::encode(ev, 0);
    QCOMPARE(rec.recordId, 99u);
}

void TestDatebookCodec::roundTripMinimalEventPreservesRecordId()
{
    PalmRecord original = makeMinimalRecord(123, /*category=*/5);
    const auto decoded = DatebookCodec::decode(original);
    QVERIFY(decoded.isValid());

    const auto re = DatebookCodec::encode(decoded.event, decoded.slot);
    QCOMPARE(re.recordId, 123u);
    QCOMPARE(static_cast<int>(re.category), 5);
}

QTEST_MAIN(TestDatebookCodec)
#include "tst_datebookcodec.moc"
