#include "calendarmapper.h"
#include <pi-datebook.h>
#include <QRegularExpression>
#include <QDate>
#include <QTime>
#include <QStringConverter>
#include <QBitArray>
#include <QTimeZone>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Alarm>
#include <KCalendarCore/Recurrence>

// Windows-1252 to Unicode mapping table for 0x80-0x9F
static const unsigned short cp1252_to_unicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
};

// Helper to decode Palm text which uses Windows-1252 encoding
static QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
}

// Helper to encode Unicode text to Windows-1252 for Palm
static QByteArray encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());

    for (QChar ch : text) {
        ushort unicode = ch.unicode();

        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && !(unicode >= 0x80 && unicode <= 0x9F)) {
            result.append(static_cast<char>(unicode));
        } else {
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (cp1252_to_unicode[i] == unicode) {
                    result.append(static_cast<char>(0x80 + i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.append('?');
            }
        }
    }

    return result;
}

CalendarMapper::CalendarMapper(QObject *parent)
    : QObject(parent)
{
}

CalendarMapper::~CalendarMapper()
{
}

CalendarMapper::Event CalendarMapper::unpackEvent(const PilotRecord *record)
{
    Event event;
    event.recordId = record->recordId();
    event.category = record->category();
    event.isPrivate = record->isSecret();
    event.isDirty = record->isDirty();
    event.isDeleted = record->isDeleted();

    // Unpack using pilot-link's datebook parser
    Appointment_t appt;
    memset(&appt, 0, sizeof(appt));

    pi_buffer_t *buf = pi_buffer_new(record->size());
    memcpy(buf->data, record->rawData(), record->size());
    buf->used = record->size();

    if (unpack_Appointment(&appt, buf, datebook_v1) < 0) {
        pi_buffer_free(buf);
        return event;  // Return empty event on error
    }

    pi_buffer_free(buf);

    // Extract basic fields
    event.isUntimed = (appt.event != 0);

    // Convert struct tm to QDateTime
    QDate beginDate(appt.begin.tm_year + 1900, appt.begin.tm_mon + 1, appt.begin.tm_mday);
    QTime beginTime(appt.begin.tm_hour, appt.begin.tm_min, 0);
    event.begin = QDateTime(beginDate, beginTime);

    QDate endDate(appt.end.tm_year + 1900, appt.end.tm_mon + 1, appt.end.tm_mday);
    QTime endTime(appt.end.tm_hour, appt.end.tm_min, 0);
    event.end = QDateTime(endDate, endTime);

    // Description and note
    if (appt.description) {
        event.description = decodePalmText(appt.description);
    }
    if (appt.note) {
        event.note = decodePalmText(appt.note);
    }

    // Alarm
    event.hasAlarm = (appt.alarm != 0);
    event.alarmAdvance = appt.advance;
    event.alarmUnits = appt.advanceUnits;

    // Repeat information
    event.repeatType = appt.repeatType;
    event.repeatForever = (appt.repeatForever != 0);

    if (!event.repeatForever) {
        QDate repEndDate(appt.repeatEnd.tm_year + 1900,
                        appt.repeatEnd.tm_mon + 1,
                        appt.repeatEnd.tm_mday);
        event.repeatEnd = QDateTime(repEndDate, QTime(23, 59, 59));
    }

    event.repeatFrequency = appt.repeatFrequency;
    event.repeatDay = appt.repeatDay;
    event.repeatWeekstart = appt.repeatWeekstart;

    // Weekly repeat days (0=Sunday, 1=Monday, etc)
    for (int i = 0; i < 7; i++) {
        event.repeatDays[i] = (appt.repeatDays[i] != 0);
    }

    // Exception dates
    for (int i = 0; i < appt.exceptions; i++) {
        QDate excDate(appt.exception[i].tm_year + 1900,
                     appt.exception[i].tm_mon + 1,
                     appt.exception[i].tm_mday);
        event.exceptions.append(QDateTime(excDate, QTime(0, 0, 0)));
    }

    free_Appointment(&appt);

    return event;
}

// Helper function to convert Qt day of week to QBitArray for that day
static QBitArray makeDayBitArray(short dayOfWeek)
{
    QBitArray result(7);
    // Qt dayOfWeek: 1=Monday, 7=Sunday
    // QBitArray: 0=Monday, 6=Sunday
    result.setBit(dayOfWeek - 1);
    return result;
}

KCalendarCore::Event::Ptr CalendarMapper::eventToKCalEvent(const Event &event, const QString &categoryName)
{
    auto kcalEvent = KCalendarCore::Event::Ptr::create();

    // UID - using Palm record ID
    kcalEvent->setUid(QStringLiteral("palm-datebook-%1").arg(event.recordId));

    // DTSTART and DTEND
    if (event.isUntimed) {
        // All-day event
        kcalEvent->setDtStart(QDateTime(event.begin.date(), QTime()));
        kcalEvent->setAllDay(true);
        // KCalendarCore handles all-day event end dates correctly
        kcalEvent->setDtEnd(QDateTime(event.end.date(), QTime()));
    } else {
        // Timed event (floating time - no timezone)
        kcalEvent->setDtStart(event.begin);
        kcalEvent->setDtEnd(event.end);
    }

    // SUMMARY and DESCRIPTION
    if (!event.description.isEmpty()) {
        kcalEvent->setSummary(event.description);
    }
    if (!event.note.isEmpty()) {
        kcalEvent->setDescription(event.note);
    }

    // CATEGORIES
    if (!categoryName.isEmpty()) {
        kcalEvent->setCategories(QStringList() << categoryName);
    }

    // CLASS - privacy
    if (event.isPrivate) {
        kcalEvent->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
    }

    // RRULE - recurrence rule
    if (event.repeatType != RepeatNone) {
        KCalendarCore::Recurrence *recurrence = kcalEvent->recurrence();

        switch (event.repeatType) {
            case RepeatDaily:
                recurrence->setDaily(event.repeatFrequency > 0 ? event.repeatFrequency : 1);
                break;
            case RepeatWeekly: {
                // Build days of week bitfield
                QBitArray days(7);
                for (int i = 0; i < 7; i++) {
                    // Palm: 0=Sunday, KCalendarCore: 0=Monday
                    // Convert: Palm Sunday(0) -> Qt Sunday(6), Palm Mon(1) -> Qt Mon(0), etc.
                    int qtDay = (i == 0) ? 6 : i - 1;
                    days.setBit(qtDay, event.repeatDays[i]);
                }
                recurrence->setWeekly(event.repeatFrequency > 0 ? event.repeatFrequency : 1, days);
                break;
            }
            case RepeatMonthlyByDay:
                // Repeat on same day of month
                recurrence->setMonthly(event.repeatFrequency > 0 ? event.repeatFrequency : 1);
                recurrence->addMonthlyDate(event.begin.date().day());
                break;
            case RepeatMonthlyByDate: {
                // Repeat on same weekday position (e.g., "2nd Monday")
                recurrence->setMonthly(event.repeatFrequency > 0 ? event.repeatFrequency : 1);
                int weekOfMonth = (event.begin.date().day() - 1) / 7 + 1;
                // Qt dayOfWeek(): 1=Monday, 7=Sunday
                short dayOfWeek = event.begin.date().dayOfWeek();
                recurrence->addMonthlyPos(weekOfMonth, makeDayBitArray(dayOfWeek));
                break;
            }
            case RepeatYearly:
                recurrence->setYearly(event.repeatFrequency > 0 ? event.repeatFrequency : 1);
                break;
            default:
                break;
        }

        // UNTIL - repeat end date
        if (!event.repeatForever && event.repeatEnd.isValid()) {
            recurrence->setEndDate(event.repeatEnd.date());
        }
    }

    // EXDATE - exception dates
    for (const QDateTime &exDate : event.exceptions) {
        if (event.isUntimed) {
            // For all-day events, use date-only exceptions
            kcalEvent->recurrence()->addExDate(exDate.date());
        } else {
            // For timed events, use date-time exceptions
            kcalEvent->recurrence()->addExDateTime(exDate);
        }
    }

    // VALARM - alarm/reminder
    if (event.hasAlarm) {
        KCalendarCore::Alarm::Ptr alarm = kcalEvent->newAlarm();
        alarm->setEnabled(true);
        alarm->setType(KCalendarCore::Alarm::Display);
        alarm->setText(QStringLiteral("Event Reminder"));

        // Calculate trigger time in seconds
        int seconds = event.alarmAdvance;
        if (event.alarmUnits == AlarmMinutes) {
            seconds *= 60;
        } else if (event.alarmUnits == AlarmHours) {
            seconds *= 60 * 60;
        } else if (event.alarmUnits == AlarmDays) {
            seconds *= 60 * 60 * 24;
        }

        alarm->setStartOffset(KCalendarCore::Duration(-seconds));
    }

    return kcalEvent;
}

QString CalendarMapper::eventToICal(const Event &event, const QString &categoryName)
{
    // Convert to KCalendarCore event
    KCalendarCore::Event::Ptr kcalEvent = eventToKCalEvent(event, categoryName);

    // Create a calendar and add the event
    KCalendarCore::MemoryCalendar::Ptr calendar(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    calendar->addEvent(kcalEvent);

    // Use ICalFormat to serialize
    KCalendarCore::ICalFormat icalFormat;
    QString icalString = icalFormat.toString(calendar);

    return icalString;
}

QString CalendarMapper::generateFilename(const Event &event)
{
    QString filename;

    // Use event description (summary) as base filename
    if (!event.description.isEmpty()) {
        filename = event.description.left(50);

        // Sanitize filename
        static QRegularExpression invalidChars("[^a-zA-Z0-9_\\-. ]");
        filename.replace(invalidChars, "_");

        // Replace multiple spaces with single underscore
        static QRegularExpression multiSpace("\\s+");
        filename.replace(multiSpace, "_");

        // Remove leading/trailing underscores
        filename = filename.trimmed();
        while (filename.startsWith('_')) filename.remove(0, 1);
        while (filename.endsWith('_')) filename.chop(1);
    }

    // If empty after sanitization, use date + record ID
    if (filename.isEmpty()) {
        filename = QStringLiteral("%1_event_%2")
            .arg(event.begin.toString("yyyyMMdd"))
            .arg(event.recordId);
    }

    // Add .ics extension
    filename += ".ics";

    return filename;
}

// ========== Reverse mapping: iCalendar → Palm ==========

CalendarMapper::Event CalendarMapper::kCalEventToEvent(const KCalendarCore::Event::Ptr &kcalEvent)
{
    Event event;
    event.recordId = 0;
    event.category = 0;
    event.isUntimed = false;
    event.hasAlarm = false;
    event.alarmAdvance = 0;
    event.alarmUnits = AlarmMinutes;
    event.repeatType = RepeatNone;
    event.repeatForever = true;
    event.repeatFrequency = 1;
    event.repeatDay = 0;
    event.repeatWeekstart = 0;
    for (int i = 0; i < 7; i++) event.repeatDays[i] = false;
    event.isPrivate = false;
    event.isDirty = false;
    event.isDeleted = false;

    if (!kcalEvent) {
        return event;
    }

    // UID - extract record ID if it's in palm-datebook-XXXX format
    QString uid = kcalEvent->uid();
    if (uid.startsWith(QLatin1String("palm-datebook-"))) {
        bool ok;
        int id = uid.mid(14).toInt(&ok);
        if (ok) event.recordId = id;
    }

    // All-day event check
    event.isUntimed = kcalEvent->allDay();

    // DTSTART and DTEND
    event.begin = kcalEvent->dtStart();
    event.end = kcalEvent->dtEnd();

    // For all-day events, KCalendarCore may give us the exclusive end date
    // Verify by checking if times are both midnight
    if (event.isUntimed && event.end.isValid() && event.begin.isValid()) {
        // If end is after begin and both are at midnight, end is exclusive
        if (event.end > event.begin &&
            event.begin.time() == QTime(0, 0, 0) &&
            event.end.time() == QTime(0, 0, 0)) {
            event.end = event.end.addDays(-1);
        }
    }

    // If no end time, set it to start time
    if (!event.end.isValid()) {
        event.end = event.begin;
    }

    // SUMMARY and DESCRIPTION
    event.description = kcalEvent->summary();
    event.note = kcalEvent->description();

    // CATEGORIES
    QStringList categories = kcalEvent->categories();
    if (!categories.isEmpty()) {
        event.categoryName = categories.first();
    }

    // CLASS - privacy
    event.isPrivate = (kcalEvent->secrecy() == KCalendarCore::Incidence::SecrecyPrivate);

    // Recurrence
    if (kcalEvent->recurs()) {
        KCalendarCore::Recurrence *recurrence = kcalEvent->recurrence();

        switch (recurrence->recurrenceType()) {
            case KCalendarCore::Recurrence::rDaily:
                event.repeatType = RepeatDaily;
                event.repeatFrequency = recurrence->frequency();
                break;

            case KCalendarCore::Recurrence::rWeekly: {
                event.repeatType = RepeatWeekly;
                event.repeatFrequency = recurrence->frequency();

                // Get days of week
                QBitArray weekDays = recurrence->days();
                for (int i = 0; i < 7; i++) {
                    // KCalendarCore: 0=Monday, 6=Sunday
                    // Palm: 0=Sunday, 1=Monday, etc.
                    int palmDay = (i + 1) % 7;  // Convert: Mon(0)->1, Tue(1)->2, ..., Sun(6)->0
                    event.repeatDays[palmDay] = weekDays.testBit(i);
                }
                break;
            }

            case KCalendarCore::Recurrence::rMonthlyDay:
                event.repeatType = RepeatMonthlyByDay;
                event.repeatFrequency = recurrence->frequency();
                // The day of month is in the monthDays list
                if (!recurrence->monthDays().isEmpty()) {
                    event.repeatDay = recurrence->monthDays().first();
                }
                break;

            case KCalendarCore::Recurrence::rMonthlyPos:
                event.repeatType = RepeatMonthlyByDate;
                event.repeatFrequency = recurrence->frequency();
                break;

            case KCalendarCore::Recurrence::rYearlyDay:
            case KCalendarCore::Recurrence::rYearlyMonth:
            case KCalendarCore::Recurrence::rYearlyPos:
                event.repeatType = RepeatYearly;
                event.repeatFrequency = recurrence->frequency();
                break;

            default:
                event.repeatType = RepeatNone;
                break;
        }

        // End date
        QDate endDate = recurrence->endDate();
        if (endDate.isValid()) {
            event.repeatEnd = QDateTime(endDate, QTime(23, 59, 59));
            event.repeatForever = false;
        } else if (recurrence->duration() > 0) {
            // COUNT-based recurrence - Palm doesn't support this directly
            event.repeatForever = true;
        } else {
            event.repeatForever = true;
        }

        // Exception dates - check both date-only and date-time exceptions
        QList<QDate> exDates = recurrence->exDates();
        for (const QDate &date : exDates) {
            event.exceptions.append(QDateTime(date, QTime(0, 0, 0)));
        }

        // Also get date-time exceptions
        QList<QDateTime> exDateTimes = recurrence->exDateTimes();
        for (const QDateTime &dt : exDateTimes) {
            event.exceptions.append(dt);
        }
    }

    // Alarms
    KCalendarCore::Alarm::List alarms = kcalEvent->alarms();
    if (!alarms.isEmpty()) {
        KCalendarCore::Alarm::Ptr alarm = alarms.first();
        if (alarm->enabled()) {
            event.hasAlarm = true;

            // Get offset in seconds (negative means before event)
            KCalendarCore::Duration offset = alarm->startOffset();
            int seconds = qAbs(offset.asSeconds());
            int minutes = seconds / 60;

            // Convert to Palm alarm units
            if (minutes >= 24 * 60) {
                event.alarmAdvance = minutes / (24 * 60);
                event.alarmUnits = AlarmDays;
            } else if (minutes >= 60) {
                event.alarmAdvance = minutes / 60;
                event.alarmUnits = AlarmHours;
            } else {
                event.alarmAdvance = minutes;
                event.alarmUnits = AlarmMinutes;
            }
        }
    }

    return event;
}

CalendarMapper::Event CalendarMapper::iCalToEvent(const QString &ical)
{
    // Use KCalendarCore to parse the iCalendar data
    KCalendarCore::MemoryCalendar::Ptr calendar(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

    KCalendarCore::ICalFormat icalFormat;
    if (!icalFormat.fromString(calendar, ical)) {
        // Parse failed, return empty event
        Event event;
        event.recordId = 0;
        event.category = 0;
        event.isUntimed = false;
        event.hasAlarm = false;
        event.alarmAdvance = 0;
        event.alarmUnits = AlarmMinutes;
        event.repeatType = RepeatNone;
        event.repeatForever = true;
        event.repeatFrequency = 1;
        event.repeatDay = 0;
        event.repeatWeekstart = 0;
        for (int i = 0; i < 7; i++) event.repeatDays[i] = false;
        event.isPrivate = false;
        event.isDirty = false;
        event.isDeleted = false;
        return event;
    }

    // Get the first event from the calendar
    KCalendarCore::Event::List events = calendar->events();
    if (events.isEmpty()) {
        // No events found, return empty event
        Event event;
        event.recordId = 0;
        event.category = 0;
        event.isUntimed = false;
        event.hasAlarm = false;
        event.alarmAdvance = 0;
        event.alarmUnits = AlarmMinutes;
        event.repeatType = RepeatNone;
        event.repeatForever = true;
        event.repeatFrequency = 1;
        event.repeatDay = 0;
        event.repeatWeekstart = 0;
        for (int i = 0; i < 7; i++) event.repeatDays[i] = false;
        event.isPrivate = false;
        event.isDirty = false;
        event.isDeleted = false;
        return event;
    }

    return kCalEventToEvent(events.first());
}

PilotRecord* CalendarMapper::packEvent(const Event &event)
{
    // Create Appointment structure
    Appointment_t appt;
    memset(&appt, 0, sizeof(appt));

    // Set untimed flag
    appt.event = event.isUntimed ? 1 : 0;

    // Convert QDateTime to struct tm
    auto setStructTm = [](struct tm &tm, const QDateTime &dt) {
        tm.tm_year = dt.date().year() - 1900;
        tm.tm_mon = dt.date().month() - 1;
        tm.tm_mday = dt.date().day();
        tm.tm_hour = dt.time().hour();
        tm.tm_min = dt.time().minute();
        tm.tm_sec = dt.time().second();
    };

    setStructTm(appt.begin, event.begin);
    setStructTm(appt.end, event.end);

    // Description and note
    if (!event.description.isEmpty()) {
        QByteArray descData = encodePalmText(event.description);
        appt.description = strdup(descData.constData());
    }
    if (!event.note.isEmpty()) {
        QByteArray noteData = encodePalmText(event.note);
        appt.note = strdup(noteData.constData());
    }

    // Alarm
    appt.alarm = event.hasAlarm ? 1 : 0;
    appt.advance = event.alarmAdvance;
    appt.advanceUnits = event.alarmUnits;

    // Repeat
    appt.repeatType = static_cast<repeatTypes>(event.repeatType);
    appt.repeatForever = event.repeatForever ? 1 : 0;

    if (!event.repeatForever && event.repeatEnd.isValid()) {
        setStructTm(appt.repeatEnd, event.repeatEnd);
    }

    appt.repeatFrequency = event.repeatFrequency;
    appt.repeatDay = static_cast<DayOfMonthType>(event.repeatDay);
    appt.repeatWeekstart = event.repeatWeekstart;

    for (int i = 0; i < 7; i++) {
        appt.repeatDays[i] = event.repeatDays[i] ? 1 : 0;
    }

    // Exception dates
    appt.exceptions = event.exceptions.size();
    if (appt.exceptions > 0) {
        appt.exception = static_cast<struct tm*>(malloc(sizeof(struct tm) * appt.exceptions));
        for (int i = 0; i < event.exceptions.size(); i++) {
            setStructTm(appt.exception[i], event.exceptions[i]);
        }
    }

    // Pack to buffer
    pi_buffer_t *buf = pi_buffer_new(0xFFFF);
    int packResult = pack_Appointment(&appt, buf, datebook_v1);

    // Free allocated memory
    free_Appointment(&appt);

    if (packResult < 0) {
        pi_buffer_free(buf);
        return nullptr;
    }

    // Create QByteArray from buffer
    QByteArray data(reinterpret_cast<const char*>(buf->data), buf->used);
    pi_buffer_free(buf);

    // Create attributes from flags
    int attr = 0;
    if (event.isPrivate) attr |= PilotRecord::AttrSecret;
    if (event.isDirty) attr |= PilotRecord::AttrDirty;
    if (event.isDeleted) attr |= PilotRecord::AttrDeleted;

    return new PilotRecord(event.recordId, event.category, attr, data);
}
