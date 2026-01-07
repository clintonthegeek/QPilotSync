#include "memomapper.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QStringConverter>

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
            // Map Windows-1252 C1 controls to their Unicode equivalents
            static const unsigned short cp1252_to_unicode[] = {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
                0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
            };

            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
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
