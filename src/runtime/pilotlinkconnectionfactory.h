#ifndef WILDPALMS_RUNTIME_PILOTLINKCONNECTIONFACTORY_H
#define WILDPALMS_RUNTIME_PILOTLINKCONNECTIONFACTORY_H

#include <QObject>

class KPilotLink;
class PalmDeviceConnection;

namespace WildPalms::PalmDevice { class PilotLinkPalmDatabaseAccess; }
namespace WildPalms::PalmSync   { class PilotLinkPalmFileInstaller; }

namespace WildPalms::Runtime {

/// Bundle of pilot-link wrappers + concrete PalmDeviceConnection
/// owned by the application runtime. Constructing it via
/// makePalmConnection() side-steps the WildPalmsCore <-> WildPalmsPalmDevice
/// AUTOMOC cycle: the factory lives in WildPalmsRuntime which
/// PRIVATE-links the device libs without WildPalmsCore having to.
struct PalmConnectionBundle {
    WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess *dbAccess     = nullptr;
    WildPalms::PalmSync::PilotLinkPalmFileInstaller    *fileInstaller = nullptr;
    PalmDeviceConnection                               *connection   = nullptr;

    /// Free all three in the right order. Caller is responsible.
    void destroy();
};

/// Construct dbAccess + fileInstaller from `link`, then build a
/// PalmDeviceConnection parented to `parent`. Caller owns the
/// returned bundle; call .destroy() when the link goes away.
PalmConnectionBundle makePalmConnection(KPilotLink *link, QObject *parent);

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_PILOTLINKCONNECTIONFACTORY_H
