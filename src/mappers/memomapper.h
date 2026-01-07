#ifndef MEMOMAPPER_H
#define MEMOMAPPER_H

#include <QString>
#include <QObject>
#include "../palm/pilotrecord.h"

/**
 * @brief Mapper for converting Palm Memo records to Markdown files
 *
 * Palm memos are simple text records. This mapper converts them to
 * Markdown files with YAML frontmatter containing metadata.
 */
class MemoMapper : public QObject
{
    Q_OBJECT

public:
    struct Memo {
        int recordId;
        int category;
        QString text;
        bool isPrivate;
        bool isDirty;
        bool isDeleted;
    };

    explicit MemoMapper(QObject *parent = nullptr);
    ~MemoMapper();

    /**
     * @brief Unpack a Palm memo record into a Memo structure
     * @param record The Palm record to unpack
     * @return Memo structure with parsed data, or empty Memo if invalid
     */
    static Memo unpackMemo(const PilotRecord *record);

    /**
     * @brief Convert a Memo to Markdown format with YAML frontmatter
     * @param memo The memo to convert
     * @param categoryName Optional category name for frontmatter
     * @return Markdown string with YAML frontmatter
     */
    static QString memoToMarkdown(const Memo &memo, const QString &categoryName = QString());

    /**
     * @brief Generate a safe filename from memo text
     * @param memo The memo
     * @return Safe filename (max 64 chars, sanitized)
     */
    static QString generateFilename(const Memo &memo);

    // ========== Reverse mapping: Markdown → Palm ==========

    /**
     * @brief Parse a Markdown file with YAML frontmatter into a Memo
     * @param markdown The markdown content including YAML frontmatter
     * @return Memo structure with parsed data
     */
    static Memo markdownToMemo(const QString &markdown);

    /**
     * @brief Pack a Memo structure into a Palm record
     * @param memo The memo to pack
     * @return PilotRecord ready for writing to Palm (caller takes ownership)
     */
    static PilotRecord* packMemo(const Memo &memo);

signals:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
};

#endif // MEMOMAPPER_H
