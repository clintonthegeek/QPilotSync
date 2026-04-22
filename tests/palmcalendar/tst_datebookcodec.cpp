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
    void roundTripAllDayEventSummaryAndDate();
    void roundTripTimedEventDescription();
    void roundTripAlarmMinutes();
    void roundTripAlarmHoursAndDays();
    void roundTripWeeklyRepeatWithDaysOfWeek();
    void roundTripDailyRepeatWithFiniteEnd();
    void roundTripYearlyRepeatForever();
    void roundTripExceptionDates();
    void roundTripPrivateFlag();
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

namespace {

/// Encode then decode and return the resulting event. This exercises
/// the full codec pipeline in one call.
KCalendarCore::Event::Ptr roundTripThroughBytes(
    const KCalendarCore::Event::Ptr &input, int slot,
    std::uint8_t *attrsOut = nullptr)
{
    const auto rec = DatebookCodec::encode(input, slot);
    if (attrsOut) *attrsOut = rec.attributes;
    auto withId = rec;
    withId.recordId = 1;  // decode needs a non-zero record ID for a valid UID.
    const auto result = DatebookCodec::decode(withId);
    return result.isValid() ? result.event : KCalendarCore::Event::Ptr{};
}

} // namespace

void TestDatebookCodec::roundTripAllDayEventSummaryAndDate()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Holiday"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QCOMPARE(rt->summary(), QStringLiteral("Holiday"));
    QVERIFY(rt->allDay());
    QCOMPARE(rt->dtStart().date(), QDate(2026, 6, 1));
}

void TestDatebookCodec::roundTripTimedEventDescription()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Standup"));
    ev->setDescription(QStringLiteral("Daily team sync"));
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(9, 30), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(9, 45), Qt::LocalTime));

    const auto rt = roundTripThroughBytes(ev, 3);
    QVERIFY(rt);
    QCOMPARE(rt->summary(),     QStringLiteral("Standup"));
    QCOMPARE(rt->description(), QStringLiteral("Daily team sync"));
    QVERIFY(!rt->allDay());
    QCOMPARE(rt->dtStart().time(), QTime(9, 30));
    QCOMPARE(rt->dtEnd().time(),   QTime(9, 45));
}

void TestDatebookCodec::roundTripAlarmMinutes()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Meeting"));
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(10, 0), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(10, 30), Qt::LocalTime));
    auto alarm = ev->newAlarm();
    alarm->setType(KCalendarCore::Alarm::Display);
    alarm->setEnabled(true);
    alarm->setStartOffset(KCalendarCore::Duration(-15 * 60));

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QCOMPARE(rt->alarms().size(), 1);
    QCOMPARE(rt->alarms().first()->startOffset().asSeconds(), -15 * 60);
}

void TestDatebookCodec::roundTripAlarmHoursAndDays()
{
    auto ev1 = KCalendarCore::Event::Ptr::create();
    ev1->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(12, 0), Qt::LocalTime));
    ev1->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(13, 0), Qt::LocalTime));
    auto a1 = ev1->newAlarm();
    a1->setType(KCalendarCore::Alarm::Display);
    a1->setEnabled(true);
    a1->setStartOffset(KCalendarCore::Duration(-2 * 60 * 60));  // 2 hours
    const auto rt1 = roundTripThroughBytes(ev1, 0);
    QVERIFY(rt1);
    QCOMPARE(rt1->alarms().first()->startOffset().asSeconds(), -2 * 60 * 60);

    auto ev2 = KCalendarCore::Event::Ptr::create();
    ev2->setDtStart(QDateTime(QDate(2026, 6, 2), QTime(12, 0), Qt::LocalTime));
    ev2->setDtEnd  (QDateTime(QDate(2026, 6, 2), QTime(13, 0), Qt::LocalTime));
    auto a2 = ev2->newAlarm();
    a2->setType(KCalendarCore::Alarm::Display);
    a2->setEnabled(true);
    a2->setStartOffset(KCalendarCore::Duration(-3 * 24 * 60 * 60));  // 3 days
    const auto rt2 = roundTripThroughBytes(ev2, 0);
    QVERIFY(rt2);
    QCOMPARE(rt2->alarms().first()->startOffset().asSeconds(),
             -3 * 24 * 60 * 60);
}

void TestDatebookCodec::roundTripWeeklyRepeatWithDaysOfWeek()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("MWF gym"));
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(7, 0), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 6, 1), QTime(8, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setWeekly(1);
    QBitArray days(7);
    days.setBit(0);  // Mon (KCal convention)
    days.setBit(2);  // Wed
    days.setBit(4);  // Fri
    rec->addWeeklyDays(days);

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    QCOMPARE(rt->recurrence()->recurrenceType(),
             KCalendarCore::Recurrence::rWeekly);
    const auto rtDays = rt->recurrence()->days();
    QVERIFY(rtDays.testBit(0));
    QVERIFY(rtDays.testBit(2));
    QVERIFY(rtDays.testBit(4));
    QVERIFY(!rtDays.testBit(1));
}

void TestDatebookCodec::roundTripDailyRepeatWithFiniteEnd()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Vitamin"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setDaily(1);
    rec->setEndDateTime(
        QDateTime(QDate(2026, 6, 30), QTime(0, 0), Qt::LocalTime));

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    QCOMPARE(rt->recurrence()->recurrenceType(),
             KCalendarCore::Recurrence::rDaily);
    QCOMPARE(rt->recurrence()->endDateTime().date(), QDate(2026, 6, 30));
}

void TestDatebookCodec::roundTripYearlyRepeatForever()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Birthday"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 15), QTime(0, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setYearly(1);

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    QCOMPARE(rt->recurrence()->recurrenceType(),
             KCalendarCore::Recurrence::rYearlyMonth);
    QCOMPARE(rt->recurrence()->duration(), -1);  // -1 == forever
}

void TestDatebookCodec::roundTripExceptionDates()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Daily with holidays"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    auto *rec = ev->recurrence();
    rec->setDaily(1);
    const auto ex1 = QDateTime(QDate(2026, 6, 7),  QTime(0, 0), Qt::LocalTime);
    const auto ex2 = QDateTime(QDate(2026, 6, 14), QTime(0, 0), Qt::LocalTime);
    rec->addExDateTime(ex1);
    rec->addExDateTime(ex2);

    const auto rt = roundTripThroughBytes(ev, 0);
    QVERIFY(rt);
    QVERIFY(rt->recurs());
    const auto rtEx = rt->recurrence()->exDateTimes();
    QCOMPARE(rtEx.size(), 2);
    QVERIFY(rtEx.contains(ex1));
    QVERIFY(rtEx.contains(ex2));
}

void TestDatebookCodec::roundTripPrivateFlag()
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setSummary(QStringLiteral("Private"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    ev->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);

    std::uint8_t attrsOut = 0;
    const auto rt = roundTripThroughBytes(ev, 0, &attrsOut);
    QVERIFY(rt);
    QVERIFY(attrsOut & WildPalms::PalmSync::PalmRecord::AttrSecret);
    // decode path re-applies the secrecy from the PalmRecord attrs —
    // but roundTripThroughBytes sets recordId=1 on the decoded record;
    // we need to re-set AttrSecret on the input to decode to see it.
    WildPalms::PalmSync::PalmRecord rec = DatebookCodec::encode(ev, 0);
    rec.recordId = 1;
    rec.attributes |= WildPalms::PalmSync::PalmRecord::AttrSecret;
    const auto decoded = DatebookCodec::decode(rec);
    QVERIFY(decoded.isValid());
    QCOMPARE(decoded.event->secrecy(),
             KCalendarCore::Incidence::SecrecyPrivate);
}

QTEST_MAIN(TestDatebookCodec)
#include "tst_datebookcodec.moc"
