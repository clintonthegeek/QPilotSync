#include <QtTest/QtTest>
#include <QSignalSpy>

#include "datadomain.h"

#include "categorymappingstore.h"
#include "mockpalmdatabaseaccess.h"
#include "palmcalendarbackend.h"

using Kalburator::Sync::DataDomain;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCalendar::PalmCalendarBackend;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestPalmCalendarBackend : public QObject
{
    Q_OBJECT
private slots:
    void identity();
    void slotFromCalendarIdParsing();
    void calendarIdForSlotFormatting();
    void loadCalendarsEmptyStoreYieldsOnlyUnfiled();
    void loadCalendarsWithPopulatedSlots();
    void loadCalendarsUnknownCollectionFails();
};

void TestPalmCalendarBackend::identity()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QCOMPARE(backend.backendType(), QStringLiteral("palm-calendar"));
    QCOMPARE(backend.dataDomain(), DataDomain::Calendar);
}

void TestPalmCalendarBackend::slotFromCalendarIdParsing()
{
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/0")), 0);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/15")), 15);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/16")), -1);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/abc")), -1);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("local:memo:1")), -1);
}

void TestPalmCalendarBackend::calendarIdForSlotFormatting()
{
    QCOMPARE(PalmCalendarBackend::calendarIdForSlot(0),
             QStringLiteral("palm:calendar/0"));
    QCOMPARE(PalmCalendarBackend::calendarIdForSlot(7),
             QStringLiteral("palm:calendar/7"));
}

void TestPalmCalendarBackend::loadCalendarsEmptyStoreYieldsOnlyUnfiled()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);
    QSignalSpy finished (&backend, &PalmCalendarBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("palm:datebook"));

    QCOMPARE(discovered.size(), 1);
    QCOMPARE(discovered[0][0].toString(), QStringLiteral("palm:datebook"));
    QCOMPARE(discovered[0][1].toString(), QStringLiteral("palm:calendar/0"));

    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("palm:datebook"));
    QCOMPARE(finished[0][1].toBool(), true);
}

void TestPalmCalendarBackend::loadCalendarsWithPopulatedSlots()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Kids"));
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("Travel"));

    PalmCalendarBackend backend(&dev, &store);
    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);

    backend.loadCalendars(QStringLiteral("palm:datebook"));

    QCOMPARE(discovered.size(), 4);  // 0 + 1 + 3 + 5
    QStringList ids;
    for (const auto &args : discovered) {
        ids << args[1].toString();
    }
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/3")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/5")));
}

void TestPalmCalendarBackend::loadCalendarsUnknownCollectionFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);
    QSignalSpy finished (&backend, &PalmCalendarBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("palm:memos"));

    QCOMPARE(discovered.size(), 0);
    QCOMPARE(finished.size(),   1);
    QCOMPARE(finished[0][1].toBool(), false);
    QCOMPARE(finished[0][2].toString(),
             QStringLiteral("not a Palm calendar collection"));
}

QTEST_MAIN(TestPalmCalendarBackend)
#include "tst_palmcalendarbackend.moc"
