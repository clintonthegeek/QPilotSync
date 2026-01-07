#include "todomapper.h"
#include <pi-todo.h>
#include <QRegularExpression>
#include <QDate>
#include <QTime>
#include <QStringConverter>

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

// Helper to format QDateTime as iCalendar date (DATE format only, no time)
static QString formatDate(const QDateTime &dt)
{
    // DATE format: YYYYMMDD
    return dt.toString("yyyyMMdd");
}

// Helper to decode Palm text which uses Windows-1252 encoding
// Palm OS uses Windows-1252 (not pure Latin-1) for the 0x80-0x9F range
// This includes characters like ™ (0x99), smart quotes, em/en dashes, etc.
static QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);

    // Manually map Windows-1252 characters in the 0x80-0x9F range
    // This range is undefined in ISO-8859-1 but defined in Windows-1252
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            // Map Windows-1252 C1 controls to their Unicode equivalents
            static const unsigned short cp1252_to_unicode[] = {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
                0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
            };

            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
}

TodoMapper::TodoMapper(QObject *parent)
    : QObject(parent)
{
}

TodoMapper::~TodoMapper()
{
}

TodoMapper::Todo TodoMapper::unpackTodo(const PilotRecord *record)
{
    Todo todo;
    todo.recordId = record->recordId();
    todo.category = record->category();
    todo.isPrivate = record->isSecret();
    todo.isDirty = record->isDirty();
    todo.isDeleted = record->isDeleted();

    // Unpack using pilot-link's ToDo parser
    ToDo_t palmTodo;
    memset(&palmTodo, 0, sizeof(palmTodo));

    pi_buffer_t *buf = pi_buffer_new(record->size());
    memcpy(buf->data, record->rawData(), record->size());
    buf->used = record->size();

    if (unpack_ToDo(&palmTodo, buf, todo_v1) < 0) {
        pi_buffer_free(buf);
        return todo;  // Return empty todo on error
    }

    pi_buffer_free(buf);

    // Extract fields
    if (palmTodo.description) {
        todo.description = decodePalmText(palmTodo.description);
    }
    if (palmTodo.note) {
        todo.note = decodePalmText(palmTodo.note);
    }

    todo.priority = palmTodo.priority;
    todo.isComplete = (palmTodo.complete != 0);
    todo.hasIndefiniteDue = (palmTodo.indefinite != 0);

    if (!todo.hasIndefiniteDue) {
        QDate dueDate(palmTodo.due.tm_year + 1900,
                     palmTodo.due.tm_mon + 1,
                     palmTodo.due.tm_mday);
        // ToDos use date only, no time
        todo.due = QDateTime(dueDate, QTime(0, 0, 0));
    }

    free_ToDo(&palmTodo);

    return todo;
}

QString TodoMapper::todoToICal(const Todo &todo, const QString &categoryName)
{
    QString ical;

    // iCalendar 2.0 format (RFC 5545)
    ical += "BEGIN:VCALENDAR\r\n";
    ical += "VERSION:2.0\r\n";
    ical += "PRODID:-//QPilotSync//NONSGML v0.1//EN\r\n";
    ical += "BEGIN:VTODO\r\n";

    // UID - using Palm record ID
    ical += foldLine(QString("UID:palm-todo-%1").arg(todo.recordId));

    // DTSTAMP - current time as creation time
    QString dtstamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
    ical += foldLine(QString("DTSTAMP:%1").arg(dtstamp));

    // SUMMARY - task title
    if (!todo.description.isEmpty()) {
        // Escape special characters per RFC 5545 section 3.3.11
        QString summary = todo.description;
        summary.replace("\\", "\\\\");
        summary.replace(";", "\\;");
        summary.replace(",", "\\,");
        summary.replace("\n", "\\n");
        ical += foldLine(QString("SUMMARY:%1").arg(summary));
    }

    // DESCRIPTION - detailed notes
    if (!todo.note.isEmpty()) {
        QString description = todo.note;
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
    if (todo.isPrivate) {
        ical += "CLASS:PRIVATE\r\n";
    }

    // PRIORITY
    // Palm: 1 (highest) to 5 (lowest)
    // iCalendar: 1 (highest) to 9 (lowest), 0 = undefined
    // Mapping: Palm 1->iCal 1, Palm 2->iCal 3, Palm 3->iCal 5, Palm 4->iCal 7, Palm 5->iCal 9
    if (todo.priority >= 1 && todo.priority <= 5) {
        int icalPriority = (todo.priority - 1) * 2 + 1;  // Maps 1,2,3,4,5 to 1,3,5,7,9
        ical += foldLine(QString("PRIORITY:%1").arg(icalPriority));
    }

    // DUE - due date (if not indefinite)
    if (!todo.hasIndefiniteDue && todo.due.isValid()) {
        // Use DATE format (not DATE-TIME) for todos
        ical += foldLine(QString("DUE;VALUE=DATE:%1").arg(formatDate(todo.due)));
    }

    // STATUS and COMPLETED
    if (todo.isComplete) {
        ical += "STATUS:COMPLETED\r\n";
        // COMPLETED timestamp - we don't have actual completion time, use current time
        QString completed = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        ical += foldLine(QString("COMPLETED:%1").arg(completed));
        // PERCENT-COMPLETE
        ical += "PERCENT-COMPLETE:100\r\n";
    } else {
        ical += "STATUS:NEEDS-ACTION\r\n";
        ical += "PERCENT-COMPLETE:0\r\n";
    }

    ical += "END:VTODO\r\n";
    ical += "END:VCALENDAR\r\n";

    return ical;
}

QString TodoMapper::generateFilename(const Todo &todo)
{
    QString filename;

    // Use todo description as base filename
    if (!todo.description.isEmpty()) {
        filename = todo.description.left(50);

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

    // If empty after sanitization, use priority + record ID
    if (filename.isEmpty()) {
        filename = QString("todo_p%1_%2")
            .arg(todo.priority)
            .arg(todo.recordId);
    }

    // Add .ics extension
    filename += ".ics";

    return filename;
}
