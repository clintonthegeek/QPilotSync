#include "kpilotlink.h"

KPilotLink::KPilotLink(QObject *parent)
    : QObject(parent)
    , m_status(Init)
{
}

KPilotLink::~KPilotLink()
{
}

void KPilotLink::setStatus(LinkStatus newStatus)
{
    if (m_status != newStatus) {
        m_status = newStatus;
        emit statusChanged(newStatus);

        // Log status changes
        QString statusStr;
        switch (newStatus) {
            case Init: statusStr = "Initialized"; break;
            case WaitingForDevice: statusStr = "Waiting for device"; break;
            case FoundDevice: statusStr = "Device found"; break;
            case CreatedSocket: statusStr = "Socket created"; break;
            case DeviceOpen: statusStr = "Device opened"; break;
            case AcceptedDevice: statusStr = "Device accepted"; break;
            case SyncDone: statusStr = "Sync complete"; break;
            case PilotLinkError: statusStr = "Error occurred"; break;
        }
        emit logMessage(QString("Status: %1").arg(statusStr));
    }
}

void KPilotLink::setError(const QString &error)
{
    m_lastError = error;
    setStatus(PilotLinkError);
    emit errorOccurred(error);
}
