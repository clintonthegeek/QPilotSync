#include <QtTest/QtTest>
#include <QTimeZone>

#include "palm/sync/palmrecord.h"

using WildPalms::PalmSync::PalmRecord;

/// Phase Ia Task 11: round-trip test for PalmRecord wire-bytes API.
/// The exact byte layout is internal — what matters is that
/// fromWireBytes(toWireBytes(r)) == r so the palm↔vcard4
/// TransformationStages can hand records across the QByteArray
/// boundary without loss.
class TestPalmRecordWireBytes : public QObject
{
    Q_OBJECT
private slots:
    void roundTripDefault();
    void roundTripPopulated();
    void roundTripWithAttributes();
    void roundTripEmptyData();
    void fromWireBytesEmptyReturnsDefault();
    void fromWireBytesGarbageDoesNotCrash();
};

void TestPalmRecordWireBytes::roundTripDefault()
{
    const PalmRecord r;
    const auto bytes = r.toWireBytes();
    QVERIFY(!bytes.isEmpty());
    const auto r2 = PalmRecord::fromWireBytes(bytes);
    QCOMPARE(r2, r);
}

void TestPalmRecordWireBytes::roundTripPopulated()
{
    PalmRecord r;
    r.recordId = 0x12345678;
    r.category = 7;
    r.attributes = 0;
    r.data = QByteArray("hello palm world\x00 with nul", 26);
    r.lastModified = QDateTime::fromString(QStringLiteral("2026-05-07T12:34:56Z"),
                                           Qt::ISODate);
    const auto bytes = r.toWireBytes();
    const auto r2 = PalmRecord::fromWireBytes(bytes);
    QCOMPARE(r2.recordId, r.recordId);
    QCOMPARE(r2.category, r.category);
    QCOMPARE(r2.attributes, r.attributes);
    QCOMPARE(r2.data, r.data);
    QCOMPARE(r2.lastModified, r.lastModified);
    QCOMPARE(r2, r);
}

void TestPalmRecordWireBytes::roundTripWithAttributes()
{
    PalmRecord r;
    r.recordId = 42;
    r.category = 15;
    r.attributes = PalmRecord::AttrDirty | PalmRecord::AttrSecret;
    r.data = QByteArray("X");
    r.lastModified = QDateTime::currentDateTimeUtc();
    const auto bytes = r.toWireBytes();
    const auto r2 = PalmRecord::fromWireBytes(bytes);
    QCOMPARE(r2, r);
    QVERIFY(r2.isDirty());
    QVERIFY(r2.isSecret());
    QVERIFY(!r2.isDeleted());
}

void TestPalmRecordWireBytes::roundTripEmptyData()
{
    PalmRecord r;
    r.recordId = 1;
    r.category = 0;
    r.data = QByteArray();
    r.lastModified = QDateTime::fromSecsSinceEpoch(0, QTimeZone::UTC);
    const auto bytes = r.toWireBytes();
    const auto r2 = PalmRecord::fromWireBytes(bytes);
    QCOMPARE(r2, r);
}

void TestPalmRecordWireBytes::fromWireBytesEmptyReturnsDefault()
{
    const auto r = PalmRecord::fromWireBytes(QByteArray());
    QCOMPARE(r, PalmRecord{});
}

void TestPalmRecordWireBytes::fromWireBytesGarbageDoesNotCrash()
{
    // Truncated stream: should fall back to a default-constructed
    // record rather than UB.
    const auto r = PalmRecord::fromWireBytes(QByteArray("\x01\x02\x03", 3));
    Q_UNUSED(r);
    // Nothing to assert beyond not crashing — content is undefined
    // for malformed input, but the method must return cleanly.
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(TestPalmRecordWireBytes)
#include "tst_palmrecord_wirebytes.moc"
