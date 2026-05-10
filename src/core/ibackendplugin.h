#ifndef WILDPALMS_IBACKENDPLUGIN_H
#define WILDPALMS_IBACKENDPLUGIN_H

#include "iplugin.h"

#include <QIcon>
#include <QStringList>

class QWidget;

// Forward-declare upstream types so this header stays Kalburator-free.
namespace Kalburator::Sync {
    class ISyncHost;
    class IBlobBackend;
    class SyncBackend;
}

namespace Kalburator::Conflict {
    class ConflictHandler;
    struct RecordSnapshot;
}

class PalmDeviceConnection; // concrete type lands in Phase E.9 as src/palm/palmdeviceconnection.h

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
    // registers the returned handler with the SyncEngine's
    // ConflictHandlerRegistry under this plugin's backend id.
    virtual Kalburator::Conflict::ConflictHandler *createConflictHandler()
    {
        return nullptr;
    }

    // ========== Ordering hints ==========
    //
    // Replaces SyncConduitBase::runBefore / runAfter. Values are plugin
    // ids of other backend plugins.
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter() const  { return {}; }

    // ========== Main view surface (Phase E.9) ==========
    //
    // Returns a dockable main-window widget (e.g. MemoView, CalendarView).
    // Default: no view. When `hasMainView()` returns true, the main window
    // adds a KPageWidgetItem created from `createMainView(parent)` with
    // title `mainViewName()` and icon `mainViewIcon()`.
    virtual bool     hasMainView() const { return false; }
    virtual QWidget *createMainView(QWidget *parent) const
    {
        Q_UNUSED(parent)
        return nullptr;
    }
    virtual QString mainViewName() const { return {}; }
    virtual QIcon   mainViewIcon() const { return {}; }

    // ========== Conflict presentation (Phase E.9) ==========
    //
    // enrichConflictSnapshot: mutate `snapshot` in place so the
    // downstream ConflictDialog has content/metadata/contentType set
    // to plugin-friendly values. `isSourceSide` is true when the
    // snapshot holds this plugin's own wire bytes (Palm), false when
    // it carries target-backend bytes (already in the plugin's
    // canonical form, e.g. Markdown).
    //
    // formatConflictRecordHtml: produce HTML for ConflictDialog's
    // detail pane. Default implementation UTF-8-decodes
    // `snapshot.content` into a `<pre>` block.
    virtual void enrichConflictSnapshot(
        Kalburator::Conflict::RecordSnapshot &snapshot,
        bool isSourceSide) const
    {
        Q_UNUSED(snapshot)
        Q_UNUSED(isSourceSide)
    }
    virtual QString formatConflictRecordHtml(
        const Kalburator::Conflict::RecordSnapshot &snapshot) const;
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IBackendPlugin,
                    "ca.vibekoder.WildPalms.IBackendPlugin/1.0")

#endif // WILDPALMS_IBACKENDPLUGIN_H
