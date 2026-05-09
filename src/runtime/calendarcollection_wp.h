#ifndef WILDPALMS_RUNTIME_CALENDARCOLLECTION_WP_H
#define WILDPALMS_RUNTIME_CALENDARCOLLECTION_WP_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

#include <icalendarcollection.h>

namespace WildPalms::FullSync {

class CalendarCollection_WP : public Kalburator::Sync::ICalendarCollection
{
public:
    explicit CalendarCollection_WP(QString id);
    ~CalendarCollection_WP() override;

    // Kalburator::Sync::ICalendarCollection
    QString id() const override;
    KCalendarCore::MemoryCalendar* calendar(const QString &calendarId) const override;
    QList<KCalendarCore::MemoryCalendar*> calendars() const override;
    void addCalendar(KCalendarCore::MemoryCalendar *calendar) override;
    void clear();
    void setCalendarColor(const QString &calendarId, const QColor &color) override;
    void setCalendarVisible(const QString &calendarId, bool visible) override;

    // Host-side accessors (not part of the library surface). Phase F will
    // wire the model layer through these.
    QColor calendarColor(const QString &calendarId) const;
    bool isCalendarVisible(const QString &calendarId) const;

private:
    QString m_id;
    QHash<QString, KCalendarCore::MemoryCalendar*> m_calendars;
    QHash<QString, QColor> m_colors;
    QHash<QString, bool> m_visibility;
};

} // namespace WildPalms::FullSync

#endif
