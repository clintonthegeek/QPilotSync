#ifndef WILDPALMS_RUNTIME_ROUTEMAPPING_H
#define WILDPALMS_RUNTIME_ROUTEMAPPING_H

#include <QHash>
#include <QString>
#include <optional>

namespace Kalburator::Sync { struct SyncMapping; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Runtime {

/// A typed translation of one persisted user SyncMapping into the pieces the
/// runtime needs to materialize a per-route LogicalCalendar.
///
/// Two kinds exist because the wizard / F.3 graph writes two shapes of
/// persisted mapping (see spec §6.2):
///   - Filtered: sourceCalendar == "palm:<domain>/<slot>" → a slot-mapped route;
///     a FilteredCollectionBackend wrapping wp-hub:<domain> with filter
///     `categories Contains <categoryName>`.
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

/// Map plugin id → canonical domain. Public for testability.
QString domainForPalmPluginId(const QString &pluginId);

/// Translate one persisted SyncMapping into a RouteSpec, or std::nullopt
/// when the mapping cannot be translated (disabled, unknown plugin, unknown
/// slot, malformed sourceCalendar).
std::optional<RouteSpec> translateRouteSpec(
    const Kalburator::Sync::SyncMapping &persisted,
    const QHash<QString, WildPalms::PalmCalendar::CategoryMappingStore*> &storesByDomain);

} // namespace WildPalms::Runtime

#endif
