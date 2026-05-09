#include "calendarcollection_wp.h"

#include <KCalendarCore/MemoryCalendar>

namespace WildPalms::FullSync {

CalendarCollection_WP::CalendarCollection_WP(QString id)
    : m_id(std::move(id))
{
}

CalendarCollection_WP::~CalendarCollection_WP()
{
    qDeleteAll(m_calendars);
}

QString CalendarCollection_WP::id() const
{
    return m_id;
}

KCalendarCore::MemoryCalendar* CalendarCollection_WP::calendar(const QString &calendarId) const
{
    return m_calendars.value(calendarId, nullptr);
}

QList<KCalendarCore::MemoryCalendar*> CalendarCollection_WP::calendars() const
{
    return m_calendars.values();
}

void CalendarCollection_WP::addCalendar(KCalendarCore::MemoryCalendar *calendar)
{
    if (!calendar) {
        return;
    }
    const QString key = calendar->id();
    if (key.isEmpty()) {
        return;
    }
    if (auto *existing = m_calendars.value(key, nullptr); existing && existing != calendar) {
        delete existing;
    }
    m_calendars.insert(key, calendar);
}

void CalendarCollection_WP::clear()
{
    qDeleteAll(m_calendars);
    m_calendars.clear();
    m_colors.clear();
    m_visibility.clear();
}

void CalendarCollection_WP::setCalendarColor(const QString &calendarId, const QColor &color)
{
    if (!m_calendars.contains(calendarId)) {
        return;
    }
    m_colors.insert(calendarId, color);
}

void CalendarCollection_WP::setCalendarVisible(const QString &calendarId, bool visible)
{
    if (!m_calendars.contains(calendarId)) {
        return;
    }
    m_visibility.insert(calendarId, visible);
}

QColor CalendarCollection_WP::calendarColor(const QString &calendarId) const
{
    return m_colors.value(calendarId);
}

bool CalendarCollection_WP::isCalendarVisible(const QString &calendarId) const
{
    return m_visibility.value(calendarId, true);
}

} // namespace WildPalms::FullSync
