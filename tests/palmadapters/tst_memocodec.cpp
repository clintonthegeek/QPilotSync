#include <QtTest/QtTest>

#include "memocodec.h"

using WildPalms::PalmCodecs::Memo;
using WildPalms::PalmCodecs::encodeMemo;
using WildPalms::PalmCodecs::decodeMemo;

class TestMemoCodec : public QObject
{
    Q_OBJECT
private slots:
    void emptyTextRoundTrips();
    void simpleAsciiRoundTrips();
    void utf8WithNewlineRoundTrips();
    void windows1252SmartQuotesRoundTrip();
    void privateFlagRoundTrips();
    void decodeEmptyBytesReturnsEmpty();
    void decodeHandlesNullTerminator();
};

void TestMemoCodec::emptyTextRoundTrips()
{
    Memo m;
    m.text = QString();
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QString());
    QCOMPARE(decoded->isPrivate, false);
}

void TestMemoCodec::simpleAsciiRoundTrips()
{
    Memo m;
    m.text = QStringLiteral("Shopping list\n- apples\n- oranges");
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, m.text);
}

void TestMemoCodec::utf8WithNewlineRoundTrips()
{
    Memo m;
    m.text = QStringLiteral("line one\nline two");
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, m.text);
}

void TestMemoCodec::windows1252SmartQuotesRoundTrip()
{
    Memo m;
    m.text = QString::fromUtf8("He said \xE2\x80\x9Chello\xE2\x80\x9D.");
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, m.text);
}

void TestMemoCodec::privateFlagRoundTrips()
{
    // The private flag lives on PalmRecord.attributes, NOT inside the
    // memo content. The codec does not read or write it; two Memos
    // differing only in isPrivate produce the same bytes.
    Memo m1;   m1.text = QStringLiteral("x"); m1.isPrivate = false;
    Memo m2;   m2.text = QStringLiteral("x"); m2.isPrivate = true;
    QCOMPARE(encodeMemo(m1), encodeMemo(m2));
}

void TestMemoCodec::decodeEmptyBytesReturnsEmpty()
{
    const auto decoded = decodeMemo(QByteArray());
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QString());
}

void TestMemoCodec::decodeHandlesNullTerminator()
{
    QByteArray bytes("hello", 5);
    bytes.append('\0');
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QStringLiteral("hello"));
}

QTEST_GUILESS_MAIN(TestMemoCodec)
#include "tst_memocodec.moc"
