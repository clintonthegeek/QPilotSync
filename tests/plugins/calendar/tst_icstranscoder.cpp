#include <QtTest/QtTest>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/calendar/icstranscoder.h"
#include "palm/calendar/datebookcodec.h"
#include "palm/sync/palmrecord.h"

using WildPalms::CalendarPlugin::encodePalmToIcs;
using WildPalms::CalendarPlugin::decodeIcsToPalm;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord makeMeetingRecord(int slot)
{
    KCalendarCore::Event::Ptr e(new KCalendarCore::Event);
    e->setUid(QStringLiteral("meeting-uid-1"));
    e->setSummary(QStringLiteral("Standup"));
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(9, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(9, 30)));
    return DatebookCodec::encode(e, slot);
}

} // namespace

class TestIcsTranscoder : public QObject
{
    Q_OBJECT
private slots:
    void encodeProducesParseableIcs();
    void encodePreservesSummary();
    void roundTripPreservesSlot();
    void decodeWithEmptyBytesReturnsNullopt();
    void decodeWithGarbageReturnsNullopt();
    void decodePreservesRecordIdWhenPresent();
};

void TestIcsTranscoder::encodeProducesParseableIcs()
{
    PalmRecord pr = makeMeetingRecord(0);
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(!ics.isEmpty());
    QVERIFY(ics.contains("BEGIN:VCALENDAR"));
    QVERIFY(ics.contains("BEGIN:VEVENT"));
    QVERIFY(ics.contains("END:VCALENDAR"));
}

void TestIcsTranscoder::encodePreservesSummary()
{
    PalmRecord pr = makeMeetingRecord(2);
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("SUMMARY:Standup"));
}

void TestIcsTranscoder::roundTripPreservesSlot()
{
    PalmRecord pr1 = makeMeetingRecord(7);
    QByteArray ics = encodePalmToIcs(pr1);
    auto pr2opt = decodeIcsToPalm(ics, 7);
    QVERIFY(pr2opt.has_value());
    QCOMPARE(static_cast<int>(pr2opt->category), 7);
}

void TestIcsTranscoder::decodeWithEmptyBytesReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray(), 0).has_value());
}

void TestIcsTranscoder::decodeWithGarbageReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray("not an ics"), 0).has_value());
}

void TestIcsTranscoder::decodePreservesRecordIdWhenPresent()
{
    PalmRecord pr1 = makeMeetingRecord(0);
    pr1.recordId = 42;
    // Re-encode through DatebookCodec so the X-WP-PALM-RECORDID prop
    // is set on the Event before encoding to ICS.
    auto decoded = DatebookCodec::decode(pr1);
    QVERIFY(decoded.isValid());
    decoded.event->setCustomProperty("WildPalms",
        QByteArray("PALM-RECORDID"), QStringLiteral("42"));
    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    cal->addEvent(decoded.event);
    QByteArray ics = fmt.toString(cal).toUtf8();

    auto pr2opt = decodeIcsToPalm(ics, 0);
    QVERIFY(pr2opt.has_value());
    QCOMPARE(pr2opt->recordId, static_cast<std::uint32_t>(42));
}

QTEST_MAIN(TestIcsTranscoder)
#include "tst_icstranscoder.moc"
