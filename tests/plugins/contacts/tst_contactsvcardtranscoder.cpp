#include <QTest>
#include <QByteArray>

#include "palm/codecs/contactcodec.h"
#include "palm/sync/palmrecord.h"
#include "plugins/contacts/contactsvcardtranscoder.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmCodecs::decodeContact;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::ContactsPlugin::encodePalmToVcard;
using WildPalms::ContactsPlugin::decodeVcardToPalm;

namespace {

PalmRecord makeRecord(const Contact &c, std::uint8_t slot, std::uint32_t id, bool secret = false)
{
    PalmRecord pr;
    pr.data       = encodeContact(c);
    pr.category   = slot;
    pr.recordId   = id;
    pr.attributes = secret ? PalmRecord::AttrSecret : 0;
    return pr;
}

Contact sampleContact()
{
    Contact c;
    c.lastName  = QStringLiteral("Doe");
    c.firstName = QStringLiteral("Jane");
    c.company   = QStringLiteral("Acme");
    c.title     = QStringLiteral("Engineer");
    c.phone[0]  = QStringLiteral("555-1111");
    c.phoneLabels << QStringLiteral("Work");
    c.address   = QStringLiteral("1 Main St");
    c.city      = QStringLiteral("Springfield");
    c.state     = QStringLiteral("IL");
    c.zip       = QStringLiteral("62701");
    c.country   = QStringLiteral("USA");
    c.note      = QStringLiteral("VIP customer");
    return c;
}

} // namespace

class TstContactsVcardTranscoder : public QObject
{
    Q_OBJECT
private slots:
    void roundTripPreservesCoreFields();
    void emptyRecordYieldsEmptyVcard();
    void noCatsYieldsSlotZero();
    void recordIdRoundTrips();
    void secretBitRoundTrips();
    void emptyVcardYieldsNullopt();
    void garbageVcardYieldsNullopt();
};

void TstContactsVcardTranscoder::roundTripPreservesCoreFields()
{
    const Contact c = sampleContact();
    const auto pr = makeRecord(c, /*slot*/ 3, /*id*/ 0x42);

    const QByteArray vcard = encodePalmToVcard(pr, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(!vcard.isEmpty());
    QVERIFY(vcard.contains("BEGIN:VCARD"));
    QVERIFY(vcard.contains("END:VCARD"));

    // No store — slot 0 (Unfiled) is returned; record ID still round-trips.
    auto decoded = decodeVcardToPalm(vcard, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->recordId, 0x42u);

    auto roundTrip = decodeContact(QByteArrayView(decoded->data));
    QVERIFY(roundTrip.has_value());
    QCOMPARE(roundTrip->lastName,  c.lastName);
    QCOMPARE(roundTrip->firstName, c.firstName);
    QCOMPARE(roundTrip->company,   c.company);
    QCOMPARE(roundTrip->title,     c.title);
    QCOMPARE(roundTrip->phone[0],  c.phone[0]);
    QCOMPARE(roundTrip->city,      c.city);
    QCOMPARE(roundTrip->note,      c.note);
}

void TstContactsVcardTranscoder::emptyRecordYieldsEmptyVcard()
{
    PalmRecord pr;   // pr.data is empty
    QVERIFY(encodePalmToVcard(pr, nullptr, {}).isEmpty());
}

void TstContactsVcardTranscoder::noCatsYieldsSlotZero()
{
    // Without a CategoryMappingStore, no CATEGORIES is emitted on encode
    // and decode returns slot 0 (Unfiled) regardless of the Palm record's slot.
    const auto pr = makeRecord(sampleContact(), /*slot*/ 5, /*id*/ 1);
    const QByteArray vcard = encodePalmToVcard(pr, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(!vcard.isEmpty());

    auto decoded = decodeVcardToPalm(vcard, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(decoded.has_value());
    QCOMPARE(int(decoded->category), 0);   // no store -> Unfiled
}

void TstContactsVcardTranscoder::recordIdRoundTrips()
{
    const auto pr = makeRecord(sampleContact(), 0, 0xABCDEFu);
    auto decoded = decodeVcardToPalm(encodePalmToVcard(pr, nullptr, {}), nullptr, {});
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->recordId, 0xABCDEFu);
}

void TstContactsVcardTranscoder::secretBitRoundTrips()
{
    const auto pr = makeRecord(sampleContact(), 2, 7, /*secret*/ true);
    auto decoded = decodeVcardToPalm(encodePalmToVcard(pr, nullptr, {}), nullptr, {});
    QVERIFY(decoded.has_value());
    QVERIFY(decoded->isSecret());
}

void TstContactsVcardTranscoder::emptyVcardYieldsNullopt()
{
    QVERIFY(!decodeVcardToPalm({}, nullptr, {}).has_value());
}

void TstContactsVcardTranscoder::garbageVcardYieldsNullopt()
{
    QVERIFY(!decodeVcardToPalm("not a vcard at all", nullptr, {}).has_value());
}

QTEST_GUILESS_MAIN(TstContactsVcardTranscoder)
#include "tst_contactsvcardtranscoder.moc"
