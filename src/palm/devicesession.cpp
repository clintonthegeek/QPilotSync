#include "devicesession.h"
#include "deviceworker.h"
#include "tickleworker.h"
#include "kpilotdevicelink.h"
#include "../sync/syncengine.h"
#include "../core/synctypes.h"
#include "../runtime/syncrunner_wp.h"

#include <QDebug>
#include <QMetaObject>

DeviceSession::DeviceSession(QObject *parent)
    : QObject(parent)
{
    qDebug() << "[DeviceSession] Created";
}

DeviceSession::~DeviceSession()
{
    disconnectDevice();
    stopTickleThread();
    stopWorkerThread();
    qDebug() << "[DeviceSession] Destroyed";
}

// ========== Connection ==========

void DeviceSession::connectDevice(const QStringList &devicePaths)
{
    if (m_deviceLink) {
        emit errorOccurred("Already connected or connection in progress");
        return;
    }

    emit connectionStarted();
    emit logMessage(QString("Connecting on %1 port(s)...").arg(devicePaths.size()));

    m_deviceLink = new KPilotDeviceLink(devicePaths, this);

    // Connect KPilotDeviceLink signals
    connect(m_deviceLink, &KPilotDeviceLink::connectionComplete,
            this, &DeviceSession::onConnectionComplete);
    connect(m_deviceLink, &KPilotDeviceLink::deviceReady,
            this, &DeviceSession::onDeviceReady);
    connect(m_deviceLink, &KPilotDeviceLink::logMessage,
            this, &DeviceSession::logMessage);
    connect(m_deviceLink, &KPilotDeviceLink::errorOccurred,
            this, &DeviceSession::errorOccurred);

    m_deviceLink->openConnection();
}

void DeviceSession::disconnectDevice()
{
    if (m_deviceLink) {
        if (m_busy) {
            requestCancel();
        }

        stopTickle();  // Stop keep-alive before disconnecting
        stopTickleThread();

        m_deviceLink->closeConnection();
        m_deviceLink->deleteLater();
        m_deviceLink = nullptr;

        emit disconnected();
        emit logMessage("Disconnected from device");
    }
}

bool DeviceSession::isConnected() const
{
    return m_deviceLink && m_deviceLink->isConnected();
}

// ========== Async Operations ==========

void DeviceSession::requestSync(Sync::SyncMode mode, Sync::SyncEngine *engine)
{
    if (!isConnected()) {
        emit errorOccurred("Not connected to device");
        return;
    }

    if (m_busy) {
        emit errorOccurred("Another operation is in progress");
        return;
    }

    if (!engine) {
        emit errorOccurred("No sync engine configured");
        return;
    }

    m_busy = true;
    m_currentOperation = "sync";
    emit operationStarted("Syncing");

    ensureWorkerThread();
    stopTickle();  // Pause tickle - sync operations keep connection alive

    // Get enabled conduits
    QStringList enabledConduits;
    for (const QString &id : engine->registeredConduits()) {
        if (engine->isConduitEnabled(id)) {
            enabledConduits << id;
        }
    }

    // Invoke sync on worker thread
    QMetaObject::invokeMethod(m_worker, "doSync",
                              Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(mode)),
                              Q_ARG(QStringList, enabledConduits),
                              Q_ARG(Sync::SyncEngine*, engine),
                              Q_ARG(QString, QString()),  // stateDir - engine already configured
                              Q_ARG(QString, QString())); // syncPath - engine already configured
}

void DeviceSession::requestSync(Sync::SyncMode mode,
                                WildPalms::Runtime::SyncRunner *runner,
                                const QStringList &enabledPluginIds)
{
    if (!isConnected()) {
        emit errorOccurred("Not connected to device");
        return;
    }

    if (m_busy) {
        emit errorOccurred("Another operation is in progress");
        return;
    }

    if (!runner) {
        emit errorOccurred("No SyncRunner configured");
        return;
    }

    m_busy = true;
    m_currentOperation = "sync";
    m_pendingSyncRunner = runner;
    m_pendingPluginIds = enabledPluginIds;
    emit operationStarted("Syncing");

    ensureWorkerThread();
    stopTickle();  // Pause tickle - sync operations keep connection alive

    QMetaObject::invokeMethod(m_worker, "doSyncRunner",
                              Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(mode)),
                              Q_ARG(QStringList, enabledPluginIds),
                              Q_ARG(WildPalms::Runtime::SyncRunner*, runner));
}

void DeviceSession::requestCancel()
{
    if (!m_busy) {
        return;
    }

    emit logMessage("Cancelling operation...");

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "doCancel",
                                  Qt::QueuedConnection);
    }
}

bool DeviceSession::isBusy() const
{
    return m_busy;
}

// ========== Connection Callbacks ==========

void DeviceSession::onConnectionComplete(bool success)
{
    qDebug() << "[DeviceSession] Connection complete:" << success;

    if (success) {
        emit logMessage("Connected to Palm device");
        emit connectionComplete(true);

        // Start tickle in keep-alive mode
        if (m_connectionMode == ConnectionMode::KeepAlive) {
            startTickle();
        }

        // Always signal readiness — doSync() handles OpenConduit internally
        emit logMessage("Palm ready for sync");
        emit readyForSync();
    } else {
        emit connectionComplete(false);

        // Clean up failed connection
        if (m_deviceLink) {
            m_deviceLink->deleteLater();
            m_deviceLink = nullptr;
        }
    }
}

void DeviceSession::onDeviceReady(const QString &userName, const QString &deviceName)
{
    qDebug() << "[DeviceSession] Device ready - user:" << userName << "device:" << deviceName;
    emit deviceReady(userName, deviceName);
}

// ========== Worker Callbacks ==========

void DeviceSession::onWorkerProgress(int current, int total, const QString &msg)
{
    emit progressUpdated(current, total, msg);
}

void DeviceSession::onWorkerPalmScreen(const QString &message)
{
    emit palmScreenMessage(message);
}

void DeviceSession::onWorkerSyncFinished(bool success, const QString &summary)
{
    m_busy = false;
    m_currentOperation.clear();

    if (m_connectionMode == ConnectionMode::DisconnectAfterSync) {
        emit logMessage("Disconnecting (connection mode: disconnect after sync)");
        disconnectDevice();
    } else {
        startTickle();  // Resume tickle to keep connection alive while idle
    }

    emit syncFinished(success, summary);
}

void DeviceSession::onWorkerSyncResultReady(const Sync::SyncResult &result)
{
    emit syncResultReady(result);
}

void DeviceSession::onWorkerOperationFinished(bool success, const QString &operation)
{
    // Note: Don't start tickle here - the specific handler (onWorkerSyncFinished)
    // already handles tickle/disconnect based on connection mode.
    // This signal is emitted alongside that, so we just update state and relay.
    m_busy = false;
    m_currentOperation.clear();
    emit operationFinished(success, operation);
}

void DeviceSession::onWorkerError(const QString &error)
{
    emit errorOccurred(error);
}

void DeviceSession::onWorkerLogMessage(const QString &message)
{
    emit logMessage(message);
}

void DeviceSession::onConnectionLost()
{
    qWarning() << "[DeviceSession] Connection lost detected by tickle thread";
    emit errorOccurred("Connection to Palm device lost");
    emit logMessage("Connection lost - Palm may have timed out or been disconnected");

    // Force cleanup of connection state
    m_busy = false;
    m_currentOperation.clear();

    // Clean up threads
    stopTickleThread();
    stopWorkerThread();

    // Clean up device link
    if (m_deviceLink) {
        m_deviceLink->deleteLater();
        m_deviceLink = nullptr;
    }

    emit disconnected();
}

// ========== Private ==========

void DeviceSession::ensureWorkerThread()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        return;  // Already running
    }

    // Clean up any existing thread
    stopWorkerThread();

    // Create new worker thread
    m_workerThread = new QThread(this);
    m_worker = new DeviceWorker();
    m_worker->moveToThread(m_workerThread);

    // Connect worker signals
    connect(m_worker, &DeviceWorker::progress,
            this, &DeviceSession::onWorkerProgress);
    connect(m_worker, &DeviceWorker::palmScreenChanged,
            this, &DeviceSession::onWorkerPalmScreen);
    connect(m_worker, &DeviceWorker::syncFinished,
            this, &DeviceSession::onWorkerSyncFinished);
    connect(m_worker, &DeviceWorker::syncResultReady,
            this, &DeviceSession::onWorkerSyncResultReady);
    connect(m_worker, &DeviceWorker::operationFinished,
            this, &DeviceSession::onWorkerOperationFinished);
    connect(m_worker, &DeviceWorker::error,
            this, &DeviceSession::onWorkerError);
    connect(m_worker, &DeviceWorker::logMessage,
            this, &DeviceSession::onWorkerLogMessage);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    // Set the socket
    if (m_deviceLink) {
        m_worker->setSocket(m_deviceLink->socketDescriptor());
    }

    m_workerThread->start();
    qDebug() << "[DeviceSession] Worker thread started";
}

void DeviceSession::stopWorkerThread()
{
    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(5000)) {
            qWarning() << "[DeviceSession] Worker thread didn't stop, terminating";
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        delete m_workerThread;
        m_workerThread = nullptr;
        m_worker = nullptr;  // Deleted by thread finished signal
        qDebug() << "[DeviceSession] Worker thread stopped";
    }
}

void DeviceSession::ensureTickleThread()
{
    if (m_tickleThread && m_tickleThread->isRunning()) {
        return;  // Already running
    }

    // Clean up any existing thread
    stopTickleThread();

    // Create new tickle thread
    m_tickleThread = new QThread(this);
    m_tickle = new TickleWorker();
    m_tickle->moveToThread(m_tickleThread);

    // Connect tickle signals
    connect(m_tickle, &TickleWorker::tickleFailed,
            this, [this](const QString &error) {
                emit logMessage(QString("Keep-alive warning: %1").arg(error));
            });
    connect(m_tickle, &TickleWorker::connectionLost,
            this, &DeviceSession::onConnectionLost);

    // Clean up tickle worker when thread finishes
    connect(m_tickleThread, &QThread::finished,
            m_tickle, &QObject::deleteLater);

    // Set the socket
    if (m_deviceLink) {
        m_tickle->setSocket(m_deviceLink->socketDescriptor());
    }

    m_tickleThread->start();
    qDebug() << "[DeviceSession] Tickle thread started";
}

void DeviceSession::stopTickleThread()
{
    if (m_tickleThread) {
        // Stop tickle first
        if (m_tickle) {
            QMetaObject::invokeMethod(m_tickle, "stop", Qt::QueuedConnection);
        }

        m_tickleThread->quit();
        if (!m_tickleThread->wait(2000)) {
            qWarning() << "[DeviceSession] Tickle thread didn't stop, terminating";
            m_tickleThread->terminate();
            m_tickleThread->wait();
        }
        delete m_tickleThread;
        m_tickleThread = nullptr;
        m_tickle = nullptr;  // Deleted by thread finished signal
        qDebug() << "[DeviceSession] Tickle thread stopped";
    }
}

void DeviceSession::startTickle()
{
    ensureTickleThread();
    if (m_tickle) {
        QMetaObject::invokeMethod(m_tickle, "start", Qt::QueuedConnection);
    }
}

void DeviceSession::stopTickle()
{
    if (m_tickle) {
        QMetaObject::invokeMethod(m_tickle, "stop", Qt::QueuedConnection);
    }
}

void DeviceSession::pauseTickle()
{
    if (m_tickle && m_tickle->isRunning()) {
        // BlockingQueuedConnection ensures the tickle is fully stopped
        // before we return — no stray DLP calls on the socket.
        QMetaObject::invokeMethod(m_tickle, "stop", Qt::BlockingQueuedConnection);
        qDebug() << "[DeviceSession] Tickle paused for exclusive socket access";
    }
}

void DeviceSession::resumeTickle()
{
    if (m_connectionMode == ConnectionMode::KeepAlive && isConnected() && !m_busy) {
        startTickle();
        qDebug() << "[DeviceSession] Tickle resumed";
    }
}

