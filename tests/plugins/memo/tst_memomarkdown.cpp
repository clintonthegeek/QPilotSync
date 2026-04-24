#include <QtTest/QtTest>
#include "plugins/memo/memomarkdown.h"

using WildPalms::Memo::MarkdownMemo;
using WildPalms::Memo::encode;
using WildPalms::Memo::decode;
using WildPalms::Memo::filenameFor;

class TestMemoMarkdown : public QObject {
    Q_OBJECT
private slots:
    void roundTripTextOnly();
    void roundTripWithCategoryAndPrivate();
    void canonicalKeyOrder();
    void defaultOmissionSlotZero();
    void defaultOmissionPrivateFalse();
    void defaultOmissionMissingCategoryName();
    void bodyTrailingNewlineCanonical();
    void parseAcceptsIntegerCategory();
    void parseAcceptsStringCategory();
    void parseToleratesMissingKeys();
    void parseToleratesMalformedFrontmatter();
    void parseAcceptsOldCreatedField();
    void filenameFromFirstLine();
    void filenameFallbackForEmptyBody();
    void filenameSanitisesSpecialChars();
};

// --- round-trip ---

void TestMemoMarkdown::roundTripTextOnly()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("hello world");
    const QString md = encode(m);

    MarkdownMemo back = decode(md);
    QCOMPARE(back.content.text, m.content.text);
    QCOMPARE(back.content.isPrivate, false);
    QCOMPARE(back.categorySlot, 0);
}

void TestMemoMarkdown::roundTripWithCategoryAndPrivate()
{
    MarkdownMemo m;
    m.recordId = 42;
    m.content.text = QStringLiteral("secret note\nsecond line");
    m.content.isPrivate = true;
    m.categorySlot = 3;
    m.categoryName = QStringLiteral("Work");
    const QString md = encode(m);

    MarkdownMemo back = decode(md);
    QCOMPARE(back.recordId, 42u);
    QCOMPARE(back.content.text, m.content.text);
    QCOMPARE(back.content.isPrivate, true);
    QCOMPARE(back.categorySlot, 3);
    QCOMPARE(back.categoryName.value_or(QString()), QStringLiteral("Work"));
}

// --- canonicalisation ---

void TestMemoMarkdown::canonicalKeyOrder()
{
    MarkdownMemo m;
    m.recordId = 1;
    m.content.text = QStringLiteral("body");
    m.content.isPrivate = true;
    m.categorySlot = 2;
    m.categoryName = QStringLiteral("Home");
    const QString md = encode(m);

    // id < category < categoryName < private, each on its own line.
    const int idxId       = md.indexOf(QStringLiteral("id:"));
    const int idxCat      = md.indexOf(QStringLiteral("category:"));
    const int idxCatName  = md.indexOf(QStringLiteral("categoryName:"));
    const int idxPrivate  = md.indexOf(QStringLiteral("private:"));
    QVERIFY(idxId >= 0 && idxCat > idxId && idxCatName > idxCat && idxPrivate > idxCatName);
}

void TestMemoMarkdown::defaultOmissionSlotZero()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("body");
    m.categorySlot = 0;  // default — no categoryName either
    QVERIFY(!encode(m).contains(QStringLiteral("category:")));
}

void TestMemoMarkdown::defaultOmissionPrivateFalse()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("body");
    m.content.isPrivate = false;
    QVERIFY(!encode(m).contains(QStringLiteral("private:")));
}

void TestMemoMarkdown::defaultOmissionMissingCategoryName()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("body");
    m.categorySlot = 5;
    m.categoryName.reset();  // no store or slot unknown
    const QString md = encode(m);
    QVERIFY(md.contains(QStringLiteral("category: 5")));
    QVERIFY(!md.contains(QStringLiteral("categoryName:")));
}

void TestMemoMarkdown::bodyTrailingNewlineCanonical()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("line one\nline two");  // no trailing \n
    const QString md = encode(m);
    QVERIFY(md.endsWith(QChar('\n')));
    QVERIFY(!md.endsWith(QStringLiteral("\n\n")));

    // Decoding strips the single trailing newline.
    MarkdownMemo back = decode(md);
    QCOMPARE(back.content.text, m.content.text);
}

// --- parse tolerance ---

void TestMemoMarkdown::parseAcceptsIntegerCategory()
{
    const QString md = QStringLiteral(
        "---\n"
        "category: 4\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.categorySlot, 4);
    QVERIFY(!m.categoryName.has_value());
}

void TestMemoMarkdown::parseAcceptsStringCategory()
{
    const QString md = QStringLiteral(
        "---\n"
        "category: Work\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.categorySlot, 0);
    QCOMPARE(m.categoryName.value_or(QString()), QStringLiteral("Work"));
}

void TestMemoMarkdown::parseToleratesMissingKeys()
{
    const QString md = QStringLiteral("just body text\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.content.text, QStringLiteral("just body text"));
    QCOMPARE(m.recordId, 0u);
    QCOMPARE(m.categorySlot, 0);
    QCOMPARE(m.content.isPrivate, false);
}

void TestMemoMarkdown::parseToleratesMalformedFrontmatter()
{
    const QString md = QStringLiteral(
        "---\n"
        "garbage without colon\n"
        "id: 7\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.recordId, 7u);
    QCOMPARE(m.content.text, QStringLiteral("body"));
}

void TestMemoMarkdown::parseAcceptsOldCreatedField()
{
    // The old memomapper emitted `created:`; we ignore it silently.
    const QString md = QStringLiteral(
        "---\n"
        "id: 3\n"
        "created: 2025-01-01T00:00:00\n"
        "---\n"
        "\n"
        "body\n");
    MarkdownMemo m = decode(md);
    QCOMPARE(m.recordId, 3u);
    QCOMPARE(m.content.text, QStringLiteral("body"));
}

// --- filenames ---

void TestMemoMarkdown::filenameFromFirstLine()
{
    MarkdownMemo m;
    m.recordId = 9;
    m.content.text = QStringLiteral("Grocery list\n- milk\n- eggs");
    QCOMPARE(filenameFor(m), QStringLiteral("Grocery_list.md"));
}

void TestMemoMarkdown::filenameFallbackForEmptyBody()
{
    MarkdownMemo m;
    m.recordId = 17;
    m.content.text.clear();
    QCOMPARE(filenameFor(m), QStringLiteral("memo_17.md"));
}

void TestMemoMarkdown::filenameSanitisesSpecialChars()
{
    MarkdownMemo m;
    m.content.text = QStringLiteral("a/b\\c:d*e");
    // Every invalid char maps to '_', then underscores collapse at trim.
    const QString f = filenameFor(m);
    QVERIFY(f.endsWith(QStringLiteral(".md")));
    QVERIFY(!f.contains('/'));
    QVERIFY(!f.contains('\\'));
    QVERIFY(!f.contains(':'));
    QVERIFY(!f.contains('*'));
}

QTEST_MAIN(TestMemoMarkdown)
#include "tst_memomarkdown.moc"
