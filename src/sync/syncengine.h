#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <functional>
#include "synctypes.h"
#include "syncstate.h"
#include "syncbackend.h"
#include "qsynccore/conflictpolicy.h"
#include "../core/iconduit.h"
#include "../core/isyncconduit.h"

class KPilotDeviceLink;

namespace Sync {

class SyncConduitBase;

/**
 * @brief Main sync orchestrator
 *
 * The SyncEngine coordinates:
 *   - Device connection management
 *   - Conduit registration and execution
 *   - Backend configuration
 *   - Sync state management
 *   - Progress reporting
 *
 * Usage:
 * @code
 * SyncEngine engine;
 *
 * // Configure backend
 * engine.setBackend(new LocalFileBackend("~/PalmSync"));
 *
 * // Register conduits
 * engine.registerConduit(new MemoConduit());
 * engine.registerConduit(new ContactConduit());
 *
 * // Connect to device
 * engine.connectDevice("/dev/ttyUSB0");
 *
 * // Run sync
 * engine.syncAll(SyncMode::HotSync);
 * @endcode
 */
class SyncEngine : public QObject
{
    Q_OBJECT

public:
    explicit SyncEngine(QObject *parent = nullptr);
    ~SyncEngine();

    // ========== Device Management ==========

    /**
     * @brief Set the device link for Palm communication
     *
     * The engine takes ownership of the device link.
     */
    void setDeviceLink(KPilotDeviceLink *link);

    /**
     * @brief Get the current device link
     */
    KPilotDeviceLink* deviceLink() const { return m_deviceLink; }

    /**
     * @brief Get the Palm username (after connection)
     */
    QString palmUserName() const { return m_palmUserName; }

    /**
     * @brief Set the Palm username from handshake data
     */
    void setPalmUserName(const QString &name) { m_palmUserName = name; }

    // ========== Backend Configuration ==========

    /**
     * @brief Set the backend for PC-side storage
     *
     * The engine takes ownership of the backend.
     */
    void setBackend(SyncBackend *backend);

    /**
     * @brief Get the current backend
     */
    SyncBackend* backend() const { return m_backend; }

    // ========== Conduit Management ==========

    /**
     * @brief Register a conduit for a data type (non-owning)
     *
     * The engine does NOT take ownership. The caller must keep the
     * conduit alive for the engine's lifetime, and unregister before
     * deleting it.
     *
     * Accepts any IConduit implementation (ISyncConduit, IToolConduit, etc.)
     */
    void registerConduit(IConduit *conduit);

    /**
     * @brief Store ordering hints for a conduit
     *
     * For SyncConduitBase conduits, ordering is read from runBefore()/runAfter().
     * For other conduit types (e.g. IToolConduit), the caller must provide
     * ordering hints explicitly via this method.
     */
    void setConduitOrdering(const QString &conduitId,
                            const QStringList &runBefore,
                            const QStringList &runAfter);

    /**
     * @brief Unregister a conduit by ID (does not delete)
     */
    void unregisterConduit(const QString &conduitId);

    /**
     * @brief Get a registered conduit by ID
     */
    IConduit* conduit(const QString &conduitId) const;

    /**
     * @brief Get list of all registered conduit IDs
     */
    QStringList registeredConduits() const;

    /**
     * @brief Check if a conduit is enabled
     */
    bool isConduitEnabled(const QString &conduitId) const;

    /**
     * @brief Enable/disable a conduit
     */
    void setConduitEnabled(const QString &conduitId, bool enabled);

    // ========== Sync Operations ==========

    /**
     * @brief Sync all enabled conduits
     *
     * Runs each conduit in order (computing dependency order internally).
     */
    SyncResult syncAll(SyncMode mode = SyncMode::HotSync);

    /**
     * @brief Sync conduits in a pre-ordered list
     *
     * Runs conduits in the exact order specified by @p orderedIds.
     * This is intended for use with ConduitManager which handles
     * dependency resolution externally.
     *
     * @param orderedIds Conduit IDs in the order they should run
     * @param mode The sync mode to use
     * @return Aggregated result from all conduit runs
     */
    SyncResult syncAllOrdered(const QStringList &orderedIds, SyncMode mode = SyncMode::HotSync);

    /**
     * @brief Sync a specific conduit
     */
    SyncResult syncConduit(const QString &conduitId, SyncMode mode = SyncMode::HotSync);

    /**
     * @brief Cancel a running sync
     */
    void cancelSync();

    /**
     * @brief Check if sync is currently running
     */
    bool isSyncing() const { return m_syncing; }

    // ========== Worker Thread Callbacks ==========

    /**
     * @brief Set external progress callback (for worker thread use)
     *
     * When set, progress updates will be sent through this callback
     * in addition to the progressUpdated signal.
     */
    void setProgressCallback(std::function<void(int, int, const QString&)> callback);

    /**
     * @brief Set external cancel check (for worker thread use)
     *
     * When set, this function will be called to check if sync
     * should be cancelled. Returns true if cancellation requested.
     */
    void setCancelCheck(std::function<bool()> callback);

    // ========== Configuration ==========

    /**
     * @brief Set the conflict resolution policy (legacy)
     */
    void setConflictPolicy(ConflictResolution policy);

    /**
     * @brief Get the current conflict resolution policy (legacy)
     */
    ConflictResolution conflictPolicy() const { return m_conflictPolicy; }

    /**
     * @brief Set the auto-resolve strategy
     * @param strategy One of: "none", "palm_wins", "pc_wins", "newer_wins", "older_wins", "duplicate"
     */
    void setConflictAutoResolve(const QString &strategy);

    /**
     * @brief Get the current auto-resolve strategy
     */
    QString conflictAutoResolve() const { return m_conflictAutoResolve; }

    /**
     * @brief Set the fallback behavior
     * @param fallback One of: "defer", "skip", "use_default"
     */
    void setConflictFallback(const QString &fallback);

    /**
     * @brief Get the current fallback behavior
     */
    QString conflictFallback() const { return m_conflictFallback; }

    /**
     * @brief Set an external conflict handler (e.g. InteractiveConflictHandler)
     *
     * When set, this handler is used instead of creating a local
     * AutomaticConflictHandler in syncConduit(). The caller retains ownership.
     */
    void setConflictHandler(QSyncCore::ConflictHandler *handler);

    /**
     * @brief Get the external conflict handler
     */
    QSyncCore::ConflictHandler* conflictHandler() const { return m_externalHandler; }

    /**
     * @brief Set the prompt strategy for interactive conflict resolution
     * @param strategy One of: "always_ask", "first_only", "batch_at_end"
     */
    void setConflictPromptStrategy(const QString &strategy);

    /**
     * @brief Set the connection behavior during conflict resolution
     * @param behavior One of: "keep_alive", "disconnect_and_defer", "timeout_and_defer"
     */
    void setConflictConnectionBehavior(const QString &behavior);

    /**
     * @brief Set the timeout in seconds for conflict resolution dialogs
     */
    void setConflictTimeoutSeconds(int seconds);

    /**
     * @brief Set the sync state directory
     *
     * Default: ~/.wildpalms/
     */
    void setStateDirectory(const QString &path);

    /**
     * @brief Get the sync state for a conduit
     */
    SyncState* stateForConduit(const QString &conduitId);

signals:
    void syncStarted();
    void syncFinished(const SyncResult &result);
    void conduitStarted(const QString &conduitId);
    void conduitFinished(const QString &conduitId, const SyncResult &result);
    void progressUpdated(int current, int total, const QString &message);
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
    void conflictDetected(const QString &conduitId, const QString &palmDesc, const QString &pcDesc);

private slots:
    void onConduitProgress(int current, int total, const QString &message);
    void onConduitLog(const QString &message);
    void onConduitError(const QString &error);
    void onConduitConflict(const QString &palmDesc, const QString &pcDesc);

private:
    void connectConduitSignals(IConduit *conduit);

    /**
     * @brief Get conduits in dependency-resolved order
     *
     * Uses topological sort based on runBefore/runAfter dependencies.
     * Throws error on circular dependencies.
     *
     * @param conduitIds IDs of conduits to order
     * @return Ordered list of conduit IDs
     */
    QStringList resolveConduitOrder(const QStringList &conduitIds);

    /**
     * @brief Check for circular dependencies
     *
     * @return Error message if circular, empty string if OK
     */
    QString checkCircularDependencies(const QStringList &conduitIds);

    KPilotDeviceLink *m_deviceLink = nullptr;
    SyncBackend *m_backend = nullptr;

    QMap<QString, IConduit*> m_conduits;
    QMap<QString, bool> m_conduitEnabled;
    QMap<QString, QStringList> m_conduitRunBefore;   ///< Ordering hints for non-SyncConduitBase
    QMap<QString, QStringList> m_conduitRunAfter;    ///< Ordering hints for non-SyncConduitBase
    QMap<QString, SyncState*> m_states;

    QString m_palmUserName;
    QString m_stateDirectory;
    ConflictResolution m_conflictPolicy = ConflictResolution::AskUser;
    QString m_conflictAutoResolve = "none";
    QString m_conflictFallback = "defer";
    QSyncCore::ConflictHandler *m_externalHandler = nullptr;
    QString m_conflictPromptStrategy = "always_ask";
    QString m_conflictConnectionBehavior = "keep_alive";
    int m_conflictTimeoutSeconds = 60;

    bool m_syncing = false;
    bool m_cancelled = false;
    QString m_currentConduit;
    QStringList m_pendingInstalls;  ///< Files queued for post-conduit installation

    // External callbacks for worker thread integration
    std::function<void(int, int, const QString&)> m_progressCallback;
    std::function<bool()> m_cancelCheck;
};

} // namespace Sync

#endif // SYNCENGINE_H
