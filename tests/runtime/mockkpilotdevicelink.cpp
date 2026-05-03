#include "mockkpilotdevicelink.h"

#include <QMetaObject>

MockKPilotDeviceLink::MockKPilotDeviceLink(const QStringList &paths, QObject *parent)
    : KPilotDeviceLink(paths, parent)
{
}

MockKPilotDeviceLink::~MockKPilotDeviceLink() = default;

void MockKPilotDeviceLink::setNextResultSuccess(const HandshakeResult &result)
{
    m_nextResult = NextResult::Success;
    m_pendingResult = result;
}

void MockKPilotDeviceLink::setNextResultFailure(const QString &error)
{
    m_nextResult = NextResult::Failure;
    m_pendingError = error;
}

bool MockKPilotDeviceLink::openConnection()
{
    // Defer the emission so callers see openConnection() return before
    // the signal fires (matches real KPilotDeviceLink's async semantics).
    // Driving the parent's private slots via QMetaObject::invokeMethod
    // populates m_handshake/m_socket/m_isConnected and emits
    // connectionComplete(bool) just like the real worker does.
    if (m_nextResult == NextResult::Success) {
        const HandshakeResult result = m_pendingResult;
        QTimer::singleShot(0, this, [this, result]() {
            QMetaObject::invokeMethod(this, "onConnectionEstablished",
                                      Qt::DirectConnection,
                                      Q_ARG(HandshakeResult, result));
        });
    } else {
        const QString error = m_pendingError;
        QTimer::singleShot(0, this, [this, error]() {
            QMetaObject::invokeMethod(this, "onConnectionFailed",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, error));
        });
    }
    return true;
}

void MockKPilotDeviceLink::closeConnection()
{
    // No worker thread to clean up; skip parent's cleanupWorker() (which
    // is harmless on a worker-less link, but also skip its pi_close path
    // since m_socket may be a sentinel like -1 used by tests).
    // Reset connection-flagged state by deferring to the parent's slot
    // for the failed path — that clears m_socket and m_isConnected
    // without touching pilot-link.
    // Simpler: just no-op. PalmDeviceAccess::doDisconnect emits
    // deviceDisconnected itself; the mock doesn't need to participate.
}
