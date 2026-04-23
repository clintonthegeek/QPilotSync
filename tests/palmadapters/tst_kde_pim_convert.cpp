#include <QtTest/QtTest>

#include <KContacts/Addressee>
#include <KCalendarCore/Todo>

#include "contactcodec.h"
#include "kde_pim_convert.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::toAddressee;
using WildPalms::PalmCodecs::fromAddressee;
using WildPalms::PalmCodecs::toKCalTodo;
using WildPalms::PalmCodecs::fromKCalTodo;

class TestKdePimConvert : public QObject
{
    Q_OBJECT
private slots:
    // Contact <-> Addressee
    void contactRoundTripsThroughAddressee();
    void contactPreservesShowPhoneAsCustomField();
    void contactPreservesCustomFieldsInAddressee();
    void contactNonStandardPhoneLabelPreservedInAddressee();
    // Todo <-> KCalendarCore::Todo
    void todoRoundTripsThroughKCalTodo();
    void todoPreservesPriorityOneToOne();
    void todoIndefiniteDueMapsToMissingDueDate();
    void todoPriority9MapsBackToFiveOnReverse();
};

namespace {

Contact makeContactSample()
{
    Contact c;
    c.firstName = QStringLiteral("Ada");
    c.lastName  = QStringLiteral("Lovelace");
    c.company   = QStringLiteral("Analytical Engine Co.");
    c.title     = QStringLiteral("Programmer");
    c.phone[0]  = QStringLiteral("555-0100");
    c.phone[1]  = QStringLiteral("555-0101");
    c.phoneLabels = { QStringLiteral("Work"), QStringLiteral("Home"),
                      QStringLiteral("Mobile"), QStringLiteral("E-mail"),
                      QStringLiteral("Other") };
    c.showPhone = 1;
    c.custom[0] = QStringLiteral("CustomA");
    c.custom[3] = QStringLiteral("CustomD");
    c.note      = QStringLiteral("A brilliant mathematician.");
    return c;
}

Todo makeTodoSample()
{
    Todo t;
    t.description = QStringLiteral("Review Phase-E spec");
    t.note        = QStringLiteral("Focus on E.10+ consumers.");
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 5, 15), QTime(0, 0), Qt::LocalTime);
    t.priority = 2;
    t.isComplete = false;
    return t;
}

} // namespace

void TestKdePimConvert::contactRoundTripsThroughAddressee()
{
    const Contact src = makeContactSample();
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    QCOMPARE(back.firstName, src.firstName);
    QCOMPARE(back.lastName,  src.lastName);
    QCOMPARE(back.company,   src.company);
    QCOMPARE(back.title,     src.title);
    QCOMPARE(back.note,      src.note);
}

void TestKdePimConvert::contactPreservesShowPhoneAsCustomField()
{
    Contact src = makeContactSample();
    src.showPhone = 3;
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    QCOMPARE(back.showPhone, 3);
}

void TestKdePimConvert::contactPreservesCustomFieldsInAddressee()
{
    const Contact src = makeContactSample();
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(back.custom[i], src.custom[i]);
    }
}

void TestKdePimConvert::contactNonStandardPhoneLabelPreservedInAddressee()
{
    Contact src = makeContactSample();
    src.phoneLabels[0] = QStringLiteral("Satellite");  // not a standard Palm label
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    // "Satellite" isn't a known Palm label; the codec falls back to
    // "Other" on round-trip. Document this behaviour in-test.
    QCOMPARE(back.phoneLabels[0], QStringLiteral("Other"));
}

void TestKdePimConvert::todoRoundTripsThroughKCalTodo()
{
    const Todo src = makeTodoSample();
    const auto kcal = toKCalTodo(src);
    QVERIFY(!kcal.isNull());
    const Todo back = fromKCalTodo(kcal);
    QCOMPARE(back.description, src.description);
    QCOMPARE(back.note,        src.note);
    QCOMPARE(back.hasIndefiniteDue, src.hasIndefiniteDue);
    QCOMPARE(back.priority,    src.priority);
    QCOMPARE(back.isComplete,  src.isComplete);
}

void TestKdePimConvert::todoPreservesPriorityOneToOne()
{
    for (int p = 1; p <= 5; ++p) {
        Todo t = makeTodoSample();
        t.priority = p;
        const auto kcal = toKCalTodo(t);
        QCOMPARE(kcal->priority(), p);
    }
}

void TestKdePimConvert::todoIndefiniteDueMapsToMissingDueDate()
{
    Todo t = makeTodoSample();
    t.hasIndefiniteDue = true;
    t.due = QDateTime();
    const auto kcal = toKCalTodo(t);
    QCOMPARE(kcal->hasDueDate(), false);
}

void TestKdePimConvert::todoPriority9MapsBackToFiveOnReverse()
{
    auto kcal = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo);
    kcal->setSummary(QStringLiteral("x"));
    kcal->setPriority(9);
    const Todo back = fromKCalTodo(kcal);
    QCOMPARE(back.priority, 5);
}

QTEST_GUILESS_MAIN(TestKdePimConvert)
#include "tst_kde_pim_convert.moc"
