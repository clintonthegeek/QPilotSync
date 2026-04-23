#include <QtTest/QtTest>

#include "categorymappingstore.h"
#include "memocodec.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmmemosadapter.h"

using WildPalms::Palm::Adapters::MemoRow;
using WildPalms::Palm::Adapters::readAllMemos;
using WildPalms::Palm::Adapters::readMemo;
using WildPalms::Palm::Adapters::writeMemo;
using WildPalms::Palm::Adapters::deleteMemo;
using WildPalms::PalmCodecs::Memo;
using WildPalms::PalmCodecs::encodeMemo;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

class TestPalmMemosAdapter : public QObject
{
    Q_OBJECT
private slots:
    void readEmptyDbReturnsEmptyList();
    void writeThenReadAllRoundTrips();
    void readByIdFindsSpecificRecord();
    void readByIdMissingReturnsNullopt();
    void writeAssignsIdWhenZero();
    void categorySlotPreservedOnWrite();
    void categoryNameResolvedFromStore();
    void deleteRemovesTheRecord();
};

namespace {

void seed(MockPalmDatabaseAccess &mock, const QString &db, const Memo &m,
          std::uint8_t categorySlot)
{
    PalmRecord rec;
    rec.category = categorySlot;
    rec.data = encodeMemo(m);
    mock.createDatabase(db);
    mock.createRecord(db, rec);
}

} // namespace

void TestPalmMemosAdapter::readEmptyDbReturnsEmptyList()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    QCOMPARE(readAllMemos(&pb, &cats).size(), 0);
}

void TestPalmMemosAdapter::writeThenReadAllRoundTrips()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 0, m);
    QVERIFY(id != 0);
    const auto rows = readAllMemos(&pb, &cats);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().content.text, QStringLiteral("x"));
}

void TestPalmMemosAdapter::readByIdFindsSpecificRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("needle");
    const auto id = writeMemo(&pb, 0, m);
    const auto row = readMemo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.text, QStringLiteral("needle"));
}

void TestPalmMemosAdapter::readByIdMissingReturnsNullopt()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto row = readMemo(&pb, &cats, 9999);
    QVERIFY(!row.has_value());
}

void TestPalmMemosAdapter::writeAssignsIdWhenZero()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id1 = writeMemo(&pb, 0, m);
    const auto id2 = writeMemo(&pb, 0, m);
    QVERIFY(id1 != 0);
    QVERIFY(id2 != 0);
    QVERIFY(id1 != id2);
}

void TestPalmMemosAdapter::categorySlotPreservedOnWrite()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 7, m);
    const auto row = readMemo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categorySlot, 7);
}

void TestPalmMemosAdapter::categoryNameResolvedFromStore()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    cats.setSlotName(QStringLiteral("MemoDB"), 3, QStringLiteral("Work"));
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 3, m);
    const auto row = readMemo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categoryName, QStringLiteral("Work"));
}

void TestPalmMemosAdapter::deleteRemovesTheRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 0, m);
    deleteMemo(&pb, id);
    QVERIFY(!readMemo(&pb, &cats, id).has_value());
}

QTEST_GUILESS_MAIN(TestPalmMemosAdapter)
#include "tst_palmmemosadapter.moc"
