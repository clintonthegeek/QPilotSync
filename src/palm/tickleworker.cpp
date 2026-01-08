#include "tickleworker.h"

#include <QDebug>
#include <QThread>

// pilot-link headers
extern "C" {
#include <pi-dlp.h>
}

TickleWorker::TickleWorker(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &TickleWorker::sendTickle);

    qDebug() << "[TickleWorker] Created on thread:" << QThread::currentThread();
}

TickleWorker::~TickleWorker()
{
    stop();
    qDebug() << "[TickleWorker] Destroyed";
}

void TickleWorker::setSocket(int socket)
{
    m_socket = socket;
    qDebug() << "[TickleWorker] Socket set to:" << socket;
}

void TickleWorker::setInterval(int intervalMs)
{
    m_intervalMs = intervalMs;
    if (m_timer->isActive()) {
        m_timer->setInterval(intervalMs);
    }
}

void TickleWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    if (m_socket < 0) {
        qWarning() << "[TickleWorker] Cannot start - no socket";
        return;
    }

    m_running = true;
    m_consecutiveFailures = 0;
    m_timer->start(m_intervalMs);
    qDebug() << "[TickleWorker] Started with interval:" << m_intervalMs << "ms on socket:" << m_socket;
}

void TickleWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running = false;
    m_timer->stop();
    qDebug() << "[TickleWorker] Stopped";
}

void TickleWorker::sendTickle()
{
    if (!m_running.load() || m_socket < 0) {
        qDebug() << "[TickleWorker] sendTickle skipped - running:" << m_running.load() << "socket:" << m_socket;
        return;
    }

    // Use dlp_GetSysDateTime as a lightweight ping
    // This just reads the Palm's system time, which is fast and harmless
    time_t palmTime = 0;
    int result = dlp_GetSysDateTime(m_socket, &palmTime);

    if (result < 0) {
        qWarning() << "[TickleWorker] Tickle FAILED:" << result;
        m_consecutiveFailures++;
        emit tickleFailed(QString("Tickle failed: %1 (attempt %2)").arg(result).arg(m_consecutiveFailures));

        // After 3 consecutive failures, connection is likely dead
        if (m_consecutiveFailures >= 3) {
            qWarning() << "[TickleWorker] Too many failures, connection appears dead";
            emit connectionLost();
            stop();
        }
    } else {
        m_consecutiveFailures = 0;
        emit tickleSent();
        qDebug() << "[TickleWorker] Tickle OK, Palm time:" << palmTime;
    }
}
