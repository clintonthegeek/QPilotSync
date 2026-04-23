#ifndef WILDPALMS_IBACKENDPLUGIN_H
#define WILDPALMS_IBACKENDPLUGIN_H

#include "iplugin.h"

#include <QStringList>

// Forward-declare upstream types so this header stays Kalburator-free.
namespace Kalburator::Sync {
    class ISyncHost;
    class IBlobBackend;
    class SyncBackend;
    namespace QSyncCore {
        class ConflictHandler;
    }
}

class PalmDeviceConnection; // concrete type lands in a later sub-phase (E.10+)

namespace WildPalms {

/**
 * @brief Plugin that provides one or more libkalburator backends.
 *
 * The manager calls createBackends() once per session; the plugin
 * returns a ProvidedBackends struct holding (at minimum) an
 * IBlobBackend* for transport and optionally a typed SyncBackend*
 * for record-typed consumers (e.g. PalmCalendarBackend).
 *
 * Ownership: the caller (BackendPluginManager) takes ownership of
 * the returned backend pointers and parents them to a suitable
 * QObject. The plugin MUST construct each backend fresh per call.
 */
class IBackendPlugin : public IPlugin
{
public:
    // ========== Database claims ==========
    //
    // Which Palm databases this plugin claims. Keys mirror the legacy
    // X-WildPalms-PalmDatabases / X-WildPalms-ClaimDescriptions JSON.
    virtual QStringList claimedDatabases() const = 0;

    // ========== Backend construction ==========
    struct ProvidedBackends {
        Kalburator::Sync::IBlobBackend *blob     = nullptr; // required
        Kalburator::Sync::SyncBackend  *calendar = nullptr; // optional
    };

    virtual ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                            PalmDeviceConnection         *device) = 0;

    // ========== Optional conflict handler ==========
    //
    // Return nullptr to use the default handler; otherwise the manager
    // registers the returned handler with the SyncCoordinator's
    // ConflictHandlerRegistry under this plugin's backend id.
    virtual Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler()
    {
        return nullptr;
    }

    // ========== Ordering hints ==========
    //
    // Replaces SyncConduitBase::runBefore / runAfter. Values are plugin
    // ids of other backend plugins.
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter() const  { return {}; }
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IBackendPlugin,
                    "ca.vibekoder.WildPalms.IBackendPlugin/1.0")

#endif // WILDPALMS_IBACKENDPLUGIN_H
