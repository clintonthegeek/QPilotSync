#ifndef KPILOTDEVICELINK_H
#define KPILOTDEVICELINK_H

#include "kpilotlink.h"
#include <QString>
#include <QStringList>
#include <QThread>
#include <QMetaType>
#include <atomic>

/**
 * @brief Data captured during the connection handshake on the worker thread
 *
 * All initial DLP reads happen on the same thread as pi_accept_to(),
 * avoiding cross-thread socket races.  Results are passed to the main
 * thread via signal.
 */
struct HandshakeResult {
    int socket = -1;
    bool userInfoValid = false;
    QString userName;
    quint32 userId = 0;
    bool sysInfoValid = false;
    quint32 romVersion = 0;
    QString productId;
    bool storageInfoValid = false;
    QString cardName;        // CardInfo.name (e.g. "Palm m515")
    QString manufacturer;    // CardInfo.manufacturer (e.g. "Palm, Inc.")
    quint64 romSize = 0;     // bytes
    quint64 ramSize = 0;     // bytes
    quint64 ramFree = 0;     // bytes
};
Q_DECLARE_METATYPE(HandshakeResult)

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
    void connectionEstablished(const HandshakeResult &result);
    void connectionFailed(const QString &error);
    void statusUpdate(const QString &status);

private:
    struct ProbeResult {
        QString port;   // Device path (empty on failure)
        int fd = -1;    // Kept open so CMP data stays in kernel buffer
    };
    ProbeResult probeForActivePort();

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
    bool isConnected() const override { return m_isConnected; }

    // Check if connection attempt is in progress
    bool isConnecting() const { return m_workerThread != nullptr && m_workerThread->isRunning(); }

    // Get the raw pilot-link socket descriptor (for pi_file_install, etc.)
    int socketDescriptor() const { return m_socket; }

    // Cancel a pending connection attempt
    void cancelConnection();

    // Handshake data captured during connection (on worker thread)
    bool handshakeUserInfoValid() const { return m_handshake.userInfoValid; }
    QString handshakeUserName() const { return m_handshake.userName; }
    quint32 handshakeUserId() const { return m_handshake.userId; }
    bool handshakeSysInfoValid() const { return m_handshake.sysInfoValid; }
    quint32 handshakeRomVersion() const { return m_handshake.romVersion; }
    QString handshakeProductId() const { return m_handshake.productId; }
    bool handshakeStorageInfoValid() const { return m_handshake.storageInfoValid; }
    QString handshakeCardName() const { return m_handshake.cardName; }
    QString handshakeManufacturer() const { return m_handshake.manufacturer; }
    quint64 handshakeRomSize() const { return m_handshake.romSize; }
    quint64 handshakeRamSize() const { return m_handshake.ramSize; }
    quint64 handshakeRamFree() const { return m_handshake.ramFree; }

    bool readUserInfo(struct PilotUser &user) override;
    bool writeUserInfo(const struct PilotUser &user) override;
    bool readSysInfo(struct SysInfo &sysInfo) override;
    bool readStorageInfo(int cardNo, struct CardInfo &cardInfo) override;

    int openDatabase(const QString &dbName, bool readWrite = false) override;
    bool closeDatabase(int handle) override;
    QStringList listDatabases() override;

    QList<PilotRecord*> readAllRecords(int dbHandle) override;
    PilotRecord* readRecordByIndex(int dbHandle, int index) override;
    PilotRecord* readRecordById(int dbHandle, int recordId) override;
    bool writeRecord(int dbHandle, PilotRecord *record) override;
    bool deleteRecord(int dbHandle, int recordId) override;
    QList<PilotRecord*> readModifiedRecords(int dbHandle) override;
    bool resetDBIndex(int dbHandle) override;

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
    bool cleanUpDatabase(int dbHandle) override;

    /**
     * @brief Reset sync flags (dirty bits) on all records
     *
     * Clears the "modified" flag on all records in the database.
     * Should be called after a successful sync.
     */
    bool resetSyncFlags(int dbHandle) override;

    /**
     * @brief Install a .pdb/.prc file onto the Palm device
     *
     * Wraps pilot-link pi_file_install(). Installs to internal storage (card 0).
     *
     * @param filePath Absolute path to the .pdb or .prc file
     * @return true on success
     */
    bool installFile(const QString &filePath) override;
    bool retrieveDatabase(const QString &dbName, const QString &destPath) override;

    void pauseTickle() override;
    void resumeTickle() override;

    /**
     * @brief Check if a database exists on the Palm device
     *
     * Wraps dlp_FindDBInfo(). Used to check for viewer apps, etc.
     *
     * @param dbName Palm database name (e.g. "PlkrMain")
     * @return true if the database exists on the device
     */
    bool findDatabase(const QString &dbName);

    /// dlp_FindDBInfo()-backed modification number; -1 on failure/not connected.
    qint64 databaseModnum(const QString &dbName) override;

signals:
    void connectionComplete(bool success);
    void ticklePauseRequested();
    void tickleResumeRequested();

private slots:
    void onConnectionEstablished(const HandshakeResult &result);
    void onConnectionFailed(const QString &error);
    void onWorkerStatus(const QString &status);

private:
    void cleanupWorker();

    QStringList m_devicePaths; // Device paths to try (e.g., "/dev/ttyUSB0", "/dev/ttyUSB1")
    int m_socket;              // pilot-link socket descriptor
    bool m_isConnected;
    HandshakeResult m_handshake;

    // Worker thread for async connection
    QThread *m_workerThread;
    ConnectionWorker *m_worker;
};

#endif // KPILOTDEVICELINK_H
