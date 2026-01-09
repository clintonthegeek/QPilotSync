/**
 * @file test_memomapper.cpp
 * @brief Unit tests for MemoMapper class
 *
 * Tests the conversion between Palm Memo format and Markdown with YAML frontmatter.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include "mappers/memomapper.h"

class TestMemoMapper : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========== memoToMarkdown Tests ==========
    void testGenerateSimpleMarkdown();
    void testGenerateMarkdownWithCategoryName();
    void testGenerateMarkdownWithCategoryNumber();
    void testGenerateMarkdownWithPrivateFlag();
    void testGenerateMarkdownWithModifiedFlag();
    void testGenerateMarkdownWithAllMetadata();
    void testGenerateMarkdownEndsWithNewline();

    // ========== markdownToMemo Tests ==========
    void testParseSimpleMarkdown();
    void testParseMarkdownWithoutFrontmatter();
    void testParseMarkdownWithId();
    void testParseMarkdownWithCategoryName();
    void testParseMarkdownWithCategoryNumber();
    void testParseMarkdownWithPrivateFlag();
    void testParseMarkdownWithModifiedFlag();
    void testParseMarkdownWithAllMetadata();
    void testParseMarkdownMultilineBody();

    // ========== Round-trip Tests ==========
    void testRoundTripSimpleMemo();
    void testRoundTripMemoWithMetadata();
    void testRoundTripMultilineMemo();

    // ========== Edge Cases ==========
    void testParseEmptyInput();
    void testParseOnlyFrontmatter();
    void testParseUnclosedFrontmatter();
    void testParseMalformedYaml();

    // ========== Filename Generation ==========
    void testGenerateFilenameFromFirstLine();
    void testGenerateFilenameWithSpecialChars();
    void testGenerateFilenameEmptyMemo();
    void testGenerateFilenameLongFirstLine();
    void testGenerateFilenameMultipleSpaces();
    void testGenerateFilenameOnlySpecialChars();
};

void TestMemoMapper::initTestCase()
{
    qDebug() << "Starting MemoMapper tests";
}

void TestMemoMapper::cleanupTestCase()
{
    qDebug() << "MemoMapper tests complete";
}

// ========== memoToMarkdown Tests ==========

void TestMemoMapper::testGenerateSimpleMarkdown()
{
    MemoMapper::Memo memo;
    memo.recordId = 12345;
    memo.category = 0;
    memo.text = "This is a simple memo.";
    memo.isPrivate = false;
    memo.isDirty = false;
    memo.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(memo);

    QVERIFY(markdown.contains("---"));
    QVERIFY(markdown.contains("id: 12345"));
    QVERIFY(markdown.contains("This is a simple memo."));
    QVERIFY(markdown.contains("created:"));  // Should have timestamp
}

void TestMemoMapper::testGenerateMarkdownWithCategoryName()
{
    MemoMapper::Memo memo;
    memo.recordId = 100;
    memo.category = 1;
    memo.text = "Categorized memo";
    memo.isPrivate = false;
    memo.isDirty = false;
    memo.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(memo, "Business");

    QVERIFY(markdown.contains("category: Business"));
}

void TestMemoMapper::testGenerateMarkdownWithCategoryNumber()
{
    MemoMapper::Memo memo;
    memo.recordId = 100;
    memo.category = 5;
    memo.text = "Numbered category memo";
    memo.isPrivate = false;
    memo.isDirty = false;
    memo.isDeleted = false;

    // No category name provided, should use number
    QString markdown = MemoMapper::memoToMarkdown(memo);

    QVERIFY(markdown.contains("category: 5"));
}

void TestMemoMapper::testGenerateMarkdownWithPrivateFlag()
{
    MemoMapper::Memo memo;
    memo.recordId = 200;
    memo.category = 0;
    memo.text = "Private memo content";
    memo.isPrivate = true;
    memo.isDirty = false;
    memo.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(memo);

    QVERIFY(markdown.contains("private: true"));
}

void TestMemoMapper::testGenerateMarkdownWithModifiedFlag()
{
    MemoMapper::Memo memo;
    memo.recordId = 300;
    memo.category = 0;
    memo.text = "Modified memo";
    memo.isPrivate = false;
    memo.isDirty = true;
    memo.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(memo);

    QVERIFY(markdown.contains("modified: true"));
}

void TestMemoMapper::testGenerateMarkdownWithAllMetadata()
{
    MemoMapper::Memo memo;
    memo.recordId = 999;
    memo.category = 3;
    memo.text = "Full metadata memo";
    memo.isPrivate = true;
    memo.isDirty = true;
    memo.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(memo, "Personal");

    QVERIFY(markdown.contains("id: 999"));
    QVERIFY(markdown.contains("category: Personal"));
    QVERIFY(markdown.contains("private: true"));
    QVERIFY(markdown.contains("modified: true"));
    QVERIFY(markdown.contains("Full metadata memo"));
}

void TestMemoMapper::testGenerateMarkdownEndsWithNewline()
{
    MemoMapper::Memo memo;
    memo.recordId = 1;
    memo.text = "No trailing newline";

    QString markdown = MemoMapper::memoToMarkdown(memo);

    QVERIFY(markdown.endsWith('\n'));
}

// ========== markdownToMemo Tests ==========

void TestMemoMapper::testParseSimpleMarkdown()
{
    QString markdown = "---\nid: 12345\n---\n\nSimple memo text\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.recordId, 12345);
    QCOMPARE(memo.text, QString("Simple memo text"));
}

void TestMemoMapper::testParseMarkdownWithoutFrontmatter()
{
    QString markdown = "Plain text without any YAML frontmatter.\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.recordId, 0);  // Default value
    QCOMPARE(memo.text, QString("Plain text without any YAML frontmatter."));
}

void TestMemoMapper::testParseMarkdownWithId()
{
    QString markdown = "---\nid: 54321\n---\n\nMemo with ID\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.recordId, 54321);
}

void TestMemoMapper::testParseMarkdownWithCategoryName()
{
    QString markdown = "---\nid: 100\ncategory: Work\n---\n\nWork memo\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.categoryName, QString("Work"));
    QCOMPARE(memo.category, 0);  // Category number should be 0 when name is used
}

void TestMemoMapper::testParseMarkdownWithCategoryNumber()
{
    QString markdown = "---\nid: 100\ncategory: 7\n---\n\nCategory numbered memo\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.category, 7);
    QVERIFY(memo.categoryName.isEmpty());
}

void TestMemoMapper::testParseMarkdownWithPrivateFlag()
{
    QString markdown = "---\nid: 200\nprivate: true\n---\n\nSecret memo\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.isPrivate, true);
}

void TestMemoMapper::testParseMarkdownWithModifiedFlag()
{
    QString markdown = "---\nid: 300\nmodified: true\n---\n\nChanged memo\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.isDirty, true);
}

void TestMemoMapper::testParseMarkdownWithAllMetadata()
{
    QString markdown = "---\n"
                       "id: 999\n"
                       "category: Personal\n"
                       "private: true\n"
                       "modified: true\n"
                       "---\n\n"
                       "Full featured memo\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.recordId, 999);
    QCOMPARE(memo.categoryName, QString("Personal"));
    QCOMPARE(memo.isPrivate, true);
    QCOMPARE(memo.isDirty, true);
    QCOMPARE(memo.text, QString("Full featured memo"));
}

void TestMemoMapper::testParseMarkdownMultilineBody()
{
    QString markdown = "---\nid: 500\n---\n\n"
                       "Line one\n"
                       "Line two\n"
                       "Line three\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    // Note: trailing newline is stripped
    QCOMPARE(memo.text, QString("Line one\nLine two\nLine three"));
}

// ========== Round-trip Tests ==========

void TestMemoMapper::testRoundTripSimpleMemo()
{
    MemoMapper::Memo original;
    original.recordId = 1000;
    original.category = 0;
    original.text = "Round trip test memo";
    original.isPrivate = false;
    original.isDirty = false;
    original.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(original);
    MemoMapper::Memo parsed = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(parsed.recordId, original.recordId);
    QCOMPARE(parsed.text, original.text);
}

void TestMemoMapper::testRoundTripMemoWithMetadata()
{
    MemoMapper::Memo original;
    original.recordId = 2000;
    original.category = 0;  // Category name is used instead
    original.categoryName = "Important";
    original.text = "Important memo content";
    original.isPrivate = true;
    original.isDirty = true;
    original.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(original, "Important");
    MemoMapper::Memo parsed = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(parsed.recordId, original.recordId);
    QCOMPARE(parsed.text, original.text);
    QCOMPARE(parsed.isPrivate, original.isPrivate);
    QCOMPARE(parsed.isDirty, original.isDirty);
    QCOMPARE(parsed.categoryName, QString("Important"));
}

void TestMemoMapper::testRoundTripMultilineMemo()
{
    MemoMapper::Memo original;
    original.recordId = 3000;
    original.category = 0;
    original.text = "Shopping List\n\n- Milk\n- Bread\n- Eggs\n- Butter";
    original.isPrivate = false;
    original.isDirty = false;
    original.isDeleted = false;

    QString markdown = MemoMapper::memoToMarkdown(original);
    MemoMapper::Memo parsed = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(parsed.text, original.text);
}

// ========== Edge Cases ==========

void TestMemoMapper::testParseEmptyInput()
{
    MemoMapper::Memo memo = MemoMapper::markdownToMemo("");

    QCOMPARE(memo.recordId, 0);
    QVERIFY(memo.text.isEmpty());
}

void TestMemoMapper::testParseOnlyFrontmatter()
{
    QString markdown = "---\nid: 100\ncategory: Test\n---\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    QCOMPARE(memo.recordId, 100);
    QVERIFY(memo.text.isEmpty());
}

void TestMemoMapper::testParseUnclosedFrontmatter()
{
    // Frontmatter starts with --- but has no closing ---
    QString markdown = "---\nid: 100\nThis looks like YAML but never closes\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    // Should treat entire content as body (no closing ---)
    QCOMPARE(memo.recordId, 0);
    // The text should contain the original content minus trailing newline
    QVERIFY(memo.text.contains("---"));
}

void TestMemoMapper::testParseMalformedYaml()
{
    // Valid frontmatter delimiters but weird content
    QString markdown = "---\nnot valid yaml at all\n---\n\nBody text\n";

    MemoMapper::Memo memo = MemoMapper::markdownToMemo(markdown);

    // Should still parse, just won't find valid keys
    QCOMPARE(memo.recordId, 0);
    QCOMPARE(memo.text, QString("Body text"));
}

// ========== Filename Generation ==========

void TestMemoMapper::testGenerateFilenameFromFirstLine()
{
    MemoMapper::Memo memo;
    memo.recordId = 123;
    memo.text = "Shopping List\nMilk\nBread";

    QString filename = MemoMapper::generateFilename(memo);

    QVERIFY(filename.endsWith(".md"));
    QVERIFY(filename.contains("Shopping"));
    QVERIFY(filename.contains("List"));
}

void TestMemoMapper::testGenerateFilenameWithSpecialChars()
{
    MemoMapper::Memo memo;
    memo.recordId = 456;
    memo.text = "Meeting: 10/15/2024 @ 3pm!\nDiscuss budget";

    QString filename = MemoMapper::generateFilename(memo);

    QVERIFY(filename.endsWith(".md"));
    // Special chars should be replaced with underscores
    QVERIFY(!filename.contains(":"));
    QVERIFY(!filename.contains("/"));
    QVERIFY(!filename.contains("@"));
    QVERIFY(!filename.contains("!"));
}

void TestMemoMapper::testGenerateFilenameEmptyMemo()
{
    MemoMapper::Memo memo;
    memo.recordId = 789;
    memo.text = "";

    QString filename = MemoMapper::generateFilename(memo);

    QVERIFY(filename.endsWith(".md"));
    // Should fall back to ID-based name
    QVERIFY(filename.contains("789") || filename.contains("memo"));
}

void TestMemoMapper::testGenerateFilenameLongFirstLine()
{
    MemoMapper::Memo memo;
    memo.recordId = 111;
    memo.text = "This is a very long first line that should be truncated because "
                "it exceeds the maximum filename length limit of fifty characters";

    QString filename = MemoMapper::generateFilename(memo);

    QVERIFY(filename.endsWith(".md"));
    // Filename base should be truncated (50 chars max + .md extension)
    QVERIFY(filename.length() <= 54);  // 50 + ".md"
}

void TestMemoMapper::testGenerateFilenameMultipleSpaces()
{
    MemoMapper::Memo memo;
    memo.recordId = 222;
    memo.text = "Multiple   spaces    here";

    QString filename = MemoMapper::generateFilename(memo);

    QVERIFY(filename.endsWith(".md"));
    // Multiple spaces should be collapsed to single underscore
    QVERIFY(!filename.contains("__"));
}

void TestMemoMapper::testGenerateFilenameOnlySpecialChars()
{
    MemoMapper::Memo memo;
    memo.recordId = 333;
    memo.text = "!@#$%^&*()\nNormal text on second line";

    QString filename = MemoMapper::generateFilename(memo);

    QVERIFY(filename.endsWith(".md"));
    // Should fall back to ID-based name since first line sanitizes to empty
    QVERIFY(filename.contains("333") || filename.contains("memo"));
}

QTEST_MAIN(TestMemoMapper)
#include "test_memomapper.moc"
