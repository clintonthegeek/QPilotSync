#include "deviceworker.h"
#include "../sync/syncengine.h"
#include "../core/synctypes.h"
#include "../runtime/syncrunner_wp.h"

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

void DeviceWorker::doSyncRunner(int mode,
                                const QStringList &enabledPluginIds,
                                WildPalms::Runtime::SyncRunner *runner)
{
    qDebug() << "[DeviceWorker] doSyncRunner() mode:" << mode
             << "plugins:" << enabledPluginIds
             << "on thread:" << QThread::currentThread();

    if (m_socket < 0) {
        emit error("No socket connection");
        emit syncFinished(false, "No connection");
        return;
    }
    if (!runner) {
        emit error("No SyncRunner provided");
        emit syncFinished(false, "Internal error");
        return;
    }

    resetCancel();

    // Refresh Palm screen + open the conduit, mirroring the legacy
    // doSync() flow so the on-device UX is unchanged.
    emit palmScreenChanged("Syncing...");
    int openResult = dlp_OpenConduit(m_socket);
    if (openResult < 0) {
        emit logMessage(QString("Warning: dlp_OpenConduit returned %1").arg(openResult));
    }

    // Forward runner progress + log signals onto our own so the
    // DeviceSession ↔ KF6MainWindow plumbing keeps working.
    QObject::connect(runner, &WildPalms::Runtime::SyncRunner::progress,
                     this, &DeviceWorker::progress, Qt::DirectConnection);
    QObject::connect(runner, &WildPalms::Runtime::SyncRunner::logMessage,
                     this, &DeviceWorker::logMessage, Qt::DirectConnection);

    Sync::SyncMode syncMode = static_cast<Sync::SyncMode>(mode);
    Sync::SyncResult result = runner->run(syncMode, enabledPluginIds);

    QObject::disconnect(runner, &WildPalms::Runtime::SyncRunner::progress,
                        this, &DeviceWorker::progress);
    QObject::disconnect(runner, &WildPalms::Runtime::SyncRunner::logMessage,
                        this, &DeviceWorker::logMessage);

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
    emit syncResultReady(result);
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
