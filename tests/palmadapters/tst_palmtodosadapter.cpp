#include <QtTest/QtTest>

#include "categorymappingstore.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmtodosadapter.h"
#include "todocodec.h"

using WildPalms::Palm::Adapters::TodoRow;
using WildPalms::Palm::Adapters::readAllTodos;
using WildPalms::Palm::Adapters::readTodo;
using WildPalms::Palm::Adapters::writeTodo;
using WildPalms::Palm::Adapters::deleteTodo;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

class TestPalmTodosAdapter : public QObject
{
    Q_OBJECT
private slots:
    void writeThenReadAllRoundTrips();
    void readByIdFindsSpecificRecord();
    void categorySlotPreserved();
    void categoryNameResolvedFromStore();
    void deleteRemovesTheRecord();
    void dueDateSurvivesRoundTrip();
    void priorityAndCompletionSurvive();
};

namespace {

Todo makeSample()
{
    Todo t;
    t.description = QStringLiteral("Write plan");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    return t;
}

} // namespace

void TestPalmTodosAdapter::writeThenReadAllRoundTrips()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 0, makeSample());
    QVERIFY(id != 0);
    const auto rows = readAllTodos(&pb, &cats);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().content.description, QStringLiteral("Write plan"));
}

void TestPalmTodosAdapter::readByIdFindsSpecificRecord()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 0, makeSample());
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.priority, 1);
}

void TestPalmTodosAdapter::categorySlotPreserved()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 9, makeSample());
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categorySlot, 9);
}

void TestPalmTodosAdapter::categoryNameResolvedFromStore()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    cats.setSlotName(QStringLiteral("ToDoDB"), 9, QStringLiteral("Project"));
    const auto id = writeTodo(&pb, 9, makeSample());
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categoryName, QStringLiteral("Project"));
}

void TestPalmTodosAdapter::deleteRemovesTheRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 0, makeSample());
    deleteTodo(&pb, id);
    QVERIFY(!readTodo(&pb, &cats, id).has_value());
}

void TestPalmTodosAdapter::dueDateSurvivesRoundTrip()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Todo t = makeSample();
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 6, 1), QTime(0, 0));
    const auto id = writeTodo(&pb, 0, t);
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.hasIndefiniteDue, false);
    QCOMPARE(row->content.due.date(), QDate(2026, 6, 1));
}

void TestPalmTodosAdapter::priorityAndCompletionSurvive()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("ToDoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Todo t = makeSample();
    t.priority = 5;
    t.isComplete = true;
    const auto id = writeTodo(&pb, 0, t);
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.priority, 5);
    QCOMPARE(row->content.isComplete, true);
}

QTEST_GUILESS_MAIN(TestPalmTodosAdapter)
#include "tst_palmtodosadapter.moc"
