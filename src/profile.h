#ifndef PROFILE_H
#define PROFILE_H

#include <QString>
#include <QStringList>
#include <QMap>

/**
 * @brief Device fingerprint for identifying a specific Palm device
 *
 * A fingerprint uniquely identifies a Palm device using its User ID
 * (a 32-bit value set on first sync) and username. This allows us to
 * detect when the wrong device is connected to a profile.
 */
struct DeviceFingerprint
{
    quint32 userId = 0;
    QString userName;

    bool isValid() const { return userId != 0 || !userName.isEmpty(); }
    bool isEmpty() const { return userId == 0 && userName.isEmpty(); }

    // Match another fingerprint (userId takes priority if both are set)
    bool matches(const DeviceFingerprint &other) const {
        if (userId != 0 && other.userId != 0) {
            return userId == other.userId;
        }
        // Fall back to username match if no userId
        return !userName.isEmpty() && userName == other.userName;
    }

    // Create a display string for the fingerprint
    QString displayString() const {
        if (isEmpty()) return QString();
        if (userName.isEmpty()) return QString("ID: %1").arg(userId);
        if (userId == 0) return userName;
        return QString("%1 (ID: %2)").arg(userName).arg(userId);
    }

    // Create a unique key for registry lookups
    QString registryKey() const {
        return QString("%1:%2").arg(userId).arg(userName);
    }

    static DeviceFingerprint fromRegistryKey(const QString &key) {
        DeviceFingerprint fp;
        int colonPos = key.indexOf(':');
        if (colonPos > 0) {
            fp.userId = key.left(colonPos).toUInt();
            fp.userName = key.mid(colonPos + 1);
        }
        return fp;
    }
};

/**
 * @brief Profile represents a sync profile with its settings
 *
 * Profile settings are stored in the sync folder itself as .qpilotsync.conf,
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

    // ========== Sync Settings ==========

    // Conflict resolution policy
    QString conflictPolicy() const;
    void setConflictPolicy(const QString &policy);

    // Conduit enable/disable
    bool conduitEnabled(const QString &conduitId) const;
    void setConduitEnabled(const QString &conduitId, bool enabled);
    QStringList enabledConduits() const;

    // ========== Persistence ==========

    // Load settings from .qpilotsync.conf in the sync folder
    bool load();

    // Save settings to .qpilotsync.conf in the sync folder
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

    // Sync settings
    QString m_conflictPolicy;
    QMap<QString, bool> m_conduitEnabled;

    // Default values
    static const QString DEFAULT_CONFLICT_POLICY;
    static const QString DEFAULT_DEVICE_PATH;
    static const QString DEFAULT_BAUD_RATE;
    static const QStringList ALL_CONDUITS;
};

#endif // PROFILE_H
