#ifndef SYNCTYPES_H
#define SYNCTYPES_H

#include <QString>
#include <QDateTime>
#include <QList>
#include <QVariant>

/**
 * @file synctypes.h
 * @brief Common types and enums for the sync engine
 *
 * Informed by KPilot's battle-tested design, adapted for modern Qt6.
 */

namespace Sync {

/**
 * @brief Sync operation modes
 *
 * Based on KPilot's SyncMode, but simplified for clarity.
 */
enum class SyncMode {
    HotSync,        ///< Only sync modified records (fast)
    FullSync,       ///< Compare all records (thorough)
    CopyPalmToPC,   ///< Overwrite PC with Palm data
    CopyPCToPalm,   ///< Overwrite Palm with PC data
    Backup,         ///< Palm → local backup only (no PC sync)
    Restore         ///< Local backup → Palm (restore from backup)
};

/**
 * @brief Conflict resolution strategies
 *
 * When both Palm and PC have modified the same record.
 */
enum class ConflictResolution {
    AskUser,        ///< Prompt user for each conflict
    PalmWins,       ///< Palm record overwrites PC
    PCWins,         ///< PC record overwrites Palm
    Duplicate,      ///< Keep both records
    NewestWins,     ///< Use modification timestamp
    Skip            ///< Leave both unchanged
};

/**
 * @brief Record change state for sync comparison
 */
enum class RecordState {
    Unchanged,      ///< No changes since last sync
    Modified,       ///< Content changed
    Deleted,        ///< Record was deleted
    New             ///< New record (no previous version)
};

/**
 * @brief Severity levels for data loss warnings
 */
enum class WarningSeverity {
    Info,           ///< Informational (no data loss)
    Warning,        ///< Data was modified/simplified
    Error           ///< Data was dropped/lost
};

/**
 * @brief Category of data loss
 */
enum class WarningCategory {
    Truncated,      ///< Data was shortened to fit
    Unsupported,    ///< Feature not supported on target
    Downgraded,     ///< Rich data simplified
    Overflow        ///< Too many items (e.g., categories)
};

/**
 * @brief Data loss warning from mapper operations
 */
struct DataLossWarning {
    WarningSeverity severity;
    WarningCategory category;
    QString field;          ///< Which field was affected
    QString originalValue;  ///< What the original value was
    QString resultValue;    ///< What it became (or empty if dropped)
    QString message;        ///< Human-readable explanation
};

/**
 * @brief Result from a mapper operation with optional warnings
 */
template<typename T>
struct MapperResult {
    T data;                             ///< The converted data
    bool isLossy = false;               ///< Was any data lost?
    QList<DataLossWarning> warnings;    ///< Specific warnings

    bool hasErrors() const {
        for (const auto &w : warnings) {
            if (w.severity == WarningSeverity::Error) return true;
        }
        return false;
    }
};

/**
 * @brief ID mapping between Palm record and PC file/record
 */
struct IDMapping {
    QString palmId;         ///< Palm record ID (as string for consistency)
    QString pcId;           ///< PC file path or record UID
    QString palmCategory;   ///< Category on Palm side
    QStringList pcCategories; ///< Categories on PC side (may be multiple)
    QDateTime lastSynced;   ///< When this mapping was last used
    bool archived = false;  ///< Record is archived (deleted but preserved)
};

/**
 * @brief Summary of sync operation results
 */
struct SyncStats {
    int created = 0;        ///< New records created
    int updated = 0;        ///< Existing records updated
    int deleted = 0;        ///< Records deleted
    int unchanged = 0;      ///< Records with no changes
    int conflicts = 0;      ///< Conflicts encountered
    int errors = 0;         ///< Errors during sync

    int total() const { return created + updated + deleted + unchanged; }

    QString summary() const {
        return QString("Created: %1, Updated: %2, Deleted: %3, Unchanged: %4, Conflicts: %5, Errors: %6")
            .arg(created).arg(updated).arg(deleted).arg(unchanged).arg(conflicts).arg(errors);
    }
};

/**
 * @brief Result of a complete sync operation
 */
struct SyncResult {
    bool success = false;
    QString errorMessage;
    SyncStats palmStats;    ///< Changes made to Palm
    SyncStats pcStats;      ///< Changes made to PC
    QList<DataLossWarning> warnings;
    QDateTime startTime;
    QDateTime endTime;

    qint64 durationMs() const {
        return startTime.msecsTo(endTime);
    }
};

/**
 * @brief Information about a Palm database
 */
struct DatabaseInfo {
    QString name;           ///< Database name (e.g., "MemoDB")
    QString creator;        ///< Creator ID
    QString type;           ///< Type ID
    int recordCount = 0;
    QDateTime lastModified;
};

/**
 * @brief Information about a sync collection (folder/calendar/etc.)
 */
struct CollectionInfo {
    QString id;             ///< Unique identifier
    QString name;           ///< Display name
    QString path;           ///< Path for file-based backends
    QString type;           ///< "memos", "contacts", "calendar", "todos"
    bool isDefault = false;
};

} // namespace Sync

// Register types for Qt metatype system (needed for cross-thread signals)
Q_DECLARE_METATYPE(Sync::SyncResult)
Q_DECLARE_METATYPE(Sync::SyncStats)

#endif // SYNCTYPES_H
