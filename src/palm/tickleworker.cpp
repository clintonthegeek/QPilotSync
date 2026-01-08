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
    m_timer->start(m_intervalMs);
    qDebug() << "[TickleWorker] Started with interval:" << m_intervalMs << "ms";
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
        return;
    }

    // Use dlp_GetSysDateTime as a lightweight ping
    // This just reads the Palm's system time, which is fast and harmless
    time_t palmTime = 0;
    int result = dlp_GetSysDateTime(m_socket, &palmTime);

    if (result < 0) {
        qWarning() << "[TickleWorker] Tickle failed:" << result;
        emit tickleFailed(QString("Tickle failed: %1").arg(result));
        // Don't stop - let DeviceSession decide what to do
    } else {
        emit tickleSent();
        // Uncomment for verbose logging:
        // qDebug() << "[TickleWorker] Tickle sent, Palm time:" << palmTime;
    }
}
