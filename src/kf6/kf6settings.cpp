#include "kf6settings.h"
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <KConfig>

KF6Settings& KF6Settings::instance()
{
    static KF6Settings instance;
    return instance;
}

KF6Settings::KF6Settings()
    : m_config(KSharedConfig::openConfig(QStringLiteral("wildpalmsrc")))
{
}

// ========== Helper Methods ==========

KConfigGroup KF6Settings::profilesGroup() const
{
    return m_config->group(QStringLiteral("Profiles"));
}

KConfigGroup KF6Settings::deviceRegistryGroup() const
{
    return m_config->group(QStringLiteral("DeviceRegistry"));
}

KConfigGroup KF6Settings::exportGroup() const
{
    return m_config->group(QStringLiteral("Export"));
}

KConfigGroup KF6Settings::windowGroup() const
{
    return m_config->group(QStringLiteral("Window"));
}

KConfigGroup KF6Settings::viewGroup() const
{
    return m_config->group(QStringLiteral("View"));
}

KConfigGroup KF6Settings::advancedGroup() const
{
    return m_config->group(QStringLiteral("Advanced"));
}

KConfigGroup KF6Settings::systemTrayGroup() const
{
    return KConfigGroup(m_config, QStringLiteral("SystemTray"));
}

KConfigGroup KF6Settings::deviceSerialsGroup() const
{
    return KConfigGroup(m_config, QStringLiteral("DeviceSerials"));
}

// ========== Profile Settings ==========

QString KF6Settings::defaultProfilePath() const
{
    return profilesGroup().readEntry("DefaultPath", QString());
}

void KF6Settings::setDefaultProfilePath(const QString &path)
{
    profilesGroup().writeEntry("DefaultPath", path);
}

QStringList KF6Settings::recentProfiles() const
{
    return profilesGroup().readEntry("Recent", QStringList());
}

void KF6Settings::addRecentProfile(const QString &path)
{
    if (path.isEmpty()) return;

    // Normalize path
    QString normalizedPath = QDir::cleanPath(path);

    QStringList recent = recentProfiles();

    // Remove if already present (will re-add at front)
    recent.removeAll(normalizedPath);

    // Add at front
    recent.prepend(normalizedPath);

    // Trim to max size
    while (recent.size() > MAX_RECENT_PROFILES) {
        recent.removeLast();
    }

    profilesGroup().writeEntry("Recent", recent);
}

void KF6Settings::removeRecentProfile(const QString &path)
{
    QStringList recent = recentProfiles();
    recent.removeAll(QDir::cleanPath(path));
    profilesGroup().writeEntry("Recent", recent);
}

void KF6Settings::clearRecentProfiles()
{
    profilesGroup().writeEntry("Recent", QStringList());
}

// ========== Device Registry ==========

void KF6Settings::registerDevice(const DeviceFingerprint &fingerprint, const QString &profilePath)
{
    if (fingerprint.isEmpty() || profilePath.isEmpty()) return;

    QString key = fingerprint.registryKey();
    deviceRegistryGroup().writeEntry(key, QDir::cleanPath(profilePath));
}

void KF6Settings::unregisterDevice(const DeviceFingerprint &fingerprint)
{
    if (fingerprint.isEmpty()) return;

    QString key = fingerprint.registryKey();
    deviceRegistryGroup().deleteEntry(key);
}

QString KF6Settings::findProfileForDevice(const DeviceFingerprint &fingerprint)
{
    if (fingerprint.isEmpty()) return QString();

    // First try exact match
    QString key = fingerprint.registryKey();
    QString result = deviceRegistryGroup().readEntry(key, QString());
    if (!result.isEmpty()) {
        return result;
    }

    // If we have a userId, try to find by userId alone (in case username changed)
    if (fingerprint.userId != 0) {
        KConfigGroup group = deviceRegistryGroup();
        QStringList keys = group.keyList();

        for (const QString &regKey : keys) {
            DeviceFingerprint regFp = DeviceFingerprint::fromRegistryKey(regKey);
            if (regFp.userId == fingerprint.userId) {
                return group.readEntry(regKey, QString());
            }
        }
    }

    return QString();
}

QMap<QString, QString> KF6Settings::deviceRegistry()
{
    QMap<QString, QString> registry;

    KConfigGroup group = deviceRegistryGroup();
    QStringList keys = group.keyList();
    for (const QString &key : keys) {
        registry[key] = group.readEntry(key, QString());
    }

    return registry;
}

void KF6Settings::clearDeviceRegistry()
{
    KConfigGroup group = deviceRegistryGroup();
    QStringList keys = group.keyList();
    for (const QString &key : keys) {
        group.deleteEntry(key);
    }
}

// ========== Export Settings ==========

QString KF6Settings::lastExportPath() const
{
    return exportGroup().readEntry("LastPath", QDir::homePath());
}

void KF6Settings::setLastExportPath(const QString &path)
{
    exportGroup().writeEntry("LastPath", path);
}

// ========== Window State ==========

QByteArray KF6Settings::windowGeometry() const
{
    return windowGroup().readEntry("Geometry", QByteArray());
}

void KF6Settings::setWindowGeometry(const QByteArray &geometry)
{
    windowGroup().writeEntry("Geometry", geometry);
}

QByteArray KF6Settings::windowState() const
{
    return windowGroup().readEntry("State", QByteArray());
}

void KF6Settings::setWindowState(const QByteArray &state)
{
    windowGroup().writeEntry("State", state);
}

QByteArray KF6Settings::splitterState() const
{
    return windowGroup().readEntry("SplitterState", QByteArray());
}

void KF6Settings::setSplitterState(const QByteArray &state)
{
    windowGroup().writeEntry("SplitterState", state);
}

bool KF6Settings::sidebarVisible() const
{
    return viewGroup().readEntry("SidebarVisible", true);
}

void KF6Settings::setSidebarVisible(bool visible)
{
    viewGroup().writeEntry("SidebarVisible", visible);
}

bool KF6Settings::logPanelVisible() const
{
    return viewGroup().readEntry("LogPanelVisible", true);
}

void KF6Settings::setLogPanelVisible(bool visible)
{
    viewGroup().writeEntry("LogPanelVisible", visible);
}

int KF6Settings::logPanelHeight() const
{
    return viewGroup().readEntry("LogPanelHeight", 200);
}

void KF6Settings::setLogPanelHeight(int height)
{
    viewGroup().writeEntry("LogPanelHeight", height);
}

// ========== Advanced Settings ==========

bool KF6Settings::debugLogging() const
{
    return advancedGroup().readEntry("DebugLogging", false);
}

void KF6Settings::setDebugLogging(bool enabled)
{
    advancedGroup().writeEntry("DebugLogging", enabled);
}

// ========== View Settings ==========

int KF6Settings::currentTabIndex() const
{
    return viewGroup().readEntry("CurrentTabIndex", 0);
}

void KF6Settings::setCurrentTabIndex(int index)
{
    viewGroup().writeEntry("CurrentTabIndex", index);
}

int KF6Settings::sidebarWidth() const
{
    return viewGroup().readEntry("SidebarWidth", 200);
}

void KF6Settings::setSidebarWidth(int width)
{
    viewGroup().writeEntry("SidebarWidth", width);
}

// ========== System Tray ==========

bool KF6Settings::minimizeToTray() const
{
    return systemTrayGroup().readEntry("MinimizeToTray", false);
}

void KF6Settings::setMinimizeToTray(bool enabled)
{
    systemTrayGroup().writeEntry("MinimizeToTray", enabled);
    m_config->sync();
}

// ========== Device Registry by USB Serial ==========

void KF6Settings::registerDeviceBySerial(const QString &usbSerial, const QString &profilePath)
{
    deviceSerialsGroup().writeEntry(usbSerial, profilePath);
    m_config->sync();
}

QString KF6Settings::findProfileBySerial(const QString &usbSerial) const
{
    return deviceSerialsGroup().readEntry(usbSerial, QString());
}

void KF6Settings::unregisterDeviceBySerial(const QString &usbSerial)
{
    deviceSerialsGroup().deleteEntry(usbSerial);
    m_config->sync();
}

void KF6Settings::sync()
{
    m_config->sync();
}
