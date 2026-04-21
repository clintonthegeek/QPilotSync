#include <QtTest/QtTest>

#include "palmrecord_bridge.h"

using WildPalms::PalmDevice::fromPilotRecord;
using WildPalms::PalmDevice::toPilotRecord;
using WildPalms::PalmSync::PalmRecord;

class TestPalmRecordBridge : public QObject
{
    Q_OBJECT
private slots:
    void fromPilotPreservesFields();
    void toPilotPreservesFields();
    void roundTripIsIdentity();
    void fromPilotMasksCategoryTo4Bits();
};

void TestPalmRecordBridge::fromPilotPreservesFields()
{
    PilotRecord src(42, 3, PilotRecord::AttrDirty | PilotRecord::AttrSecret,
                    QByteArrayLiteral("payload"));
    const auto out = fromPilotRecord(src);
    QCOMPARE(out.recordId, 42u);
    QCOMPARE(out.category, static_cast<std::uint8_t>(3));
    QCOMPARE(out.attributes,
             static_cast<std::uint8_t>(
                 PalmRecord::AttrDirty | PalmRecord::AttrSecret));
    QCOMPARE(out.data, QByteArrayLiteral("payload"));
    QVERIFY(!out.lastModified.isValid());
}

void TestPalmRecordBridge::toPilotPreservesFields()
{
    PalmRecord src;
    src.recordId = 7;
    src.category = 11;
    src.attributes = PalmRecord::AttrArchived;
    src.data = QByteArrayLiteral("body");

    const auto out = toPilotRecord(src);
    QCOMPARE(out.recordId(), 7);
    QCOMPARE(out.category(), 11);
    QCOMPARE(out.attributes(),
             static_cast<int>(PalmRecord::AttrArchived));
    QCOMPARE(out.data(), QByteArrayLiteral("body"));
}

void TestPalmRecordBridge::roundTripIsIdentity()
{
    PilotRecord original(99, 5, PilotRecord::AttrDeleted,
                         QByteArrayLiteral("x"));
    const auto intermediate = fromPilotRecord(original);
    const auto back = toPilotRecord(intermediate);
    QCOMPARE(back.recordId(), original.recordId());
    QCOMPARE(back.category(), original.category());
    QCOMPARE(back.attributes(), original.attributes());
    QCOMPARE(back.data(), original.data());
}

void TestPalmRecordBridge::fromPilotMasksCategoryTo4Bits()
{
    // Palm category is 4 bits; higher bits should be masked off.
    PilotRecord src(1, 0xFF, 0, QByteArray());
    const auto out = fromPilotRecord(src);
    QCOMPARE(out.category, static_cast<std::uint8_t>(0x0F));
}

QTEST_MAIN(TestPalmRecordBridge)
#include "tst_palmrecord_bridge.moc"
