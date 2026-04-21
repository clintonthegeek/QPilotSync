#include "palmbackendconfig.h"

namespace WildPalms::PalmConflict {

QString connectionBehaviorToString(ConnectionBehavior b)
{
    switch (b) {
        case ConnectionBehavior::KeepAlive:          return QStringLiteral("KeepAlive");
        case ConnectionBehavior::DisconnectAndDefer: return QStringLiteral("DisconnectAndDefer");
        case ConnectionBehavior::TimeoutThenDefer:   return QStringLiteral("TimeoutThenDefer");
    }
    return QStringLiteral("KeepAlive");
}

ConnectionBehavior connectionBehaviorFromString(const QString &s)
{
    if (s == QStringLiteral("DisconnectAndDefer")) return ConnectionBehavior::DisconnectAndDefer;
    if (s == QStringLiteral("TimeoutThenDefer"))   return ConnectionBehavior::TimeoutThenDefer;
    return ConnectionBehavior::KeepAlive;
}

} // namespace WildPalms::PalmConflict
