#include <QtTest/QtTest>

#include "todocodec.h"

using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;

class TestTodoCodec : public QObject
{
    Q_OBJECT
private slots:
    void emptyTodoRoundTrips();
    void descriptionAndNoteRoundTrip();
    void indefiniteDueFlagRoundTrips();
    void concreteDueDateRoundTrips();
    void priority1RoundTrips();
    void priority5RoundTrips();
    void completionFlagRoundTrips();
    void unicodeDescriptionRoundTrips();
    void decodeEmptyBytesReturnsNullopt();
};

void TestTodoCodec::emptyTodoRoundTrips()
{
    Todo t{};
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(*decoded, t);
}

void TestTodoCodec::descriptionAndNoteRoundTrip()
{
    Todo t{};
    t.description = QStringLiteral("Write the plan");
    t.note        = QStringLiteral("Must include fixtures.");
    t.hasIndefiniteDue = true;
    t.priority = 3;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->description, t.description);
    QCOMPARE(decoded->note,        t.note);
}

void TestTodoCodec::indefiniteDueFlagRoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->hasIndefiniteDue, true);
}

void TestTodoCodec::concreteDueDateRoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 5, 1), QTime(0, 0));
    t.priority = 2;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->hasIndefiniteDue, false);
    QCOMPARE(decoded->due.date(), QDate(2026, 5, 1));
}

void TestTodoCodec::priority1RoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->priority, 1);
}

void TestTodoCodec::priority5RoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 5;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->priority, 5);
}

void TestTodoCodec::completionFlagRoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    t.isComplete = true;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->isComplete, true);
}

void TestTodoCodec::unicodeDescriptionRoundTrips()
{
    Todo t{};
    t.description = QString::fromUtf8("Caf\xC3\xA9 \xE2\x80\x94 discuss");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->description, t.description);
}

void TestTodoCodec::decodeEmptyBytesReturnsNullopt()
{
    const auto decoded = decodeTodo(QByteArray());
    QCOMPARE(decoded.has_value(), false);
}

QTEST_GUILESS_MAIN(TestTodoCodec)
#include "tst_todocodec.moc"
