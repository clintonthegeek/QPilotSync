#include "pilotlinkconnectionfactory.h"

#include "palm/palmdeviceconnection.h"
#include "palm/device/pilotlinkpalmdatabaseaccess.h"
#include "palm/device/pilotlinkpalmfileinstaller.h"

namespace WildPalms::Runtime {

PalmConnectionBundle makePalmConnection(KPilotLink *link, QObject *parent)
{
    PalmConnectionBundle b;
    if (!link) return b;
    b.dbAccess      = new WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess(link);
    b.fileInstaller = new WildPalms::PalmSync::PilotLinkPalmFileInstaller(link);
    b.connection    = new PalmDeviceConnection(b.dbAccess, b.fileInstaller, parent);
    return b;
}

void PalmConnectionBundle::destroy()
{
    delete connection;    connection    = nullptr;
    delete dbAccess;      dbAccess      = nullptr;
    delete fileInstaller; fileInstaller = nullptr;
}

} // namespace WildPalms::Runtime
