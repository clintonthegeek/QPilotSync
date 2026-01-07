#ifndef CALENDARMAPPER_H
#define CALENDARMAPPER_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QObject>
#include "../palm/pilotrecord.h"

/**
 * @brief Mapper for converting Palm Datebook records to iCalendar format
 *
 * Palm DatebookDB stores appointments/events with time, repeat, and alarm info.
 * This mapper converts them to standard iCalendar 2.0 (RFC 5545) format.
 */
class CalendarMapper : public QObject
{
    Q_OBJECT

public:
    enum RepeatType {
        RepeatNone = 0,
        RepeatDaily = 1,
        RepeatWeekly = 2,
        RepeatMonthlyByDay = 3,
        RepeatMonthlyByDate = 4,
        RepeatYearly = 5
    };

    enum AlarmUnits {
        AlarmMinutes = 0,
        AlarmHours = 1,
        AlarmDays = 2
    };

    struct Event {
        int recordId;
        int category;

        bool isUntimed;           // All-day event flag
        QDateTime begin;
        QDateTime end;

        QString description;      // Summary/title
        QString note;            // Detailed notes

        bool hasAlarm;
        int alarmAdvance;        // How far in advance
        int alarmUnits;          // Minutes/Hours/Days

        int repeatType;          // RepeatType enum
        bool repeatForever;
        QDateTime repeatEnd;
        int repeatFrequency;     // Interval (every N days/weeks/etc)
        int repeatDay;           // For monthlyByDay
        bool repeatDays[7];      // For weekly repeats (Sun-Sat)
        int repeatWeekstart;     // 0=Sunday, 1=Monday, etc

        QList<QDateTime> exceptions;  // Exception dates

        bool isPrivate;
        bool isDirty;
        bool isDeleted;
    };

    explicit CalendarMapper(QObject *parent = nullptr);
    ~CalendarMapper();

    /**
     * @brief Unpack a Palm datebook record into an Event structure
     * @param record The Palm record to unpack
     * @return Event structure with parsed data, or empty Event if invalid
     */
    static Event unpackEvent(const PilotRecord *record);

    /**
     * @brief Convert an Event to iCalendar 2.0 format (RFC 5545)
     * @param event The event to convert
     * @param categoryName Optional category name
     * @return iCalendar string with VCALENDAR and VEVENT
     */
    static QString eventToICal(const Event &event, const QString &categoryName = QString());

    /**
     * @brief Generate a safe filename from event description
     * @param event The event
     * @return Safe filename for the .ics file
     */
    static QString generateFilename(const Event &event);

    // ========== Reverse mapping: iCalendar → Palm ==========

    /**
     * @brief Parse an iCalendar VEVENT into an Event structure
     * @param ical The iCalendar content (RFC 5545 format)
     * @return Event structure with parsed data
     */
    static Event iCalToEvent(const QString &ical);

    /**
     * @brief Pack an Event structure into a Palm record
     * @param event The event to pack
     * @return PilotRecord ready for writing to Palm (caller takes ownership)
     */
    static PilotRecord* packEvent(const Event &event);

signals:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
};

#endif // CALENDARMAPPER_H
