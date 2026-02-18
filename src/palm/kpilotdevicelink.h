#ifndef KPILOTDEVICELINK_H
#define KPILOTDEVICELINK_H

#include "kpilotlink.h"
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>

/**
 * @brief Worker object for blocking pilot-link connection in separate thread
 *
 * Tries each device path sequentially with pi_accept_to() using a short
 * timeout.  The correct port responds immediately (Palm is already
 * transmitting); wrong ports time out after @a timeoutSeconds.
 */
class ConnectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionWorker(const QStringList &devicePaths,
                              int timeoutSeconds = 5,
                              QObject *parent = nullptr);
    ~ConnectionWorker();

    void requestCancel();

public slots:
    void doConnect();

signals:
    void connectionEstablished(int socket);
    void connectionFailed(const QString &error);
    void statusUpdate(const QString &status);

private:
    QString probeForActivePort();

    QStringList m_devicePaths;
    int m_timeoutSeconds;
    std::atomic<bool> m_cancelRequested;
};

/**
 * @brief Real hardware implementation of KPilotLink using pilot-link
 *
 * This class implements device communication using the pilot-link library
 * to talk to actual Palm devices over USB or serial connections.
 *
 * Connection is performed asynchronously in a worker thread to avoid
 * blocking the Qt event loop.
 */
class KPilotDeviceLink : public KPilotLink
{
    Q_OBJECT

public:
    explicit KPilotDeviceLink(const QStringList &devicePaths, QObject *parent = nullptr);
    ~KPilotDeviceLink() override;

    // KPilotLink interface implementation
    bool openConnection() override;  // Now starts async connection
    void closeConnection() override;
    LinkStatus status() const override { return m_status; }

    // Check if fully connected (async connection complete)
    bool isConnected() const { return m_isConnected; }

    // Check if connection attempt is in progress
    bool isConnecting() const { return m_workerThread != nullptr && m_workerThread->isRunning(); }

    // Get the raw pilot-link socket descriptor (for pi_file_install, etc.)
    int socketDescriptor() const { return m_socket; }

    // Cancel a pending connection attempt
    void cancelConnection();

    bool readUserInfo(struct PilotUser &user) override;
    bool writeUserInfo(const struct PilotUser &user) override;
    bool readSysInfo(struct SysInfo &sysInfo) override;

    int openDatabase(const QString &dbName, bool readWrite = false) override;
    bool closeDatabase(int handle) override;
    QStringList listDatabases() override;

    QList<PilotRecord*> readAllRecords(int dbHandle) override;
    PilotRecord* readRecordByIndex(int dbHandle, int index) override;
    PilotRecord* readRecordById(int dbHandle, int recordId) override;
    bool writeRecord(int dbHandle, PilotRecord *record) override;
    bool deleteRecord(int dbHandle, int recordId) override;

    bool readAppBlock(int dbHandle, unsigned char *buffer, size_t *size) override;
    bool writeAppBlock(int dbHandle, const unsigned char *buffer, size_t size) override;

    bool beginSync() override;
    bool endSync() override;

    /**
     * @brief Clean up deleted records in the database
     *
     * Removes records marked for deletion from the Palm database.
     * Should be called after sync to finalize deletions.
     */
    bool cleanUpDatabase(int dbHandle);

    /**
     * @brief Reset sync flags (dirty bits) on all records
     *
     * Clears the "modified" flag on all records in the database.
     * Should be called after a successful sync.
     */
    bool resetSyncFlags(int dbHandle);

    /**
     * @brief Install a .pdb/.prc file onto the Palm device
     *
     * Wraps pilot-link pi_file_install(). Installs to internal storage (card 0).
     *
     * @param filePath Absolute path to the .pdb or .prc file
     * @return true on success
     */
    bool installFile(const QString &filePath);

    /**
     * @brief Check if a database exists on the Palm device
     *
     * Wraps dlp_FindDBInfo(). Used to check for viewer apps, etc.
     *
     * @param dbName Palm database name (e.g. "PlkrMain")
     * @return true if the database exists on the device
     */
    bool findDatabase(const QString &dbName);

signals:
    void connectionComplete(bool success);

private slots:
    void onConnectionEstablished(int socket);
    void onConnectionFailed(const QString &error);
    void onWorkerStatus(const QString &status);

private:
    void cleanupWorker();

    QStringList m_devicePaths; // Device paths to try (e.g., "/dev/ttyUSB0", "/dev/ttyUSB1")
    int m_socket;              // pilot-link socket descriptor
    bool m_isConnected;

    // Worker thread for async connection
    QThread *m_workerThread;
    ConnectionWorker *m_worker;
};

#endif // KPILOTDEVICELINK_H
