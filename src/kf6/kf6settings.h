#ifndef KF6SETTINGS_H
#define KF6SETTINGS_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <KSharedConfig>
#include <KConfigGroup>

/**
 * @brief KDE Frameworks 6 settings manager using KConfig
 *
 * Provides the same interface as the original Settings class but uses
 * KSharedConfig for storage, ensuring proper KDE desktop integration.
 *
 * Settings are stored in:
 *   - Linux: ~/.config/wildpalmsrc
 *
 * This class is a drop-in replacement for the QSettings-based Settings class.
 */
class KF6Settings
{
public:
    static KF6Settings& instance();

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

    // ========== Export Settings ==========
    QString lastExportPath() const;
    void setLastExportPath(const QString &path);

    // ========== Window State ==========
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    QByteArray windowState() const;
    void setWindowState(const QByteArray &state);

    // Splitter state for the new layout
    QByteArray splitterState() const;
    void setSplitterState(const QByteArray &state);

    // Sidebar visibility
    bool sidebarVisible() const;
    void setSidebarVisible(bool visible);

    // Log panel visibility
    bool logPanelVisible() const;
    void setLogPanelVisible(bool visible);

    // Log panel height
    int logPanelHeight() const;
    void setLogPanelHeight(int height);

    // ========== System Tray ==========
    bool minimizeToTray() const;
    void setMinimizeToTray(bool enabled);

    // ========== Advanced Settings ==========
    bool debugLogging() const;
    void setDebugLogging(bool enabled);

    // ========== View Settings ==========
    // Current tab index in data browser
    int currentTabIndex() const;
    void setCurrentTabIndex(int index);

    // Sidebar width
    int sidebarWidth() const;
    void setSidebarWidth(int width);

    // Sync to disk
    void sync();

private:
    KF6Settings();
    ~KF6Settings() = default;
    KF6Settings(const KF6Settings&) = delete;
    KF6Settings& operator=(const KF6Settings&) = delete;

    KSharedConfig::Ptr m_config;

    // Helper to get config groups
    KConfigGroup profilesGroup() const;
    KConfigGroup exportGroup() const;
    KConfigGroup windowGroup() const;
    KConfigGroup viewGroup() const;
    KConfigGroup advancedGroup() const;
    KConfigGroup systemTrayGroup() const;

    void migrateLegacyDeviceRegistry();
};

#endif // KF6SETTINGS_H
