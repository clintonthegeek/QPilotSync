#include "datebookcodec.h"

#include <algorithm>
#include <cstring>

#include <QByteArray>
#include <QDateTime>
#include <QDebug>

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

    // Build the Event scaffold — content-carrying fields land in task 3.
    auto event = KCalendarCore::Event::Ptr::create();
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::RecordIdProperty),
                             QString::number(record.recordId));
    event->setCustomProperty("KCalendarCore",
                             QByteArray(DatebookCodec::CategorySlotProperty),
                             QString::number(record.category));

    // Generate a stable UID derived from recordId so repeated decodes
    // of the same record produce the same UID.
    event->setUid(QStringLiteral("palm-datebook-%1").arg(record.recordId));

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

    // Pack via pisock. In this task we pack a minimal Appointment_t
    // (just the flag stubs zeroed) — content fields land in task 3.
    ScopedAppointment appt;
    // unpack-then-pack of a minimal appointment needs sensible defaults.
    appt.a.event = 1;           // untimed (all-day) placeholder
    appt.a.alarm = 0;
    appt.a.repeatType = repeatNone;
    appt.a.repeatFrequency = 0;
    appt.a.exceptions = 0;
    appt.a.description = nullptr;
    appt.a.note = nullptr;
    const auto now = QDateTime::currentDateTime();
    std::tm tm{};
    tm.tm_year = now.date().year() - 1900;
    tm.tm_mon  = now.date().month() - 1;
    tm.tm_mday = now.date().day();
    appt.a.begin = tm;
    appt.a.end   = tm;

    ScopedBuffer buf(256);
    if (!buf.buf) {
        return {};
    }
    const int rc = pack_Appointment(&appt.a, buf.buf, datebook_v1);
    if (rc < 0 || buf.buf->used == 0) {
        return {};
    }

    rec.data = QByteArray(reinterpret_cast<const char *>(buf.buf->data),
                          static_cast<int>(buf.buf->used));
    rec.lastModified = QDateTime::currentDateTimeUtc();
    return rec;
}

} // namespace WildPalms::PalmCalendar
