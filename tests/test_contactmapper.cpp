/**
 * @file test_contactmapper.cpp
 * @brief Unit tests for ContactMapper class
 *
 * Tests the conversion between Palm Address format and vCard (RFC 6350).
 */

#include <QtTest/QtTest>
#include <QDebug>
#include "mappers/contactmapper.h"

class TestContactMapper : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========== vCardToContact Tests ==========
    void testParseSimpleContact();
    void testParseContactWithPhones();
    void testParseContactWithEmail();
    void testParseContactWithAddress();
    void testParseContactWithNote();
    void testParseContactWithOrganization();
    void testParseContactWithCustomFields();
    void testParseContactWithCategory();

    // ========== contactToVCard Tests ==========
    void testGenerateSimpleVCard();
    void testGenerateVCardWithPhones();
    void testGenerateVCardWithAddress();
    void testGenerateVCardWithCustomFields();
    void testGenerateVCardCompanyOnly();

    // ========== Round-trip Tests ==========
    void testRoundTripSimpleContact();
    void testRoundTripContactWithPhones();
    void testRoundTripContactWithAddress();
    void testRoundTripContactWithCustomFields();

    // ========== Edge Cases ==========
    void testParseEmptyInput();
    void testParseInvalidInput();
    void testParseMalformedName();
    void testLineFolding();

    // ========== Filename Generation ==========
    void testGenerateFilenameFullName();
    void testGenerateFilenameFirstOnly();
    void testGenerateFilenameLastOnly();
    void testGenerateFilenameCompanyOnly();
    void testGenerateFilenameEmpty();

private:
    QString makeVCard(const QString &fn, const QString &n,
                      const QStringList &extraProps = {});
};

void TestContactMapper::initTestCase()
{
    qDebug() << "Starting ContactMapper tests";
}

void TestContactMapper::cleanupTestCase()
{
    qDebug() << "ContactMapper tests complete";
}

QString TestContactMapper::makeVCard(const QString &fn, const QString &n,
                                      const QStringList &extraProps)
{
    QString vcard;
    vcard += "BEGIN:VCARD\r\n";
    vcard += "VERSION:4.0\r\n";
    vcard += QString("FN:%1\r\n").arg(fn);
    vcard += QString("N:%1\r\n").arg(n);
    for (const QString &prop : extraProps) {
        vcard += prop + "\r\n";
    }
    vcard += "END:VCARD\r\n";
    return vcard;
}

// ========== vCardToContact Tests ==========

void TestContactMapper::testParseSimpleContact()
{
    QString vcard = makeVCard("John Smith", "Smith;John;;;");

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.firstName, QString("John"));
    QCOMPARE(contact.lastName, QString("Smith"));
}

void TestContactMapper::testParseContactWithPhones()
{
    QStringList props;
    props << "TEL;TYPE=work:555-1234";
    props << "TEL;TYPE=home:555-5678";
    props << "TEL;TYPE=cell:555-9999";

    QString vcard = makeVCard("Jane Doe", "Doe;Jane;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.firstName, QString("Jane"));
    QCOMPARE(contact.lastName, QString("Doe"));
    QCOMPARE(contact.phone1, QString("555-1234"));
    QCOMPARE(contact.phone2, QString("555-5678"));
    QCOMPARE(contact.phone3, QString("555-9999"));

    // Check labels - work=0, home=1, cell=7
    QCOMPARE(contact.phoneLabels[0], QString("0"));  // work
    QCOMPARE(contact.phoneLabels[1], QString("1"));  // home
    QCOMPARE(contact.phoneLabels[2], QString("7"));  // cell/mobile
}

void TestContactMapper::testParseContactWithEmail()
{
    QStringList props;
    props << "EMAIL;TYPE=internet:john@example.com";

    QString vcard = makeVCard("John Doe", "Doe;John;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.phone1, QString("john@example.com"));
    QCOMPARE(contact.phoneLabels[0], QString("4"));  // Email label
}

void TestContactMapper::testParseContactWithAddress()
{
    QStringList props;
    props << "ADR;TYPE=work:;;123 Main Street;Springfield;IL;62701;USA";

    QString vcard = makeVCard("Bob Builder", "Builder;Bob;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.address, QString("123 Main Street"));
    QCOMPARE(contact.city, QString("Springfield"));
    QCOMPARE(contact.state, QString("IL"));
    QCOMPARE(contact.zip, QString("62701"));
    QCOMPARE(contact.country, QString("USA"));
}

void TestContactMapper::testParseContactWithNote()
{
    QStringList props;
    props << "NOTE:This is a test note";

    QString vcard = makeVCard("Test Person", "Person;Test;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.note, QString("This is a test note"));
}

void TestContactMapper::testParseContactWithOrganization()
{
    QStringList props;
    props << "ORG:Acme Corporation";
    props << "TITLE:Software Engineer";

    QString vcard = makeVCard("Alice Smith", "Smith;Alice;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.company, QString("Acme Corporation"));
    QCOMPARE(contact.title, QString("Software Engineer"));
}

void TestContactMapper::testParseContactWithCustomFields()
{
    QStringList props;
    props << "X-PALM-CUSTOM1:Birthday: Jan 1";
    props << "X-PALM-CUSTOM2:Spouse: Jane";
    props << "X-PALM-CUSTOM3:Children: 2";
    props << "X-PALM-CUSTOM4:Anniversary";

    QString vcard = makeVCard("Custom Contact", "Contact;Custom;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.custom1, QString("Birthday: Jan 1"));
    QCOMPARE(contact.custom2, QString("Spouse: Jane"));
    QCOMPARE(contact.custom3, QString("Children: 2"));
    QCOMPARE(contact.custom4, QString("Anniversary"));
}

void TestContactMapper::testParseContactWithCategory()
{
    QStringList props;
    props << "CATEGORIES:Business,VIP";

    QString vcard = makeVCard("VIP Contact", "Contact;VIP;;;", props);

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    QCOMPARE(contact.categoryName, QString("Business"));
}

// ========== contactToVCard Tests ==========

void TestContactMapper::testGenerateSimpleVCard()
{
    ContactMapper::Contact contact;
    contact.recordId = 12345;
    contact.firstName = "John";
    contact.lastName = "Smith";

    QString vcard = ContactMapper::contactToVCard(contact);

    QVERIFY(vcard.contains("BEGIN:VCARD"));
    QVERIFY(vcard.contains("VERSION:4.0"));
    QVERIFY(vcard.contains("FN:John Smith"));
    QVERIFY(vcard.contains("N:Smith;John;;;"));
    QVERIFY(vcard.contains("UID:palm-12345"));
    QVERIFY(vcard.contains("END:VCARD"));
}

void TestContactMapper::testGenerateVCardWithPhones()
{
    ContactMapper::Contact contact;
    contact.recordId = 100;
    contact.firstName = "Jane";
    contact.lastName = "Doe";
    contact.phone1 = "555-1234";
    contact.phone2 = "555-5678";
    contact.phoneLabels << "0" << "1" << "7" << "4" << "3";  // Work, Home, Mobile, Email, Other

    QString vcard = ContactMapper::contactToVCard(contact);

    QVERIFY(vcard.contains("TEL;TYPE=work,voice:555-1234"));
    QVERIFY(vcard.contains("TEL;TYPE=home,voice:555-5678"));
}

void TestContactMapper::testGenerateVCardWithAddress()
{
    ContactMapper::Contact contact;
    contact.recordId = 200;
    contact.firstName = "Bob";
    contact.lastName = "Builder";
    contact.address = "123 Main St";
    contact.city = "Anytown";
    contact.state = "CA";
    contact.zip = "90210";
    contact.country = "USA";

    QString vcard = ContactMapper::contactToVCard(contact);

    QVERIFY(vcard.contains("ADR;TYPE=work:;;123 Main St;Anytown;CA;90210;USA"));
}

void TestContactMapper::testGenerateVCardWithCustomFields()
{
    ContactMapper::Contact contact;
    contact.recordId = 300;
    contact.firstName = "Custom";
    contact.lastName = "Person";
    contact.custom1 = "Field 1";
    contact.custom2 = "Field 2";

    QString vcard = ContactMapper::contactToVCard(contact);

    QVERIFY(vcard.contains("X-PALM-CUSTOM1:Field 1"));
    QVERIFY(vcard.contains("X-PALM-CUSTOM2:Field 2"));
}

void TestContactMapper::testGenerateVCardCompanyOnly()
{
    ContactMapper::Contact contact;
    contact.recordId = 400;
    contact.company = "Acme Inc";

    QString vcard = ContactMapper::contactToVCard(contact);

    // FN should fall back to company when no name
    QVERIFY(vcard.contains("FN:Acme Inc"));
    QVERIFY(vcard.contains("ORG:Acme Inc"));
}

// ========== Round-trip Tests ==========

void TestContactMapper::testRoundTripSimpleContact()
{
    ContactMapper::Contact original;
    original.recordId = 1000;
    original.firstName = "Round";
    original.lastName = "Trip";

    QString vcard = ContactMapper::contactToVCard(original);
    ContactMapper::Contact parsed = ContactMapper::vCardToContact(vcard);

    QCOMPARE(parsed.firstName, original.firstName);
    QCOMPARE(parsed.lastName, original.lastName);
    QCOMPARE(parsed.recordId, original.recordId);
}

void TestContactMapper::testRoundTripContactWithPhones()
{
    ContactMapper::Contact original;
    original.recordId = 1001;
    original.firstName = "Phone";
    original.lastName = "Test";
    original.phone1 = "555-1111";
    original.phone2 = "555-2222";
    original.phoneLabels << "0" << "1" << "7" << "4" << "3";

    QString vcard = ContactMapper::contactToVCard(original);
    ContactMapper::Contact parsed = ContactMapper::vCardToContact(vcard);

    QCOMPARE(parsed.firstName, original.firstName);
    QCOMPARE(parsed.lastName, original.lastName);
    QCOMPARE(parsed.phone1, original.phone1);
    QCOMPARE(parsed.phone2, original.phone2);
}

void TestContactMapper::testRoundTripContactWithAddress()
{
    ContactMapper::Contact original;
    original.recordId = 1002;
    original.firstName = "Address";
    original.lastName = "Test";
    original.address = "456 Oak Ave";
    original.city = "Portland";
    original.state = "OR";
    original.zip = "97201";
    original.country = "USA";

    QString vcard = ContactMapper::contactToVCard(original);
    ContactMapper::Contact parsed = ContactMapper::vCardToContact(vcard);

    QCOMPARE(parsed.address, original.address);
    QCOMPARE(parsed.city, original.city);
    QCOMPARE(parsed.state, original.state);
    QCOMPARE(parsed.zip, original.zip);
    QCOMPARE(parsed.country, original.country);
}

void TestContactMapper::testRoundTripContactWithCustomFields()
{
    ContactMapper::Contact original;
    original.recordId = 1003;
    original.firstName = "Custom";
    original.lastName = "Test";
    original.custom1 = "Value 1";
    original.custom2 = "Value 2";
    original.custom3 = "Value 3";
    original.custom4 = "Value 4";

    QString vcard = ContactMapper::contactToVCard(original);
    ContactMapper::Contact parsed = ContactMapper::vCardToContact(vcard);

    QCOMPARE(parsed.custom1, original.custom1);
    QCOMPARE(parsed.custom2, original.custom2);
    QCOMPARE(parsed.custom3, original.custom3);
    QCOMPARE(parsed.custom4, original.custom4);
}

// ========== Edge Cases ==========

void TestContactMapper::testParseEmptyInput()
{
    ContactMapper::Contact contact = ContactMapper::vCardToContact("");

    QVERIFY(contact.firstName.isEmpty());
    QVERIFY(contact.lastName.isEmpty());
}

void TestContactMapper::testParseInvalidInput()
{
    ContactMapper::Contact contact = ContactMapper::vCardToContact("This is not a vCard");

    QVERIFY(contact.firstName.isEmpty());
    QVERIFY(contact.lastName.isEmpty());
}

void TestContactMapper::testParseMalformedName()
{
    // Test the fallback logic for malformed N field
    // Some vCards have full name in lastName field incorrectly
    QString vcard;
    vcard += "BEGIN:VCARD\r\n";
    vcard += "VERSION:4.0\r\n";
    vcard += "FN:Joe's Pizza\r\n";
    vcard += "N:Joe's Pizza;;;;\r\n";  // Whole name in lastName
    vcard += "END:VCARD\r\n";

    ContactMapper::Contact contact = ContactMapper::vCardToContact(vcard);

    // Should use FN as fallback since N looks malformed
    QCOMPARE(contact.lastName, QString("Joe's Pizza"));
}

void TestContactMapper::testLineFolding()
{
    // Test with a long note that would cause line folding
    ContactMapper::Contact original;
    original.recordId = 2000;
    original.firstName = "Folding";
    original.lastName = "Test";
    original.note = "This is a very long note that should exceed the 75 character line limit specified in RFC 6350 for vCard format";

    QString vcard = ContactMapper::contactToVCard(original);

    // Check that folding occurred (continuation lines start with space)
    QStringList lines = vcard.split("\r\n");
    bool foundContinuation = false;
    for (const QString &line : lines) {
        if (line.startsWith(" ")) {
            foundContinuation = true;
            break;
        }
    }
    QVERIFY(foundContinuation);

    // Round-trip should preserve content
    ContactMapper::Contact parsed = ContactMapper::vCardToContact(vcard);
    QCOMPARE(parsed.note, original.note);
}

// ========== Filename Generation ==========

void TestContactMapper::testGenerateFilenameFullName()
{
    ContactMapper::Contact contact;
    contact.firstName = "John";
    contact.lastName = "Smith";
    contact.recordId = 123;

    QString filename = ContactMapper::generateFilename(contact);

    QVERIFY(filename.endsWith(".vcf"));
    QVERIFY(filename.contains("John"));
    QVERIFY(filename.contains("Smith"));
}

void TestContactMapper::testGenerateFilenameFirstOnly()
{
    ContactMapper::Contact contact;
    contact.firstName = "Madonna";
    contact.recordId = 456;

    QString filename = ContactMapper::generateFilename(contact);

    QVERIFY(filename.endsWith(".vcf"));
    QVERIFY(filename.contains("Madonna"));
}

void TestContactMapper::testGenerateFilenameLastOnly()
{
    ContactMapper::Contact contact;
    contact.lastName = "Prince";
    contact.recordId = 789;

    QString filename = ContactMapper::generateFilename(contact);

    QVERIFY(filename.endsWith(".vcf"));
    QVERIFY(filename.contains("Prince"));
}

void TestContactMapper::testGenerateFilenameCompanyOnly()
{
    ContactMapper::Contact contact;
    contact.company = "Acme Corp";
    contact.recordId = 999;

    QString filename = ContactMapper::generateFilename(contact);

    QVERIFY(filename.endsWith(".vcf"));
    QVERIFY(filename.contains("Acme"));
}

void TestContactMapper::testGenerateFilenameEmpty()
{
    ContactMapper::Contact contact;
    contact.recordId = 111;

    QString filename = ContactMapper::generateFilename(contact);

    QVERIFY(filename.endsWith(".vcf"));
    QVERIFY(filename.contains("111") || filename.contains("contact"));
}

QTEST_MAIN(TestContactMapper)
#include "test_contactmapper.moc"
