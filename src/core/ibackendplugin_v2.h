#ifndef WILDPALMS_IBACKENDPLUGIN_V2_H
#define WILDPALMS_IBACKENDPLUGIN_V2_H

#include "iplugin.h"

#include <QIcon>
#include <QStringList>
#include <memory>

class QWidget;

namespace Kalburator::Sync {
    class IBlobBackend;
}
namespace Kalburator::Shape {
    class DomainRegistry;
}
namespace Kalburator::Conflict {
    class ConflictHandler;
}
namespace WildPalms::Runtime {
    class PalmDeviceAccess;
}

namespace WildPalms {

/**
 * @brief Plugin contract v2 for the Palm runtime rewrite.
 *
 * Differences from v1 (ibackendplugin.h, deprecated):
 * - Returns ONLY a Palm-side IBlobBackend. PC-side backend is
 *   configured per-mapping by the user, not chosen by the plugin.
 * - Optional registerDomain() hook lets plugins introducing
 *   non-stock domains register a DomainPlugin with libkalburator's
 *   DomainRegistry at plugin-load time.
 * - Receives PalmDeviceAccess (self-marshalling) instead of the
 *   raw PalmDeviceConnection.
 */
class IBackendPluginV2 : public IPlugin {
public:
    // ── Database claims ──────────────────────────────────────────────
    virtual QStringList claimedDatabases() const = 0;

    // ── Palm-side backend ────────────────────────────────────────────
    /**
     * The plugin's IBlobBackend for the Palm side. Caller takes ownership.
     *
     * The returned backend's device access calls MUST go through the
     * supplied PalmDeviceAccess (which self-marshals to the link
     * thread). Backends that hold device pointers directly will be
     * called from the engine worker thread and break on real hardware.
     */
    virtual std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) = 0;

    // ── Optional domain registration ─────────────────────────────────
    /**
     * Called once at plugin load, before any sync begins. Allows
     * plugins introducing non-stock domains to register a DomainPlugin.
     * Default: no-op for plugins using stock domains (calendar, memo,
     * contacts, todo).
     */
    virtual void registerDomain(Kalburator::Shape::DomainRegistry &) {}

    // ── Conduit ordering ─────────────────────────────────────────────
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter()  const { return {}; }

    // ── Optional conflict handler ────────────────────────────────────
    virtual Kalburator::Conflict::ConflictHandler *createConflictHandler()
    {
        return nullptr;
    }

    // ── GUI surface ──────────────────────────────────────────────────
    virtual bool     hasMainView()    const { return false; }
    virtual QString  mainViewName()   const { return {}; }
    virtual QIcon    mainViewIcon()   const { return {}; }
    virtual QWidget *createMainView(QWidget *parent) const
    {
        Q_UNUSED(parent)
        return nullptr;
    }
};

}  // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IBackendPluginV2,
                    "ca.vibekoder.WildPalms.IBackendPluginV2/1.0")

#endif
