/**
 * @file test_todomapper.cpp
 * @brief Unit tests for TodoMapper class
 *
 * Tests the conversion between Palm ToDo format and iCalendar VTODO (RFC 5545).
 */

#include <QtTest/QtTest>
#include <QDebug>
#include "mappers/todomapper.h"

class TestTodoMapper : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========== todoToICal Tests ==========
    void testGenerateSimpleICal();
    void testGenerateICalWithDueDate();
    void testGenerateICalWithNoDueDate();
    void testGenerateICalWithPriority();
    void testGenerateICalCompleted();
    void testGenerateICalWithNote();
    void testGenerateICalWithCategory();
    void testGenerateICalPrivate();

    // ========== iCalToTodo Tests ==========
    void testParseSimpleVTodo();
    void testParseVTodoWithDueDate();
    void testParseVTodoWithPriority();
    void testParseVTodoCompleted();
    void testParseVTodoWithDescription();
    void testParseVTodoWithCategory();
    void testParseVTodoWithPercentComplete();
    void testParseVTodoPrivate();

    // ========== Priority Mapping Tests ==========
    void testPriorityMappingPalmToICal();
    void testPriorityMappingICalToPalm();

    // ========== Round-trip Tests ==========
    void testRoundTripSimpleTodo();
    void testRoundTripTodoWithDueDate();
    void testRoundTripTodoComplete();
    void testRoundTripTodoWithNote();

    // ========== Edge Cases ==========
    void testParseEmptyInput();
    void testParseInvalidInput();
    void testParseNoVTodo();
    void testSpecialCharactersInSummary();

    // ========== Filename Generation ==========
    void testGenerateFilenameFromDescription();
    void testGenerateFilenameWithSpecialChars();
    void testGenerateFilenameEmptyDescription();
    void testGenerateFilenameLongDescription();

private:
    QString makeVTodo(const QString &summary, const QStringList &extraProps = {});
};

void TestTodoMapper::initTestCase()
{
    qDebug() << "Starting TodoMapper tests";
}

void TestTodoMapper::cleanupTestCase()
{
    qDebug() << "TodoMapper tests complete";
}

QString TestTodoMapper::makeVTodo(const QString &summary, const QStringList &extraProps)
{
    QString ical;
    ical += "BEGIN:VCALENDAR\r\n";
    ical += "VERSION:2.0\r\n";
    ical += "PRODID:-//Test//NONSGML//EN\r\n";
    ical += "BEGIN:VTODO\r\n";
    ical += "DTSTAMP:20240101T120000Z\r\n";
    ical += QString("SUMMARY:%1\r\n").arg(summary);
    for (const QString &prop : extraProps) {
        ical += prop + "\r\n";
    }
    ical += "END:VTODO\r\n";
    ical += "END:VCALENDAR\r\n";
    return ical;
}

// ========== todoToICal Tests ==========

void TestTodoMapper::testGenerateSimpleICal()
{
    TodoMapper::Todo todo;
    todo.recordId = 12345;
    todo.category = 0;
    todo.description = "Buy groceries";
    todo.priority = 3;
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(todo);

    QVERIFY(ical.contains("BEGIN:VCALENDAR"));
    QVERIFY(ical.contains("BEGIN:VTODO"));
    QVERIFY(ical.contains("SUMMARY:Buy groceries"));
    QVERIFY(ical.contains("UID:palm-todo-12345"));
    QVERIFY(ical.contains("STATUS:NEEDS-ACTION"));
    QVERIFY(ical.contains("END:VTODO"));
    QVERIFY(ical.contains("END:VCALENDAR"));
}

void TestTodoMapper::testGenerateICalWithDueDate()
{
    TodoMapper::Todo todo;
    todo.recordId = 100;
    todo.description = "Submit report";
    todo.priority = 1;
    todo.isComplete = false;
    todo.hasIndefiniteDue = false;
    todo.due = QDateTime(QDate(2024, 12, 31), QTime(0, 0, 0));

    QString ical = TodoMapper::todoToICal(todo);

    QVERIFY(ical.contains("DUE;VALUE=DATE:20241231"));
}

void TestTodoMapper::testGenerateICalWithNoDueDate()
{
    TodoMapper::Todo todo;
    todo.recordId = 200;
    todo.description = "Someday task";
    todo.priority = 5;
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(todo);

    QVERIFY(!ical.contains("DUE"));
}

void TestTodoMapper::testGenerateICalWithPriority()
{
    TodoMapper::Todo todo;
    todo.recordId = 300;
    todo.description = "High priority task";
    todo.priority = 1;  // Palm highest
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(todo);

    // Palm 1 -> iCal 1
    QVERIFY(ical.contains("PRIORITY:1"));
}

void TestTodoMapper::testGenerateICalCompleted()
{
    TodoMapper::Todo todo;
    todo.recordId = 400;
    todo.description = "Finished task";
    todo.priority = 3;
    todo.isComplete = true;
    todo.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(todo);

    QVERIFY(ical.contains("STATUS:COMPLETED"));
    QVERIFY(ical.contains("PERCENT-COMPLETE:100"));
    QVERIFY(ical.contains("COMPLETED:"));  // Should have completion timestamp
}

void TestTodoMapper::testGenerateICalWithNote()
{
    TodoMapper::Todo todo;
    todo.recordId = 500;
    todo.description = "Task with notes";
    todo.note = "Additional details here";
    todo.priority = 3;
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(todo);

    QVERIFY(ical.contains("DESCRIPTION:Additional details here"));
}

void TestTodoMapper::testGenerateICalWithCategory()
{
    TodoMapper::Todo todo;
    todo.recordId = 600;
    todo.description = "Work task";
    todo.priority = 2;
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(todo, "Business");

    QVERIFY(ical.contains("CATEGORIES:Business"));
}

void TestTodoMapper::testGenerateICalPrivate()
{
    TodoMapper::Todo todo;
    todo.recordId = 700;
    todo.description = "Private task";
    todo.priority = 3;
    todo.isComplete = false;
    todo.hasIndefiniteDue = true;
    todo.isPrivate = true;

    QString ical = TodoMapper::todoToICal(todo);

    QVERIFY(ical.contains("CLASS:PRIVATE"));
}

// ========== iCalToTodo Tests ==========

void TestTodoMapper::testParseSimpleVTodo()
{
    QString ical = makeVTodo("Simple task");

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.description, QString("Simple task"));
}

void TestTodoMapper::testParseVTodoWithDueDate()
{
    QStringList props;
    props << "DUE;VALUE=DATE:20241225";

    QString ical = makeVTodo("Christmas shopping", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.description, QString("Christmas shopping"));
    QCOMPARE(todo.hasIndefiniteDue, false);
    QCOMPARE(todo.due.date(), QDate(2024, 12, 25));
}

void TestTodoMapper::testParseVTodoWithPriority()
{
    QStringList props;
    props << "PRIORITY:1";  // iCal highest

    QString ical = makeVTodo("Urgent task", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.priority, 1);  // Palm highest
}

void TestTodoMapper::testParseVTodoCompleted()
{
    QStringList props;
    props << "STATUS:COMPLETED";
    props << "COMPLETED:20240115T120000Z";

    QString ical = makeVTodo("Done task", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.isComplete, true);
}

void TestTodoMapper::testParseVTodoWithDescription()
{
    QStringList props;
    props << "DESCRIPTION:More details about the task";

    QString ical = makeVTodo("Task with details", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.description, QString("Task with details"));
    QCOMPARE(todo.note, QString("More details about the task"));
}

void TestTodoMapper::testParseVTodoWithCategory()
{
    QStringList props;
    props << "CATEGORIES:Personal,Important";

    QString ical = makeVTodo("Categorized task", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.categoryName, QString("Personal"));  // Takes first category
}

void TestTodoMapper::testParseVTodoWithPercentComplete()
{
    QStringList props;
    props << "PERCENT-COMPLETE:100";

    QString ical = makeVTodo("Percent-complete task", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.isComplete, true);
}

void TestTodoMapper::testParseVTodoPrivate()
{
    QStringList props;
    props << "CLASS:PRIVATE";

    QString ical = makeVTodo("Secret task", props);

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QCOMPARE(todo.isPrivate, true);
}

// ========== Priority Mapping Tests ==========

void TestTodoMapper::testPriorityMappingPalmToICal()
{
    // Palm priority 1-5 should map to iCal 1,3,5,7,9
    QList<QPair<int, int>> mappings = {
        {1, 1}, {2, 3}, {3, 5}, {4, 7}, {5, 9}
    };

    for (const auto &mapping : mappings) {
        TodoMapper::Todo todo;
        todo.recordId = mapping.first * 100;
        todo.description = QString("Priority %1").arg(mapping.first);
        todo.priority = mapping.first;
        todo.hasIndefiniteDue = true;
        todo.isComplete = false;

        QString ical = TodoMapper::todoToICal(todo);
        QVERIFY2(ical.contains(QString("PRIORITY:%1").arg(mapping.second)),
                 qPrintable(QString("Palm priority %1 should map to iCal %2").arg(mapping.first).arg(mapping.second)));
    }
}

void TestTodoMapper::testPriorityMappingICalToPalm()
{
    // iCal priority 1-9 should map to Palm 1-5
    // iCal 1,2->Palm 1; 3,4->2; 5,6->3; 7,8->4; 9->5
    QList<QPair<int, int>> mappings = {
        {1, 1}, {2, 1}, {3, 2}, {4, 2}, {5, 3}, {6, 3}, {7, 4}, {8, 4}, {9, 5}
    };

    for (const auto &mapping : mappings) {
        QStringList props;
        props << QString("PRIORITY:%1").arg(mapping.first);
        QString ical = makeVTodo(QString("Priority %1 task").arg(mapping.first), props);

        TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);
        QCOMPARE(todo.priority, mapping.second);
    }
}

// ========== Round-trip Tests ==========

void TestTodoMapper::testRoundTripSimpleTodo()
{
    TodoMapper::Todo original;
    original.recordId = 1000;
    original.description = "Round trip task";
    original.priority = 3;
    original.isComplete = false;
    original.hasIndefiniteDue = true;
    original.isPrivate = false;

    QString ical = TodoMapper::todoToICal(original);
    TodoMapper::Todo parsed = TodoMapper::iCalToTodo(ical);

    QCOMPARE(parsed.recordId, original.recordId);
    QCOMPARE(parsed.description, original.description);
    QCOMPARE(parsed.priority, original.priority);
    QCOMPARE(parsed.isComplete, original.isComplete);
}

void TestTodoMapper::testRoundTripTodoWithDueDate()
{
    TodoMapper::Todo original;
    original.recordId = 2000;
    original.description = "Due date task";
    original.priority = 2;
    original.isComplete = false;
    original.hasIndefiniteDue = false;
    original.due = QDateTime(QDate(2024, 6, 15), QTime(0, 0, 0));

    QString ical = TodoMapper::todoToICal(original);
    TodoMapper::Todo parsed = TodoMapper::iCalToTodo(ical);

    QCOMPARE(parsed.description, original.description);
    QCOMPARE(parsed.hasIndefiniteDue, false);
    QCOMPARE(parsed.due.date(), original.due.date());
}

void TestTodoMapper::testRoundTripTodoComplete()
{
    TodoMapper::Todo original;
    original.recordId = 3000;
    original.description = "Completed task";
    original.priority = 4;
    original.isComplete = true;
    original.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(original);
    TodoMapper::Todo parsed = TodoMapper::iCalToTodo(ical);

    QCOMPARE(parsed.isComplete, true);
}

void TestTodoMapper::testRoundTripTodoWithNote()
{
    TodoMapper::Todo original;
    original.recordId = 4000;
    original.description = "Task with note";
    original.note = "These are the detailed notes";
    original.priority = 3;
    original.isComplete = false;
    original.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(original);
    TodoMapper::Todo parsed = TodoMapper::iCalToTodo(ical);

    QCOMPARE(parsed.description, original.description);
    QCOMPARE(parsed.note, original.note);
}

// ========== Edge Cases ==========

void TestTodoMapper::testParseEmptyInput()
{
    TodoMapper::Todo todo = TodoMapper::iCalToTodo("");

    QVERIFY(todo.description.isEmpty());
    QCOMPARE(todo.priority, 3);  // Default
    QCOMPARE(todo.isComplete, false);
}

void TestTodoMapper::testParseInvalidInput()
{
    TodoMapper::Todo todo = TodoMapper::iCalToTodo("This is not iCalendar data");

    QVERIFY(todo.description.isEmpty());
}

void TestTodoMapper::testParseNoVTodo()
{
    // Valid VCALENDAR but no VTODO inside
    QString ical = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nEND:VCALENDAR\r\n";

    TodoMapper::Todo todo = TodoMapper::iCalToTodo(ical);

    QVERIFY(todo.description.isEmpty());
}

void TestTodoMapper::testSpecialCharactersInSummary()
{
    TodoMapper::Todo original;
    original.recordId = 5000;
    original.description = "Task with special chars: comma, semicolon; newline\nhere";
    original.priority = 3;
    original.isComplete = false;
    original.hasIndefiniteDue = true;

    QString ical = TodoMapper::todoToICal(original);

    // Special characters should be escaped
    QVERIFY(ical.contains("\\,") || !ical.contains("comma,"));
    QVERIFY(ical.contains("\\;") || !ical.contains("semicolon;"));
    QVERIFY(ical.contains("\\n") || !ical.contains("\n"));

    // Round trip should preserve content
    TodoMapper::Todo parsed = TodoMapper::iCalToTodo(ical);
    QCOMPARE(parsed.description, original.description);
}

// ========== Filename Generation ==========

void TestTodoMapper::testGenerateFilenameFromDescription()
{
    TodoMapper::Todo todo;
    todo.recordId = 123;
    todo.description = "Buy groceries";
    todo.priority = 3;

    QString filename = TodoMapper::generateFilename(todo);

    QVERIFY(filename.endsWith(".ics"));
    QVERIFY(filename.contains("Buy"));
    QVERIFY(filename.contains("groceries"));
}

void TestTodoMapper::testGenerateFilenameWithSpecialChars()
{
    TodoMapper::Todo todo;
    todo.recordId = 456;
    todo.description = "Meeting @ 3:00pm!";
    todo.priority = 2;

    QString filename = TodoMapper::generateFilename(todo);

    QVERIFY(filename.endsWith(".ics"));
    QVERIFY(!filename.contains("@"));
    QVERIFY(!filename.contains(":"));
    QVERIFY(!filename.contains("!"));
}

void TestTodoMapper::testGenerateFilenameEmptyDescription()
{
    TodoMapper::Todo todo;
    todo.recordId = 789;
    todo.description = "";
    todo.priority = 1;

    QString filename = TodoMapper::generateFilename(todo);

    QVERIFY(filename.endsWith(".ics"));
    // Should fall back to priority + record ID
    QVERIFY(filename.contains("789") || filename.contains("todo"));
}

void TestTodoMapper::testGenerateFilenameLongDescription()
{
    TodoMapper::Todo todo;
    todo.recordId = 111;
    todo.description = "This is a very long task description that should be "
                       "truncated because it exceeds the maximum filename length";
    todo.priority = 3;

    QString filename = TodoMapper::generateFilename(todo);

    QVERIFY(filename.endsWith(".ics"));
    QVERIFY(filename.length() <= 55);  // 50 + ".ics"
}

QTEST_MAIN(TestTodoMapper)
#include "test_todomapper.moc"
