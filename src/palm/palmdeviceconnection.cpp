#include "palmdeviceconnection.h"

#include "sync/palmbackend.h"

PalmDeviceConnection::PalmDeviceConnection(
    WildPalms::PalmSync::IPalmDatabaseAccess *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_palmBackend(new WildPalms::PalmSync::PalmBackend(device, this))
{
}

PalmDeviceConnection::~PalmDeviceConnection() = default;

WildPalms::PalmSync::IPalmDatabaseAccess *PalmDeviceConnection::device() const
{
    return m_device;
}

WildPalms::PalmSync::PalmBackend *PalmDeviceConnection::palmBackend() const
{
    return m_palmBackend;
}
