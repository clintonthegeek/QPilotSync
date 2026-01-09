/**
 * @file test_categoryinfo.cpp
 * @brief Unit tests for CategoryInfo class
 *
 * Tests the Palm category information parsing and manipulation.
 * Note: Many tests require valid Palm AppInfo block data which is difficult
 * to synthesize. Tests focus on boundary conditions and interface behavior.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include <cstring>
#include <pi-appinfo.h>
#include "palm/categoryinfo.h"

class TestCategoryInfo : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========== Construction Tests ==========
    void testDefaultConstruction();
    void testIsValidInitialState();

    // ========== Parse Boundary Tests ==========
    void testParseNullData();
    void testParseEmptyData();
    void testParseTooSmallData();

    // ========== Unparsed State Behavior ==========
    void testCategoryNameUnparsed();
    void testCategoryIndexUnparsed();
    void testAllCategoriesUnparsed();
    void testUsedCategoriesUnparsed();
    void testFindEmptySlotUnparsed();
    void testAddCategoryUnparsed();
    void testSetCategoryUnparsed();
    void testPackUnparsed();

    // ========== Boundary Index Tests ==========
    void testCategoryNameNegativeIndex();
    void testCategoryNameLargeIndex();
    void testSetCategoryNegativeIndex();
    void testSetCategoryLargeIndex();

    // ========== Empty Name Handling ==========
    void testCategoryIndexEmptyName();
    void testAddCategoryEmptyName();

    // ========== Pack Size Consistency ==========
    void testPackSizeConsistent();

    // ========== Dirty Flag Defaults ==========
    void testDirtyFlagDefault();
    void testClearDirtyDefault();
};

void TestCategoryInfo::initTestCase()
{
    qDebug() << "Starting CategoryInfo tests";
}

void TestCategoryInfo::cleanupTestCase()
{
    qDebug() << "CategoryInfo tests complete";
}

// ========== Construction Tests ==========

void TestCategoryInfo::testDefaultConstruction()
{
    CategoryInfo catInfo;

    // Newly constructed object should be invalid and not dirty
    QCOMPARE(catInfo.isValid(), false);
    QCOMPARE(catInfo.isDirty(), false);
}

void TestCategoryInfo::testIsValidInitialState()
{
    CategoryInfo catInfo;

    QCOMPARE(catInfo.isValid(), false);
}

// ========== Parse Boundary Tests ==========

void TestCategoryInfo::testParseNullData()
{
    CategoryInfo catInfo;
    bool result = catInfo.parse(nullptr, 100);

    QCOMPARE(result, false);
    QCOMPARE(catInfo.isValid(), false);
}

void TestCategoryInfo::testParseEmptyData()
{
    CategoryInfo catInfo;
    unsigned char data[1] = {0};
    bool result = catInfo.parse(data, 0);

    QCOMPARE(result, false);
    QCOMPARE(catInfo.isValid(), false);
}

void TestCategoryInfo::testParseTooSmallData()
{
    CategoryInfo catInfo;
    unsigned char data[10] = {0};  // Too small for CategoryAppInfo_t
    bool result = catInfo.parse(data, sizeof(data));

    QCOMPARE(result, false);
    QCOMPARE(catInfo.isValid(), false);
}

// ========== Unparsed State Behavior ==========

void TestCategoryInfo::testCategoryNameUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - should return empty for all indices

    QVERIFY(catInfo.categoryName(0).isEmpty());
    QVERIFY(catInfo.categoryName(1).isEmpty());
    QVERIFY(catInfo.categoryName(15).isEmpty());
}

void TestCategoryInfo::testCategoryIndexUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - should return -1 for any name

    QCOMPARE(catInfo.categoryIndex("Business"), -1);
    QCOMPARE(catInfo.categoryIndex("Personal"), -1);
}

void TestCategoryInfo::testAllCategoriesUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - allCategories returns empty strings for unparsed

    QStringList all = catInfo.allCategories();
    // Should still return 16 entries (all empty)
    QCOMPARE(all.size(), 16);
    for (const QString &name : all) {
        QVERIFY(name.isEmpty());
    }
}

void TestCategoryInfo::testUsedCategoriesUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - usedCategories returns empty list

    QStringList used = catInfo.usedCategories();
    QCOMPARE(used.size(), 0);
}

void TestCategoryInfo::testFindEmptySlotUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - should return -1

    int slot = catInfo.findEmptySlot();
    QCOMPARE(slot, -1);
}

void TestCategoryInfo::testAddCategoryUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - adding should fail

    int index = catInfo.addCategory("NewCategory");
    QCOMPARE(index, -1);
}

void TestCategoryInfo::testSetCategoryUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - setting should fail

    bool result = catInfo.setCategory(1, "Test");
    QCOMPARE(result, false);
}

void TestCategoryInfo::testPackUnparsed()
{
    CategoryInfo catInfo;
    // Not parsed - pack should fail

    unsigned char buffer[512];
    int result = catInfo.pack(buffer, sizeof(buffer));
    QCOMPARE(result, -1);
}

// ========== Boundary Index Tests ==========

void TestCategoryInfo::testCategoryNameNegativeIndex()
{
    CategoryInfo catInfo;

    QVERIFY(catInfo.categoryName(-1).isEmpty());
    QVERIFY(catInfo.categoryName(-100).isEmpty());
}

void TestCategoryInfo::testCategoryNameLargeIndex()
{
    CategoryInfo catInfo;

    // Index 16 is out of range (max is 15)
    QVERIFY(catInfo.categoryName(16).isEmpty());
    QVERIFY(catInfo.categoryName(100).isEmpty());
    QVERIFY(catInfo.categoryName(INT_MAX).isEmpty());
}

void TestCategoryInfo::testSetCategoryNegativeIndex()
{
    CategoryInfo catInfo;

    bool result = catInfo.setCategory(-1, "Test");
    QCOMPARE(result, false);
}

void TestCategoryInfo::testSetCategoryLargeIndex()
{
    CategoryInfo catInfo;

    bool result = catInfo.setCategory(16, "Test");
    QCOMPARE(result, false);

    result = catInfo.setCategory(100, "Test");
    QCOMPARE(result, false);
}

// ========== Empty Name Handling ==========

void TestCategoryInfo::testCategoryIndexEmptyName()
{
    CategoryInfo catInfo;

    // Empty name should return -1
    QCOMPARE(catInfo.categoryIndex(""), -1);
}

void TestCategoryInfo::testAddCategoryEmptyName()
{
    CategoryInfo catInfo;

    // Empty name should fail
    int index = catInfo.addCategory("");
    QCOMPARE(index, -1);
}

// ========== Pack Size Consistency ==========

void TestCategoryInfo::testPackSizeConsistent()
{
    CategoryInfo catInfo;

    // Pack size should always be the size of CategoryAppInfo_t
    size_t size = catInfo.packSize();
    QVERIFY(size > 0);
    QCOMPARE(size, sizeof(CategoryAppInfo_t));
}

// ========== Dirty Flag Defaults ==========

void TestCategoryInfo::testDirtyFlagDefault()
{
    CategoryInfo catInfo;

    // Default should not be dirty
    QCOMPARE(catInfo.isDirty(), false);
}

void TestCategoryInfo::testClearDirtyDefault()
{
    CategoryInfo catInfo;

    // Clearing dirty on unparsed object should be safe
    catInfo.clearDirty();
    QCOMPARE(catInfo.isDirty(), false);
}

QTEST_MAIN(TestCategoryInfo)
#include "test_categoryinfo.moc"
