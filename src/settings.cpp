#include "settings.h"
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

Settings& Settings::instance()
{
    static Settings instance;
    return instance;
}

Settings::Settings()
    : m_settings("QPilotSync", "QPilotSync")
{
}

// ========== Profile Settings ==========

QString Settings::defaultProfilePath() const
{
    return m_settings.value("profiles/defaultPath", QString()).toString();
}

void Settings::setDefaultProfilePath(const QString &path)
{
    m_settings.setValue("profiles/defaultPath", path);
}

QStringList Settings::recentProfiles() const
{
    return m_settings.value("profiles/recent", QStringList()).toStringList();
}

void Settings::addRecentProfile(const QString &path)
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

    m_settings.setValue("profiles/recent", recent);
}

void Settings::removeRecentProfile(const QString &path)
{
    QStringList recent = recentProfiles();
    recent.removeAll(QDir::cleanPath(path));
    m_settings.setValue("profiles/recent", recent);
}

void Settings::clearRecentProfiles()
{
    m_settings.setValue("profiles/recent", QStringList());
}

// ========== Device Registry ==========

void Settings::registerDevice(const DeviceFingerprint &fingerprint, const QString &profilePath)
{
    if (fingerprint.isEmpty() || profilePath.isEmpty()) return;

    QString key = fingerprint.registryKey();
    m_settings.setValue(QString("deviceRegistry/%1").arg(key), QDir::cleanPath(profilePath));
}

void Settings::unregisterDevice(const DeviceFingerprint &fingerprint)
{
    if (fingerprint.isEmpty()) return;

    QString key = fingerprint.registryKey();
    m_settings.remove(QString("deviceRegistry/%1").arg(key));
}

QString Settings::findProfileForDevice(const DeviceFingerprint &fingerprint)
{
    if (fingerprint.isEmpty()) return QString();

    // First try exact match
    QString key = fingerprint.registryKey();
    QString result = m_settings.value(QString("deviceRegistry/%1").arg(key), QString()).toString();
    if (!result.isEmpty()) {
        return result;
    }

    // If we have a userId, try to find by userId alone (in case username changed)
    if (fingerprint.userId != 0) {
        m_settings.beginGroup("deviceRegistry");
        QStringList keys = m_settings.childKeys();
        m_settings.endGroup();

        for (const QString &regKey : keys) {
            DeviceFingerprint regFp = DeviceFingerprint::fromRegistryKey(regKey);
            if (regFp.userId == fingerprint.userId) {
                return m_settings.value(QString("deviceRegistry/%1").arg(regKey), QString()).toString();
            }
        }
    }

    return QString();
}

QMap<QString, QString> Settings::deviceRegistry()
{
    QMap<QString, QString> registry;

    m_settings.beginGroup("deviceRegistry");
    QStringList keys = m_settings.childKeys();
    for (const QString &key : keys) {
        registry[key] = m_settings.value(key).toString();
    }
    m_settings.endGroup();

    return registry;
}

void Settings::clearDeviceRegistry()
{
    m_settings.beginGroup("deviceRegistry");
    m_settings.remove("");
    m_settings.endGroup();
}

// ========== Export Settings ==========

QString Settings::lastExportPath() const
{
    return m_settings.value("export/lastPath", QDir::homePath()).toString();
}

void Settings::setLastExportPath(const QString &path)
{
    m_settings.setValue("export/lastPath", path);
}

// ========== Window State ==========

QByteArray Settings::windowGeometry() const
{
    return m_settings.value("window/geometry").toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &geometry)
{
    m_settings.setValue("window/geometry", geometry);
}

QByteArray Settings::windowState() const
{
    return m_settings.value("window/state").toByteArray();
}

void Settings::setWindowState(const QByteArray &state)
{
    m_settings.setValue("window/state", state);
}

// ========== Advanced Settings ==========

bool Settings::debugLogging() const
{
    return m_settings.value("advanced/debugLogging", false).toBool();
}

void Settings::setDebugLogging(bool enabled)
{
    m_settings.setValue("advanced/debugLogging", enabled);
}

void Settings::sync()
{
    m_settings.sync();
}
