#ifndef SYNCSTATE_H
#define SYNCSTATE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMap>
#include <QJsonObject>
#include "synctypes.h"

namespace Sync {

/**
 * @brief Manages sync state including ID mappings and baseline tracking
 *
 * Inspired by KPilot's IDMapping class, but using JSON for persistence
 * and designed for integration with git-based state tracking.
 *
 * State is stored in:
 *   <stateBaseDir>/<username>/<conduit>/
 *     ├── mappings.json    - ID mappings between Palm and PC
 *     ├── baseline/        - Snapshot of PC data after last sync
 *     └── sync.log         - Audit log of sync operations
 */
class SyncState : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a SyncState for a user/conduit combination
     * @param userName Palm username (for multi-device support)
     * @param conduitId Conduit identifier (e.g., "memos", "contacts")
     * @param parent Parent QObject
     */
    explicit SyncState(const QString &userName,
                       const QString &conduitId,
                       QObject *parent = nullptr);
    ~SyncState();

    // ========== ID Mapping Operations ==========

    /**
     * @brief Create a mapping between Palm and PC records
     * @param palmId Palm record ID
     * @param pcId PC file path or UID
     */
    void mapIds(const QString &palmId, const QString &pcId);

    /**
     * @brief Remove mapping by Palm ID
     */
    void removePalmMapping(const QString &palmId);

    /**
     * @brief Remove mapping by PC ID
     */
    void removePCMapping(const QString &pcId);

    /**
     * @brief Get PC ID for a Palm record
     * @return PC ID or empty string if no mapping exists
     */
    QString pcIdForPalm(const QString &palmId) const;

    /**
     * @brief Get Palm ID for a PC record
     * @return Palm ID or empty string if no mapping exists
     */
    QString palmIdForPC(const QString &pcId) const;

    /**
     * @brief Check if a Palm ID has a mapping
     */
    bool hasPalmMapping(const QString &palmId) const;

    /**
     * @brief Check if a PC ID has a mapping
     */
    bool hasPCMapping(const QString &pcId) const;

    /**
     * @brief Get all Palm IDs in the mapping
     */
    QStringList allPalmIds() const;

    /**
     * @brief Get all PC IDs in the mapping
     */
    QStringList allPCIds() const;

    /**
     * @brief Get the full mapping entry
     */
    IDMapping getMapping(const QString &palmId) const;

    /**
     * @brief Update category info for a mapping
     */
    void updateCategories(const QString &palmId,
                          const QString &palmCategory,
                          const QStringList &pcCategories);

    // ========== Baseline Operations ==========

    /**
     * @brief Get the baseline directory path
     */
    QString baselinePath() const;

    /**
     * @brief Save current PC state as baseline (called after successful sync)
     * @param pcFiles Map of PC ID → file content hash
     */
    void saveBaseline(const QMap<QString, QString> &pcFileHashes);

    /**
     * @brief Get baseline hash for a PC file
     * @return Hash from last sync, or empty if new file
     */
    QString baselineHash(const QString &pcId) const;

    /**
     * @brief Check if PC file has changed since baseline
     * @param pcId PC file identifier
     * @param currentHash Current content hash
     */
    bool hasFileChanged(const QString &pcId, const QString &currentHash) const;

    // ========== Sync Metadata ==========

    /**
     * @brief Get timestamp of last successful sync
     */
    QDateTime lastSyncTime() const;

    /**
     * @brief Set last sync timestamp (called after successful sync)
     */
    void setLastSyncTime(const QDateTime &time);

    /**
     * @brief Get the PC name where last sync occurred
     */
    QString lastSyncPC() const;

    /**
     * @brief Set the PC name for sync tracking
     */
    void setLastSyncPC(const QString &pcName);

    /**
     * @brief Check if this is a first sync (no previous state)
     */
    bool isFirstSync() const;

    /**
     * @brief Validate mappings against current data
     * @param palmIds Current Palm record IDs
     * @return true if all mappings are valid
     */
    bool validateMappings(const QStringList &palmIds) const;

    // ========== Persistence ==========

    /**
     * @brief Load state from disk
     * @return true if loaded successfully (or if no previous state exists)
     */
    bool load();

    /**
     * @brief Save state to disk
     * @return true if saved successfully
     */
    bool save();

    /**
     * @brief Clear all state (use with caution)
     */
    void clear();

    /**
     * @brief Get the state directory path
     */
    QString statePath() const;

    /**
     * @brief Set the base directory for state storage
     * @param baseDir Base directory (state will be in baseDir/userName/conduitId/)
     *
     * Must be called before load() or save().
     */
    void setStateDirectory(const QString &baseDir);

signals:
    void stateChanged();
    void errorOccurred(const QString &error);

private:
    QString m_userName;
    QString m_conduitId;
    QString m_stateDir;

    // ID mappings: Palm ID → full mapping info
    QMap<QString, IDMapping> m_mappings;

    // Reverse lookup: PC ID → Palm ID
    QMap<QString, QString> m_pcToPalmMap;

    // Baseline hashes: PC ID → content hash
    QMap<QString, QString> m_baselineHashes;

    // Sync metadata
    QDateTime m_lastSyncTime;
    QString m_lastSyncPC;

    void ensureStateDir();
    QJsonObject mappingToJson(const IDMapping &mapping) const;
    IDMapping mappingFromJson(const QJsonObject &json) const;
};

} // namespace Sync

#endif // SYNCSTATE_H
