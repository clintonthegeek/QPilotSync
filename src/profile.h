#ifndef PROFILE_H
#define PROFILE_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>

/**
 * @brief Connection mode for Palm device
 *
 * Controls how the connection is managed between operations.
 */
enum class ConnectionMode
{
    /**
     * Keep connection alive using periodic tickle (dlp_GetSysDateTime).
     * Allows multiple operations without reconnecting.
     * Note: Palm screen shows last operation until disconnect.
     */
    KeepAlive,

    /**
     * Disconnect after each operation completes (traditional HotSync).
     * Palm shows "HotSync Complete" and closes cleanly.
     * Requires pressing HotSync button again for next operation.
     */
    DisconnectAfterSync
};

/**
 * @brief Device fingerprint for identifying a specific Palm device
 *
 * A fingerprint uniquely identifies a Palm device using its USB serial number
 * (most reliable), User ID (a 32-bit value set on first sync), and username.
 * This allows us to detect when the wrong device is connected to a profile.
 */
struct DeviceFingerprint
{
    // --- Identity fields (used for matching) ---
    quint32 userId = 0;
    QString userName;
    QString usbSerialNumber;  // USB descriptor serial (e.g. "L0JG14I11398")

    // --- Informational fields (NOT used for identity matching) ---
    QString modelName;         // from CardInfo.name (e.g. "Palm m515")
    QString manufacturer;      // from CardInfo.manufacturer (e.g. "Palm, Inc.")
    quint32 romVersion = 0;    // from SysInfo.romVersion
    QString productId;         // from SysInfo.prodID
    quint64 romSize = 0;       // ROM bytes
    quint64 ramSize = 0;       // total RAM bytes
    quint64 ramFree = 0;       // free RAM bytes (snapshot at last connect)

    bool isValid() const { return userId != 0 || !userName.isEmpty() || !usbSerialNumber.isEmpty(); }
    bool isEmpty() const { return userId == 0 && userName.isEmpty() && usbSerialNumber.isEmpty(); }

    // Match another fingerprint (USB serial takes priority, then userId, then userName)
    bool matches(const DeviceFingerprint &other) const {
        // USB serial number is the most reliable identifier
        if (!usbSerialNumber.isEmpty() && !other.usbSerialNumber.isEmpty()) {
            return usbSerialNumber == other.usbSerialNumber;
        }
        if (userId != 0 && other.userId != 0) {
            return userId == other.userId;
        }
        return !userName.isEmpty() && userName == other.userName;
    }

    // Create a display string for the fingerprint
    // Uses model name as primary label when available
    QString displayString() const {
        if (isEmpty()) return QString();

        // Build identity suffix: "Clinton, ID: 12345"
        QString identity;
        if (!userName.isEmpty() && userId != 0) {
            identity = QString("%1, ID: %2").arg(userName).arg(userId);
        } else if (!userName.isEmpty()) {
            identity = userName;
        } else if (userId != 0) {
            identity = QString("ID: %1").arg(userId);
        }

        // Use model name as primary label if available
        if (!modelName.isEmpty()) {
            if (identity.isEmpty()) return modelName;
            return QString("%1 (%2)").arg(modelName, identity);
        }

        // Fallback: identity-only display
        if (!usbSerialNumber.isEmpty()) {
            if (identity.isEmpty()) return QString("S/N: %1").arg(usbSerialNumber);
            return QString("%1 (S/N: %2)").arg(identity, usbSerialNumber);
        }
        return identity;
    }

    // Format Palm OS version from romVersion field (e.g. "5.2.1")
    QString palmOSVersionString() const {
        if (romVersion == 0) return QString();
        int major = (romVersion >> 16) & 0xFF;
        int minor = (romVersion >> 8) & 0xFF;
        int patch = romVersion & 0xFF;
        if (patch == 0) return QString("%1.%2").arg(major).arg(minor);
        return QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
    }

    // Format a byte count as a human-readable size (e.g. "16 MB")
    static QString formatMemorySize(quint64 bytes) {
        if (bytes == 0) return QString();
        if (bytes >= 1024 * 1024) return QString("%1 MB").arg(bytes / (1024 * 1024));
        if (bytes >= 1024) return QString("%1 KB").arg(bytes / 1024);
        return QString("%1 B").arg(bytes);
    }

    // Check if extended device info (model/memory/OS) is available
    bool hasExtendedInfo() const {
        return !modelName.isEmpty() || romVersion != 0 || ramSize != 0;
    }

    // Create a unique key for registry lookups (format: userId:userName:serial)
    QString registryKey() const {
        return QString("%1:%2:%3").arg(userId).arg(userName, usbSerialNumber);
    }

    static DeviceFingerprint fromRegistryKey(const QString &key) {
        DeviceFingerprint fp;
        int firstColon = key.indexOf(':');
        if (firstColon > 0) {
            fp.userId = key.left(firstColon).toUInt();
            int secondColon = key.indexOf(':', firstColon + 1);
            if (secondColon > 0) {
                fp.userName = key.mid(firstColon + 1, secondColon - firstColon - 1);
                fp.usbSerialNumber = key.mid(secondColon + 1);
            } else {
                fp.userName = key.mid(firstColon + 1);
            }
        }
        return fp;
    }
};

/**
 * @brief Profile represents a sync profile with its settings
 *
 * Profile settings are stored in the sync folder itself as .wildpalms.conf,
 * making profiles portable - you can move the entire sync folder and the
 * settings travel with it.
 *
 * Each profile corresponds to:
 *   - A specific Palm device (identified by fingerprint)
 *   - A sync folder with memos/, contacts/, calendar/, todos/
 *   - Device-specific connection settings (port, baud rate)
 */
class Profile
{
public:
    /**
     * @brief Create a profile for the given sync folder path
     * @param syncFolderPath Path to the sync folder (e.g., ~/PalmSync)
     */
    explicit Profile(const QString &syncFolderPath = QString());

    // Profile location
    QString syncFolderPath() const { return m_syncFolderPath; }
    void setSyncFolderPath(const QString &path);

    // Profile identity
    QString name() const;
    void setName(const QString &name);

    // Check if profile is valid (folder exists and is writable)
    bool isValid() const;

    // Check if profile config file exists
    bool exists() const;

    // ========== Device Settings ==========

    // Device connection settings
    QString devicePath() const;
    void setDevicePath(const QString &path);

    QString baudRate() const;
    void setBaudRate(const QString &rate);

    // Device fingerprint - identifies which Palm this profile is for
    DeviceFingerprint deviceFingerprint() const;
    void setDeviceFingerprint(const DeviceFingerprint &fingerprint);

    // Check if this profile has a registered device
    bool hasRegisteredDevice() const;

    // Connection mode - how to manage connection between operations
    ConnectionMode connectionMode() const;
    void setConnectionMode(ConnectionMode mode);

    // Auto-sync after connection
    bool autoSyncOnConnect() const;
    void setAutoSyncOnConnect(bool enabled);

    // Default sync type for auto-sync
    QString defaultSyncType() const;  // "hotsync" or "fullsync"
    void setDefaultSyncType(const QString &type);

    // Last sync timestamp (overall, not per-conduit)
    QDateTime lastSyncTime() const;
    void setLastSyncTime(const QDateTime &time);

    // ========== Sync Settings ==========

    // Conflict resolution policy (legacy - maps to autoResolve)
    QString conflictPolicy() const;
    void setConflictPolicy(const QString &policy);

    // Auto-resolve strategy: "none", "palm_wins", "pc_wins", "newer_wins", "older_wins", "duplicate"
    QString conflictAutoResolve() const;
    void setConflictAutoResolve(const QString &strategy);

    // Fallback behavior: "defer", "skip", "use_default", "abort"
    QString conflictFallback() const;
    void setConflictFallback(const QString &fallback);

    // Conduit enable/disable
    bool conduitEnabled(const QString &conduitId) const;
    void setConduitEnabled(const QString &conduitId, bool enabled);
    QStringList enabledConduits() const;

    // Conduit-specific settings
    QJsonObject conduitSettings(const QString &conduitId) const;
    void setConduitSettings(const QString &conduitId, const QJsonObject &settings);

    // ========== Persistence ==========

    // Load settings from .wildpalms.conf in the sync folder
    bool load();

    // Save settings to .wildpalms.conf in the sync folder
    bool save();

    // Initialize a new profile (create directories and default config)
    bool initialize();

    // Get the path to the profile config file
    QString configFilePath() const;

    // Get the path to the state directory
    QString stateDirectoryPath() const;

    // Get the path to the install folder (for .prc/.pdb files to install)
    QString installFolderPath() const;

private:
    QString m_syncFolderPath;
    QString m_name;

    // Device settings
    QString m_devicePath;
    QString m_baudRate;
    DeviceFingerprint m_deviceFingerprint;
    ConnectionMode m_connectionMode = ConnectionMode::KeepAlive;
    bool m_autoSyncOnConnect = false;
    QString m_defaultSyncType = "hotsync";

    // Sync metadata
    QDateTime m_lastSyncTime;

    // Sync settings
    QString m_conflictPolicy;
    QString m_conflictAutoResolve = "none";
    QString m_conflictFallback = "defer";
    QMap<QString, bool> m_conduitEnabled;
    QMap<QString, QJsonObject> m_conduitSettings;

    // Default values
    static const QString DEFAULT_CONFLICT_POLICY;
    static const QString DEFAULT_DEVICE_PATH;
    static const QString DEFAULT_BAUD_RATE;
    static const QStringList ALL_CONDUITS;
};

#endif // PROFILE_H
