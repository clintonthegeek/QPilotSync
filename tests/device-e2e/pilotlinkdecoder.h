#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace WildPalms {
namespace DeviceE2E {

// One decoded Palm DatebookDB appointment. Field names reflect the PALM side:
// `description` is the Palm appointment description (= the canon summary/title);
// `note` is the Palm note (= the canon description).
struct DecodedAppointment {
    QString description;
    QString note;
    QDateTime begin;   // local time (Palm stores wall-clock; set TZ=UTC for determinism)
    QDateTime end;
    bool allDay = false;
    bool hasAlarm = false;
    int advance = 0;
    int advanceUnits = 0; // pilot-link advMinutes/advHours/advDays
    int category = 0;     // category index from the record header (Unfiled = 0)
};

// Decode a single raw DatebookDB record (the bytes of one appointment).
// `ok` is set false if unpack fails. `category` is the record's category index.
DecodedAppointment decodeAppointmentRecord(const QByteArray &raw, int category, bool *ok = nullptr);

// Decode all non-deleted appointment records from a DatebookDB .pdb file.
QList<DecodedAppointment> readAppointments(const QString &pdbPath);

} // namespace DeviceE2E
} // namespace WildPalms
