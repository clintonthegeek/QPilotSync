#ifndef TODOMAPPER_H
#define TODOMAPPER_H

#include <QString>
#include <QDateTime>
#include <QObject>
#include "../palm/pilotrecord.h"

/**
 * @brief Mapper for converting Palm ToDo records to iCalendar VTODO format
 *
 * Palm ToDoDB stores tasks with priority, due date, and completion status.
 * This mapper converts them to standard iCalendar 2.0 (RFC 5545) VTODO format.
 */
class TodoMapper : public QObject
{
    Q_OBJECT

public:
    struct Todo {
        int recordId;
        int category;
        QString categoryName;  // Category name parsed from file

        QString description;      // Task title
        QString note;            // Detailed notes

        bool hasIndefiniteDue;   // No due date specified
        QDateTime due;           // Due date (if not indefinite)

        int priority;            // 1 (highest) to 5 (lowest)
        bool isComplete;         // Completion status

        bool isPrivate;
        bool isDirty;
        bool isDeleted;
    };

    explicit TodoMapper(QObject *parent = nullptr);
    ~TodoMapper();

    /**
     * @brief Unpack a Palm ToDo record into a Todo structure
     * @param record The Palm record to unpack
     * @return Todo structure with parsed data, or empty Todo if invalid
     */
    static Todo unpackTodo(const PilotRecord *record);

    /**
     * @brief Convert a Todo to iCalendar 2.0 VTODO format (RFC 5545)
     * @param todo The todo to convert
     * @param categoryName Optional category name
     * @return iCalendar string with VCALENDAR and VTODO
     */
    static QString todoToICal(const Todo &todo, const QString &categoryName = QString());

    /**
     * @brief Generate a safe filename from todo description
     * @param todo The todo
     * @return Safe filename for the .ics file
     */
    static QString generateFilename(const Todo &todo);

    // ========== Reverse mapping: iCalendar VTODO → Palm ==========

    /**
     * @brief Parse an iCalendar VTODO into a Todo structure
     * @param ical The iCalendar content (RFC 5545 format)
     * @return Todo structure with parsed data
     */
    static Todo iCalToTodo(const QString &ical);

    /**
     * @brief Pack a Todo structure into a Palm record
     * @param todo The todo to pack
     * @return PilotRecord ready for writing to Palm (caller takes ownership)
     */
    static PilotRecord* packTodo(const Todo &todo);

signals:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
};

#endif // TODOMAPPER_H
