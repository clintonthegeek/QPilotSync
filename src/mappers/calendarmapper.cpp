#include "calendarmapper.h"
#include <pi-datebook.h>
#include <QRegularExpression>
#include <QDate>
#include <QTime>
#include <QStringConverter>

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

// Helper function to fold iCalendar lines to 75 octets as per RFC 5545 section 3.1
static QString foldLine(const QString &line)
{
    const int MAX_LINE_LENGTH = 75;  // octets, excluding CRLF

    QByteArray utf8 = line.toUtf8();
    if (utf8.length() <= MAX_LINE_LENGTH) {
        return line + "\r\n";
    }

    QString result;
    int pos = 0;

    while (pos < utf8.length()) {
        int chunkSize = MAX_LINE_LENGTH;

        // For first line, use full 75 octets
        // For continuation lines, use 74 octets (to account for leading space)
        if (pos > 0) {
            chunkSize = MAX_LINE_LENGTH - 1;
        }

        // Don't split in the middle of a UTF-8 multi-byte character
        // UTF-8 continuation bytes have the form 10xxxxxx (0x80-0xBF)
        while (chunkSize > 0 && pos + chunkSize < utf8.length() &&
               (utf8[pos + chunkSize] & 0xC0) == 0x80) {
            chunkSize--;
        }

        // Extract chunk and convert back to QString
        QByteArray chunk = utf8.mid(pos, chunkSize);
        QString chunkStr = QString::fromUtf8(chunk);

        if (pos == 0) {
            result += chunkStr + "\r\n";
        } else {
            result += " " + chunkStr + "\r\n";  // Continuation line starts with space
        }

        pos += chunkSize;
    }

    return result;
}

// Helper to format QDateTime as iCalendar date-time (floating time, no timezone)
static QString formatDateTime(const QDateTime &dt, bool dateOnly = false)
{
    if (dateOnly) {
        // DATE format: YYYYMMDD
        return dt.toString("yyyyMMdd");
    } else {
        // DATE-TIME format: YYYYMMDDTHHMMSS (floating time)
        return dt.toString("yyyyMMdd'T'HHmmss");
    }
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

QString CalendarMapper::eventToICal(const Event &event, const QString &categoryName)
{
    QString ical;

    // iCalendar 2.0 format (RFC 5545)
    ical += "BEGIN:VCALENDAR\r\n";
    ical += "VERSION:2.0\r\n";
    ical += "PRODID:-//QPilotSync//NONSGML v0.1//EN\r\n";
    ical += "BEGIN:VEVENT\r\n";

    // UID - using Palm record ID
    ical += foldLine(QString("UID:palm-datebook-%1").arg(event.recordId));

    // DTSTAMP - current time as creation time
    QString dtstamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
    ical += foldLine(QString("DTSTAMP:%1").arg(dtstamp));

    // DTSTART - start date/time
    if (event.isUntimed) {
        // All-day event - use DATE format
        ical += foldLine(QString("DTSTART;VALUE=DATE:%1").arg(formatDateTime(event.begin, true)));
    } else {
        // Timed event - use DATE-TIME format (floating time)
        ical += foldLine(QString("DTSTART:%1").arg(formatDateTime(event.begin, false)));
    }

    // DTEND - end date/time
    if (event.isUntimed) {
        // For all-day events, DTEND is non-inclusive, so add 1 day
        QDateTime endDate = event.end.addDays(1);
        ical += foldLine(QString("DTEND;VALUE=DATE:%1").arg(formatDateTime(endDate, true)));
    } else {
        ical += foldLine(QString("DTEND:%1").arg(formatDateTime(event.end, false)));
    }

    // SUMMARY - event title/description
    if (!event.description.isEmpty()) {
        // Escape special characters per RFC 5545 section 3.3.11
        QString summary = event.description;
        summary.replace("\\", "\\\\");
        summary.replace(";", "\\;");
        summary.replace(",", "\\,");
        summary.replace("\n", "\\n");
        ical += foldLine(QString("SUMMARY:%1").arg(summary));
    }

    // DESCRIPTION - detailed notes
    if (!event.note.isEmpty()) {
        QString description = event.note;
        description.replace("\\", "\\\\");
        description.replace(";", "\\;");
        description.replace(",", "\\,");
        description.replace("\n", "\\n");
        ical += foldLine(QString("DESCRIPTION:%1").arg(description));
    }

    // CATEGORIES
    if (!categoryName.isEmpty()) {
        ical += foldLine(QString("CATEGORIES:%1").arg(categoryName));
    }

    // CLASS - privacy
    if (event.isPrivate) {
        ical += "CLASS:PRIVATE\r\n";
    }

    // RRULE - recurrence rule
    if (event.repeatType != RepeatNone) {
        QString rrule = "RRULE:";

        switch (event.repeatType) {
            case RepeatDaily:
                rrule += "FREQ=DAILY";
                break;
            case RepeatWeekly:
                rrule += "FREQ=WEEKLY";
                break;
            case RepeatMonthlyByDay:
                rrule += "FREQ=MONTHLY";
                break;
            case RepeatMonthlyByDate:
                rrule += "FREQ=MONTHLY";
                break;
            case RepeatYearly:
                rrule += "FREQ=YEARLY";
                break;
            default:
                break;
        }

        // INTERVAL
        if (event.repeatFrequency > 1) {
            rrule += QString(";INTERVAL=%1").arg(event.repeatFrequency);
        }

        // BYDAY for weekly repeats
        if (event.repeatType == RepeatWeekly) {
            QStringList days;
            QStringList dayNames = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
            for (int i = 0; i < 7; i++) {
                if (event.repeatDays[i]) {
                    days.append(dayNames[i]);
                }
            }
            if (!days.isEmpty()) {
                rrule += QString(";BYDAY=%1").arg(days.join(","));
            }

            // WKST - week start
            if (event.repeatWeekstart > 0 && event.repeatWeekstart < 7) {
                rrule += QString(";WKST=%1").arg(dayNames[event.repeatWeekstart]);
            }
        }

        // BYMONTHDAY for monthly by day
        if (event.repeatType == RepeatMonthlyByDay) {
            rrule += QString(";BYMONTHDAY=%1").arg(event.begin.date().day());
        }

        // BYDAY for monthly by date (e.g., "2nd Monday")
        if (event.repeatType == RepeatMonthlyByDate) {
            int weekOfMonth = (event.begin.date().day() - 1) / 7 + 1;
            QStringList dayNames = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
            int dayOfWeek = event.begin.date().dayOfWeek() % 7;  // Qt: 1=Monday, convert to 0=Sunday
            rrule += QString(";BYDAY=%1%2").arg(weekOfMonth).arg(dayNames[dayOfWeek]);
        }

        // UNTIL - repeat end date
        if (!event.repeatForever && event.repeatEnd.isValid()) {
            if (event.isUntimed) {
                rrule += QString(";UNTIL=%1").arg(formatDateTime(event.repeatEnd, true));
            } else {
                rrule += QString(";UNTIL=%1").arg(formatDateTime(event.repeatEnd, false));
            }
        }

        ical += foldLine(rrule);
    }

    // EXDATE - exception dates
    if (!event.exceptions.isEmpty()) {
        QStringList exdates;
        for (const QDateTime &exDate : event.exceptions) {
            if (event.isUntimed) {
                exdates.append(formatDateTime(exDate, true));
            } else {
                exdates.append(formatDateTime(exDate, false));
            }
        }

        if (event.isUntimed) {
            ical += foldLine(QString("EXDATE;VALUE=DATE:%1").arg(exdates.join(",")));
        } else {
            ical += foldLine(QString("EXDATE:%1").arg(exdates.join(",")));
        }
    }

    // VALARM - alarm/reminder
    if (event.hasAlarm) {
        ical += "BEGIN:VALARM\r\n";
        ical += "ACTION:DISPLAY\r\n";
        ical += foldLine("DESCRIPTION:Event Reminder");

        // Calculate trigger time
        int minutes = event.alarmAdvance;
        if (event.alarmUnits == AlarmHours) {
            minutes *= 60;
        } else if (event.alarmUnits == AlarmDays) {
            minutes *= 60 * 24;
        }

        // TRIGGER in ISO 8601 duration format: -P[n]D[T[n]H[n]M]
        // Examples: -PT15M, -PT2H, -P1D, -P1DT2H30M
        QString trigger = "TRIGGER:-P";

        if (minutes >= 1440) {  // Has days component
            int days = minutes / 1440;
            int remainingMins = minutes % 1440;
            trigger += QString("%1D").arg(days);

            // Add time components if any
            if (remainingMins > 0) {
                trigger += "T";  // Time separator
                if (remainingMins >= 60) {
                    int hours = remainingMins / 60;
                    trigger += QString("%1H").arg(hours);
                    if (remainingMins % 60 > 0) {
                        trigger += QString("%1M").arg(remainingMins % 60);
                    }
                } else {
                    trigger += QString("%1M").arg(remainingMins);
                }
            }
        } else if (minutes >= 60) {  // Hours only (no days)
            trigger += "T";  // Time separator
            int hours = minutes / 60;
            int remainingMins = minutes % 60;
            trigger += QString("%1H").arg(hours);
            if (remainingMins > 0) {
                trigger += QString("%1M").arg(remainingMins);
            }
        } else {  // Minutes only
            trigger += "T";  // Time separator
            trigger += QString("%1M").arg(minutes);
        }

        ical += foldLine(trigger);
        ical += "END:VALARM\r\n";
    }

    ical += "END:VEVENT\r\n";
    ical += "END:VCALENDAR\r\n";

    return ical;
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
        filename = QString("%1_event_%2")
            .arg(event.begin.toString("yyyyMMdd"))
            .arg(event.recordId);
    }

    // Add .ics extension
    filename += ".ics";

    return filename;
}

// ========== Reverse mapping: iCalendar → Palm ==========

// Helper to unfold iCalendar content (reverse of foldLine)
static QString unfoldICalContent(const QString &content)
{
    QString result = content;
    result.replace("\r\n ", "");
    result.replace("\r\n\t", "");
    result.replace("\n ", "");
    result.replace("\n\t", "");
    return result;
}

// Helper to parse iCalendar date-time string (YYYYMMDDTHHMMSS or YYYYMMDD)
static QDateTime parseICalDateTime(const QString &str)
{
    QString value = str;

    // Remove any VALUE=DATE prefix
    if (value.contains("VALUE=DATE:")) {
        value = value.section("VALUE=DATE:", 1);
    }

    // Handle UTC marker 'Z' at end
    bool isUtc = value.endsWith('Z');
    if (isUtc) {
        value.chop(1);
    }

    // Parse based on format
    if (value.length() == 8) {
        // DATE only: YYYYMMDD
        QDate date = QDate::fromString(value, "yyyyMMdd");
        return QDateTime(date, QTime(0, 0, 0));
    } else if (value.length() >= 15) {
        // DATE-TIME: YYYYMMDDTHHMMSS
        QString dateStr = value.left(8);
        QString timeStr = value.mid(9, 6);
        QDate date = QDate::fromString(dateStr, "yyyyMMdd");
        QTime time = QTime::fromString(timeStr, "HHmmss");
        return QDateTime(date, time);
    }

    return QDateTime();
}

// Helper to unescape iCalendar text values
static QString unescapeICalText(const QString &text)
{
    QString result = text;
    result.replace("\\n", "\n");
    result.replace("\\N", "\n");
    result.replace("\\,", ",");
    result.replace("\\;", ";");
    result.replace("\\\\", "\\");
    return result;
}

// Helper to parse RRULE into event repeat fields
static void parseRRule(const QString &rrule, CalendarMapper::Event &event)
{
    QStringList parts = rrule.split(';', Qt::SkipEmptyParts);

    for (const QString &part : parts) {
        int eqPos = part.indexOf('=');
        if (eqPos == -1) continue;

        QString key = part.left(eqPos).toUpper();
        QString value = part.mid(eqPos + 1);

        if (key == "FREQ") {
            if (value == "DAILY") {
                event.repeatType = CalendarMapper::RepeatDaily;
            } else if (value == "WEEKLY") {
                event.repeatType = CalendarMapper::RepeatWeekly;
            } else if (value == "MONTHLY") {
                event.repeatType = CalendarMapper::RepeatMonthlyByDate;  // Default, may be overridden
            } else if (value == "YEARLY") {
                event.repeatType = CalendarMapper::RepeatYearly;
            }
        } else if (key == "INTERVAL") {
            event.repeatFrequency = value.toInt();
            if (event.repeatFrequency < 1) event.repeatFrequency = 1;
        } else if (key == "UNTIL") {
            event.repeatEnd = parseICalDateTime(value);
            event.repeatForever = false;
        } else if (key == "COUNT") {
            // COUNT not directly supported by Palm, leave as forever
            event.repeatForever = true;
        } else if (key == "BYDAY") {
            // Weekly: SU,MO,TU,WE,TH,FR,SA
            QMap<QString, int> dayMap = {
                {"SU", 0}, {"MO", 1}, {"TU", 2}, {"WE", 3},
                {"TH", 4}, {"FR", 5}, {"SA", 6}
            };

            QStringList days = value.split(',');
            for (const QString &day : days) {
                // Handle positional days like "2MO" (second Monday)
                QString dayCode = day.right(2).toUpper();
                if (dayMap.contains(dayCode)) {
                    int dayIndex = dayMap[dayCode];
                    event.repeatDays[dayIndex] = true;
                }
            }
        } else if (key == "BYMONTHDAY") {
            event.repeatType = CalendarMapper::RepeatMonthlyByDay;
            event.repeatDay = value.toInt();
        } else if (key == "WKST") {
            QMap<QString, int> dayMap = {
                {"SU", 0}, {"MO", 1}, {"TU", 2}, {"WE", 3},
                {"TH", 4}, {"FR", 5}, {"SA", 6}
            };
            if (dayMap.contains(value.toUpper())) {
                event.repeatWeekstart = dayMap[value.toUpper()];
            }
        }
    }
}

// Helper to parse TRIGGER duration into minutes
static int parseTriggerDuration(const QString &trigger)
{
    QString value = trigger;

    // Remove leading '-' if present
    bool negative = value.startsWith('-');
    if (negative) value = value.mid(1);

    // Remove 'P' for period
    if (value.startsWith('P')) value = value.mid(1);

    int minutes = 0;

    // Parse days
    int dPos = value.indexOf('D');
    if (dPos > 0) {
        minutes += value.left(dPos).toInt() * 24 * 60;
        value = value.mid(dPos + 1);
    }

    // Parse time portion after 'T'
    if (value.startsWith('T')) {
        value = value.mid(1);
    }

    // Parse hours
    int hPos = value.indexOf('H');
    if (hPos > 0) {
        minutes += value.left(hPos).toInt() * 60;
        value = value.mid(hPos + 1);
    }

    // Parse minutes
    int mPos = value.indexOf('M');
    if (mPos > 0) {
        minutes += value.left(mPos).toInt();
    }

    return minutes;
}

CalendarMapper::Event CalendarMapper::iCalToEvent(const QString &ical)
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

    // Unfold the content
    QString content = unfoldICalContent(ical);

    // Split into lines
    QStringList lines = content.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);

    bool inVEvent = false;
    bool inVAlarm = false;

    for (const QString &line : lines) {
        if (line.startsWith("BEGIN:VEVENT")) {
            inVEvent = true;
            continue;
        }
        if (line.startsWith("END:VEVENT")) {
            inVEvent = false;
            continue;
        }
        if (line.startsWith("BEGIN:VALARM")) {
            inVAlarm = true;
            continue;
        }
        if (line.startsWith("END:VALARM")) {
            inVAlarm = false;
            continue;
        }

        if (!inVEvent) continue;

        // Parse property and value
        int colonPos = line.indexOf(':');
        if (colonPos == -1) continue;

        QString propertyPart = line.left(colonPos);
        QString value = line.mid(colonPos + 1);

        // Split property name from parameters
        QString propertyName = propertyPart.split(';').first().toUpper();

        if (inVAlarm) {
            if (propertyName == "TRIGGER") {
                event.hasAlarm = true;
                int minutes = parseTriggerDuration(value);

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
            continue;
        }

        if (propertyName == "DTSTART") {
            // Check if VALUE=DATE (all-day event)
            if (propertyPart.contains("VALUE=DATE") && !propertyPart.contains("VALUE=DATE-TIME")) {
                event.isUntimed = true;
            }
            event.begin = parseICalDateTime(value);
        } else if (propertyName == "DTEND") {
            event.end = parseICalDateTime(value);
            // For all-day events, DTEND is non-inclusive, so subtract 1 day
            if (event.isUntimed && event.end.isValid()) {
                event.end = event.end.addDays(-1);
            }
        } else if (propertyName == "SUMMARY") {
            event.description = unescapeICalText(value);
        } else if (propertyName == "DESCRIPTION") {
            event.note = unescapeICalText(value);
        } else if (propertyName == "CLASS") {
            event.isPrivate = (value.toUpper() == "PRIVATE");
        } else if (propertyName == "RRULE") {
            parseRRule(value, event);
        } else if (propertyName == "EXDATE") {
            // Parse exception dates
            QStringList exDates = value.split(',');
            for (const QString &exDateStr : exDates) {
                QDateTime exDate = parseICalDateTime(exDateStr);
                if (exDate.isValid()) {
                    event.exceptions.append(exDate);
                }
            }
        } else if (propertyName == "UID") {
            // Extract record ID from UID if it's in palm-datebook-XXXX format
            if (value.startsWith("palm-datebook-")) {
                bool ok;
                int id = value.mid(14).toInt(&ok);
                if (ok) event.recordId = id;
            }
        } else if (propertyName == "CATEGORIES") {
            // Store first category name for lookup by conduit
            QStringList cats = value.split(',');
            if (!cats.isEmpty()) {
                event.categoryName = cats.first().trimmed();
            }
        }
    }

    // If no end time, set it to start time (instant event)
    if (!event.end.isValid()) {
        event.end = event.begin;
    }

    return event;
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
