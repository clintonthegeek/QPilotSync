#ifndef WILDPALMS_RUNTIME_PALMTICKLE_H
#define WILDPALMS_RUNTIME_PALMTICKLE_H

#include <QObject>
#include <QTimer>
#include <atomic>

class KPilotLink;

namespace WildPalms::Runtime {

/**
 * @brief Keep-alive ticker for an open Palm connection.
 *
 * Replaces the legacy TickleWorker. Lives on the same thread as
 * its owning PalmDeviceAccess (the link thread). Listens to
 * KPilotLink's ticklePauseRequested/tickleResumeRequested signals
 * to suspend ticks during bulk DLP work.
 *
 * Sends dlp_GetSysDateTime() every 5s as a lightweight ping. After
 * 3 consecutive failures, emits connectionLost() and stops itself.
 *
 * Lifetime: created when PalmDeviceAccess opens a connection,
 * destroyed when the connection closes. Single instance per link.
 */
class PalmTickle : public QObject
{
    Q_OBJECT
public:
    /// @param link  Non-owning. Caller guarantees link outlives this.
    /// @param socket  pilot-link socket descriptor for dlp_* calls.
    /// @param parent  QObject parent (must be on the link thread).
    PalmTickle(KPilotLink *link, int socket, QObject *parent);
    ~PalmTickle() override;

    /// Start the periodic ping. No-op if already running.
    void start();

    /// Stop the periodic ping. No-op if not running.
    void stop();

    /// Configure tick interval (default 5000 ms).
    void setInterval(int intervalMs);

signals:
    /// Emitted when 3 consecutive ticks have failed.
    void connectionLost();

    /// Emitted on each successful tick (mostly for tests / logging).
    void tickSent();

private slots:
    void sendTick();

private:
    KPilotLink        *m_link;          // borrowed, non-owning
    int                m_socket;
    QTimer            *m_timer;
    std::atomic<bool>  m_running { false };
    int                m_consecutiveFailures = 0;
    int                m_intervalMs = 5000;
};

} // namespace WildPalms::Runtime

#endif
