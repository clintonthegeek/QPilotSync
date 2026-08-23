#include <QRegularExpression>
#include <QtTest/QtTest>

#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/todos/todoicstranscoder.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/todocodec.h"
#include "palm/sync/palmrecord.h"

using WildPalms::TodoPlugin::encodePalmToIcs;
using WildPalms::TodoPlugin::decodeIcsToPalm;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;
using WildPalms::PalmSync::PalmRecord;

namespace {

const QString kDb = QStringLiteral("ToDoDB");

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
    void decodeCategoryViaNameMapping();
    void decodePreservesRecordIdWhenPresent();
};

void TestTodoIcsTranscoder::encodeProducesParseableVtodo()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    QVERIFY(!ics.isEmpty());
    QVERIFY(ics.contains("BEGIN:VCALENDAR"));
    QVERIFY(ics.contains("BEGIN:VTODO"));
    QVERIFY(ics.contains("END:VCALENDAR"));
}

void TestTodoIcsTranscoder::encodePreservesSummaryAndDescription()
{
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    QVERIFY(ics.contains("SUMMARY:Buy groceries"));
    // RFC 5545 escapes commas in TEXT-typed properties; KCal emits
    // "Milk\, bread\, eggs" inside DESCRIPTION:.
    QVERIFY(ics.contains("Milk\\, bread\\, eggs"));
}

void TestTodoIcsTranscoder::encodeStampsCategoryAndRecordIdProperties()
{
    PalmRecord pr = makeTodoRecordFull();   // category = 3, recordId = 17
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    // Newer KCalendarCore appends ";VALUE=TEXT" to X- properties, so match
    // the property name + value rather than the exact serialized form.
    static const QRegularExpression catSlot(
        QStringLiteral("X-WP-PALM-CATEGORY-SLOT[^:]*:3\\b"));
    static const QRegularExpression recId(
        QStringLiteral("X-WP-PALM-RECORDID[^:]*:17\\b"));
    QVERIFY(catSlot.match(QString::fromUtf8(ics)).hasMatch());
    QVERIFY(recId.match(QString::fromUtf8(ics)).hasMatch());
}

void TestTodoIcsTranscoder::encodeIndefiniteDueOmitsDtDue()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("No due"), 0);
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    QVERIFY(!ics.contains("DUE:"));
    QVERIFY(!ics.contains("DUE;"));
}

void TestTodoIcsTranscoder::encodeCompletedTodoSetsCompleted()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("Done"), 0, /*complete=*/true);
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    // VTODO completion shows up as STATUS:COMPLETED + COMPLETED:<timestamp>.
    QVERIFY(ics.contains("STATUS:COMPLETED"));
    QVERIFY(ics.contains("COMPLETED:"));
}

void TestTodoIcsTranscoder::encodePrivateSetsClassification()
{
    PalmRecord pr = makeTodoRecordFull();   // isPrivate = true
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    QVERIFY(ics.contains("CLASS:PRIVATE"));
}

void TestTodoIcsTranscoder::roundTripPreservesAllFields()
{
    // Round-trip without a category store: slot propagates via slotHint=0
    // (no name mapping). The category in pr is 3 but without a store the
    // decode side returns slot 0 (Unfiled) — that is the expected behaviour
    // when no store is threaded.
    PalmRecord pr = makeTodoRecordFull();
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    auto roundTripped = decodeIcsToPalm(ics, nullptr, kDb);
    QVERIFY(roundTripped.has_value());
    // Without a store, categories are not set, so slot resolves to 0.
    QCOMPARE(static_cast<int>(roundTripped->category), 0);
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
    QVERIFY(!decodeIcsToPalm(QByteArray(), nullptr, kDb).has_value());
}

void TestTodoIcsTranscoder::decodeWithGarbageReturnsNullopt()
{
    QVERIFY(!decodeIcsToPalm(QByteArray("not a vcalendar"), nullptr, kDb).has_value());
}

void TestTodoIcsTranscoder::decodeCategoryViaNameMapping()
{
    // The slot now round-trips via the category NAME (CategoryMappingStore),
    // not a raw slotHint: slot 5 -> "Project X" -> CATEGORIES -> slot 5.
    CategoryMappingStore cats;
    QVERIFY(cats.setSlotName(kDb, 5, QStringLiteral("Project X")));

    PalmRecord pr = makeTodoRecord(QStringLiteral("Slot test"), 5);
    QByteArray ics = encodePalmToIcs(pr, &cats, kDb);
    QVERIFY(ics.contains("CATEGORIES:Project X"));

    auto rt = decodeIcsToPalm(ics, &cats, kDb);
    QVERIFY(rt.has_value());
    QCOMPARE(static_cast<int>(rt->category), 5);
}

void TestTodoIcsTranscoder::decodePreservesRecordIdWhenPresent()
{
    PalmRecord pr = makeTodoRecord(QStringLiteral("With id"), 0);   // recordId = 42
    QByteArray ics = encodePalmToIcs(pr, nullptr, kDb);
    auto rt = decodeIcsToPalm(ics, nullptr, kDb);
    QVERIFY(rt.has_value());
    QCOMPARE(rt->recordId, 42u);
}

QTEST_MAIN(TestTodoIcsTranscoder)
#include "tst_todoicstranscoder.moc"
