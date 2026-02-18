#ifndef QSYNCCORE_SYNCCOMMON_H
#define QSYNCCORE_SYNCCOMMON_H

/**
 * @file synccommon.h
 * @brief Common types for the QSyncCore shared sync library
 *
 * This header contains platform-agnostic types that can be shared
 * between Wild Palms, PlanStanLite, and other sync-enabled applications.
 *
 * Design goals:
 *   - No dependency on specific device/backend implementations
 *   - Qt-based for easy integration with Qt applications
 *   - Suitable for extraction into a standalone library
 */

#include <QString>
#include <QStringList>
#include <QDateTime>

namespace QSyncCore {

/**
 * @brief Unique identifier for a record in any sync store
 *
 * Abstracts away the difference between Palm record IDs (uint32),
 * file paths, UUIDs, etc.
 */
using RecordId = QString;

/**
 * @brief Bidirectional mapping between two record identifiers
 *
 * Used to track correspondence between records in different stores
 * (e.g., Palm device ↔ local files, local files ↔ cloud service).
 */
struct IdMapping
{
    RecordId sourceId;          ///< ID in the source store
    RecordId targetId;          ///< ID in the target store
    QDateTime lastSynced;       ///< When this mapping was last used
    QString sourceCategory;     ///< Category in source (if applicable)
    QStringList targetCategories; ///< Categories in target (may be multiple)
    bool archived = false;      ///< Soft-delete flag for mapping

    bool isValid() const { return !sourceId.isEmpty() && !targetId.isEmpty(); }
};

/**
 * @brief Conflict resolution strategies
 *
 * When both sides of a sync have modified the same record,
 * this determines how to resolve the conflict.
 */
enum class ConflictResolution
{
    SourceWins,     ///< Source version overwrites target
    TargetWins,     ///< Target version overwrites source
    Duplicate,      ///< Keep both versions (create duplicates)
    Skip,           ///< Don't sync this record, log the conflict
    AskUser,        ///< Pause and ask user (interactive mode)
    Merge           ///< Attempt automatic merge (if supported)
};

/**
 * @brief Record state for sync tracking
 */
enum class RecordState
{
    Unchanged,      ///< No modifications since last sync
    Modified,       ///< Content changed since last sync
    Deleted,        ///< Marked for deletion
    New,            ///< Created since last sync (no mapping exists)
    Conflict        ///< Both sides modified (needs resolution)
};

/**
 * @brief Statistics for a sync operation
 */
struct SyncStats
{
    int created = 0;        ///< New records created
    int updated = 0;        ///< Existing records updated
    int deleted = 0;        ///< Records deleted
    int unchanged = 0;      ///< Records with no changes
    int conflicts = 0;      ///< Unresolved conflicts
    int errors = 0;         ///< Records that failed to sync

    int total() const { return created + updated + deleted + unchanged + conflicts + errors; }
    int changed() const { return created + updated + deleted; }
    bool hasErrors() const { return errors > 0; }
    bool hasConflicts() const { return conflicts > 0; }
};

/**
 * @brief Result of a sync operation
 */
struct SyncResult
{
    bool success = false;           ///< Overall success/failure
    QString errorMessage;           ///< Error message if failed
    SyncStats sourceStats;          ///< Changes applied to source
    SyncStats targetStats;          ///< Changes applied to target
    QDateTime startTime;            ///< When sync started
    QDateTime endTime;              ///< When sync completed

    qint64 durationMs() const {
        return startTime.msecsTo(endTime);
    }
};

/**
 * @brief Warning severity levels
 */
enum class WarningSeverity
{
    Info,       ///< Informational message
    Warning,    ///< Potential issue that didn't prevent sync
    Error       ///< Error that affected some records
};

/**
 * @brief Data loss warning for sync review
 */
struct DataLossWarning
{
    WarningSeverity severity = WarningSeverity::Warning;
    QString message;
    QString affectedRecord;
    QString recommendation;
};

} // namespace QSyncCore

#endif // QSYNCCORE_SYNCCOMMON_H
