#include "todomapper.h"
#include <pi-todo.h>
#include <QRegularExpression>
#include <QDate>
#include <QTime>
#include <QStringConverter>
#include <QTimeZone>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

// Windows-1252 to Unicode mapping table for 0x80-0x9F
static const unsigned short cp1252_to_unicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
};

// Helper to decode Palm text which uses Windows-1252 encoding
static QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
}

// Helper to encode Unicode text to Windows-1252 for Palm
static QByteArray encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());

    for (QChar ch : text) {
        ushort unicode = ch.unicode();

        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && !(unicode >= 0x80 && unicode <= 0x9F)) {
            result.append(static_cast<char>(unicode));
        } else {
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (cp1252_to_unicode[i] == unicode) {
                    result.append(static_cast<char>(0x80 + i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.append('?');
            }
        }
    }

    return result;
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

KCalendarCore::Todo::Ptr TodoMapper::todoToKCalTodo(const Todo &todo, const QString &categoryName)
{
    auto kcalTodo = KCalendarCore::Todo::Ptr::create();

    // UID - using Palm record ID
    kcalTodo->setUid(QStringLiteral("palm-todo-%1").arg(todo.recordId));

    // SUMMARY and DESCRIPTION
    if (!todo.description.isEmpty()) {
        kcalTodo->setSummary(todo.description);
    }
    if (!todo.note.isEmpty()) {
        kcalTodo->setDescription(todo.note);
    }

    // CATEGORIES
    if (!categoryName.isEmpty()) {
        kcalTodo->setCategories(QStringList() << categoryName);
    }

    // CLASS - privacy
    if (todo.isPrivate) {
        kcalTodo->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
    }

    // PRIORITY
    // Palm: 1 (highest) to 5 (lowest)
    // iCalendar: 1 (highest) to 9 (lowest), 0 = undefined
    // Mapping: Palm 1->iCal 1, Palm 2->iCal 3, Palm 3->iCal 5, Palm 4->iCal 7, Palm 5->iCal 9
    if (todo.priority >= 1 && todo.priority <= 5) {
        int icalPriority = (todo.priority - 1) * 2 + 1;  // Maps 1,2,3,4,5 to 1,3,5,7,9
        kcalTodo->setPriority(icalPriority);
    }

    // DUE - due date (if not indefinite)
    if (!todo.hasIndefiniteDue && todo.due.isValid()) {
        // Use DATE only for todos (all-day due date)
        kcalTodo->setDtDue(QDateTime(todo.due.date(), QTime()), true);  // true = allDay
    }

    // STATUS and COMPLETED
    if (todo.isComplete) {
        kcalTodo->setStatus(KCalendarCore::Incidence::StatusCompleted);
        kcalTodo->setCompleted(QDateTime::currentDateTimeUtc());
        kcalTodo->setPercentComplete(100);
    } else {
        kcalTodo->setStatus(KCalendarCore::Incidence::StatusNeedsAction);
        kcalTodo->setPercentComplete(0);
    }

    return kcalTodo;
}

QString TodoMapper::todoToICal(const Todo &todo, const QString &categoryName)
{
    // Convert to KCalendarCore todo
    KCalendarCore::Todo::Ptr kcalTodo = todoToKCalTodo(todo, categoryName);

    // Create a calendar and add the todo
    KCalendarCore::MemoryCalendar::Ptr calendar(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    calendar->addTodo(kcalTodo);

    // Use ICalFormat to serialize
    KCalendarCore::ICalFormat icalFormat;
    QString icalString = icalFormat.toString(calendar);

    return icalString;
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
        filename = QStringLiteral("todo_p%1_%2")
            .arg(todo.priority)
            .arg(todo.recordId);
    }

    // Add .ics extension
    filename += ".ics";

    return filename;
}

// ========== Reverse mapping: iCalendar VTODO → Palm ==========

TodoMapper::Todo TodoMapper::kCalTodoToTodo(const KCalendarCore::Todo::Ptr &kcalTodo)
{
    Todo todo;
    todo.recordId = 0;
    todo.category = 0;
    todo.priority = 3;  // Default middle priority
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;
    todo.isPrivate = false;
    todo.isDirty = false;
    todo.isDeleted = false;

    if (!kcalTodo) {
        return todo;
    }

    // UID - extract record ID if it's in palm-todo-XXXX format
    QString uid = kcalTodo->uid();
    if (uid.startsWith(QLatin1String("palm-todo-"))) {
        bool ok;
        int id = uid.mid(10).toInt(&ok);
        if (ok) todo.recordId = id;
    }

    // SUMMARY and DESCRIPTION
    todo.description = kcalTodo->summary();
    todo.note = kcalTodo->description();

    // CATEGORIES
    QStringList categories = kcalTodo->categories();
    if (!categories.isEmpty()) {
        todo.categoryName = categories.first();
    }

    // CLASS - privacy
    todo.isPrivate = (kcalTodo->secrecy() == KCalendarCore::Incidence::SecrecyPrivate);

    // PRIORITY
    // iCalendar: 1 (highest) to 9 (lowest), 0 = undefined
    // Palm: 1 (highest) to 5 (lowest)
    // Mapping: iCal 1-2->Palm 1, 3-4->2, 5->3, 6-7->4, 8-9->5
    int icalPriority = kcalTodo->priority();
    if (icalPriority >= 1 && icalPriority <= 9) {
        todo.priority = (icalPriority + 1) / 2;  // Maps 1,2->1, 3,4->2, 5,6->3, 7,8->4, 9->5
        if (todo.priority > 5) todo.priority = 5;
        if (todo.priority < 1) todo.priority = 1;
    }

    // DUE - due date
    if (kcalTodo->hasDueDate()) {
        todo.due = kcalTodo->dtDue();
        todo.hasIndefiniteDue = false;
    }

    // STATUS and COMPLETED
    todo.isComplete = kcalTodo->isCompleted();

    return todo;
}

TodoMapper::Todo TodoMapper::iCalToTodo(const QString &ical)
{
    // Use KCalendarCore to parse the iCalendar data
    KCalendarCore::MemoryCalendar::Ptr calendar(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

    KCalendarCore::ICalFormat icalFormat;
    if (!icalFormat.fromString(calendar, ical)) {
        // Parse failed, return empty todo
        Todo todo;
        todo.recordId = 0;
        todo.category = 0;
        todo.priority = 3;
        todo.isComplete = false;
        todo.hasIndefiniteDue = true;
        todo.isPrivate = false;
        todo.isDirty = false;
        todo.isDeleted = false;
        return todo;
    }

    // Get the first todo from the calendar
    KCalendarCore::Todo::List todos = calendar->todos();
    if (todos.isEmpty()) {
        // No todos found, return empty todo
        Todo todo;
        todo.recordId = 0;
        todo.category = 0;
        todo.priority = 3;
        todo.isComplete = false;
        todo.hasIndefiniteDue = true;
        todo.isPrivate = false;
        todo.isDirty = false;
        todo.isDeleted = false;
        return todo;
    }

    return kCalTodoToTodo(todos.first());
}

PilotRecord* TodoMapper::packTodo(const Todo &todo)
{
    // Create ToDo structure
    ToDo_t palmTodo;
    memset(&palmTodo, 0, sizeof(palmTodo));

    // Description and note
    if (!todo.description.isEmpty()) {
        QByteArray descData = encodePalmText(todo.description);
        palmTodo.description = strdup(descData.constData());
    }
    if (!todo.note.isEmpty()) {
        QByteArray noteData = encodePalmText(todo.note);
        palmTodo.note = strdup(noteData.constData());
    }

    palmTodo.priority = todo.priority;
    palmTodo.complete = todo.isComplete ? 1 : 0;
    palmTodo.indefinite = todo.hasIndefiniteDue ? 1 : 0;

    if (!todo.hasIndefiniteDue && todo.due.isValid()) {
        palmTodo.due.tm_year = todo.due.date().year() - 1900;
        palmTodo.due.tm_mon = todo.due.date().month() - 1;
        palmTodo.due.tm_mday = todo.due.date().day();
    }

    // Pack to buffer
    pi_buffer_t *buf = pi_buffer_new(0xFFFF);
    int packResult = pack_ToDo(&palmTodo, buf, todo_v1);

    // Free allocated memory
    free_ToDo(&palmTodo);

    if (packResult < 0) {
        pi_buffer_free(buf);
        return nullptr;
    }

    // Create QByteArray from buffer
    QByteArray data(reinterpret_cast<const char*>(buf->data), buf->used);
    pi_buffer_free(buf);

    // Create attributes from flags
    int attr = 0;
    if (todo.isPrivate) attr |= PilotRecord::AttrSecret;
    if (todo.isDirty) attr |= PilotRecord::AttrDirty;
    if (todo.isDeleted) attr |= PilotRecord::AttrDeleted;

    return new PilotRecord(todo.recordId, todo.category, attr, data);
}
