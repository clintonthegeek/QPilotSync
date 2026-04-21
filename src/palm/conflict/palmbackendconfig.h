#ifndef WILDPALMS_CONFLICT_PALMBACKENDCONFIG_H
#define WILDPALMS_CONFLICT_PALMBACKENDCONFIG_H

#include <QString>

namespace WildPalms::PalmConflict {

/**
 * @brief HotSync connection-persistence policy during conflict resolution.
 *
 * Palm DLP sessions keep the device in a "listening" mode; leaving the
 * session open while a user ponders a conflict prompt ties up the
 * cradle. These three modes cover the realistic choices:
 */
enum class ConnectionBehavior {
    KeepAlive,             ///< Session stays open through the prompt; caller
                           ///< issues periodic DLP tickles.
    DisconnectAndDefer,    ///< Session closes immediately; conflict is
                           ///< persisted for later resolution.
    TimeoutThenDefer,      ///< Session stays open for `connectionTimeoutSeconds`,
                           ///< then closes and defers.
};

QString connectionBehaviorToString(ConnectionBehavior b);
ConnectionBehavior connectionBehaviorFromString(const QString &s);

/**
 * @brief Palm-specific config consulted by `PalmConflictHandler`.
 *
 * Every field has a sane default so callers can default-construct and
 * mutate only what they care about. This struct is a plain-old-data
 * holder — no Qt MOC, no signals, no ownership semantics.
 *
 * Stored on the PalmBackend instance (later — E.16 wires it). The
 * handler reads it via a borrowed pointer.
 */
struct PalmBackendConfig {
    ConnectionBehavior connectionBehavior  = ConnectionBehavior::KeepAlive;
    int                connectionTimeoutSeconds = 60;
    int                hotSyncTickleIntervalSeconds = 5;
    QString            userName;  ///< Set from dlp_ReadUserInfo at session
                                  ///< start. Empty in test/default contexts.

    bool operator==(const PalmBackendConfig &other) const = default;
};

} // namespace WildPalms::PalmConflict

#endif // WILDPALMS_CONFLICT_PALMBACKENDCONFIG_H
