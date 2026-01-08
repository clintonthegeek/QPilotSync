#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>
#include <QStringList>
#include <QSettings>
#include <QMap>

#include "profile.h"  // For DeviceFingerprint

/**
 * @brief Global application settings manager using QSettings
 *
 * Persists user preferences that are NOT profile-specific.
 * Profile-specific settings (device, sync folder, conflict policy, conduits)
 * are stored in the Profile class within the sync folder itself.
 *
 * Uses QSettings for platform-appropriate storage:
 *   - Linux: ~/.config/QPilotSync/QPilotSync.conf
 *   - Windows: Registry
 *   - macOS: plist
 */
class Settings
{
public:
    static Settings& instance();

    // ========== Profile Settings ==========

    // Default profile path - loaded automatically on startup
    QString defaultProfilePath() const;
    void setDefaultProfilePath(const QString &path);

    // Recent profiles list (most recent first)
    QStringList recentProfiles() const;
    void addRecentProfile(const QString &path);
    void removeRecentProfile(const QString &path);
    void clearRecentProfiles();

    // Maximum number of recent profiles to remember
    static const int MAX_RECENT_PROFILES = 10;

    // ========== Device Registry ==========
    // Maps device fingerprints to profile paths
    // This allows us to identify which profile a connected device belongs to

    // Register a device with a profile
    void registerDevice(const DeviceFingerprint &fingerprint, const QString &profilePath);

    // Unregister a device (when profile is deleted or device is unassigned)
    void unregisterDevice(const DeviceFingerprint &fingerprint);

    // Look up which profile a device belongs to (returns empty string if not found)
    QString findProfileForDevice(const DeviceFingerprint &fingerprint);

    // Get all registered devices (fingerprint key -> profile path)
    QMap<QString, QString> deviceRegistry();

    // Clear all device registrations
    void clearDeviceRegistry();

    // ========== Export Settings ==========
    QString lastExportPath() const;
    void setLastExportPath(const QString &path);

    // ========== Window State ==========
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    QByteArray windowState() const;
    void setWindowState(const QByteArray &state);

    // ========== Advanced Settings ==========
    bool debugLogging() const;
    void setDebugLogging(bool enabled);

    // Sync to disk
    void sync();

private:
    Settings();
    ~Settings() = default;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    QSettings m_settings;
};

#endif // SETTINGS_H
