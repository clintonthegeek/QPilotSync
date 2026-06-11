#ifndef WILDPALMS_RUNTIME_ROUTEMAPPING_H
#define WILDPALMS_RUNTIME_ROUTEMAPPING_H

#include <QList>
#include <QString>
#include <optional>

namespace Kalburator::Sync { struct SyncMapping; }
namespace WildPalms::Plugins { class PimPlugin; }

namespace WildPalms::Runtime {

/// A typed translation of one persisted user SyncMapping into the pieces the
/// runtime needs to materialize a per-route LogicalCalendar.
///
/// Two kinds exist because the wizard / F.3 graph writes two shapes of
/// persisted mapping (see spec §6.2):
///   - Filtered: sourceCalendar == "palm:<domain>/name:<categoryName>" → a
///     category-mapped route; a FilteredCollectionBackend wrapping
///     wp-hub:<domain> with filter `categories Contains <categoryName>`.
///   - Direct:   sourceCalendar.isEmpty() → a wildcard route; the LC's Primary
///     binds directly to wp-hub:<domain> (no filter, no wrapper backend).
struct RouteSpec {
    enum class Kind { Filtered, Direct };
    Kind     kind;
    QString  domain;             ///< "calendar"/"contacts"/"todo"/"note"
    QString  hubCollectionId;    ///< the hub's per-domain collection id (== domain today)
    QString  categoryName;       ///< empty for Direct
    QString  remoteBackendId;    ///< from persisted.targetBackend
    QString  remoteCollectionId; ///< from persisted.targetCalendar
    QString  lcId;               ///< "wp-route-<persisted.id>"
};

inline bool operator==(const RouteSpec &a, const RouteSpec &b) {
    return a.kind == b.kind && a.domain == b.domain
        && a.hubCollectionId == b.hubCollectionId
        && a.categoryName == b.categoryName
        && a.remoteBackendId == b.remoteBackendId
        && a.remoteCollectionId == b.remoteCollectionId
        && a.lcId == b.lcId;
}

/// Substrate A3: per-route binding state. A well-formed category route always
/// produces a spec (hub<->remote filtering works by name); the status reports
/// whether the category is bound to a device slot yet, instead of today's
/// silent drop.
enum class RouteStatus {
    Active,            ///< runnable; category (if any) bound on-device
    WaitingForDevice,  ///< named category; no device snapshot yet
    NoFreeSlot,        ///< named category; device table full, not placed
    NotARoute,         ///< disabled, unknown conduit, or malformed row
};

struct RouteTranslation {
    std::optional<RouteSpec> spec;     ///< set whenever the row is well-formed
    RouteStatus status = RouteStatus::NotARoute;
    QString categoryName;              ///< parsed name for Filtered rows
};

/// Translate one persisted row against the registered conduit descriptors.
/// Category rows use the names-first form "palm:<domain>/name:<categoryName>"
/// (substrate A3). Never silently drops a well-formed row: the status says
/// why a route is not (yet) fully bound.
RouteTranslation translateRouteSpec(
    const Kalburator::Sync::SyncMapping &p,
    const QList<WildPalms::Plugins::PimPlugin*> &conduits);

} // namespace WildPalms::Runtime

#endif
