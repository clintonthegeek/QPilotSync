#include "memomapper.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QStringConverter>

// Windows-1252 to Unicode mapping table for 0x80-0x9F
static const unsigned short cp1252_to_unicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
};

// Helper to decode Palm text which uses Windows-1252 encoding
// Palm OS uses Windows-1252 (not pure Latin-1) for the 0x80-0x9F range
// This includes characters like ™ (0x99), smart quotes, em/en dashes, etc.
static QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);

    // Manually map Windows-1252 characters in the 0x80-0x9F range
    // This range is undefined in ISO-8859-1 but defined in Windows-1252
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
}

// Helper to encode Unicode text to Windows-1252 for Palm
static QByteArray encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());

    for (QChar ch : text) {
        ushort unicode = ch.unicode();

        if (unicode < 0x80) {
            // ASCII - direct copy
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && !(unicode >= 0x80 && unicode <= 0x9F)) {
            // Latin-1 (0xA0-0xFF) - direct copy
            result.append(static_cast<char>(unicode));
        } else {
            // Check if this Unicode char maps to Windows-1252 0x80-0x9F range
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (cp1252_to_unicode[i] == unicode) {
                    result.append(static_cast<char>(0x80 + i));
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Unsupported character - use '?' as placeholder
                result.append('?');
            }
        }
    }

    return result;
}

MemoMapper::MemoMapper(QObject *parent)
    : QObject(parent)
{
}

MemoMapper::~MemoMapper()
{
}

MemoMapper::Memo MemoMapper::unpackMemo(const PilotRecord *record)
{
    Memo memo;
    memo.recordId = record->recordId();
    memo.category = record->category();
    memo.isPrivate = record->isSecret();
    memo.isDirty = record->isDirty();
    memo.isDeleted = record->isDeleted();

    // Palm memos are just null-terminated text strings
    QByteArray data = record->data();
    if (data.isEmpty()) {
        memo.text = QString();
        return memo;
    }

    // Convert from Windows-1252 (Palm's encoding for text)
    memo.text = decodePalmText(data.constData());

    return memo;
}

QString MemoMapper::memoToMarkdown(const Memo &memo, const QString &categoryName)
{
    QString markdown;

    // YAML frontmatter
    markdown += "---\n";
    markdown += QString("id: %1\n").arg(memo.recordId);

    if (!categoryName.isEmpty()) {
        markdown += QString("category: %1\n").arg(categoryName);
    } else if (memo.category > 0) {
        markdown += QString("category: %1\n").arg(memo.category);
    }

    markdown += QString("created: %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));

    if (memo.isPrivate) {
        markdown += "private: true\n";
    }

    if (memo.isDirty) {
        markdown += "modified: true\n";
    }

    markdown += "---\n\n";

    // Memo content
    markdown += memo.text;

    // Ensure file ends with newline
    if (!markdown.endsWith('\n')) {
        markdown += '\n';
    }

    return markdown;
}

QString MemoMapper::generateFilename(const Memo &memo)
{
    QString filename;

    // Use first line or first 50 chars of memo as base filename
    QString firstLine = memo.text.split('\n').first().trimmed();
    if (firstLine.isEmpty()) {
        filename = QString("memo_%1").arg(memo.recordId);
    } else {
        // Take first 50 chars
        filename = firstLine.left(50);

        // Replace invalid filename characters with underscore
        static QRegularExpression invalidChars("[^a-zA-Z0-9_\\-. ]");
        filename.replace(invalidChars, "_");

        // Replace multiple spaces with single underscore
        static QRegularExpression multiSpace("\\s+");
        filename.replace(multiSpace, "_");

        // Remove leading/trailing underscores
        filename = filename.trimmed();
        while (filename.startsWith('_')) filename.remove(0, 1);
        while (filename.endsWith('_')) filename.chop(1);

        // If sanitization left nothing useful, fall back to ID
        if (filename.isEmpty()) {
            filename = QString("memo_%1").arg(memo.recordId);
        }
    }

    // Add .md extension
    filename += ".md";

    return filename;
}

// ========== Reverse mapping: Markdown → Palm ==========

MemoMapper::Memo MemoMapper::markdownToMemo(const QString &markdown)
{
    Memo memo;
    memo.recordId = 0;
    memo.category = 0;
    memo.isPrivate = false;
    memo.isDirty = false;
    memo.isDeleted = false;

    QString content = markdown;
    QString body;

    // Check for YAML frontmatter (starts with "---")
    if (content.startsWith("---")) {
        // Find the closing "---"
        int endFrontmatter = content.indexOf("\n---", 3);
        if (endFrontmatter != -1) {
            QString frontmatter = content.mid(4, endFrontmatter - 4);
            body = content.mid(endFrontmatter + 5); // Skip "\n---\n"

            // Remove leading newlines from body
            while (body.startsWith('\n')) {
                body.remove(0, 1);
            }

            // Parse YAML frontmatter (simple key: value parsing)
            QStringList lines = frontmatter.split('\n');
            for (const QString &line : lines) {
                int colonPos = line.indexOf(':');
                if (colonPos == -1) continue;

                QString key = line.left(colonPos).trimmed().toLower();
                QString value = line.mid(colonPos + 1).trimmed();

                if (key == "id") {
                    memo.recordId = value.toInt();
                } else if (key == "category") {
                    // Try to parse as number first
                    bool ok;
                    int catNum = value.toInt(&ok);
                    if (ok) {
                        memo.category = catNum;
                    }
                    // Category name handling would require category lookup
                } else if (key == "private") {
                    memo.isPrivate = (value.toLower() == "true");
                } else if (key == "modified") {
                    memo.isDirty = (value.toLower() == "true");
                }
            }
        } else {
            // No closing ---, treat entire content as body
            body = content;
        }
    } else {
        // No frontmatter, entire content is body
        body = content;
    }

    // Remove trailing newline if present (Palm memos don't have trailing newlines)
    if (body.endsWith('\n')) {
        body.chop(1);
    }

    memo.text = body;
    return memo;
}

PilotRecord* MemoMapper::packMemo(const Memo &memo)
{
    // Encode text to Windows-1252
    QByteArray palmData = encodePalmText(memo.text);

    // Add null terminator
    palmData.append('\0');

    // Create attributes from flags
    int attr = 0;
    if (memo.isPrivate) attr |= PilotRecord::AttrSecret;
    if (memo.isDirty) attr |= PilotRecord::AttrDirty;
    if (memo.isDeleted) attr |= PilotRecord::AttrDeleted;

    return new PilotRecord(memo.recordId, memo.category, attr, palmData);
}
