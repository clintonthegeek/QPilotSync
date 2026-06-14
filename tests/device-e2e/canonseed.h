#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace WildPalms {
namespace DeviceE2E {

// A minimal calendar event to seed into the hub. Datetimes are UTC.
// categories are intentionally omitted (event lands in Unfiled / slot 0) to
// avoid the device AppInfo category-reconciliation dependency.
struct CanonCalendarEventSpec {
    QString uid = QStringLiteral("seed-event-001@wildpalms");
    QString summary = QStringLiteral("Seeded Event");        // -> Palm appointment description (title)
    QString description = QStringLiteral("Note body text");  // -> Palm note
    QDateTime start = QDateTime(QDate(2026, 7, 1), QTime(9, 0, 0), QTimeZone::utc());
    QDateTime end = QDateTime(QDate(2026, 7, 1), QTime(10, 0, 0), QTimeZone::utc());
    bool allDay = false;
    bool withAlarm = true;
    int alarmOffsetSeconds = -600; // 10 minutes before
};

// Returns the canon-envelope JSON bytes for the hub's "calendar" collection.
QByteArray buildCanonCalendarEvent(const CanonCalendarEventSpec &spec);

} // namespace DeviceE2E
} // namespace WildPalms
