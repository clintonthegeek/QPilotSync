#include "routemapping.h"

#include "palm/calendar/categorymappingstore.h"
#include "synctypes.h"

namespace WildPalms::Runtime {

QString domainForPalmPluginId(const QString &pluginId)
{
    if (pluginId == QLatin1String("calendar")) return QStringLiteral("calendar");
    if (pluginId == QLatin1String("contacts")) return QStringLiteral("contacts");
    if (pluginId == QLatin1String("memo"))     return QStringLiteral("note");
    if (pluginId == QLatin1String("todo"))     return QStringLiteral("todo");
    return {};
}

std::optional<RouteSpec> translateRouteSpec(
    const Kalburator::Sync::SyncMapping &p,
    const QHash<QString, WildPalms::PalmCalendar::CategoryMappingStore*> &stores)
{
    if (!p.enabled) return std::nullopt;

    const QString domain = domainForPalmPluginId(p.sourceBackend);
    if (domain.isEmpty()) return std::nullopt;

    RouteSpec spec;
    spec.domain             = domain;
    spec.hubCollectionId    = domain;
    spec.remoteBackendId    = p.targetBackend;
    spec.remoteCollectionId = p.targetCalendar;
    spec.lcId               = QStringLiteral("wp-route-") + p.id;

    if (p.sourceCalendar.isEmpty()) {
        spec.kind         = RouteSpec::Kind::Direct;
        spec.categoryName = QString();
        return spec;
    }

    const QString prefix = QStringLiteral("palm:") + domain + QLatin1Char('/');
    if (!p.sourceCalendar.startsWith(prefix)) return std::nullopt;
    bool ok = false;
    const int slot = p.sourceCalendar.mid(prefix.size()).toInt(&ok);
    if (!ok || slot < 0 || slot > 15) return std::nullopt;

    auto storeIt = stores.constFind(domain);
    if (storeIt == stores.constEnd() || !storeIt.value()) return std::nullopt;

    const QString dbName = (domain == QLatin1String("calendar")) ? QStringLiteral("DatebookDB")
                         : (domain == QLatin1String("contacts")) ? QStringLiteral("AddressDB")
                         : (domain == QLatin1String("todo"))     ? QStringLiteral("ToDoDB")
                         : (domain == QLatin1String("note"))     ? QStringLiteral("MemoDB")
                         : QString();
    if (dbName.isEmpty()) return std::nullopt;

    const QString name = storeIt.value()->slotName(dbName, slot);
    if (name.isEmpty()) return std::nullopt;

    spec.kind         = RouteSpec::Kind::Filtered;
    spec.categoryName = name;
    return spec;
}

} // namespace WildPalms::Runtime
