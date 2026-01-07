#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include "synctypes.h"
#include "syncstate.h"
#include "syncbackend.h"
#include "conduit.h"

class KPilotDeviceLink;

namespace Sync {

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
     * @brief Register a conduit for a data type
     *
     * The engine takes ownership of the conduit.
     */
    void registerConduit(Conduit *conduit);

    /**
     * @brief Unregister a conduit by ID
     */
    void unregisterConduit(const QString &conduitId);

    /**
     * @brief Get a registered conduit by ID
     */
    Conduit* conduit(const QString &conduitId) const;

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
     * Runs each conduit in order, collecting results.
     */
    SyncResult syncAll(SyncMode mode = SyncMode::HotSync);

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

    // ========== Configuration ==========

    /**
     * @brief Set the conflict resolution policy
     */
    void setConflictPolicy(ConflictResolution policy);

    /**
     * @brief Get the current conflict resolution policy
     */
    ConflictResolution conflictPolicy() const { return m_conflictPolicy; }

    /**
     * @brief Set the sync state directory
     *
     * Default: ~/.qpilotsync/
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
    void connectConduitSignals(Conduit *conduit);

    KPilotDeviceLink *m_deviceLink = nullptr;
    SyncBackend *m_backend = nullptr;

    QMap<QString, Conduit*> m_conduits;
    QMap<QString, bool> m_conduitEnabled;
    QMap<QString, SyncState*> m_states;

    QString m_palmUserName;
    QString m_stateDirectory;
    ConflictResolution m_conflictPolicy = ConflictResolution::AskUser;

    bool m_syncing = false;
    bool m_cancelled = false;
    QString m_currentConduit;
};

} // namespace Sync

#endif // SYNCENGINE_H
