#include "datebookcodec.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <QBitArray>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Duration>
#include <KCalendarCore/Recurrence>

extern "C" {
#include <pi-buffer.h>
#include <pi-datebook.h>
}

namespace WildPalms::PalmCalendar {

using WildPalms::PalmSync::PalmRecord;

namespace {

/// Scoped wrapper so we free the pisock Appointment_t's allocations
/// even on early return.
struct ScopedAppointment {
    Appointment_t a{};
    ~ScopedAppointment() { free_Appointment(&a); }
    ScopedAppointment() = default;
    ScopedAppointment(const ScopedAppointment &) = delete;
    ScopedAppointment &operator=(const ScopedAppointment &) = delete;
};

/// Scoped wrapper for pi_buffer_t.
struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 256) {
        buf = pi_buffer_new(initial);
    }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

/// Palm `struct tm` -> QDateTime. For untimed (all-day) records,
/// only the date portion is meaningful; return a date-only QDateTime
/// (time 00:00 local).
inline QDateTime palmTmToQDateTime(const std::tm &tm, bool untimed)
{
    QDate date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    if (!date.isValid()) {
        return {};
    }
    if (untimed) {
        return QDateTime(date, QTime(0, 0), Qt::LocalTime);
    }
    return QDateTime(date, QTime(tm.tm_hour, tm.tm_min), Qt::LocalTime);
}

inline std::tm qDateTimeToPalmTm(const QDateTime &dt, bool untimed)
{
    std::tm tm{};
    tm.tm_year = dt.date().year() - 1900;
    tm.tm_mon  = dt.date().month() - 1;
    tm.tm_mday = dt.date().day();
    if (!untimed) {
        tm.tm_hour = dt.time().hour();
        tm.tm_min  = dt.time().minute();
    } else {
        tm.tm_hour = 0;
        tm.tm_min  = 0;
    }
    tm.tm_sec  = 0;
    return tm;
}

/// alarm-advance + units -> VALARM on the event.
inline void palmAlarmToVAlarm(int advance, int units,
                              const KCalendarCore::Event::Ptr &event)
{
    auto alarm = event->newAlarm();
    alarm->setType(KCalendarCore::Alarm::Display);
    alarm->setEnabled(true);
    // Palm advance is "how long BEFORE the event"; iCal TRIGGER uses
    // negative duration (Duration ctor takes seconds).
    int seconds = 0;
    switch (units) {
        case 0: seconds = advance * 60;           break;  // minutes
        case 1: seconds = advance * 60 * 60;      break;  // hours
        case 2: seconds = advance * 60 * 60 * 24; break;  // days
        default: seconds = advance * 60;          break;
    }
    alarm->setStartOffset(KCalendarCore::Duration(-seconds));
}

inline bool vAlarmToPalmAlarm(const KCalendarCore::Event::Ptr &event,
                              int *advanceOut, int *unitsOut)
{
    if (event->alarms().isEmpty()) {
        return false;
    }
    const auto alarm = event->alarms().first();
    if (!alarm->enabled()) {
        return false;
    }
    const int secondsBefore = -alarm->startOffset().asSeconds();
    if (secondsBefore <= 0) {
        return false;
    }
    // Pick the largest unit that divides cleanly; prefer days > hours > minutes.
    if (secondsBefore % (60 * 60 * 24) == 0) {
        *unitsOut = 2;
        *advanceOut = secondsBefore / (60 * 60 * 24);
    } else if (secondsBefore % (60 * 60) == 0) {
        *unitsOut = 1;
        *advanceOut = secondsBefore / (60 * 60);
    } else {
        *unitsOut = 0;
        *advanceOut = secondsBefore / 60;
    }
    return true;
}

/// Palm repeat block -> KCalendarCore::RecurrenceRule.
inline void palmRepeatToRRule(const Appointment_t &a,
                              const KCalendarCore::Event::Ptr &event)
{
    using namespace KCalendarCore;

    auto *recurrence = event->recurrence();
    if (!recurrence) return;

    switch (a.repeatType) {
        case repeatDaily:
            recurrence->setDaily(a.repeatFrequency ? a.repeatFrequency : 1);
            break;
        case repeatWeekly: {
            recurrence->setWeekly(a.repeatFrequency ? a.repeatFrequency : 1);
            // Palm repeatDays[0..6] is Sun..Sat; KCal uses Mon=0..Sun=6.
            QBitArray days(7);
            const int palmToKCalMon[7] = { 6, 0, 1, 2, 3, 4, 5 };
            for (int i = 0; i < 7; ++i) {
                if (a.repeatDays[i]) {
                    days.setBit(palmToKCalMon[i]);
                }
            }
            if (days.count(true) > 0) {
                recurrence->addWeeklyDays(days);
            }
            break;
        }
        case repeatMonthlyByDay:
            recurrence->setMonthly(a.repeatFrequency ? a.repeatFrequency : 1);
            break;
        case repeatMonthlyByDate:
            recurrence->setMonthly(a.repeatFrequency ? a.repeatFrequency : 1);
            recurrence->addMonthlyDate(a.begin.tm_mday);
            break;
        case repeatYearly:
            recurrence->setYearly(a.repeatFrequency ? a.repeatFrequency : 1);
            break;
        case repeatNone:
        default:
            return;
    }

    if (!a.repeatForever) {
        const auto endDt = palmTmToQDateTime(a.repeatEnd, a.event != 0);
        if (endDt.isValid()) {
            recurrence->setEndDateTime(endDt);
        }
    }
}

inline void rruleToPalmRepeat(const KCalendarCore::Event::Ptr &event,
                              Appointment_t &a)
{
    using namespace KCalendarCore;
    if (!event->recurs()) {
        a.repeatType = repeatNone;
        a.repeatForever = 1;
        a.repeatFrequency = 0;
        return;
    }

    auto *recurrence = event->recurrence();
    const auto freq = recurrence->recurrenceType();
    a.repeatFrequency = recurrence->frequency();
    a.repeatForever = recurrence->duration() == -1 ? 1 : 0;
    if (!a.repeatForever) {
        a.repeatEnd = qDateTimeToPalmTm(recurrence->endDateTime(),
                                        event->allDay());
    }

    switch (freq) {
        case Recurrence::rDaily:
            a.repeatType = repeatDaily;
            break;
        case Recurrence::rWeekly: {
            a.repeatType = repeatWeekly;
            const auto days = recurrence->days();
            // KCal days: Mon=0..Sun=6. Palm days: Sun=0..Sat=6.
            const int kCalToPalm[7] = { 1, 2, 3, 4, 5, 6, 0 };
            for (int i = 0; i < 7; ++i) {
                if (i < days.size() && days.testBit(i)) {
                    a.repeatDays[kCalToPalm[i]] = 1;
                }
            }
            break;
        }
        case Recurrence::rMonthlyDay:
        case Recurrence::rMonthlyPos:
            a.repeatType = repeatMonthlyByDate;
            break;
        case Recurrence::rYearlyMonth:
        case Recurrence::rYearlyDay:
        case Recurrence::rYearlyPos:
            a.repeatType = repeatYearly;
            break;
        default:
            a.repeatType = repeatNone;
            break;
    }
}

} // namespace

DatebookCodec::DecodeResult
DatebookCodec::decode(const PalmRecord &record)
{
    DecodeResult result;
    result.slot = static_cast<int>(record.category);

    if (record.isDeleted()) {
        result.failureReason = QStringLiteral("deleted");
        return result;
    }

    if (record.data.isEmpty()) {
        result.failureReason = QStringLiteral("empty-record");
        return result;
    }

    // Unpack via pisock.
    ScopedAppointment appt;
    ScopedBuffer buf(record.data.size());
    if (!buf.buf) {
        result.failureReason = QStringLiteral("pi-buffer-alloc-failed");
        return result;
    }
    pi_buffer_append(buf.buf, record.data.constData(),
                     static_cast<std::size_t>(record.data.size()));

    const int rc = unpack_Appointment(&appt.a, buf.buf, datebook_v1);
    if (rc < 0) {
        result.failureReason = QStringLiteral("unpack-failed:rc=%1").arg(rc);
        return result;
    }

    // Build the Event and populate content.
    auto event = KCalendarCore::Event::Ptr::create();
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::RecordIdProperty),
                             QString::number(record.recordId));
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::CategorySlotProperty),
                             QString::number(record.category));
    event->setUid(QStringLiteral("palm-datebook-%1").arg(record.recordId));

    // Description -> SUMMARY. Palm uses Windows-1252; QString::fromLatin1
    // accepts the subset below 0x80 correctly, high-bit chars may need
    // cp1252 transliteration (deferred — see plan "Scope not in E.6").
    if (appt.a.description) {
        event->setSummary(QString::fromLatin1(appt.a.description));
    }
    if (appt.a.note) {
        event->setDescription(QString::fromLatin1(appt.a.note));
    }

    // Begin/end times. `event` flag non-zero => untimed (all-day).
    const bool untimed = appt.a.event != 0;
    const auto beginDt = palmTmToQDateTime(appt.a.begin, untimed);
    if (beginDt.isValid()) {
        event->setDtStart(beginDt);
        event->setAllDay(untimed);
    }
    if (!untimed) {
        const auto endDt = palmTmToQDateTime(appt.a.end, /*untimed=*/false);
        if (endDt.isValid()) {
            event->setDtEnd(endDt);
        }
    }

    // Alarm.
    if (appt.a.alarm) {
        palmAlarmToVAlarm(appt.a.advance, appt.a.advanceUnits, event);
    }

    // Repeat.
    if (appt.a.repeatType != repeatNone) {
        palmRepeatToRRule(appt.a, event);
    }

    // Exceptions (EXDATE). Present only when exceptions > 0 and
    // appt.a.exception is non-null.
    for (int i = 0; i < appt.a.exceptions; ++i) {
        const auto exDt = palmTmToQDateTime(appt.a.exception[i], untimed);
        if (exDt.isValid() && event->recurrence()) {
            event->recurrence()->addExDateTime(exDt);
        }
    }

    // Private flag: AttrSecret -> CLASS:PRIVATE.
    if (record.isSecret()) {
        event->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
    }

    result.event = event;
    return result;
}

PalmRecord DatebookCodec::encode(const KCalendarCore::Event::Ptr &event,
                                 int slot)
{
    PalmRecord rec;
    if (!event) {
        return rec;  // Empty rec signals failure upstream.
    }

    rec.category = static_cast<std::uint8_t>(std::clamp(slot, 0, 15));

    // Record ID from X-WP-PALM-RECORDID if present.
    const auto idStr = event->customProperty(
        "KCalendarCore", QByteArray(DatebookCodec::RecordIdProperty));
    if (!idStr.isEmpty()) {
        bool ok = false;
        const auto id = idStr.toUInt(&ok);
        if (ok) {
            rec.recordId = id;
        }
    }

    // Pack via pisock. Build an Appointment_t from the event fields.
    ScopedAppointment appt;

    const bool untimed = event->allDay();
    appt.a.event = untimed ? 1 : 0;

    const auto dtStart = event->dtStart();
    appt.a.begin = qDateTimeToPalmTm(dtStart, untimed);
    if (!untimed) {
        const auto dtEnd = event->dtEnd().isValid() ? event->dtEnd() : dtStart;
        appt.a.end = qDateTimeToPalmTm(dtEnd, false);
    } else {
        appt.a.end = appt.a.begin;
    }

    // Description / note. strdup so pisock can free_Appointment them.
    const auto summary = event->summary().toLatin1();
    if (!summary.isEmpty()) {
        appt.a.description = ::strdup(summary.constData());
    }
    const auto notes = event->description().toLatin1();
    if (!notes.isEmpty()) {
        appt.a.note = ::strdup(notes.constData());
    }

    // Alarm.
    int advance = 0, units = 0;
    if (vAlarmToPalmAlarm(event, &advance, &units)) {
        appt.a.alarm = 1;
        appt.a.advance = advance;
        appt.a.advanceUnits = units;
    }

    // Repeat.
    rruleToPalmRepeat(event, appt.a);

    // Exceptions (EXDATE).
    const auto exDates = event->recurs()
        ? event->recurrence()->exDateTimes()
        : QList<QDateTime>{};
    if (!exDates.isEmpty()) {
        appt.a.exceptions = exDates.size();
        appt.a.exception = static_cast<std::tm *>(
            ::calloc(exDates.size(), sizeof(std::tm)));
        for (int i = 0; i < exDates.size(); ++i) {
            appt.a.exception[i] = qDateTimeToPalmTm(exDates[i], untimed);
        }
    }

    ScopedBuffer buf(1024);
    if (!buf.buf) {
        return {};
    }
    const int rc = pack_Appointment(&appt.a, buf.buf, datebook_v1);
    if (rc < 0) {
        return {};
    }

    rec.data = QByteArray(reinterpret_cast<const char *>(buf.buf->data),
                          static_cast<int>(buf.buf->used));
    rec.lastModified = QDateTime::currentDateTimeUtc();

    // Private flag: CLASS:PRIVATE -> AttrSecret.
    if (event->secrecy() == KCalendarCore::Incidence::SecrecyPrivate) {
        rec.attributes |= PalmRecord::AttrSecret;
    }
    return rec;
}

} // namespace WildPalms::PalmCalendar
