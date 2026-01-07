#ifndef LOCALFILEBACKEND_H
#define LOCALFILEBACKEND_H

#include "syncbackend.h"
#include <QString>
#include <QDir>
#include <QMap>

namespace Sync {

/**
 * @brief File-based storage backend
 *
 * Stores sync data as individual files on disk:
 *   <basePath>/
 *   ├── memos/           - Markdown files (.md)
 *   ├── contacts/        - vCard files (.vcf)
 *   ├── calendar/        - iCalendar event files (.ics)
 *   └── todos/           - iCalendar todo files (.ics)
 *
 * Each collection is a subdirectory. Records are individual files.
 * File modification times are used for change detection.
 */
class LocalFileBackend : public SyncBackend
{
    Q_OBJECT

public:
    /**
     * @brief Create a local file backend
     * @param basePath Root directory for all collections
     * @param parent Parent QObject
     */
    explicit LocalFileBackend(const QString &basePath, QObject *parent = nullptr);
    ~LocalFileBackend() override = default;

    // ========== Backend Identity ==========

    QString backendId() const override { return "local-file"; }
    QString displayName() const override { return "Local Files"; }
    bool isAvailable() const override;

    // ========== Collection Management ==========

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    // ========== Record Operations ==========

    QList<BackendRecord*> loadRecords(const QString &collectionId) override;
    BackendRecord* loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId, const BackendRecord &record) override;
    bool updateRecord(const BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    // ========== Change Detection ==========

    QList<BackendRecord*> modifiedSince(const QString &collectionId,
                                         const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                              const QDateTime &since) override;

    // ========== Configuration ==========

    /**
     * @brief Get the base path for this backend
     */
    QString basePath() const { return m_basePath; }

    /**
     * @brief Set file extension for a collection type
     *
     * Default extensions:
     *   - memos: .md
     *   - contacts: .vcf
     *   - calendar: .ics
     *   - todos: .ics
     */
    void setFileExtension(const QString &collectionId, const QString &extension);

    /**
     * @brief Get file extension for a collection
     */
    QString fileExtension(const QString &collectionId) const;

    /**
     * @brief Calculate content hash for change detection
     */
    static QString calculateHash(const QByteArray &data);

private:
    QString collectionPath(const QString &collectionId) const;
    QString recordPath(const QString &collectionId, const QString &filename) const;
    QString sanitizeFilename(const QString &name) const;
    QString generateUniqueFilename(const QString &collectionId,
                                    const QString &baseName,
                                    const QString &extension) const;

    QString m_basePath;
    QMap<QString, QString> m_extensions;  // collectionId -> extension

    // Default collection types we support
    static const QStringList s_defaultCollections;
};

} // namespace Sync

#endif // LOCALFILEBACKEND_H
