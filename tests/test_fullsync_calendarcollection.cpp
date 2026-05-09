// Phase D unit test — CalendarCollection_WP implements ICalendarCollection.

#include <QtTest/QtTest>
#include <QTimeZone>
#include <KCalendarCore/MemoryCalendar>

#include "calendarcollection_wp.h"

using WildPalms::FullSync::CalendarCollection_WP;

class TestFullSyncCalendarCollection : public QObject
{
    Q_OBJECT

private slots:
    void idIsReturned();
    void addCalendarAndRetrieve();
    void addRejectsNullptrAndEmptyId();
    void setColorOnlyAppliesWhenCalendarPresent();
    void setVisibleOnlyAppliesWhenCalendarPresent();
    void clear_removesAllCalendars();
};

void TestFullSyncCalendarCollection::idIsReturned()
{
    CalendarCollection_WP c(QStringLiteral("profile-A"));
    QCOMPARE(c.id(), QStringLiteral("profile-A"));
}

void TestFullSyncCalendarCollection::addCalendarAndRetrieve()
{
    CalendarCollection_WP c(QStringLiteral("profile-A"));
    auto *cal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    cal->setId(QStringLiteral("cal-1"));
    c.addCalendar(cal);

    QCOMPARE(c.calendars().size(), 1);
    QVERIFY(c.calendar(QStringLiteral("cal-1")) == cal);
    QVERIFY(c.calendar(QStringLiteral("missing")) == nullptr);
}

void TestFullSyncCalendarCollection::addRejectsNullptrAndEmptyId()
{
    CalendarCollection_WP c(QStringLiteral("profile-A"));
    c.addCalendar(nullptr);
    QCOMPARE(c.calendars().size(), 0);

    auto *cal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    // No id set → should be rejected, and responsibility for deletion
    // falls back on this test.
    c.addCalendar(cal);
    QCOMPARE(c.calendars().size(), 0);
    delete cal;
}

void TestFullSyncCalendarCollection::setColorOnlyAppliesWhenCalendarPresent()
{
    CalendarCollection_WP c(QStringLiteral("profile-A"));
    c.setCalendarColor(QStringLiteral("cal-1"), QColor(Qt::red));
    QVERIFY(!c.calendarColor(QStringLiteral("cal-1")).isValid());

    auto *cal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    cal->setId(QStringLiteral("cal-1"));
    c.addCalendar(cal);
    c.setCalendarColor(QStringLiteral("cal-1"), QColor(Qt::red));
    QCOMPARE(c.calendarColor(QStringLiteral("cal-1")), QColor(Qt::red));
}

void TestFullSyncCalendarCollection::setVisibleOnlyAppliesWhenCalendarPresent()
{
    CalendarCollection_WP c(QStringLiteral("profile-A"));
    c.setCalendarVisible(QStringLiteral("cal-1"), false);
    // Default visibility for absent calendars is true (no record kept).
    QVERIFY(c.isCalendarVisible(QStringLiteral("cal-1")));

    auto *cal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    cal->setId(QStringLiteral("cal-1"));
    c.addCalendar(cal);
    c.setCalendarVisible(QStringLiteral("cal-1"), false);
    QVERIFY(!c.isCalendarVisible(QStringLiteral("cal-1")));
}

void TestFullSyncCalendarCollection::clear_removesAllCalendars()
{
    CalendarCollection_WP col(QStringLiteral("test-clear"));
    auto *cal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    cal->setId(QStringLiteral("palm:calendar/0"));
    col.addCalendar(cal);
    QCOMPARE(col.calendars().size(), 1);

    col.clear();

    QCOMPARE(col.calendars().size(), 0);
    QVERIFY(col.calendar(QStringLiteral("palm:calendar/0")) == nullptr);
}

QTEST_MAIN(TestFullSyncCalendarCollection)
#include "test_fullsync_calendarcollection.moc"
