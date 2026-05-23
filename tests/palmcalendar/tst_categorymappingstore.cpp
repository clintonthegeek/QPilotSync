#include <QtTest/QtTest>

#include "categorymappingstore.h"

using WildPalms::PalmCalendar::CategoryMappingStore;

class TestCategoryMappingStore : public QObject
{
    Q_OBJECT
private slots:
    void slotZeroAlwaysReturnsUnfiled();
    void setAndGetRoundTripForUserSlots();
    void emptyNameRemovesSlot();
    void dbIsolationBetweenDatabases();
    void slotZeroRejectsArbitraryNames();
    void populatedSlotsIsSortedAndExcludesZero();
    void outOfRangeSlotRejected();
    void sixteenSlotNamesProducesFixedShape();
};

void TestCategoryMappingStore::slotZeroAlwaysReturnsUnfiled()
{
    CategoryMappingStore store;
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 0),
             QStringLiteral("Unfiled"));

    // Even for a DB we've never touched.
    QCOMPARE(store.slotName(QStringLiteral("MemoDB"), 0),
             QStringLiteral("Unfiled"));
}

void TestCategoryMappingStore::setAndGetRoundTripForUserSlots()
{
    CategoryMappingStore store;
    QVERIFY(store.setSlotName(QStringLiteral("DatebookDB"), 3,
                              QStringLiteral("Work")));
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 3),
             QStringLiteral("Work"));
}

void TestCategoryMappingStore::emptyNameRemovesSlot()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 5,
                      QStringLiteral("Personal"));
    QCOMPARE(store.populatedSlots(QStringLiteral("DatebookDB")).size(), 1);

    QVERIFY(store.setSlotName(QStringLiteral("DatebookDB"), 5, QString{}));
    QVERIFY(store.slotName(QStringLiteral("DatebookDB"), 5).isEmpty());
    QVERIFY(store.populatedSlots(QStringLiteral("DatebookDB")).isEmpty());
}

void TestCategoryMappingStore::dbIsolationBetweenDatabases()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("MemoDB"),     1, QStringLiteral("Ideas"));

    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 1),
             QStringLiteral("Work"));
    QCOMPARE(store.slotName(QStringLiteral("MemoDB"), 1),
             QStringLiteral("Ideas"));
}

void TestCategoryMappingStore::slotZeroRejectsArbitraryNames()
{
    CategoryMappingStore store;
    // setSlotName for slot 0 is a no-op accept only for "Unfiled".
    QVERIFY(!store.setSlotName(QStringLiteral("DatebookDB"), 0,
                               QStringLiteral("NotUnfiled")));
    // slot 0 still reads as "Unfiled"
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 0),
             QStringLiteral("Unfiled"));
}

void TestCategoryMappingStore::populatedSlotsIsSortedAndExcludesZero()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("E"));
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("A"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("C"));

    const auto populated = store.populatedSlots(QStringLiteral("DatebookDB"));
    QCOMPARE(populated, (QList<int>{1, 3, 5}));
}

void TestCategoryMappingStore::outOfRangeSlotRejected()
{
    CategoryMappingStore store;
    QVERIFY(!store.setSlotName(QStringLiteral("DatebookDB"), -1,
                               QStringLiteral("X")));
    QVERIFY(!store.setSlotName(QStringLiteral("DatebookDB"), 16,
                               QStringLiteral("X")));
    QVERIFY(store.populatedSlots(QStringLiteral("DatebookDB")).isEmpty());
}

void TestCategoryMappingStore::sixteenSlotNamesProducesFixedShape()
{
    CategoryMappingStore store;

    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Personal"));

    const QStringList names =
        store.sixteenSlotNames(QStringLiteral("DatebookDB"));

    QCOMPARE(names.size(), 16);
    QCOMPARE(names.at(0), QStringLiteral("Unfiled"));
    QCOMPARE(names.at(1), QStringLiteral("Work"));
    QCOMPARE(names.at(2), QString());
    QCOMPARE(names.at(3), QStringLiteral("Personal"));
    for (int i = 4; i < 16; ++i)
        QCOMPARE(names.at(i), QString());

    // Different dbName returns empty (all empties + slot 0 = "Unfiled")
    const QStringList empty =
        store.sixteenSlotNames(QStringLiteral("MissingDB"));
    QCOMPARE(empty.size(), 16);
    QCOMPARE(empty.at(0), QStringLiteral("Unfiled"));
    for (int i = 1; i < 16; ++i)
        QCOMPARE(empty.at(i), QString());
}

QTEST_MAIN(TestCategoryMappingStore)
#include "tst_categorymappingstore.moc"
