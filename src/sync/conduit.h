#ifndef CONDUIT_H
#define CONDUIT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QIcon>
#include <QJsonObject>
#include <QDateTime>
#include <functional>
#include "synctypes.h"
#include "syncstate.h"
#include "syncbackend.h"
#include "qsynccore/conflictpolicy.h"
#include "qsynccore/conflictstore.h"
#include "../core/isyncconduit.h"
#include "../palm/categoryinfo.h"

class QWidget;

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

    // Legacy conflict resolution (for backwards compatibility)
    ConflictResolution conflictPolicy = ConflictResolution::AskUser;

    // New conflict handling system
    QSyncCore::ConflictHandler *conflictHandler = nullptr;  ///< Handler for conflicts
    QSyncCore::ConflictStore *conflictStore = nullptr;      ///< Store for deferred conflicts
    QSyncCore::ConflictPolicy conflictSettings;             ///< Conflict resolution settings

    QString palmDatabase;    ///< Palm database name (e.g., "MemoDB")
    QString collectionId;    ///< Backend collection ID
    QString userName;        ///< Palm username
    QString syncSessionId;   ///< Unique ID for this sync session

    // Install conduit support
    QStringList installQueue;   ///< .pdb/.prc file paths queued for installation
    QString syncFolderPath;     ///< Base path for the profile's sync folder

    bool isFirstSync = false;
    bool cancelled = false;

    QSet<QString> baselineUpdatedPcIds;   ///< PC IDs created/updated during sync
    QSet<QString> baselineDeletedPcIds;   ///< PC IDs deleted during sync
};

/**
 * @brief Abstract base class for bidirectional sync conduits
 *
 * A conduit handles synchronization for one type of data (memos, contacts, etc.).
 * It knows how to:
 *   - Read/write its Palm database format
 *   - Convert between Palm and backend formats
 *   - Apply sync logic (compare, merge, update)
 *
 * Inspired by KPilot's RecordConduit pattern, but simplified.
 *
 * Implements ISyncConduit (which extends IConduit) so that conduits
 * can be managed uniformly through the plugin interface.
 *
 * To create a new conduit:
 * 1. Subclass SyncConduitBase
 * 2. Implement the pure virtual methods
 * 3. Register with SyncEngine
 *
 * Example conduits:
 *   - MemoConduit: MemoDB ↔ Markdown files
 *   - ContactConduit: AddressDB ↔ vCard files
 *   - CalendarConduit: DatebookDB ↔ iCalendar files
 *   - TodoConduit: ToDoDB ↔ iCalendar VTODO files
 */
class SyncConduitBase : public QObject, public ISyncConduit
{
    Q_OBJECT
    Q_INTERFACES(IConduit ISyncConduit)

public:
    explicit SyncConduitBase(QObject *parent = nullptr) : QObject(parent) {}
    ~SyncConduitBase() override;

    // ========== Conduit Identity ==========

    /**
     * @brief Unique identifier for this conduit
     *
     * Examples: "memos", "contacts", "calendar", "todos"
     */
    QString conduitId() const override = 0;

    /**
     * @brief Human-readable name for display
     */
    QString displayName() const override = 0;

    /**
     * @brief Palm database name this conduit handles
     *
     * Examples: "MemoDB", "AddressDB", "DatebookDB", "ToDoDB"
     */
    QString palmDatabaseName() const override = 0;

    /**
     * @brief File extension for this conduit's export format
     *
     * Examples: ".md", ".vcf", ".ics"
     */
    QString fileExtension() const override = 0;

    // ========== Conduit Metadata ==========

    /**
     * @brief Icon for this conduit (for UI display)
     *
     * Default returns a null icon. Override to provide custom icon.
     */
    QIcon icon() const override { return QIcon(); }

    /**
     * @brief Description of what this conduit does
     */
    QString description() const override { return QString(); }

    /**
     * @brief Version string for this conduit
     */
    QString version() const override { return "1.0.0"; }

    // ========== Capabilities ==========

    /**
     * @brief Whether this conduit requires a Palm device connection
     *
     * False for conduits like WebCalendar that fetch from web.
     */
    bool requiresDevice() const override { return true; }

    /**
     * @brief Whether this conduit can write to Palm
     */
    bool canSyncToPalm() const override { return true; }

    /**
     * @brief Whether this conduit can read from Palm
     */
    bool canSyncFromPalm() const override { return true; }

    // ========== Dependency Ordering ==========

    /**
     * @brief Conduit IDs that this conduit must run BEFORE
     *
     * Example: WebCalendarConduit returns {"calendar"} to run before CalendarConduit
     */
    virtual QStringList runBefore() const { return {}; }

    /**
     * @brief Conduit IDs that this conduit must run AFTER
     */
    virtual QStringList runAfter() const { return {}; }

    // ========== Settings ==========

    /**
     * @brief Whether this conduit has configurable settings
     */
    virtual bool hasSettings() const { return false; }

    /**
     * @brief Create a settings widget for this conduit
     *
     * Called when user clicks "Settings..." for this conduit.
     * The caller takes ownership of the returned widget.
     *
     * @param parent Parent widget
     * @return Settings widget, or nullptr if no settings
     */
    virtual QWidget* createSettingsWidget(QWidget *parent) { Q_UNUSED(parent); return nullptr; }

    /**
     * @brief Load conduit settings from JSON
     *
     * Called when profile is loaded.
     */
    virtual void loadSettings(const QJsonObject &settings) { Q_UNUSED(settings); }

    /**
     * @brief Save conduit settings to JSON
     *
     * Called when profile is saved.
     */
    virtual QJsonObject saveSettings() const { return QJsonObject(); }

    // ========== IConduit UI Contribution (default stubs) ==========

    /**
     * @brief Whether this conduit provides a browser/editor view
     *
     * Default returns false. Override in conduits that provide a view.
     */
    bool hasView() const override { return false; }

    /**
     * @brief Create the conduit's main view widget (caller owns)
     *
     * Default returns nullptr. Override to provide a view.
     */
    QWidget *createView(QWidget *) override { return nullptr; }

    /**
     * @brief Display name for the view tab/page
     *
     * Default delegates to displayName().
     */
    QString viewName() const override { return displayName(); }

    /**
     * @brief Icon for the view tab/page
     *
     * Default delegates to icon().
     */
    QIcon viewIcon() const override { return icon(); }

    // ========== IConduit Configuration (default stubs) ==========

    /**
     * @brief Number of config pages this conduit provides
     */
    int configPages() const override { return 0; }

    /**
     * @brief Create a config page widget (caller owns)
     */
    QWidget *createConfigPage(int index, QWidget *parent) override {
        Q_UNUSED(index) Q_UNUSED(parent) return nullptr;
    }

    /**
     * @brief Load conduit settings from persistent storage (IConduit interface)
     *
     * Note: The JSON-based loadSettings(QJsonObject) is the primary method.
     * This no-arg version is the IConduit interface stub.
     */
    void loadSettings() override {}

    /**
     * @brief Save conduit settings to persistent storage (IConduit interface)
     *
     * Note: The JSON-based saveSettings() const is the primary method.
     * This void version is the IConduit interface stub.
     */
    void saveSettings() override {}

    // ========== Sync Conduit Specifics ==========

    /**
     * @brief Get the last time this conduit ran successfully
     */
    QDateTime lastRunTime() const { return m_lastRunTime; }

    /**
     * @brief Set the last run time
     */
    void setLastRunTime(const QDateTime &time) { m_lastRunTime = time; }

    // ========== Pre-Sync Check ==========

    /**
     * @brief Check if this conduit should run in this sync cycle
     *
     * Used by interval-based conduits (e.g., weekly fetch) to skip
     * if they ran recently. Default always returns true.
     *
     * @param context Sync context
     * @return true if conduit should run, false to skip
     */
    bool shouldRun(const SyncContext *context) const override { Q_UNUSED(context); return true; }

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
    SyncResult sync(SyncContext *context) override;

    /**
     * @brief Check if conduit can sync in the current state
     *
     * Called before sync() to verify prerequisites.
     */
    bool canSync(const SyncContext *context) const override;

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
    BackendRecord* palmToBackend(PilotRecord *palmRecord,
                                  SyncContext *context) override = 0;

    /**
     * @brief Convert a backend record to Palm format
     *
     * @param backendRecord Record from backend storage
     * @param context Sync context
     * @return Palm record ready for writing (caller owns)
     */
    PilotRecord* backendToPalm(BackendRecord *backendRecord,
                                SyncContext *context) override = 0;

    /**
     * @brief Check if two records are equal (ignoring metadata)
     *
     * Used for conflict detection and duplicate matching.
     */
    bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override = 0;

    /**
     * @brief Find a matching backend record for a Palm record
     *
     * Used during first sync when no ID mappings exist.
     * Default implementation uses description matching.
     */
    BackendRecord* findMatch(PilotRecord *palmRecord,
                              const QList<BackendRecord*> &candidates) override;

    /**
     * @brief Get a description for a Palm record (for matching/display)
     */
    QString palmRecordDescription(PilotRecord *record) const override = 0;

    /**
     * @brief Get category name for a Palm category index
     *
     * Override in derived classes that handle categories.
     * Default returns empty string for all indices.
     *
     * @param categoryIndex Palm category index (0-15)
     * @return Category name, or empty string if not available
     */
    QString categoryNameForIndex(int categoryIndex) const override {
        return categoryName(categoryIndex);
    }

Q_SIGNALS:
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

    /**
     * @brief Resolve conflict using the new handler system
     */
    bool resolveConflictWithHandler(PilotRecord *palmRecord,
                                     BackendRecord *backendRecord,
                                     SyncContext *context,
                                     SyncStats &palmStats,
                                     SyncStats &pcStats);

    /**
     * @brief Apply a conflict decision
     */
    bool applyConflictDecision(const QSyncCore::ConflictRecord &conflict,
                                QSyncCore::ConflictDecision decision,
                                PilotRecord *palmRecord,
                                BackendRecord *backendRecord,
                                SyncContext *context,
                                SyncStats &palmStats,
                                SyncStats &pcStats);

    /**
     * @brief Legacy conflict resolution (for backwards compatibility)
     */
    bool resolveConflictLegacy(PilotRecord *palmRecord,
                                BackendRecord *backendRecord,
                                SyncContext *context,
                                SyncStats &palmStats,
                                SyncStats &pcStats);

    /**
     * @brief Apply previously resolved conflicts from the conflict store
     *
     * Called at the start of sync to apply user-resolved conflicts
     * before the main sync algorithm runs.
     *
     * @param context Sync context
     * @param palmStats Stats for Palm-side changes
     * @param pcStats Stats for PC-side changes
     * @return Number of conflicts applied
     */
    int applyResolvedConflicts(SyncContext *context,
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

    void saveBaselineIncremental(SyncContext *context,
                                 const QSet<QString> &updatedPcIds,
                                 const QSet<QString> &deletedPcIds);

    bool writeModifiedCategories(SyncContext *context) override;

    /**
     * @brief Check if cancellation was requested
     */
    bool isCancelled() const { return m_cancelCheck && m_cancelCheck(); }

    CategoryInfo *m_categories = nullptr;
    QByteArray m_originalAppInfo;

    void loadCategories(SyncContext *context);
    void persistCategoriesForViews(SyncContext *context);
    QString categoryName(int categoryIndex) const;

    int m_dbHandle = -1;  ///< Open Palm database handle
    std::function<bool()> m_cancelCheck;  ///< External cancellation check
    QDateTime m_lastRunTime;  ///< Last successful run time
};

// Backward compatibility alias
using Conduit = SyncConduitBase;

} // namespace Sync

#endif // CONDUIT_H
