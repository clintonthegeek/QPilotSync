#include "palmdeviceconnection.h"

#include "sync/palmbackend.h"

PalmDeviceConnection::PalmDeviceConnection(
    WildPalms::PalmSync::IPalmDatabaseAccess *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_palmBackend(new WildPalms::PalmSync::PalmBackend(device, this))
    , m_fileInstaller(nullptr)
{
}

PalmDeviceConnection::PalmDeviceConnection(
    WildPalms::PalmSync::IPalmDatabaseAccess *device,
    WildPalms::PalmSync::IPalmFileInstaller  *fileInstaller,
    QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_palmBackend(new WildPalms::PalmSync::PalmBackend(device, this))
    , m_fileInstaller(fileInstaller)
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

WildPalms::PalmSync::IPalmFileInstaller *PalmDeviceConnection::fileInstaller() const
{
    return m_fileInstaller;
}
