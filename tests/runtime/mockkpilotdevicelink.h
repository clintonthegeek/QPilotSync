#ifndef WILDPALMS_TESTS_MOCKKPILOTDEVICELINK_H
#define WILDPALMS_TESTS_MOCKKPILOTDEVICELINK_H

#include "palm/kpilotdevicelink.h"

#include <QStringList>
#include <QTimer>

/// Test-only KPilotDeviceLink. openConnection() doesn't spawn a worker
/// thread; instead, schedules a deferred dispatch via
/// QTimer::singleShot(0) that drives the parent's onConnectionEstablished
/// or onConnectionFailed slot (via QMetaObject::invokeMethod, which can
/// reach private slots). This populates the parent's m_handshake/m_socket
/// /m_isConnected state and emits connectionComplete(bool) just like the
/// real worker path. Tests configure the outcome before calling
/// openConnection().
///
/// Database/record/handshake methods are inherited as no-op stubs from
/// the parent — the test only exercises the connect lifecycle.
class MockKPilotDeviceLink : public KPilotDeviceLink
{
    Q_OBJECT
public:
    explicit MockKPilotDeviceLink(const QStringList &paths,
                                  QObject *parent = nullptr);
    ~MockKPilotDeviceLink() override;

    /// Configure the outcome of the next openConnection() call.
    void setNextResultSuccess(const HandshakeResult &result);
    void setNextResultFailure(const QString &error);

    bool openConnection() override;
    void closeConnection() override;

private:
    enum class NextResult { Success, Failure } m_nextResult = NextResult::Failure;
    HandshakeResult m_pendingResult;
    QString         m_pendingError = QStringLiteral("mock: openConnection not configured");
};

#endif
