#ifndef QSYNCCORE_BASELINESTORE_H
#define QSYNCCORE_BASELINESTORE_H

/**
 * @file baselinestore.h
 * @brief Hash-based change detection for sync operations
 *
 * Stores content hashes (fingerprints) of records after each successful
 * sync. On the next sync, compare current hashes against the baseline
 * to detect which records have been modified.
 *
 * This is essential for detecting changes on the "PC side" where we
 * don't have built-in dirty flags like Palm devices do.
 *
 * Example usage:
 *   1. After sync completes, call saveBaseline() with current hashes
 *   2. On next sync, compare each record's current hash against baseline
 *   3. If hashes differ, record was modified since last sync
 *
 * Hash algorithm is not specified by this class - caller provides hashes.
 * Recommended: SHA-256 of normalized content (for text-based formats).
 */

#include "synccommon.h"

#include <QObject>
#include <QMap>
#include <QJsonObject>

namespace QSyncCore {

/**
 * @brief Stores content hashes for detecting changes between syncs
 */
class BaselineStore : public QObject
{
    Q_OBJECT

public:
    explicit BaselineStore(QObject *parent = nullptr);
    ~BaselineStore() override = default;

    // ========== Baseline Operations ==========

    /**
     * @brief Save baseline hashes for all records
     *
     * Call this after a successful sync to record the current state.
     * This replaces any existing baseline.
     *
     * @param hashes Map of record ID → content hash
     */
    void saveBaseline(const QMap<RecordId, QString> &hashes);

    /**
     * @brief Update baseline hash for a single record
     *
     * Use this for incremental updates during sync.
     */
    void setHash(const RecordId &recordId, const QString &hash);

    /**
     * @brief Remove a record from the baseline
     *
     * Call when a record is deleted.
     */
    void removeHash(const RecordId &recordId);

    /**
     * @brief Get the baseline hash for a record
     * @return Hash from last sync, or empty string if record is new
     */
    QString hash(const RecordId &recordId) const;

    /**
     * @brief Check if a record has changed since baseline
     *
     * @param recordId Record identifier
     * @param currentHash Current content hash
     * @return true if record is new or has different hash
     */
    bool hasChanged(const RecordId &recordId, const QString &currentHash) const;

    /**
     * @brief Check if a record exists in baseline
     */
    bool hasRecord(const RecordId &recordId) const;

    /**
     * @brief Get all record IDs in baseline
     */
    QStringList allRecordIds() const;

    /**
     * @brief Get number of records in baseline
     */
    int count() const { return m_hashes.size(); }

    /**
     * @brief Check if baseline is empty (first sync)
     */
    bool isEmpty() const { return m_hashes.isEmpty(); }

    // ========== Serialization ==========

    /**
     * @brief Serialize baseline to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Load baseline from JSON
     * @return Number of hashes loaded
     */
    int fromJson(const QJsonObject &json);

    /**
     * @brief Clear all baseline data
     */
    void clear();

signals:
    /**
     * @brief Emitted when baseline is modified
     */
    void baselineChanged();

private:
    // Record ID → content hash
    QMap<RecordId, QString> m_hashes;
};

} // namespace QSyncCore

#endif // QSYNCCORE_BASELINESTORE_H
