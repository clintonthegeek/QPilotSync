#include "deviceworker.h"
#include "../sync/syncengine.h"
#include "../sync/synctypes.h"

#include <QDebug>
#include <QThread>

// pilot-link headers
extern "C" {
#include <pi-socket.h>
#include <pi-dlp.h>
}

DeviceWorker::DeviceWorker(QObject *parent)
    : QObject(parent)
{
    qDebug() << "[DeviceWorker] Created on thread:" << QThread::currentThread();
}

DeviceWorker::~DeviceWorker()
{
    qDebug() << "[DeviceWorker] Destroyed";
}

void DeviceWorker::setSocket(int socket)
{
    m_socket = socket;
    qDebug() << "[DeviceWorker] Socket set to:" << socket;
}

void DeviceWorker::doOpenConduit()
{
    qDebug() << "[DeviceWorker] doOpenConduit() on thread:" << QThread::currentThread();

    if (m_socket < 0) {
        emit error("No socket connection");
        emit openConduitFinished(false);
        return;
    }

    emit palmScreenChanged("Syncing...");
    emit logMessage("Opening conduit session...");

    int result = dlp_OpenConduit(m_socket);
    if (result < 0) {
        emit error(QString("dlp_OpenConduit failed: %1 (pi_error: %2, palmos: %3)")
                       .arg(result)
                       .arg(pi_error(m_socket))
                       .arg(pi_palmos_error(m_socket)));
        emit openConduitFinished(false);
        return;
    }

    emit logMessage("Conduit session opened - Palm ready for sync");
    emit openConduitFinished(true);
}

void DeviceWorker::doEndSync(bool success)
{
    qDebug() << "[DeviceWorker] doEndSync() success:" << success;

    if (m_socket < 0) {
        return;
    }

    int status = success ? dlpEndCodeNormal : dlpEndCodeOther;
    emit palmScreenChanged(success ? "Sync Complete" : "Sync Error");

    // Note: dlp_EndOfSync is typically called by the connection close
    // but we can call it explicitly if needed
    // dlp_EndOfSync(m_socket, status);

    emit logMessage(success ? "Sync session ended normally" : "Sync session ended with error");
}

void DeviceWorker::doSync(int mode,
                          const QStringList &conduitIds,
                          Sync::SyncEngine *engine,
                          const QString &stateDir,
                          const QString &syncPath)
{
    qDebug() << "[DeviceWorker] doSync() mode:" << mode
             << "conduits:" << conduitIds
             << "on thread:" << QThread::currentThread();

    if (m_socket < 0) {
        emit error("No socket connection");
        emit syncFinished(false, "No connection");
        return;
    }

    if (!engine) {
        emit error("No sync engine provided");
        emit syncFinished(false, "Internal error");
        return;
    }

    resetCancel();

    // First, open the conduit to update Palm screen
    emit palmScreenChanged("Syncing...");
    int openResult = dlp_OpenConduit(m_socket);
    if (openResult < 0) {
        emit logMessage(QString("Warning: dlp_OpenConduit returned %1").arg(openResult));
        // Continue anyway - some devices may not require this
    }

    // Set up cancellation check callback
    engine->setCancelCheck([this]() { return isCancelled(); });

    // Set up progress callback
    engine->setProgressCallback([this](int current, int total, const QString &msg) {
        emit progress(current, total, msg);
    });

    // Run the sync
    Sync::SyncMode syncMode = static_cast<Sync::SyncMode>(mode);
    Sync::SyncResult result = engine->syncAll(syncMode);

    // Clear callbacks
    engine->setCancelCheck(nullptr);
    engine->setProgressCallback(nullptr);

    // Report result
    QString summary;
    if (result.success) {
        summary = QString("Palm: %1, PC: %2")
                      .arg(result.palmStats.summary())
                      .arg(result.pcStats.summary());
        emit palmScreenChanged("Sync complete");
    } else {
        summary = result.errorMessage;
        emit palmScreenChanged("Sync error");
    }

    emit logMessage(summary);
    emit syncFinished(result.success, summary);
    emit syncResultReady(result);  // Emit full result for detailed handling
    emit operationFinished(result.success, "sync");
}

void DeviceWorker::doCancel()
{
    qDebug() << "[DeviceWorker] Cancel requested";
    m_cancelRequested = true;
}

void DeviceWorker::resetCancel()
{
    m_cancelRequested = false;
}

bool DeviceWorker::isCancelled() const
{
    return m_cancelRequested.load();
}
