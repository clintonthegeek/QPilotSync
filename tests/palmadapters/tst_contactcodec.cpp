#include <QtTest/QtTest>

#include "contactcodec.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmCodecs::decodeContact;

class TestContactCodec : public QObject
{
    Q_OBJECT
private slots:
    void emptyContactRoundTrips();
    void firstAndLastNameRoundTrip();
    void companyAndTitleRoundTrip();
    void allFivePhoneSlotsRoundTrip();
    void phoneLabelsRoundTrip();
    void showPhoneNonZeroRoundTrips();
    void addressFieldsRoundTrip();
    void customFieldsRoundTrip();
    void noteRoundTrip();
    void utf8SmartQuotesInNameRoundTrip();
    void decodeEmptyBytesReturnsNullopt();
};

namespace {

Contact makeSample()
{
    Contact c;
    c.lastName  = QStringLiteral("Doe");
    c.firstName = QStringLiteral("Jane");
    c.company   = QStringLiteral("Acme, Inc.");
    c.title     = QStringLiteral("Engineer");
    c.phone[0]  = QStringLiteral("555-0100");
    c.phone[1]  = QStringLiteral("555-0101");
    c.phone[2]  = QStringLiteral("555-0102");
    c.phone[3]  = QStringLiteral("jane@example.com");
    c.phone[4]  = QStringLiteral("555-0104");
    c.phoneLabels = { QStringLiteral("Work"),
                      QStringLiteral("Home"),
                      QStringLiteral("Mobile"),
                      QStringLiteral("E-mail"),
                      QStringLiteral("Other") };
    c.showPhone = 0;
    c.address   = QStringLiteral("123 Main St");
    c.city      = QStringLiteral("Springfield");
    c.state     = QStringLiteral("IL");
    c.zip       = QStringLiteral("62701");
    c.country   = QStringLiteral("USA");
    c.custom[0] = QStringLiteral("Field A");
    c.custom[1] = QStringLiteral("Field B");
    c.custom[2] = QStringLiteral("");
    c.custom[3] = QStringLiteral("Field D");
    c.note      = QStringLiteral("Multi-line note\nsecond line.");
    c.isPrivate = false;
    return c;
}

} // namespace

void TestContactCodec::emptyContactRoundTrips()
{
    Contact c{};
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(*decoded, c);
}

void TestContactCodec::firstAndLastNameRoundTrip()
{
    Contact c{};
    c.firstName = QStringLiteral("Alice");
    c.lastName  = QStringLiteral("Liddell");
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->firstName, c.firstName);
    QCOMPARE(decoded->lastName,  c.lastName);
}

void TestContactCodec::companyAndTitleRoundTrip()
{
    Contact c{};
    c.company = QStringLiteral("Initech");
    c.title   = QStringLiteral("Software Engineer");
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->company, c.company);
    QCOMPARE(decoded->title,   c.title);
}

void TestContactCodec::allFivePhoneSlotsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(decoded->phone[i], c.phone[i]);
    }
}

void TestContactCodec::phoneLabelsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->phoneLabels, c.phoneLabels);
}

void TestContactCodec::showPhoneNonZeroRoundTrips()
{
    Contact c = makeSample();
    c.showPhone = 3;
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->showPhone, 3);
}

void TestContactCodec::addressFieldsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->address, c.address);
    QCOMPARE(decoded->city,    c.city);
    QCOMPARE(decoded->state,   c.state);
    QCOMPARE(decoded->zip,     c.zip);
    QCOMPARE(decoded->country, c.country);
}

void TestContactCodec::customFieldsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(decoded->custom[i], c.custom[i]);
    }
}

void TestContactCodec::noteRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->note, c.note);
}

void TestContactCodec::utf8SmartQuotesInNameRoundTrip()
{
    Contact c{};
    c.firstName = QString::fromUtf8("Jos\xC3\xA9");  // José
    c.lastName  = QString::fromUtf8("O\xE2\x80\x99""Connor"); // O'Connor
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->firstName, c.firstName);
    QCOMPARE(decoded->lastName,  c.lastName);
}

void TestContactCodec::decodeEmptyBytesReturnsNullopt()
{
    const auto decoded = decodeContact(QByteArray());
    QCOMPARE(decoded.has_value(), false);
}

QTEST_GUILESS_MAIN(TestContactCodec)
#include "tst_contactcodec.moc"
