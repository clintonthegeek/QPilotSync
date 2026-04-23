#include <QtTest/QtTest>

#include "categorymappingstore.h"
#include "contactcodec.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmcontactsadapter.h"

using WildPalms::Palm::Adapters::ContactRow;
using WildPalms::Palm::Adapters::readAllContacts;
using WildPalms::Palm::Adapters::readContact;
using WildPalms::Palm::Adapters::writeContact;
using WildPalms::Palm::Adapters::deleteContact;
using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

class TestPalmContactsAdapter : public QObject
{
    Q_OBJECT
private slots:
    void writeThenReadAllRoundTrips();
    void readByIdFindsSpecificRecord();
    void categorySlotPreserved();
    void categoryNameResolvedFromStore();
    void deleteRemovesTheRecord();
    void phoneSlotsSurviveRoundTrip();
};

namespace {

Contact makeSample()
{
    Contact c;
    c.firstName = QStringLiteral("Grace");
    c.lastName  = QStringLiteral("Hopper");
    c.phone[0]  = QStringLiteral("555-0100");
    c.phoneLabels = { QStringLiteral("Work"), QStringLiteral("Home"),
                      QStringLiteral("Mobile"), QStringLiteral("E-mail"),
                      QStringLiteral("Other") };
    return c;
}

} // namespace

void TestPalmContactsAdapter::writeThenReadAllRoundTrips()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 0, makeSample());
    QVERIFY(id != 0);
    const auto rows = readAllContacts(&pb, &cats);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().content.firstName, QStringLiteral("Grace"));
}

void TestPalmContactsAdapter::readByIdFindsSpecificRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 0, makeSample());
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.lastName, QStringLiteral("Hopper"));
}

void TestPalmContactsAdapter::categorySlotPreserved()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 5, makeSample());
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categorySlot, 5);
}

void TestPalmContactsAdapter::categoryNameResolvedFromStore()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    cats.setSlotName(QStringLiteral("AddressDB"), 5, QStringLiteral("Family"));
    const auto id = writeContact(&pb, 5, makeSample());
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categoryName, QStringLiteral("Family"));
}

void TestPalmContactsAdapter::deleteRemovesTheRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 0, makeSample());
    deleteContact(&pb, id);
    QVERIFY(!readContact(&pb, &cats, id).has_value());
}

void TestPalmContactsAdapter::phoneSlotsSurviveRoundTrip()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Contact c = makeSample();
    for (int i = 0; i < 5; ++i) c.phone[i] = QStringLiteral("555-010%1").arg(i);
    const auto id = writeContact(&pb, 0, c);
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(row->content.phone[i], c.phone[i]);
    }
}

QTEST_GUILESS_MAIN(TestPalmContactsAdapter)
#include "tst_palmcontactsadapter.moc"
