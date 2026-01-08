#ifndef TICKLEWORKER_H
#define TICKLEWORKER_H

#include <QObject>
#include <QTimer>
#include <atomic>

/**
 * @brief Worker for sending keep-alive signals to Palm device
 *
 * Palm devices can timeout during long operations. The TickleWorker
 * sends periodic lightweight DLP calls to keep the connection alive.
 *
 * This runs on its own thread and is controlled by DeviceSession.
 * It starts when an operation begins and stops when it completes.
 *
 * Implementation notes:
 * - Uses dlp_GetSysDateTime() as a lightweight ping
 * - Sends tickle every 5 seconds by default
 * - Shares socket with DeviceWorker (safe because tickle is atomic)
 */
class TickleWorker : public QObject
{
    Q_OBJECT

public:
    explicit TickleWorker(QObject *parent = nullptr);
    ~TickleWorker() override;

    /**
     * @brief Set the socket descriptor for DLP operations
     */
    void setSocket(int socket);

    /**
     * @brief Set the tickle interval in milliseconds
     *
     * Default is 5000ms (5 seconds).
     */
    void setInterval(int intervalMs);

    /**
     * @brief Check if tickle is currently running
     */
    bool isRunning() const { return m_running.load(); }

public slots:
    /**
     * @brief Start sending periodic tickles
     *
     * Call this when an operation starts.
     */
    void start();

    /**
     * @brief Stop sending tickles
     *
     * Call this when the operation completes.
     */
    void stop();

signals:
    /**
     * @brief Emitted when a tickle is sent
     */
    void tickleSent();

    /**
     * @brief Emitted if tickle fails (connection may be lost)
     */
    void tickleFailed(const QString &error);

    /**
     * @brief Emitted when connection appears to be dead
     *
     * Triggered after multiple consecutive tickle failures.
     */
    void connectionLost();

private slots:
    void sendTickle();

private:
    QTimer *m_timer = nullptr;
    int m_socket = -1;
    int m_intervalMs = 5000;
    int m_consecutiveFailures = 0;
    std::atomic<bool> m_running{false};
};

#endif // TICKLEWORKER_H
