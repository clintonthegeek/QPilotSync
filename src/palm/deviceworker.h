#ifndef DEVICEWORKER_H
#define DEVICEWORKER_H

#include <QObject>
#include <QStringList>
#include <atomic>
#include <functional>

#include "../sync/synctypes.h"

// Forward declarations
namespace Sync {
class SyncEngine;
class InstallConduit;
enum class SyncMode;
}

/**
 * @brief Worker object for executing blocking DLP operations
 *
 * This class runs on a dedicated worker thread and handles all
 * blocking pilot-link calls. It communicates with the main thread
 * via signals.
 *
 * Key responsibilities:
 * - Execute sync operations
 * - Install files to Palm
 * - Export data from Palm
 * - Report progress
 * - Support cancellation
 */
class DeviceWorker : public QObject
{
    Q_OBJECT

public:
    explicit DeviceWorker(QObject *parent = nullptr);
    ~DeviceWorker() override;

    /**
     * @brief Set the socket descriptor for DLP operations
     *
     * Called after connection is established. The worker takes
     * ownership of the socket for all operations.
     */
    void setSocket(int socket);

    /**
     * @brief Get the current socket descriptor
     */
    int socket() const { return m_socket; }

public slots:
    /**
     * @brief Signal to Palm that a conduit is starting
     *
     * Calls dlp_OpenConduit() to update Palm screen from
     * "Identifying User" to sync status.
     */
    void doOpenConduit();

    /**
     * @brief End the sync session
     *
     * Calls dlp_EndOfSync() with the given status.
     * @param success true for normal end, false for error
     */
    void doEndSync(bool success);

    /**
     * @brief Install files to Palm device
     *
     * @param filePaths List of .prc/.pdb files to install
     */
    void doInstall(const QStringList &filePaths);

    /**
     * @brief Execute a sync operation
     *
     * @param mode Sync mode (HotSync, FullSync, etc.)
     * @param conduitIds List of conduit IDs to sync
     * @param engine Pointer to sync engine (must be thread-safe or copied)
     * @param stateDir State directory path
     * @param syncPath Sync folder path
     */
    void doSync(int mode,
                const QStringList &conduitIds,
                Sync::SyncEngine *engine,
                const QString &stateDir,
                const QString &syncPath);

    /**
     * @brief Request cancellation of current operation
     *
     * Sets a flag that will be checked between operations.
     * The current atomic operation will complete before stopping.
     */
    void doCancel();

    /**
     * @brief Reset cancel flag for new operation
     */
    void resetCancel();

signals:
    /**
     * @brief Progress update during operation
     */
    void progress(int current, int total, const QString &message);

    /**
     * @brief Palm screen message changed
     */
    void palmScreenChanged(const QString &message);

    /**
     * @brief Install operation completed
     */
    void installFinished(bool success, int successCount, int failCount);

    /**
     * @brief Sync operation completed (simple version)
     */
    void syncFinished(bool success, const QString &summary);

    /**
     * @brief Sync operation completed with full result
     */
    void syncResultReady(const Sync::SyncResult &result);

    /**
     * @brief OpenConduit completed
     */
    void openConduitFinished(bool success);

    /**
     * @brief General operation completed
     */
    void operationFinished(bool success, const QString &operation);

    /**
     * @brief Error occurred
     */
    void error(const QString &message);

    /**
     * @brief Log message for UI
     */
    void logMessage(const QString &message);

private:
    /**
     * @brief Check if cancellation was requested
     */
    bool isCancelled() const;

    int m_socket = -1;
    std::atomic<bool> m_cancelRequested{false};
};

#endif // DEVICEWORKER_H
