#include <QtTest/QtTest>

#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/todos/todoicstranscoder.h"
#include "palm/codecs/todocodec.h"
#include "palm/sync/palmrecord.h"

using WildPalms::TodoPlugin::encodePalmToIcs;
using WildPalms::TodoPlugin::decodeIcsToPalm;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;
using WildPalms::PalmSync::PalmRecord;

namespace {

PalmRecord makeTodoRecord(const QString &description,
                          int slot,
                          bool complete = false,
                          int priority = 1)
{
    Todo t;
    t.description = description;
    t.note = QStringLiteral("");
    t.hasIndefiniteDue = true;
    t.priority = priority;
    t.isComplete = complete;
    t.isPrivate = false;
    PalmRecord pr;
    pr.recordId = 42;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeTodo(t);
    return pr;
}

PalmRecord makeTodoRecordFull()
{
    Todo t;
    t.description = QStringLiteral("Buy groceries");
    t.note = QStringLiteral("Milk, bread, eggs");
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 5, 15), QTime(0, 0));
    t.priority = 2;
    t.isComplete = false;
    t.isPrivate = true;
    PalmRecord pr;
    pr.recordId = 17;
    pr.category = 3;
    // Privacy lives in the PalmRecord header (AttrSecret), not the
    // ToDo data blob — pack_ToDo doesn't pack a private bit.
    pr.attributes = PalmRecord::AttrSecret;
    pr.data = encodeTodo(t);
    return pr;
}

} // namespace

class TestTodoIcsTranscoder : public QObject
{
    Q_OBJECT
private slots:
    void encodeProducesParseableVtodo();
    void encodePreservesSummaryAndDescription();
    void encodeStampsCategoryAndRecordIdProperties();
    void encodeIndefiniteDueOmitsDtDue();
    void encodeCompletedTodoSetsCompleted();
    void encodePrivateSetsClassification();
    void roundTripPreservesAllFields();
    void decodeWithEmptyBytesReturnsNullopt();
    void decodeWithGarbageReturnsNullopt();
    void decodeSlotHintOverridesEmbeddedSlot();
    void decodePreservesRecordIdWhenPresent();
};

void TestTodoIcsTranscoder::encodeProducesParseableVtodo()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(!ics.isEmpty());
    QVERIFY(ics.contains("BEGIN:VCALENDAR"));
    QVERIFY(ics.contains("BEGIN:VTODO"));
    QVERIFY(ics.contains("END:VCALENDAR"));
}

void TestTodoIcsTranscoder::encodePreservesSummaryAndDescription()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("SUMMARY:Buy groceries"));
    // RFC 5545 escapes commas in TEXT-typed properties; KCal emits
    // "Milk\, bread\, eggs" inside DESCRIPTION:.
    QVERIFY(ics.contains("Milk\\, bread\\, eggs"));
}

void TestTodoIcsTranscoder::encodeStampsCategoryAndRecordIdProperties()
{
    PalmRecord pr = makeTodoRecordFull();   // category = 3, recordId = 17
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("X-WP-PALM-CATEGORY-SLOT:3"));
    QVERIFY(ics.contains("X-WP-PALM-RECORDID:17"));
}

void TestTodoIcsTranscoder::encodeIndefiniteDueOmitsDtDue()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("No due"), 0);
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(!ics.contains("DUE:"));
    QVERIFY(!ics.contains("DUE;"));
}

void TestTodoIcsTranscoder::encodeCompletedTodoSetsCompleted()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("Done"), 0, /*complete=*/true);
    QByteArray ics = encodePalmToIcs(pr);
    // VTODO completion shows up as STATUS:COMPLETED + COMPLETED:<timestamp>.
    QVERIFY(ics.contains("STATUS:COMPLETED"));
    QVERIFY(ics.contains("COMPLETED:"));
}

void TestTodoIcsTranscoder::encodePrivateSetsClassification()
{
    PalmRecord pr = makeTodoRecordFull();   // isPrivate = true
    QByteArray ics = encodePalmToIcs(pr);
    QVERIFY(ics.contains("CLASS:PRIVATE"));
}

void TestTodoIcsTranscoder::roundTripPreservesAllFields()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr);
    auto roundTripped = decodeIcsToPalm(ics, 3);
    QVERIFY(roundTripped.has_value());
    QCOMPARE(static_cast<int>(roundTripped->category), 3);
    QCOMPARE(roundTripped->recordId, 17u);

    auto rtTodo = decodeTodo(QByteArrayView(roundTripped->data));
    auto srcTodo = decodeTodo(QByteArrayView(pr.data));
    QVERIFY(rtTodo.has_value());
    QVERIFY(srcTodo.has_value());
    QCOMPARE(rtTodo->description, srcTodo->description);
    QCOMPARE(rtTodo->note, srcTodo->note);
    QCOMPARE(rtTodo->hasIndefiniteDue, srcTodo->hasIndefiniteDue);
    QCOMPARE(rtTodo->due, srcTodo->due);
    QCOMPARE(rtTodo->priority, srcTodo->priority);
    QCOMPARE(rtTodo->isComplete, srcTodo->isComplete);
    QCOMPARE(rtTodo->isPrivate, srcTodo->isPrivate);
}

void TestTodoIcsTranscoder::decodeWithEmptyBytesReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray(), 0).has_value());
}

void TestTodoIcsTranscoder::decodeWithGarbageReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray("not a vcalendar"), 0).has_value());
}

void TestTodoIcsTranscoder::decodeSlotHintOverridesEmbeddedSlot()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("Slot test"), 5);
    QByteArray ics = encodePalmToIcs(pr);
    auto rt = decodeIcsToPalm(ics, /*slotHint=*/9);
    QVERIFY(rt.has_value());
    QCOMPARE(static_cast<int>(rt->category), 9);
}

void TestTodoIcsTranscoder::decodePreservesRecordIdWhenPresent()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("With id"), 0);   // recordId = 42
    QByteArray ics = encodePalmToIcs(pr);
    auto rt = decodeIcsToPalm(ics, 0);
    QVERIFY(rt.has_value());
    QCOMPARE(rt->recordId, 42u);
}

QTEST_MAIN(TestTodoIcsTranscoder)
#include "tst_todoicstranscoder.moc"
