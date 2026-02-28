#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <QObject>
#include <QThread>
#include <QStringList>
#include <atomic>
#include <functional>

#include "../sync/synctypes.h"
#include "../profile.h"  // For ConnectionMode

// Forward declarations
class KPilotDeviceLink;
class DeviceWorker;
class TickleWorker;

namespace Sync {
class SyncEngine;
enum class SyncMode;
}

/**
 * @brief Thread-safe interface for Palm device operations
 *
 * DeviceSession provides an async API for all device operations.
 * It manages the connection and dispatches work to a background
 * worker thread, keeping the UI responsive.
 *
 * Usage:
 *   1. Call connectDevice() to start connection
 *   2. Wait for deviceReady() signal
 *   3. Call requestSync(), etc.
 *   4. Results come via signals (syncFinished, etc.)
 *   5. Call disconnectDevice() when done
 *
 * All operations are non-blocking and can be cancelled with requestCancel().
 */
class DeviceSession : public QObject
{
    Q_OBJECT

public:
    explicit DeviceSession(QObject *parent = nullptr);
    ~DeviceSession() override;

    // ========== Connection ==========

    /**
     * @brief Start device connection (async)
     *
     * Tries each port in @a devicePaths sequentially with a bounded
     * timeout.  Emits connectionComplete() when done.
     */
    void connectDevice(const QStringList &devicePaths);

    /**
     * @brief Disconnect from device
     */
    void disconnectDevice();

    /**
     * @brief Check if connected to device
     */
    bool isConnected() const;

    /**
     * @brief Get the underlying device link (for compatibility)
     *
     * @deprecated Prefer using DeviceSession methods directly
     */
    KPilotDeviceLink* deviceLink() const { return m_deviceLink; }

    // ========== Async Operations ==========

    /**
     * @brief Run sync operation (async)
     *
     * Runs the specified sync mode on enabled conduits.
     * Progress via progressUpdated(), results via syncFinished().
     */
    void requestSync(Sync::SyncMode mode, Sync::SyncEngine *engine);

    /**
     * @brief Cancel current operation
     *
     * Requests cancellation of any running operation.
     * The operation will stop at the next safe point.
     */
    void requestCancel();

    // ========== State ==========

    /**
     * @brief Check if an operation is in progress
     */
    bool isBusy() const;

    /**
     * @brief Get current operation name
     */
    QString currentOperation() const { return m_currentOperation; }

    /**
     * @brief Pause the keep-alive tickle for exclusive socket access
     *
     * Call this before doing direct DLP operations on the socket
     * (e.g. pi_file_install from the install conduit).  The tickle
     * will resume when the next operation starts, or call resumeTickle().
     */
    void pauseTickle();

    /**
     * @brief Resume the keep-alive tickle after a manual pause
     */
    void resumeTickle();

    /**
     * @brief Set connection mode (keep alive vs disconnect after sync)
     */
    void setConnectionMode(ConnectionMode mode) { m_connectionMode = mode; }

    /**
     * @brief Get current connection mode
     */
    ConnectionMode connectionMode() const { return m_connectionMode; }

signals:
    // ========== Connection Signals ==========

    void connectionStarted();
    void connectionComplete(bool success);
    void deviceReady(const QString &userName, const QString &deviceId);
    void readyForSync();  // Emitted when device is fully ready for operations
    void disconnected();

    // ========== Operation Lifecycle ==========

    void operationStarted(const QString &operationName);
    void operationFinished(bool success, const QString &summary);

    // ========== Progress ==========

    void progressUpdated(int current, int total, const QString &message);
    void palmScreenMessage(const QString &message);

    // ========== Results ==========

    void syncFinished(bool success, const QString &summary);
    void syncResultReady(const Sync::SyncResult &result);

    // ========== Logging ==========

    void logMessage(const QString &message);
    void errorOccurred(const QString &error);

private slots:
    // Connection callbacks
    void onConnectionComplete(bool success);
    void onDeviceReady(const QString &userName, const QString &deviceName);

    // Worker callbacks
    void onWorkerProgress(int current, int total, const QString &msg);
    void onWorkerPalmScreen(const QString &message);
    void onWorkerSyncFinished(bool success, const QString &summary);
    void onWorkerSyncResultReady(const Sync::SyncResult &result);
    void onWorkerOperationFinished(bool success, const QString &operation);
    void onWorkerError(const QString &error);
    void onWorkerLogMessage(const QString &message);

    // Tickle callbacks
    void onConnectionLost();

private:
    void ensureWorkerThread();
    void stopWorkerThread();
    void ensureTickleThread();
    void stopTickleThread();
    void startTickle();
    void stopTickle();

    KPilotDeviceLink *m_deviceLink = nullptr;
    QThread *m_workerThread = nullptr;
    DeviceWorker *m_worker = nullptr;
    QThread *m_tickleThread = nullptr;
    TickleWorker *m_tickle = nullptr;

    std::atomic<bool> m_busy{false};
    QString m_currentOperation;
    ConnectionMode m_connectionMode = ConnectionMode::KeepAlive;

    // Pending operation state
    Sync::SyncEngine *m_pendingSyncEngine = nullptr;
    Sync::SyncMode m_pendingSyncMode;
};

#endif // DEVICESESSION_H
