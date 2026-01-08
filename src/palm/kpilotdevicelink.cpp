#include "kpilotdevicelink.h"
#include "pilotrecord.h"

// pilot-link headers
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>
#include <pi-file.h>
#include <pi-buffer.h>

#include <QDebug>
#include <QCoreApplication>
#include <cstring>

// ============================================================================
// ConnectionWorker - runs blocking pilot-link calls in a separate thread
// ============================================================================

ConnectionWorker::ConnectionWorker(const QString &devicePath, QObject *parent)
    : QObject(parent)
    , m_devicePath(devicePath)
    , m_socket(-1)
    , m_cancelRequested(false)
{
    qDebug() << "[ConnectionWorker] Created for device:" << devicePath;
}

ConnectionWorker::~ConnectionWorker()
{
    qDebug() << "[ConnectionWorker] Destroyed";
    // Note: socket cleanup is handled by KPilotDeviceLink or forceCloseSocket
}

void ConnectionWorker::requestCancel()
{
    qDebug() << "[ConnectionWorker] Cancel requested";
    m_cancelRequested = true;
}

void ConnectionWorker::forceCloseSocket()
{
    QMutexLocker locker(&m_socketMutex);
    int sock = m_socket.load();
    if (sock >= 0) {
        qDebug() << "[ConnectionWorker] Force-closing socket" << sock << "to interrupt pi_accept()";
        m_cancelRequested = true;
        pi_close(sock);
        m_socket = -1;
    }
}

void ConnectionWorker::doConnect()
{
    qDebug() << "[ConnectionWorker] doConnect() starting on thread:" << QThread::currentThread();
    qDebug() << "[ConnectionWorker] Device path:" << m_devicePath;

    emit statusUpdate("Creating pilot-link socket...");

    // Create socket
    qDebug() << "[ConnectionWorker] Calling pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP)";
    int sock = pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP);
    if (sock < 0) {
        QString error = QString("Failed to create pilot-link socket (errno: %1)").arg(errno);
        qWarning() << "[ConnectionWorker]" << error;
        emit connectionFailed(error);
        return;
    }

    {
        QMutexLocker locker(&m_socketMutex);
        m_socket = sock;
    }
    qDebug() << "[ConnectionWorker] Socket created successfully, fd:" << sock;

    if (m_cancelRequested) {
        qDebug() << "[ConnectionWorker] Cancel requested after socket creation";
        QMutexLocker locker(&m_socketMutex);
        if (m_socket >= 0) {
            pi_close(m_socket);
            m_socket = -1;
        }
        emit connectionFailed("Connection cancelled");
        return;
    }

    // Bind to device
    emit statusUpdate(QString("Binding to device %1...").arg(m_devicePath));
    qDebug() << "[ConnectionWorker] Calling pi_bind() with path:" << m_devicePath.toUtf8().constData();

    int bindResult = pi_bind(sock, m_devicePath.toUtf8().constData());
    if (bindResult < 0) {
        QString error = QString("Failed to bind to device %1 (result: %2, errno: %3)")
            .arg(m_devicePath).arg(bindResult).arg(errno);
        qWarning() << "[ConnectionWorker]" << error;
        QMutexLocker locker(&m_socketMutex);
        if (m_socket >= 0) {
            pi_close(m_socket);
            m_socket = -1;
        }
        emit connectionFailed(error);
        return;
    }
    qDebug() << "[ConnectionWorker] Bind successful, result:" << bindResult;

    if (m_cancelRequested) {
        qDebug() << "[ConnectionWorker] Cancel requested after bind";
        QMutexLocker locker(&m_socketMutex);
        if (m_socket >= 0) {
            pi_close(m_socket);
            m_socket = -1;
        }
        emit connectionFailed("Connection cancelled");
        return;
    }

    // Listen for connection
    emit statusUpdate("Listening for device...");
    qDebug() << "[ConnectionWorker] Calling pi_listen() with backlog 1";

    int listenResult = pi_listen(sock, 1);
    if (listenResult < 0) {
        QString error = QString("Failed to listen on device (result: %1, errno: %2)")
            .arg(listenResult).arg(errno);
        qWarning() << "[ConnectionWorker]" << error;
        QMutexLocker locker(&m_socketMutex);
        if (m_socket >= 0) {
            pi_close(m_socket);
            m_socket = -1;
        }
        emit connectionFailed(error);
        return;
    }
    qDebug() << "[ConnectionWorker] Listen successful";

    if (m_cancelRequested) {
        qDebug() << "[ConnectionWorker] Cancel requested after listen";
        QMutexLocker locker(&m_socketMutex);
        if (m_socket >= 0) {
            pi_close(m_socket);
            m_socket = -1;
        }
        emit connectionFailed("Connection cancelled");
        return;
    }

    // Accept connection - THIS BLOCKS until HotSync button is pressed
    emit statusUpdate("Waiting for HotSync button press... (press button on Palm now)");
    qDebug() << "[ConnectionWorker] Calling pi_accept() - THIS WILL BLOCK until HotSync";
    qDebug() << "[ConnectionWorker] Press the HotSync button on your Palm device now!";

    int acceptResult = pi_accept(sock, nullptr, nullptr);
    if (acceptResult < 0) {
        if (m_cancelRequested) {
            qDebug() << "[ConnectionWorker] Accept interrupted - connection cancelled by user";
            emit connectionFailed("Connection cancelled by user");
        } else {
            QString error = QString("Failed to accept connection (result: %1, errno: %2)")
                .arg(acceptResult).arg(errno);
            qWarning() << "[ConnectionWorker]" << error;
            emit connectionFailed(error);
        }
        // Socket may already be closed by forceCloseSocket(), check first
        QMutexLocker locker(&m_socketMutex);
        if (m_socket >= 0) {
            pi_close(m_socket);
            m_socket = -1;
        }
        return;
    }

    qDebug() << "[ConnectionWorker] Connection accepted! Accept result:" << acceptResult;
    emit statusUpdate("Device connected!");
    emit connectionEstablished(sock);
}

// ============================================================================
// KPilotDeviceLink
// ============================================================================

KPilotDeviceLink::KPilotDeviceLink(const QString &devicePath, QObject *parent)
    : KPilotLink(parent)
    , m_devicePath(devicePath)
    , m_socket(-1)
    , m_isConnected(false)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
    qDebug() << "[KPilotDeviceLink] Initialized for device:" << devicePath;
    emit logMessage(QString("Initialized device link for: %1").arg(devicePath));
}

KPilotDeviceLink::~KPilotDeviceLink()
{
    qDebug() << "[KPilotDeviceLink] Destructor called";
    closeConnection();
}

void KPilotDeviceLink::cleanupWorker()
{
    qDebug() << "[KPilotDeviceLink] cleanupWorker() called";

    if (m_worker) {
        qDebug() << "[KPilotDeviceLink] Requesting worker cancellation";
        m_worker->requestCancel();
    }

    if (m_workerThread) {
        qDebug() << "[KPilotDeviceLink] Waiting for worker thread to finish...";
        m_workerThread->quit();
        if (!m_workerThread->wait(3000)) {
            qWarning() << "[KPilotDeviceLink] Worker thread did not finish in time, terminating";
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        qDebug() << "[KPilotDeviceLink] Worker thread finished";

        delete m_workerThread;
        m_workerThread = nullptr;
    }

    // Worker is deleted by thread's finished signal via deleteLater
    m_worker = nullptr;
}

void KPilotDeviceLink::cancelConnection()
{
    qDebug() << "[KPilotDeviceLink] cancelConnection() called";

    if (!m_worker) {
        qDebug() << "[KPilotDeviceLink] No active connection attempt to cancel";
        return;
    }

    emit logMessage("Cancelling connection attempt...");

    // Force close the socket to interrupt pi_accept()
    m_worker->forceCloseSocket();

    // Wait for worker to finish cleanly
    if (m_workerThread) {
        qDebug() << "[KPilotDeviceLink] Waiting for worker thread after cancel...";
        if (!m_workerThread->wait(2000)) {
            qWarning() << "[KPilotDeviceLink] Worker thread did not respond to cancel, terminating";
            m_workerThread->terminate();
            m_workerThread->wait();
        }
    }

    cleanupWorker();
    setStatus(Init);
    emit logMessage("Connection cancelled");
}

bool KPilotDeviceLink::openConnection()
{
    qDebug() << "[KPilotDeviceLink] openConnection() called";

    if (m_isConnected) {
        qDebug() << "[KPilotDeviceLink] Already connected, returning true";
        emit logMessage("Already connected");
        return true;
    }

    // Clean up any existing worker
    cleanupWorker();

    emit logMessage(QString("Opening connection to %1...").arg(m_devicePath));
    setStatus(WaitingForDevice);

    // Create worker thread
    qDebug() << "[KPilotDeviceLink] Creating worker thread";
    m_workerThread = new QThread(this);
    m_worker = new ConnectionWorker(m_devicePath);
    m_worker->moveToThread(m_workerThread);

    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &ConnectionWorker::doConnect);
    connect(m_worker, &ConnectionWorker::connectionEstablished,
            this, &KPilotDeviceLink::onConnectionEstablished);
    connect(m_worker, &ConnectionWorker::connectionFailed,
            this, &KPilotDeviceLink::onConnectionFailed);
    connect(m_worker, &ConnectionWorker::statusUpdate,
            this, &KPilotDeviceLink::onWorkerStatus);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Start the worker thread
    qDebug() << "[KPilotDeviceLink] Starting worker thread";
    m_workerThread->start();

    emit logMessage("Connection started in background - press HotSync button on Palm");
    qDebug() << "[KPilotDeviceLink] openConnection() returning true (async)";

    return true;  // Connection started successfully (but not yet complete)
}

void KPilotDeviceLink::onConnectionEstablished(int socket)
{
    qDebug() << "[KPilotDeviceLink] onConnectionEstablished() socket:" << socket;

    m_socket = socket;
    m_isConnected = true;
    setStatus(AcceptedDevice);

    emit logMessage("Device connected successfully!");
    emit connectionComplete(true);

    qDebug() << "[KPilotDeviceLink] Connection established, m_isConnected = true";
}

void KPilotDeviceLink::onConnectionFailed(const QString &error)
{
    qWarning() << "[KPilotDeviceLink] onConnectionFailed():" << error;

    m_socket = -1;
    m_isConnected = false;
    setStatus(PilotLinkError);
    setError(error);

    emit connectionComplete(false);
}

void KPilotDeviceLink::onWorkerStatus(const QString &status)
{
    qDebug() << "[KPilotDeviceLink] Worker status:" << status;
    emit logMessage(status);
}

void KPilotDeviceLink::closeConnection()
{
    qDebug() << "[KPilotDeviceLink] closeConnection() called, m_isConnected:" << m_isConnected;

    // First clean up any pending worker
    cleanupWorker();

    if (m_socket >= 0) {
        qDebug() << "[KPilotDeviceLink] Closing socket:" << m_socket;
        emit logMessage("Closing connection...");
        pi_close(m_socket);
        m_socket = -1;
    }

    m_isConnected = false;
    setStatus(Init);
    qDebug() << "[KPilotDeviceLink] Connection closed";
}

bool KPilotDeviceLink::readUserInfo(struct PilotUser &user)
{
    qDebug() << "[KPilotDeviceLink] readUserInfo() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readUserInfo() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_ReadUserInfo()";
    struct PilotUser pilotUser;
    int result = dlp_ReadUserInfo(m_socket, &pilotUser);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadUserInfo() failed, result:" << result;
        setError("Failed to read user info");
        return false;
    }

    user = pilotUser;
    qDebug() << "[KPilotDeviceLink] User info read successfully:"
             << "username=" << user.username
             << "userID=" << user.userID;

    emit logMessage(QString("User: %1 (ID: %2)")
                   .arg(user.username)
                   .arg(user.userID));

    emit deviceReady(QString::fromUtf8(user.username), QString("Palm Device"));

    return true;
}

bool KPilotDeviceLink::writeUserInfo(const struct PilotUser &user)
{
    qDebug() << "[KPilotDeviceLink] writeUserInfo() called for user:" << user.username;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] writeUserInfo() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_WriteUserInfo()";
    int result = dlp_WriteUserInfo(m_socket, const_cast<struct PilotUser*>(&user));
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_WriteUserInfo() failed, result:" << result;
        setError("Failed to write user info");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] User info written successfully";
    emit logMessage(QString("User info updated: %1").arg(user.username));
    return true;
}

bool KPilotDeviceLink::readSysInfo(struct SysInfo &sysInfo)
{
    qDebug() << "[KPilotDeviceLink] readSysInfo() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readSysInfo() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_ReadSysInfo()";
    struct SysInfo info;
    int result = dlp_ReadSysInfo(m_socket, &info);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadSysInfo() failed, result:" << result;
        setError("Failed to read system info");
        return false;
    }

    sysInfo = info;
    qDebug() << "[KPilotDeviceLink] System info read:"
             << "romVersion=0x" << Qt::hex << sysInfo.romVersion
             << "prodID=" << sysInfo.prodID;

    emit logMessage(QString("ROM Version: %1.%2")
                   .arg((sysInfo.romVersion >> 16) & 0xFF)
                   .arg((sysInfo.romVersion >> 8) & 0xFF));

    return true;
}

int KPilotDeviceLink::openDatabase(const QString &dbName, bool readWrite)
{
    qDebug() << "[KPilotDeviceLink] openDatabase() called for:" << dbName
             << "readWrite:" << readWrite;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] openDatabase() - not connected";
        setError("Not connected");
        return -1;
    }

    int dbHandle = 0;
    int mode = readWrite ? (dlpOpenRead | dlpOpenWrite) : dlpOpenRead;

    qDebug() << "[KPilotDeviceLink] Calling dlp_OpenDB() mode:" << mode;
    emit logMessage(QString("Opening database: %1 (%2)")
                   .arg(dbName, readWrite ? "read-write" : "read-only"));

    int result = dlp_OpenDB(m_socket, 0, mode, dbName.toUtf8().constData(), &dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_OpenDB() failed, result:" << result;
        setError(QString("Failed to open database: %1").arg(dbName));
        return -1;
    }

    qDebug() << "[KPilotDeviceLink] Database opened, handle:" << dbHandle;
    emit logMessage(QString("Database opened with handle: %1").arg(dbHandle));
    return dbHandle;
}

bool KPilotDeviceLink::closeDatabase(int handle)
{
    qDebug() << "[KPilotDeviceLink] closeDatabase() called for handle:" << handle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] closeDatabase() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_CloseDB()";
    int result = dlp_CloseDB(m_socket, handle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_CloseDB() failed, result:" << result;
        setError(QString("Failed to close database handle: %1").arg(handle));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Database closed successfully";
    emit logMessage(QString("Database closed: %1").arg(handle));
    return true;
}

QStringList KPilotDeviceLink::listDatabases()
{
    qDebug() << "[KPilotDeviceLink] listDatabases() called";
    QStringList databases;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] listDatabases() - not connected";
        setError("Not connected");
        return databases;
    }

    emit logMessage("Listing databases...");

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    int dbIndex = 0;
    int flags = dlpDBListRAM;  // List databases in RAM

    qDebug() << "[KPilotDeviceLink] Starting database enumeration";

    while (true) {
        struct DBInfo info;
        int result = dlp_ReadDBList(m_socket, 0, flags, dbIndex, buffer);
        if (result < 0) {
            qDebug() << "[KPilotDeviceLink] dlp_ReadDBList() ended at index:" << dbIndex;
            break;
        }

        // Parse database info from buffer
        memcpy(&info, buffer->data, sizeof(info));

        QString dbName = QString::fromLatin1(info.name);
        databases.append(dbName);

        qDebug() << "[KPilotDeviceLink] Found database:" << dbName;
        emit logMessage(QString("  Found: %1").arg(dbName));
        dbIndex++;
    }

    pi_buffer_free(buffer);

    qDebug() << "[KPilotDeviceLink] Total databases found:" << databases.size();
    emit logMessage(QString("Found %1 databases").arg(databases.size()));
    return databases;
}

QList<PilotRecord*> KPilotDeviceLink::readAllRecords(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] readAllRecords() called for handle:" << dbHandle;
    QList<PilotRecord*> records;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readAllRecords() - not connected";
        setError("Not connected");
        return records;
    }

    emit logMessage(QString("Reading all records from database handle %1...").arg(dbHandle));

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    int index = 0;

    while (m_isConnected) {
        recordid_t id = 0;
        int attr = 0;
        int category = 0;

        int result = dlp_ReadRecordByIndex(m_socket, dbHandle, index,
                                          buffer, &id, &attr, &category);

        if (result < 0) {
            if (index == 0) {
                qWarning() << "[KPilotDeviceLink] Failed to read first record, possible disconnect";
                setError("Failed to read records - device may be disconnected");
                setStatus(PilotLinkError);
                m_isConnected = false;
            } else {
                qDebug() << "[KPilotDeviceLink] End of records at index:" << index;
            }
            break;
        }

        QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
        PilotRecord *record = new PilotRecord(id, category, attr, data);
        records.append(record);

        if (index % 50 == 0 && index > 0) {
            qDebug() << "[KPilotDeviceLink] Read" << index << "records so far...";
        }

        index++;
    }

    pi_buffer_free(buffer);

    qDebug() << "[KPilotDeviceLink] Total records read:" << records.size();
    emit logMessage(QString("Read %1 records").arg(records.size()));
    return records;
}

PilotRecord* KPilotDeviceLink::readRecordByIndex(int dbHandle, int index)
{
    qDebug() << "[KPilotDeviceLink] readRecordByIndex() handle:" << dbHandle << "index:" << index;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readRecordByIndex() - not connected";
        setError("Not connected");
        return nullptr;
    }

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    recordid_t id = 0;
    int attr = 0;
    int category = 0;

    int result = dlp_ReadRecordByIndex(m_socket, dbHandle, index,
                                      buffer, &id, &attr, &category);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadRecordByIndex() failed, result:" << result;
        pi_buffer_free(buffer);
        setError(QString("Failed to read record at index: %1").arg(index));
        return nullptr;
    }

    QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
    PilotRecord *record = new PilotRecord(id, category, attr, data);

    pi_buffer_free(buffer);
    qDebug() << "[KPilotDeviceLink] Record read successfully, id:" << id;
    return record;
}

PilotRecord* KPilotDeviceLink::readRecordById(int dbHandle, int recordId)
{
    qDebug() << "[KPilotDeviceLink] readRecordById() handle:" << dbHandle << "id:" << recordId;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readRecordById() - not connected";
        setError("Not connected");
        return nullptr;
    }

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    int attr = 0;
    int category = 0;
    int index = 0;

    int result = dlp_ReadRecordById(m_socket, dbHandle, recordId, buffer,
                                   &index, &attr, &category);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadRecordById() failed, result:" << result;
        pi_buffer_free(buffer);
        setError(QString("Failed to read record by ID: %1").arg(recordId));
        return nullptr;
    }

    QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
    PilotRecord *record = new PilotRecord(recordId, category, attr, data);

    pi_buffer_free(buffer);
    qDebug() << "[KPilotDeviceLink] Record read successfully by id:" << recordId;
    return record;
}

bool KPilotDeviceLink::writeRecord(int dbHandle, PilotRecord *record)
{
    qDebug() << "[KPilotDeviceLink] writeRecord() called for handle:" << dbHandle
             << "recordId:" << record->id() << "category:" << record->category();

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] writeRecord() - not connected";
        setError("Not connected");
        return false;
    }

    if (!record) {
        qWarning() << "[KPilotDeviceLink] writeRecord() - null record";
        setError("Cannot write null record");
        return false;
    }

    const QByteArray &data = record->data();
    recordid_t newRecordId = 0;

    // flags: 0 = normal write
    // recuid: 0 = create new record, otherwise update existing
    recordid_t recuid = record->id();

    qDebug() << "[KPilotDeviceLink] Calling dlp_WriteRecord() size:" << data.size()
             << "category:" << record->category() << "recuid:" << recuid;

    int result = dlp_WriteRecord(m_socket, dbHandle, 0, recuid,
                                 record->category(),
                                 reinterpret_cast<const void*>(data.constData()),
                                 data.size(), &newRecordId);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_WriteRecord() failed, result:" << result;
        setError(QString("Failed to write record: error %1").arg(result));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Record written successfully, newRecordId:" << newRecordId;

    // Update record with new ID if it was a create operation
    if (recuid == 0 && newRecordId != 0) {
        record->setId(newRecordId);
    }

    emit logMessage(QString("Record written (ID: %1, size: %2 bytes)")
                   .arg(newRecordId).arg(data.size()));
    return true;
}

bool KPilotDeviceLink::deleteRecord(int dbHandle, int recordId)
{
    qDebug() << "[KPilotDeviceLink] deleteRecord() handle:" << dbHandle << "recordId:" << recordId;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] deleteRecord() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_DeleteRecord()";
    int result = dlp_DeleteRecord(m_socket, dbHandle, 0, recordId);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_DeleteRecord() failed, result:" << result;
        setError(QString("Failed to delete record %1: error %2").arg(recordId).arg(result));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Record deleted successfully";
    emit logMessage(QString("Record deleted (ID: %1)").arg(recordId));
    return true;
}

bool KPilotDeviceLink::readAppBlock(int dbHandle, unsigned char *buffer, size_t *size)
{
    qDebug() << "[KPilotDeviceLink] readAppBlock() called for handle:" << dbHandle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readAppBlock() - not connected";
        setError("Not connected");
        return false;
    }

    pi_buffer_t *buf = pi_buffer_new(0xffff);

    qDebug() << "[KPilotDeviceLink] Calling dlp_ReadAppBlock()";
    int result = dlp_ReadAppBlock(m_socket, dbHandle, 0, -1, buf);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadAppBlock() failed, result:" << result;
        pi_buffer_free(buf);
        setError("Failed to read AppInfo block");
        return false;
    }

    *size = buf->used;
    memcpy(buffer, buf->data, buf->used);

    pi_buffer_free(buf);
    qDebug() << "[KPilotDeviceLink] AppInfo block read," << *size << "bytes";
    emit logMessage(QString("Read AppInfo block (%1 bytes)").arg(*size));

    return true;
}

bool KPilotDeviceLink::writeAppBlock(int dbHandle, const unsigned char *buffer, size_t size)
{
    qDebug() << "[KPilotDeviceLink] writeAppBlock() called for handle:" << dbHandle
             << "size:" << size;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] writeAppBlock() - not connected";
        setError("Not connected");
        return false;
    }

    if (!buffer || size == 0) {
        qWarning() << "[KPilotDeviceLink] writeAppBlock() - invalid buffer";
        setError("Invalid buffer");
        return false;
    }

    int result = dlp_WriteAppBlock(m_socket, dbHandle,
                                   reinterpret_cast<const void*>(buffer), size);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_WriteAppBlock() failed, result:" << result;
        setError(QString("Failed to write AppInfo block: error %1").arg(result));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] AppInfo block written successfully";
    emit logMessage(QString("AppInfo block written (%1 bytes)").arg(size));
    return true;
}

bool KPilotDeviceLink::beginSync()
{
    qDebug() << "[KPilotDeviceLink] beginSync() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] beginSync() - not connected";
        setError("Not connected");
        return false;
    }

    emit logMessage("Beginning sync...");

    qDebug() << "[KPilotDeviceLink] Calling dlp_OpenConduit()";
    int result = dlp_OpenConduit(m_socket);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_OpenConduit() failed, result:" << result;
        setError("Failed to open sync conduit");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Sync conduit opened";
    return true;
}

bool KPilotDeviceLink::endSync()
{
    qDebug() << "[KPilotDeviceLink] endSync() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] endSync() - not connected";
        setError("Not connected");
        return false;
    }

    emit logMessage("Ending sync...");

    char logEntry[] = "Sync completed by QPilotSync.\n";
    qDebug() << "[KPilotDeviceLink] Adding sync log entry";
    if (dlp_AddSyncLogEntry(m_socket, logEntry) < 0) {
        qWarning() << "[KPilotDeviceLink] Failed to add sync log entry (non-fatal)";
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_EndOfSync()";
    int result = dlp_EndOfSync(m_socket, 0);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_EndOfSync() failed, result:" << result;
        setError("Failed to end sync");
        return false;
    }

    setStatus(SyncDone);
    qDebug() << "[KPilotDeviceLink] Sync complete!";
    emit logMessage("Sync complete!");

    return true;
}

bool KPilotDeviceLink::cleanUpDatabase(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] cleanUpDatabase() called for handle:" << dbHandle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] cleanUpDatabase() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_CleanUpDatabase()";
    int result = dlp_CleanUpDatabase(m_socket, dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_CleanUpDatabase() failed, result:" << result;
        setError("Failed to clean up database");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Database cleanup complete";
    return true;
}

bool KPilotDeviceLink::resetSyncFlags(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] resetSyncFlags() called for handle:" << dbHandle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] resetSyncFlags() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_ResetSyncFlags()";
    int result = dlp_ResetSyncFlags(m_socket, dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ResetSyncFlags() failed, result:" << result;
        setError("Failed to reset sync flags");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Sync flags reset complete";
    return true;
}
