#include "domainfilter.h"

#include <collectioninfo.h>

namespace WildPalms::Wizard {

bool collectionMatchesDomain(const Kalburator::Sync::CollectionInfo &c,
                             const QString &pluginId)
{
    if (pluginId == QStringLiteral("calendar")) {
        // When the provider reports component caps, they are authoritative:
        // DAV providers type every collection "calendar", including
        // tasks-only ones. Bare type match is the fallback for providers
        // that don't report contentTypes (e.g. Akonadi).
        if (!c.contentTypes.isEmpty())
            return c.contentTypes.contains(QStringLiteral("VEVENT"));
        return c.type == QStringLiteral("calendar");
    }
    if (pluginId == QStringLiteral("todo"))
        return c.type == QStringLiteral("todos")
            || c.contentTypes.contains(QStringLiteral("VTODO"));
    if (pluginId == QStringLiteral("contacts"))
        return c.type == QStringLiteral("contacts")
            || c.contentTypes.contains(QStringLiteral("VCARD"));
    if (pluginId == QStringLiteral("memo"))
        return c.type == QStringLiteral("memos");
    return false;
}

}  // namespace WildPalms::Wizard
