#include "domainfilter.h"

#include <collectioninfo.h>

namespace WildPalms::Wizard {

bool collectionMatchesDomain(const Kalburator::Sync::CollectionInfo &c,
                             const QString &pluginId)
{
    if (pluginId == QStringLiteral("calendar"))
        return c.type == QStringLiteral("calendar")
            || c.contentTypes.contains(QStringLiteral("VEVENT"));
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
