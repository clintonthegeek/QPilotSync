#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <QObject>
#include <QThread>
#include <QStringList>
#include <atomic>
#include <functional>

#include "../sync/synctypes.h"

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
 *   3. Call requestSync(), requestInstall(), etc.
 *   4. Results come via signals (syncFinished, installFinished, etc.)
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
     * Uses the existing KPilotDeviceLink connection mechanism.
     * Emits connectionComplete() when done.
     */
    void connectDevice(const QString &devicePath);

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
     * @brief Install files to Palm (async)
     *
     * Files are installed in order. Progress is reported via
     * progressUpdated(). Results via installFinished().
     */
    void requestInstall(const QStringList &filePaths);

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

signals:
    // ========== Connection Signals ==========

    void connectionStarted();
    void connectionComplete(bool success);
    void deviceReady(const QString &userName, const QString &deviceId);
    void disconnected();

    // ========== Operation Lifecycle ==========

    void operationStarted(const QString &operationName);
    void operationFinished(bool success, const QString &summary);
    void operationCancelled();

    // ========== Progress ==========

    void progressUpdated(int current, int total, const QString &message);
    void palmScreenMessage(const QString &message);

    // ========== Results ==========

    void installFinished(bool success, int successCount, int failCount);
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
    void onWorkerInstallFinished(bool success, int successCount, int failCount);
    void onWorkerSyncFinished(bool success, const QString &summary);
    void onWorkerSyncResultReady(const Sync::SyncResult &result);
    void onWorkerOpenConduitFinished(bool success);
    void onWorkerOperationFinished(bool success, const QString &operation);
    void onWorkerError(const QString &error);
    void onWorkerLogMessage(const QString &message);

private:
    void ensureWorkerThread();
    void stopWorkerThread();
    void ensureTickleThread();
    void stopTickleThread();
    void startTickle();
    void stopTickle();
    void openConduitAsync();

    KPilotDeviceLink *m_deviceLink = nullptr;
    QThread *m_workerThread = nullptr;
    DeviceWorker *m_worker = nullptr;
    QThread *m_tickleThread = nullptr;
    TickleWorker *m_tickle = nullptr;

    std::atomic<bool> m_busy{false};
    QString m_currentOperation;
    bool m_conduitOpened = false;

    // Pending operation state
    Sync::SyncEngine *m_pendingSyncEngine = nullptr;
    Sync::SyncMode m_pendingSyncMode;
    QStringList m_pendingInstallFiles;
};

#endif // DEVICESESSION_H
