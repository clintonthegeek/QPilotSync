#include "palmtickle.h"

#include "palm/kpilotlink.h"

#include <QDebug>

extern "C" {
#include <pi-dlp.h>
}

namespace WildPalms::Runtime {

PalmTickle::PalmTickle(KPilotLink *link, int socket, QObject *parent)
    : QObject(parent)
    , m_link(link)
    , m_socket(socket)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &PalmTickle::sendTick);

    if (m_link) {
        connect(m_link, &KPilotLink::statusChanged, this,
                [this](KPilotLink::LinkStatus s) {
                    if (s == KPilotLink::PilotLinkError) stop();
                },
                Qt::QueuedConnection);
    }
}

PalmTickle::~PalmTickle()
{
    stop();
}

void PalmTickle::start()
{
    if (m_running.exchange(true)) return;
    if (m_socket < 0) {
        qWarning() << "[PalmTickle] start() called with no socket";
        m_running = false;
        return;
    }
    m_consecutiveFailures = 0;
    m_timer->start();
}

void PalmTickle::stop()
{
    if (!m_running.exchange(false)) return;
    m_timer->stop();
}

void PalmTickle::setInterval(int intervalMs)
{
    m_intervalMs = intervalMs;
    if (m_timer->isActive()) m_timer->setInterval(intervalMs);
}

void PalmTickle::sendTick()
{
    if (!m_running.load() || m_socket < 0) return;

    time_t palmTime = 0;
    const int rc = dlp_GetSysDateTime(m_socket, &palmTime);
    if (rc < 0) {
        ++m_consecutiveFailures;
        qWarning() << "[PalmTickle] tick failed rc=" << rc
                   << "consecutive=" << m_consecutiveFailures;
        if (m_consecutiveFailures >= 3) {
            stop();
            emit connectionLost();
        }
        return;
    }
    m_consecutiveFailures = 0;
    emit tickSent();
}

} // namespace WildPalms::Runtime
