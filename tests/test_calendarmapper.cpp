/**
 * @file test_calendarmapper.cpp
 * @brief Unit tests for CalendarMapper class
 *
 * Tests the conversion between Palm Datebook format and iCalendar (RFC 5545).
 * These tests focus on the iCal parsing and generation since we can test
 * those without actual Palm device data.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include "mappers/calendarmapper.h"

class TestCalendarMapper : public QObject
{
    Q_OBJECT

private slots:
    // ========== Setup/Cleanup ==========
    void initTestCase();
    void cleanupTestCase();

    // ========== iCalToEvent Tests ==========
    void testParseSimpleEvent();
    void testParseAllDayEvent();
    void testParseEventWithAlarm();
    void testParseEventWithDescription();
    void testParsePrivateEvent();

    // ========== Recurrence Tests ==========
    void testParseDailyRecurrence();
    void testParseWeeklyRecurrence();
    void testParseWeeklyRecurrenceWithDays();
    void testParseMonthlyByDayRecurrence();
    void testParseYearlyRecurrence();
    void testParseRecurrenceWithUntil();
    void testParseRecurrenceWithExceptions();

    // ========== eventToICal Tests ==========
    void testGenerateSimpleEvent();
    void testGenerateAllDayEvent();
    void testGenerateEventWithAlarm();
    void testGenerateWeeklyRecurrence();
    void testGenerateExceptionDates();

    // ========== Round-trip Tests ==========
    void testRoundTripSimpleEvent();
    void testRoundTripAllDayEvent();
    void testRoundTripEventWithAlarm();
    void testRoundTripWeeklyRecurrence();
    void testRoundTripRecurrenceWithExceptions();

    // ========== Edge Cases ==========
    void testParseEmptyInput();
    void testParseInvalidInput();
    void testSpecialCharacterEscaping();
    void testLineFolding();

    // ========== Filename Generation ==========
    void testGenerateFilenameSimple();
    void testGenerateFilenameSpecialChars();
    void testGenerateFilenameEmpty();

private:
    // Helper to create a minimal iCalendar event
    QString makeICalEvent(const QString &summary,
                          const QString &dtstart,
                          const QString &dtend = QString(),
                          const QStringList &extraProps = {});
};

void TestCalendarMapper::initTestCase()
{
    qDebug() << "Starting CalendarMapper tests";
}

void TestCalendarMapper::cleanupTestCase()
{
    qDebug() << "CalendarMapper tests complete";
}

// Helper to create test iCalendar content
QString TestCalendarMapper::makeICalEvent(const QString &summary,
                                           const QString &dtstart,
                                           const QString &dtend,
                                           const QStringList &extraProps)
{
    QString ical;
    ical += "BEGIN:VCALENDAR\r\n";
    ical += "VERSION:2.0\r\n";
    ical += "PRODID:-//Test//Test//EN\r\n";
    ical += "BEGIN:VEVENT\r\n";
    ical += "UID:test-event-1\r\n";
    ical += QString("DTSTART:%1\r\n").arg(dtstart);
    if (!dtend.isEmpty()) {
        ical += QString("DTEND:%1\r\n").arg(dtend);
    }
    ical += QString("SUMMARY:%1\r\n").arg(summary);
    for (const QString &prop : extraProps) {
        ical += prop + "\r\n";
    }
    ical += "END:VEVENT\r\n";
    ical += "END:VCALENDAR\r\n";
    return ical;
}

// ========== iCalToEvent Tests ==========

void TestCalendarMapper::testParseSimpleEvent()
{
    QString ical = makeICalEvent("Team Meeting", "20260115T140000", "20260115T150000");

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.description, QString("Team Meeting"));
    QCOMPARE(event.begin.date(), QDate(2026, 1, 15));
    QCOMPARE(event.begin.time(), QTime(14, 0, 0));
    QCOMPARE(event.end.date(), QDate(2026, 1, 15));
    QCOMPARE(event.end.time(), QTime(15, 0, 0));
    QVERIFY(!event.isUntimed);
    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatNone));
}

void TestCalendarMapper::testParseAllDayEvent()
{
    QString ical;
    ical += "BEGIN:VCALENDAR\r\n";
    ical += "VERSION:2.0\r\n";
    ical += "BEGIN:VEVENT\r\n";
    ical += "DTSTART;VALUE=DATE:20260120\r\n";
    ical += "DTEND;VALUE=DATE:20260121\r\n";  // Non-inclusive, so this is a 1-day event
    ical += "SUMMARY:Company Holiday\r\n";
    ical += "END:VEVENT\r\n";
    ical += "END:VCALENDAR\r\n";

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.description, QString("Company Holiday"));
    QVERIFY(event.isUntimed);
    QCOMPARE(event.begin.date(), QDate(2026, 1, 20));
    // DTEND is non-inclusive for DATE values, so end should be 20th after adjustment
    QCOMPARE(event.end.date(), QDate(2026, 1, 20));
}

void TestCalendarMapper::testParseEventWithAlarm()
{
    QStringList props;
    props << "BEGIN:VALARM";
    props << "ACTION:DISPLAY";
    props << "TRIGGER:-PT15M";
    props << "END:VALARM";

    QString ical = makeICalEvent("Reminder Test", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QVERIFY(event.hasAlarm);
    QCOMPARE(event.alarmAdvance, 15);
    QCOMPARE(event.alarmUnits, static_cast<int>(CalendarMapper::AlarmMinutes));
}

void TestCalendarMapper::testParseEventWithDescription()
{
    QStringList props;
    props << "DESCRIPTION:This is a detailed description\\nwith multiple lines";

    QString ical = makeICalEvent("Event With Notes", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.description, QString("Event With Notes"));
    QVERIFY(event.note.contains("detailed description"));
    QVERIFY(event.note.contains("\n"));  // Should have unescaped newline
}

void TestCalendarMapper::testParsePrivateEvent()
{
    QStringList props;
    props << "CLASS:PRIVATE";

    QString ical = makeICalEvent("Secret Meeting", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QVERIFY(event.isPrivate);
}

// ========== Recurrence Tests ==========

void TestCalendarMapper::testParseDailyRecurrence()
{
    QStringList props;
    props << "RRULE:FREQ=DAILY;INTERVAL=1";

    QString ical = makeICalEvent("Daily Standup", "20260115T090000", "20260115T091500", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatDaily));
    QCOMPARE(event.repeatFrequency, 1);
    QVERIFY(event.repeatForever);
}

void TestCalendarMapper::testParseWeeklyRecurrence()
{
    QStringList props;
    props << "RRULE:FREQ=WEEKLY;INTERVAL=2";

    QString ical = makeICalEvent("Bi-weekly Meeting", "20260115T140000", "20260115T150000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatWeekly));
    QCOMPARE(event.repeatFrequency, 2);
}

void TestCalendarMapper::testParseWeeklyRecurrenceWithDays()
{
    QStringList props;
    props << "RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR";

    QString ical = makeICalEvent("MWF Meeting", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatWeekly));
    QVERIFY(!event.repeatDays[0]);  // Sunday
    QVERIFY(event.repeatDays[1]);   // Monday
    QVERIFY(!event.repeatDays[2]);  // Tuesday
    QVERIFY(event.repeatDays[3]);   // Wednesday
    QVERIFY(!event.repeatDays[4]);  // Thursday
    QVERIFY(event.repeatDays[5]);   // Friday
    QVERIFY(!event.repeatDays[6]);  // Saturday
}

void TestCalendarMapper::testParseMonthlyByDayRecurrence()
{
    QStringList props;
    props << "RRULE:FREQ=MONTHLY;BYMONTHDAY=15";

    QString ical = makeICalEvent("Monthly Report", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatMonthlyByDay));
    QCOMPARE(event.repeatDay, 15);
}

void TestCalendarMapper::testParseYearlyRecurrence()
{
    QStringList props;
    props << "RRULE:FREQ=YEARLY";

    QString ical = makeICalEvent("Birthday", "20260315T000000", "20260315T235959", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatYearly));
}

void TestCalendarMapper::testParseRecurrenceWithUntil()
{
    QStringList props;
    props << "RRULE:FREQ=DAILY;UNTIL=20260131T235959";

    QString ical = makeICalEvent("Limited Series", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.repeatType, static_cast<int>(CalendarMapper::RepeatDaily));
    QVERIFY(!event.repeatForever);
    QCOMPARE(event.repeatEnd.date(), QDate(2026, 1, 31));
}

void TestCalendarMapper::testParseRecurrenceWithExceptions()
{
    QStringList props;
    props << "RRULE:FREQ=DAILY";
    props << "EXDATE:20260120T100000,20260125T100000";

    QString ical = makeICalEvent("Daily Except Some", "20260115T100000", "20260115T110000", props);

    CalendarMapper::Event event = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(event.exceptions.size(), 2);
    QCOMPARE(event.exceptions[0].date(), QDate(2026, 1, 20));
    QCOMPARE(event.exceptions[1].date(), QDate(2026, 1, 25));
}

// ========== eventToICal Tests ==========

void TestCalendarMapper::testGenerateSimpleEvent()
{
    CalendarMapper::Event event;
    event.recordId = 12345;
    event.description = "Test Event";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(14, 0, 0));
    event.end = QDateTime(QDate(2026, 1, 15), QTime(15, 0, 0));
    event.isUntimed = false;
    event.repeatType = CalendarMapper::RepeatNone;

    QString ical = CalendarMapper::eventToICal(event);

    QVERIFY(ical.contains("BEGIN:VCALENDAR"));
    QVERIFY(ical.contains("BEGIN:VEVENT"));
    QVERIFY(ical.contains("SUMMARY:Test Event"));
    QVERIFY(ical.contains("DTSTART:20260115T140000"));
    QVERIFY(ical.contains("DTEND:20260115T150000"));
    QVERIFY(ical.contains("UID:palm-datebook-12345"));
    QVERIFY(ical.contains("END:VEVENT"));
    QVERIFY(ical.contains("END:VCALENDAR"));
}

void TestCalendarMapper::testGenerateAllDayEvent()
{
    CalendarMapper::Event event;
    event.recordId = 100;
    event.description = "Holiday";
    event.begin = QDateTime(QDate(2026, 1, 20), QTime(0, 0, 0));
    event.end = QDateTime(QDate(2026, 1, 20), QTime(23, 59, 59));
    event.isUntimed = true;
    event.repeatType = CalendarMapper::RepeatNone;

    QString ical = CalendarMapper::eventToICal(event);

    QVERIFY(ical.contains("DTSTART;VALUE=DATE:20260120"));
    // All-day DTEND should be next day (non-inclusive)
    QVERIFY(ical.contains("DTEND;VALUE=DATE:20260121"));
}

void TestCalendarMapper::testGenerateEventWithAlarm()
{
    CalendarMapper::Event event;
    event.recordId = 200;
    event.description = "Meeting with Alarm";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(10, 0, 0));
    event.end = QDateTime(QDate(2026, 1, 15), QTime(11, 0, 0));
    event.isUntimed = false;
    event.hasAlarm = true;
    event.alarmAdvance = 30;
    event.alarmUnits = CalendarMapper::AlarmMinutes;
    event.repeatType = CalendarMapper::RepeatNone;

    QString ical = CalendarMapper::eventToICal(event);

    QVERIFY(ical.contains("BEGIN:VALARM"));
    QVERIFY(ical.contains("ACTION:DISPLAY"));
    QVERIFY(ical.contains("TRIGGER:-PT30M"));
    QVERIFY(ical.contains("END:VALARM"));
}

void TestCalendarMapper::testGenerateWeeklyRecurrence()
{
    CalendarMapper::Event event;
    event.recordId = 300;
    event.description = "Weekly Meeting";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(14, 0, 0));
    event.end = QDateTime(QDate(2026, 1, 15), QTime(15, 0, 0));
    event.isUntimed = false;
    event.repeatType = CalendarMapper::RepeatWeekly;
    event.repeatFrequency = 1;
    event.repeatForever = true;
    for (int i = 0; i < 7; i++) event.repeatDays[i] = false;
    event.repeatDays[1] = true;  // Monday
    event.repeatDays[4] = true;  // Thursday

    QString ical = CalendarMapper::eventToICal(event);

    QVERIFY(ical.contains("RRULE:FREQ=WEEKLY"));
    QVERIFY(ical.contains("BYDAY="));
    QVERIFY(ical.contains("MO"));
    QVERIFY(ical.contains("TH"));
}

void TestCalendarMapper::testGenerateExceptionDates()
{
    CalendarMapper::Event event;
    event.recordId = 400;
    event.description = "Event with Exceptions";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(10, 0, 0));
    event.end = QDateTime(QDate(2026, 1, 15), QTime(11, 0, 0));
    event.isUntimed = false;
    event.repeatType = CalendarMapper::RepeatDaily;
    event.repeatForever = true;
    event.exceptions.append(QDateTime(QDate(2026, 1, 20), QTime(10, 0, 0)));
    event.exceptions.append(QDateTime(QDate(2026, 1, 25), QTime(10, 0, 0)));

    QString ical = CalendarMapper::eventToICal(event);

    QVERIFY(ical.contains("EXDATE:"));
    QVERIFY(ical.contains("20260120T100000"));
    QVERIFY(ical.contains("20260125T100000"));
}

// ========== Round-trip Tests ==========

void TestCalendarMapper::testRoundTripSimpleEvent()
{
    // Create original event
    CalendarMapper::Event original;
    original.recordId = 1000;
    original.description = "Round Trip Test";
    original.begin = QDateTime(QDate(2026, 2, 10), QTime(9, 30, 0));
    original.end = QDateTime(QDate(2026, 2, 10), QTime(10, 30, 0));
    original.isUntimed = false;
    original.repeatType = CalendarMapper::RepeatNone;
    original.hasAlarm = false;

    // Convert to iCal and back
    QString ical = CalendarMapper::eventToICal(original);
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);

    // Compare
    QCOMPARE(parsed.description, original.description);
    QCOMPARE(parsed.begin, original.begin);
    QCOMPARE(parsed.end, original.end);
    QCOMPARE(parsed.isUntimed, original.isUntimed);
    QCOMPARE(parsed.repeatType, original.repeatType);
}

void TestCalendarMapper::testRoundTripAllDayEvent()
{
    CalendarMapper::Event original;
    original.recordId = 1001;
    original.description = "All Day Round Trip";
    original.begin = QDateTime(QDate(2026, 3, 15), QTime(0, 0, 0));
    original.end = QDateTime(QDate(2026, 3, 15), QTime(23, 59, 59));
    original.isUntimed = true;
    original.repeatType = CalendarMapper::RepeatNone;

    QString ical = CalendarMapper::eventToICal(original);
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(parsed.description, original.description);
    QCOMPARE(parsed.isUntimed, true);
    QCOMPARE(parsed.begin.date(), original.begin.date());
    QCOMPARE(parsed.end.date(), original.end.date());
}

void TestCalendarMapper::testRoundTripEventWithAlarm()
{
    CalendarMapper::Event original;
    original.recordId = 1002;
    original.description = "Alarm Round Trip";
    original.begin = QDateTime(QDate(2026, 4, 1), QTime(14, 0, 0));
    original.end = QDateTime(QDate(2026, 4, 1), QTime(15, 0, 0));
    original.isUntimed = false;
    original.repeatType = CalendarMapper::RepeatNone;
    original.hasAlarm = true;
    original.alarmAdvance = 2;
    original.alarmUnits = CalendarMapper::AlarmHours;

    QString ical = CalendarMapper::eventToICal(original);
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(parsed.hasAlarm, true);
    QCOMPARE(parsed.alarmAdvance, 2);
    QCOMPARE(parsed.alarmUnits, static_cast<int>(CalendarMapper::AlarmHours));
}

void TestCalendarMapper::testRoundTripWeeklyRecurrence()
{
    CalendarMapper::Event original;
    original.recordId = 1003;
    original.description = "Weekly Round Trip";
    original.begin = QDateTime(QDate(2026, 1, 12), QTime(10, 0, 0));  // Monday
    original.end = QDateTime(QDate(2026, 1, 12), QTime(11, 0, 0));
    original.isUntimed = false;
    original.repeatType = CalendarMapper::RepeatWeekly;
    original.repeatFrequency = 1;
    original.repeatForever = false;
    original.repeatEnd = QDateTime(QDate(2026, 6, 30), QTime(23, 59, 59));
    for (int i = 0; i < 7; i++) original.repeatDays[i] = false;
    original.repeatDays[1] = true;  // Monday
    original.repeatDays[3] = true;  // Wednesday

    QString ical = CalendarMapper::eventToICal(original);
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(parsed.repeatType, static_cast<int>(CalendarMapper::RepeatWeekly));
    QCOMPARE(parsed.repeatDays[1], true);   // Monday
    QCOMPARE(parsed.repeatDays[3], true);   // Wednesday
    QCOMPARE(parsed.repeatForever, false);
    QCOMPARE(parsed.repeatEnd.date(), QDate(2026, 6, 30));
}

void TestCalendarMapper::testRoundTripRecurrenceWithExceptions()
{
    CalendarMapper::Event original;
    original.recordId = 1004;
    original.description = "Exception Round Trip";
    original.begin = QDateTime(QDate(2026, 1, 15), QTime(9, 0, 0));
    original.end = QDateTime(QDate(2026, 1, 15), QTime(10, 0, 0));
    original.isUntimed = false;
    original.repeatType = CalendarMapper::RepeatDaily;
    original.repeatForever = true;
    original.exceptions.append(QDateTime(QDate(2026, 1, 20), QTime(9, 0, 0)));
    original.exceptions.append(QDateTime(QDate(2026, 1, 25), QTime(9, 0, 0)));

    QString ical = CalendarMapper::eventToICal(original);
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);

    QCOMPARE(parsed.exceptions.size(), 2);
    QCOMPARE(parsed.exceptions[0].date(), QDate(2026, 1, 20));
    QCOMPARE(parsed.exceptions[1].date(), QDate(2026, 1, 25));
}

// ========== Edge Cases ==========

void TestCalendarMapper::testParseEmptyInput()
{
    CalendarMapper::Event event = CalendarMapper::iCalToEvent("");

    // Should return empty/default event without crashing
    QVERIFY(event.description.isEmpty());
    QVERIFY(!event.begin.isValid() || event.begin.isNull());
}

void TestCalendarMapper::testParseInvalidInput()
{
    CalendarMapper::Event event = CalendarMapper::iCalToEvent("This is not iCalendar data");

    // Should handle gracefully
    QVERIFY(event.description.isEmpty());
}

void TestCalendarMapper::testSpecialCharacterEscaping()
{
    // Test that special characters are properly escaped/unescaped
    CalendarMapper::Event original;
    original.recordId = 2000;
    original.description = "Meeting; with, special\\chars";
    original.note = "Line 1\nLine 2\nLine 3";
    original.begin = QDateTime(QDate(2026, 1, 15), QTime(10, 0, 0));
    original.end = QDateTime(QDate(2026, 1, 15), QTime(11, 0, 0));
    original.isUntimed = false;
    original.repeatType = CalendarMapper::RepeatNone;

    QString ical = CalendarMapper::eventToICal(original);

    // Check escaping in output
    QVERIFY(ical.contains("\\;"));  // Semicolon escaped
    QVERIFY(ical.contains("\\,"));  // Comma escaped
    QVERIFY(ical.contains("\\n"));  // Newline escaped in DESCRIPTION

    // Round-trip should preserve original text
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);
    QCOMPARE(parsed.description, original.description);
    QCOMPARE(parsed.note, original.note);
}

void TestCalendarMapper::testLineFolding()
{
    // Test with a very long summary that should trigger line folding
    CalendarMapper::Event original;
    original.recordId = 2001;
    original.description = "This is a very long event summary that should definitely exceed the 75 octet line length limit specified in RFC 5545";
    original.begin = QDateTime(QDate(2026, 1, 15), QTime(10, 0, 0));
    original.end = QDateTime(QDate(2026, 1, 15), QTime(11, 0, 0));
    original.isUntimed = false;
    original.repeatType = CalendarMapper::RepeatNone;

    QString ical = CalendarMapper::eventToICal(original);

    // The output should have continuation lines (starting with space)
    QStringList lines = ical.split("\r\n");
    bool foundContinuation = false;
    for (const QString &line : lines) {
        if (line.startsWith(" ")) {
            foundContinuation = true;
            break;
        }
    }
    QVERIFY(foundContinuation);

    // Round-trip should still work
    CalendarMapper::Event parsed = CalendarMapper::iCalToEvent(ical);
    QCOMPARE(parsed.description, original.description);
}

// ========== Filename Generation ==========

void TestCalendarMapper::testGenerateFilenameSimple()
{
    CalendarMapper::Event event;
    event.description = "Team Meeting";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(14, 0, 0));
    event.recordId = 123;

    QString filename = CalendarMapper::generateFilename(event);

    QVERIFY(filename.endsWith(".ics"));
    QVERIFY(filename.contains("Team"));
    QVERIFY(filename.contains("Meeting"));
}

void TestCalendarMapper::testGenerateFilenameSpecialChars()
{
    CalendarMapper::Event event;
    event.description = "Meeting: Q1/Q2 Review <Important>";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(14, 0, 0));
    event.recordId = 456;

    QString filename = CalendarMapper::generateFilename(event);

    // Should not contain problematic characters
    QVERIFY(!filename.contains(":"));
    QVERIFY(!filename.contains("/"));
    QVERIFY(!filename.contains("<"));
    QVERIFY(!filename.contains(">"));
    QVERIFY(filename.endsWith(".ics"));
}

void TestCalendarMapper::testGenerateFilenameEmpty()
{
    CalendarMapper::Event event;
    event.description = "";
    event.begin = QDateTime(QDate(2026, 1, 15), QTime(14, 0, 0));
    event.recordId = 789;

    QString filename = CalendarMapper::generateFilename(event);

    // Should generate something based on date/ID
    QVERIFY(!filename.isEmpty());
    QVERIFY(filename.endsWith(".ics"));
    QVERIFY(filename.contains("20260115") || filename.contains("789"));
}

QTEST_MAIN(TestCalendarMapper)
#include "test_calendarmapper.moc"
