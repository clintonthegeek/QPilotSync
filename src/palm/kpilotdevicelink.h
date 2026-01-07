#ifndef KPILOTDEVICELINK_H
#define KPILOTDEVICELINK_H

#include "kpilotlink.h"
#include <QString>
#include <QThread>
#include <QMutex>
#include <atomic>

/**
 * @brief Worker object for blocking pilot-link connection in separate thread
 */
class ConnectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionWorker(const QString &devicePath, QObject *parent = nullptr);
    ~ConnectionWorker();

    int socket() const { return m_socket; }
    void requestCancel();

    // Force-close the socket to interrupt blocking pi_accept()
    // Thread-safe: can be called from main thread
    void forceCloseSocket();

public slots:
    void doConnect();

signals:
    void connectionEstablished(int socket);
    void connectionFailed(const QString &error);
    void statusUpdate(const QString &status);

private:
    QString m_devicePath;
    std::atomic<int> m_socket;  // atomic for thread-safe access
    std::atomic<bool> m_cancelRequested;
    QMutex m_socketMutex;  // protects socket close operations
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
    explicit KPilotDeviceLink(const QString &devicePath, QObject *parent = nullptr);
    ~KPilotDeviceLink() override;

    // KPilotLink interface implementation
    bool openConnection() override;  // Now starts async connection
    void closeConnection() override;
    LinkStatus status() const override { return m_status; }

    // Check if fully connected (async connection complete)
    bool isConnected() const { return m_isConnected; }

    // Check if connection attempt is in progress
    bool isConnecting() const { return m_workerThread != nullptr && m_workerThread->isRunning(); }

    // Cancel a pending connection attempt
    void cancelConnection();

    bool readUserInfo(struct PilotUser &user) override;
    bool writeUserInfo(const struct PilotUser &user) override;
    bool readSysInfo(struct SysInfo &sysInfo) override;

    int openDatabase(const QString &dbName) override;
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

signals:
    void connectionComplete(bool success);

private slots:
    void onConnectionEstablished(int socket);
    void onConnectionFailed(const QString &error);
    void onWorkerStatus(const QString &status);

private:
    void cleanupWorker();

    QString m_devicePath;      // Device path (e.g., "/dev/ttyUSB0", "usb:")
    int m_socket;              // pilot-link socket descriptor
    bool m_isConnected;

    // Worker thread for async connection
    QThread *m_workerThread;
    ConnectionWorker *m_worker;
};

#endif // KPILOTDEVICELINK_H
