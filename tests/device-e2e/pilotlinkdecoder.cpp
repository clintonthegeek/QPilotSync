#include "pilotlinkdecoder.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-datebook.h>
#include <pi-file.h>
#include <pi-dlp.h>
}

#include <ctime>

namespace WildPalms {
namespace DeviceE2E {

static QDateTime tmToQDateTime(const struct tm &t)
{
    // pilot-link struct tm: tm_year is years since 1900, tm_mon is 0-based.
    const QDate date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    const QTime time(t.tm_hour, t.tm_min, t.tm_sec);
    return QDateTime(date, time); // Qt::LocalTime; TZ=UTC makes this deterministic
}

DecodedAppointment decodeAppointmentRecord(const QByteArray &raw, int category, bool *ok)
{
    DecodedAppointment d;
    d.category = category;
    if (ok)
        *ok = false;

    if (raw.isEmpty())
        return d;

    pi_buffer_t *buf = pi_buffer_new(size_t(raw.size()));
    if (!buf)
        return d;
    pi_buffer_append(buf, raw.constData(), size_t(raw.size()));

    Appointment_t a{};
    const int rc = unpack_Appointment(&a, buf, datebook_v1);
    if (rc < 0) {
        pi_buffer_free(buf);
        return d;
    }

    d.allDay = (a.event != 0);
    d.begin = tmToQDateTime(a.begin);
    if (!d.allDay)
        d.end = tmToQDateTime(a.end);
    if (a.description)
        d.description = QString::fromLatin1(a.description);
    if (a.note)
        d.note = QString::fromLatin1(a.note);
    d.hasAlarm = (a.alarm != 0);
    d.advance = a.advance;
    d.advanceUnits = a.advanceUnits;

    free_Appointment(&a);
    pi_buffer_free(buf);
    if (ok)
        *ok = true;
    return d;
}

QList<DecodedAppointment> readAppointments(const QString &pdbPath)
{
    QList<DecodedAppointment> out;
    pi_file_t *pf = pi_file_open(pdbPath.toLocal8Bit().constData());
    if (!pf)
        return out;

    int entries = 0;
    pi_file_get_entries(pf, &entries);
    for (int i = 0; i < entries; ++i) {
        void *rawBuf = nullptr;
        size_t size = 0;
        int attrs = 0;
        int category = 0;
        recordid_t uid = 0;
        if (pi_file_read_record(pf, i, &rawBuf, &size, &attrs, &category, &uid) < 0)
            continue;
        if (attrs & dlpRecAttrDeleted) // skip tombstones (rawBuf belongs to pf; do not free)
            continue;
        const QByteArray raw(reinterpret_cast<const char *>(rawBuf), static_cast<int>(size));
        bool ok = false;
        const DecodedAppointment d = decodeAppointmentRecord(raw, category, &ok);
        if (ok)
            out.append(d);
    }

    pi_file_close(pf);
    return out;
}

} // namespace DeviceE2E
} // namespace WildPalms
