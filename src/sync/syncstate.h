#ifndef SYNCSTATE_H
#define SYNCSTATE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMap>
#include <QJsonObject>
#include "synctypes.h"
#include "qsynccore/idmappingstore.h"
#include "qsynccore/baselinestore.h"
#include "qsynccore/conflictstore.h"

namespace Sync {

/**
 * @brief Manages sync state including ID mappings and baseline tracking
 *
 * This class composes QSyncCore components (IdMappingStore, BaselineStore)
 * with QPilotSync-specific persistence and metadata.
 *
 * State is stored in:
 *   <stateBaseDir>/<username>/<conduit>/
 *     ├── mappings.json    - ID mappings and baseline hashes
 *     └── sync.log         - Audit log of sync operations
 *
 * The underlying ID mapping and baseline stores are from the shared
 * QSyncCore library, enabling future reuse in PlanStanLite.
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
    // These delegate to the underlying IdMappingStore

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
    // These delegate to the underlying BaselineStore

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

    // ========== Access to Underlying Stores ==========

    /**
     * @brief Get the underlying ID mapping store
     *
     * Provides direct access for advanced use cases.
     */
    QSyncCore::IdMappingStore* idMappingStore() { return m_idMappings; }
    const QSyncCore::IdMappingStore* idMappingStore() const { return m_idMappings; }

    /**
     * @brief Get the underlying baseline store
     *
     * Provides direct access for advanced use cases.
     */
    QSyncCore::BaselineStore* baselineStore() { return m_baseline; }
    const QSyncCore::BaselineStore* baselineStore() const { return m_baseline; }

    /**
     * @brief Get the conflict store for deferred resolution
     *
     * Provides direct access for managing pending conflicts.
     */
    QSyncCore::ConflictStore* conflictStore() { return m_conflicts; }
    const QSyncCore::ConflictStore* conflictStore() const { return m_conflicts; }

    // ========== Conflict Operations ==========

    /**
     * @brief Check if there are pending conflicts to review
     */
    bool hasPendingConflicts() const;

    /**
     * @brief Get count of pending conflicts
     */
    int pendingConflictCount() const;

    /**
     * @brief Get all pending conflicts for this conduit
     */
    QList<QSyncCore::ConflictRecord> pendingConflicts() const;

    /**
     * @brief Clear all pending conflicts (after batch resolution)
     */
    void clearPendingConflicts();

signals:
    void stateChanged();
    void errorOccurred(const QString &error);

private:
    QString m_userName;
    QString m_conduitId;
    QString m_stateDir;

    // Composed QSyncCore components
    QSyncCore::IdMappingStore *m_idMappings;
    QSyncCore::BaselineStore *m_baseline;
    QSyncCore::ConflictStore *m_conflicts;

    // Sync metadata
    QDateTime m_lastSyncTime;
    QString m_lastSyncPC;

    void ensureStateDir();
};

} // namespace Sync

#endif // SYNCSTATE_H
