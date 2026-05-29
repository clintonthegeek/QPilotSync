// src/plugins/pimplugin.h
#ifndef WILDPALMS_PLUGINS_PIMPLUGIN_H
#define WILDPALMS_PLUGINS_PIMPLUGIN_H

#include "plugin.h"   // Kalburator::Plugin (libkalburator)

namespace Kalburator::Sync { class SyncBackendBase; }
namespace WildPalms::Runtime { class PalmRuntime; }

namespace WildPalms::Plugins {

/**
 * @brief WP-local intermediate base for the four PIM-view plugins
 *        (Calendar, Contacts, Memo, ToDo).
 *
 * Adds two non-pure-virtual lifecycle hooks that PalmRuntime calls
 * after constructing the canonical hub. PalmRuntime dispatches via
 * dynamic_cast<PimPlugin*>; non-PIM plugins (e.g. plucker) inherit
 * Kalburator::Plugin directly and are skipped by the cast.
 *
 * The defaults are no-ops so a PIM plugin can override only what it
 * needs.
 */
class PimPlugin : public Kalburator::Plugin {
public:
    /// Called once per PalmRuntime instance, after m_hub is constructed
    /// and registered. The plugin builds any HubFooReader it needs.
    /// hub is borrowed; lifetime is the PalmRuntime's. Takes
    /// SyncBackendBase (not SyncBackend) so the same API works for the
    /// calendar-typed SyncBackend descendants and for non-calendar
    /// backends (GenericSqliteBackend, RawFilesBackend, ...) that
    /// inherit SyncBackendBase directly post-P3 neutralization.
    virtual void setHub(Kalburator::Sync::SyncBackendBase *hub) { Q_UNUSED(hub); }

    /// Called once per PalmRuntime instance, alongside setHub. The
    /// plugin caches the pointer so createMainView can connect
    /// runtime->syncCompleted to view->refresh.
    virtual void setRuntime(WildPalms::Runtime::PalmRuntime *runtime) {
        Q_UNUSED(runtime);
    }
};

} // namespace WildPalms::Plugins

#endif // WILDPALMS_PLUGINS_PIMPLUGIN_H
