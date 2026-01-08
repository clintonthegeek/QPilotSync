#include "deviceworker.h"
#include "../sync/syncengine.h"
#include "../sync/synctypes.h"
#include "../sync/conduits/installconduit.h"

#include <QDebug>
#include <QThread>
#include <QFile>
#include <QFileInfo>

// pilot-link headers
extern "C" {
#include <pi-dlp.h>
#include <pi-file.h>
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
        emit error(QString("dlp_OpenConduit failed: %1").arg(result));
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

void DeviceWorker::doInstall(const QStringList &filePaths)
{
    qDebug() << "[DeviceWorker] doInstall() files:" << filePaths.size()
             << "on thread:" << QThread::currentThread();

    if (m_socket < 0) {
        emit error("No socket connection");
        emit installFinished(false, 0, filePaths.size());
        return;
    }

    if (filePaths.isEmpty()) {
        emit installFinished(true, 0, 0);
        return;
    }

    resetCancel();

    int successCount = 0;
    int failCount = 0;
    int total = filePaths.size();

    emit palmScreenChanged("Installing files...");

    for (int i = 0; i < filePaths.size(); ++i) {
        if (isCancelled()) {
            emit logMessage("Install cancelled by user");
            break;
        }

        const QString &filePath = filePaths[i];
        QFileInfo fileInfo(filePath);
        QString fileName = fileInfo.fileName();

        emit progress(i + 1, total, QString("Installing %1").arg(fileName));
        emit logMessage(QString("Installing: %1").arg(fileName));

        // Use pi_file_install from pilot-link
        struct pi_file *pf = pi_file_open(filePath.toLocal8Bit().constData());
        if (!pf) {
            emit logMessage(QString("Failed to open: %1").arg(fileName));
            failCount++;
            continue;
        }

        int result = pi_file_install(pf, m_socket, 0, nullptr);
        pi_file_close(pf);

        if (result < 0) {
            emit logMessage(QString("Failed to install %1: error %2").arg(fileName).arg(result));
            failCount++;
        } else {
            emit logMessage(QString("Installed: %1").arg(fileName));
            successCount++;
        }
    }

    emit progress(total, total, "Install complete");

    // Call dlp_OpenConduit to reset Palm screen back to ready state
    dlp_OpenConduit(m_socket);

    emit palmScreenChanged("Install complete");

    QString summary = QString("Installed %1 file(s), %2 failed")
                          .arg(successCount).arg(failCount);
    emit logMessage(summary);
    emit installFinished(failCount == 0, successCount, failCount);
    emit operationFinished(failCount == 0, "install");
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
