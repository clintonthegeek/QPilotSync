#ifndef CONDUIT_H
#define CONDUIT_H

#include <QObject>
#include <QString>
#include <QList>
#include <functional>
#include "synctypes.h"
#include "syncstate.h"
#include "syncbackend.h"

// Forward declarations
class KPilotDeviceLink;
class PilotRecord;

namespace Sync {

/**
 * @brief Context passed to conduits during sync operations
 *
 * Contains everything a conduit needs to perform its sync.
 */
class SyncContext
{
public:
    KPilotDeviceLink *deviceLink = nullptr;  ///< Connection to Palm device
    SyncBackend *backend = nullptr;          ///< PC-side storage
    SyncState *state = nullptr;              ///< ID mappings and baseline
    SyncMode mode = SyncMode::HotSync;       ///< Current sync mode
    ConflictResolution conflictPolicy = ConflictResolution::AskUser;

    QString palmDatabase;    ///< Palm database name (e.g., "MemoDB")
    QString collectionId;    ///< Backend collection ID
    QString userName;        ///< Palm username

    bool isFirstSync = false;
    bool cancelled = false;
};

/**
 * @brief Abstract base class for sync conduits
 *
 * A conduit handles synchronization for one type of data (memos, contacts, etc.).
 * It knows how to:
 *   - Read/write its Palm database format
 *   - Convert between Palm and backend formats
 *   - Apply sync logic (compare, merge, update)
 *
 * Inspired by KPilot's RecordConduit pattern, but simplified.
 *
 * To create a new conduit:
 * 1. Subclass Conduit
 * 2. Implement the pure virtual methods
 * 3. Register with SyncEngine
 *
 * Example conduits:
 *   - MemoConduit: MemoDB ↔ Markdown files
 *   - ContactConduit: AddressDB ↔ vCard files
 *   - CalendarConduit: DatebookDB ↔ iCalendar files
 *   - TodoConduit: ToDoDB ↔ iCalendar VTODO files
 */
class Conduit : public QObject
{
    Q_OBJECT

public:
    explicit Conduit(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~Conduit() = default;

    // ========== Conduit Identity ==========

    /**
     * @brief Unique identifier for this conduit
     *
     * Examples: "memos", "contacts", "calendar", "todos"
     */
    virtual QString conduitId() const = 0;

    /**
     * @brief Human-readable name for display
     */
    virtual QString displayName() const = 0;

    /**
     * @brief Palm database name this conduit handles
     *
     * Examples: "MemoDB", "AddressDB", "DatebookDB", "ToDoDB"
     */
    virtual QString palmDatabaseName() const = 0;

    /**
     * @brief File extension for this conduit's export format
     *
     * Examples: ".md", ".vcf", ".ics"
     */
    virtual QString fileExtension() const = 0;

    // ========== Core Sync Operation ==========

    /**
     * @brief Perform the sync operation
     *
     * This is the main entry point. The default implementation:
     * 1. Opens the Palm database
     * 2. Loads backend records
     * 3. Calls the appropriate sync algorithm based on mode
     * 4. Commits changes to both sides
     *
     * Override for custom sync behavior.
     *
     * @param context Sync context with all required objects
     * @return Result with statistics and any warnings
     */
    virtual SyncResult sync(SyncContext *context);

    /**
     * @brief Check if conduit can sync in the current state
     *
     * Called before sync() to verify prerequisites.
     */
    virtual bool canSync(const SyncContext *context) const;

    /**
     * @brief Set external cancel check callback
     *
     * When set, this function will be called to check if sync
     * should be cancelled. Returns true if cancellation requested.
     */
    void setCancelCheck(std::function<bool()> callback) { m_cancelCheck = callback; }

    // ========== Record Conversion ==========

    /**
     * @brief Convert a Palm record to backend format
     *
     * @param palmRecord Raw Palm record data
     * @param context Sync context (for category lookup, etc.)
     * @return Backend record ready for storage
     */
    virtual BackendRecord* palmToBackend(PilotRecord *palmRecord,
                                          SyncContext *context) = 0;

    /**
     * @brief Convert a backend record to Palm format
     *
     * @param backendRecord Record from backend storage
     * @param context Sync context
     * @return Palm record ready for writing (caller owns)
     */
    virtual PilotRecord* backendToPalm(BackendRecord *backendRecord,
                                        SyncContext *context) = 0;

    /**
     * @brief Check if two records are equal (ignoring metadata)
     *
     * Used for conflict detection and duplicate matching.
     */
    virtual bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const = 0;

    /**
     * @brief Find a matching backend record for a Palm record
     *
     * Used during first sync when no ID mappings exist.
     * Default implementation uses description matching.
     */
    virtual BackendRecord* findMatch(PilotRecord *palmRecord,
                                      const QList<BackendRecord*> &candidates);

    /**
     * @brief Get a description for a Palm record (for matching/display)
     */
    virtual QString palmRecordDescription(PilotRecord *record) const = 0;

signals:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);
    void conflictDetected(const QString &palmDesc, const QString &backendDesc);

protected:
    // ========== Sync Algorithms ==========

    /**
     * @brief Hot sync - only modified records
     */
    virtual SyncResult hotSync(SyncContext *context);

    /**
     * @brief Full sync - compare all records
     */
    virtual SyncResult fullSync(SyncContext *context);

    /**
     * @brief First sync - no previous state
     */
    virtual SyncResult firstSync(SyncContext *context);

    /**
     * @brief Copy all records from Palm to PC
     */
    virtual SyncResult copyPalmToPC(SyncContext *context);

    /**
     * @brief Copy all records from PC to Palm
     */
    virtual SyncResult copyPCToPalm(SyncContext *context);

    /**
     * @brief Backup Palm to PC (preserve old PC files)
     *
     * Like copyPalmToPC but doesn't delete PC files that
     * don't have Palm counterparts (preserves old backups).
     */
    virtual SyncResult backup(SyncContext *context);

    /**
     * @brief Restore PC to Palm (full restore)
     *
     * Completely overwrites Palm with PC data, including
     * deleting Palm records that don't exist on PC.
     */
    virtual SyncResult restore(SyncContext *context);

    /**
     * @brief Sync a single record pair
     *
     * Core sync logic for comparing and updating records.
     *
     * @param palmRecord Current Palm record (may be null if deleted/new on PC)
     * @param backendRecord Current backend record (may be null if new on Palm)
     * @param context Sync context
     * @param stats Stats to update
     */
    virtual void syncRecord(PilotRecord *palmRecord,
                            BackendRecord *backendRecord,
                            SyncContext *context,
                            SyncStats &palmStats,
                            SyncStats &pcStats);

    /**
     * @brief Handle a conflict between modified records
     *
     * @return true if conflict was resolved, false if skipped
     */
    virtual bool resolveConflict(PilotRecord *palmRecord,
                                  BackendRecord *backendRecord,
                                  SyncContext *context,
                                  SyncStats &palmStats,
                                  SyncStats &pcStats);

    // ========== Helper Methods ==========

    /**
     * @brief Read all records from Palm database
     */
    QList<PilotRecord*> readPalmRecords(SyncContext *context, bool modifiedOnly);

    /**
     * @brief Write a record to Palm
     */
    bool writePalmRecord(PilotRecord *record, SyncContext *context);

    /**
     * @brief Delete a record from Palm
     */
    bool deletePalmRecord(const QString &palmId, SyncContext *context);

    /**
     * @brief Check volatility (warn if too many changes)
     *
     * @param stats Proposed changes
     * @param totalRecords Total record count
     * @param threshold Percentage threshold (0-100)
     * @return true if changes are acceptable
     */
    bool checkVolatility(const SyncStats &stats, int totalRecords, int threshold = 70);

    /**
     * @brief Save current backend file hashes as baseline
     *
     * Called after successful sync to record the current state
     * for change detection in the next sync.
     */
    void saveBaseline(SyncContext *context);

    /**
     * @brief Write modified categories back to Palm
     *
     * Called before closing the database if categories were added/modified
     * during PC→Palm sync. Override in derived classes that handle categories.
     *
     * @param context Sync context with device link
     * @return true if categories were written or no changes needed
     */
    virtual bool writeModifiedCategories(SyncContext *context);

    /**
     * @brief Check if cancellation was requested
     */
    bool isCancelled() const { return m_cancelCheck && m_cancelCheck(); }

    int m_dbHandle = -1;  ///< Open Palm database handle
    std::function<bool()> m_cancelCheck;  ///< External cancellation check
};

} // namespace Sync

#endif // CONDUIT_H
