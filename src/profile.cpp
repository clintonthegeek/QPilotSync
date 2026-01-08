#include "profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

const QString Profile::DEFAULT_CONFLICT_POLICY = "ask";
const QString Profile::DEFAULT_DEVICE_PATH = "/dev/ttyUSB0";
const QString Profile::DEFAULT_BAUD_RATE = "115200";
const QStringList Profile::ALL_CONDUITS = {"memos", "contacts", "calendar", "todos"};

Profile::Profile(const QString &syncFolderPath)
    : m_syncFolderPath(syncFolderPath)
    , m_devicePath(DEFAULT_DEVICE_PATH)
    , m_baudRate(DEFAULT_BAUD_RATE)
    , m_conflictPolicy(DEFAULT_CONFLICT_POLICY)
{
    // Enable all conduits by default
    for (const QString &conduit : ALL_CONDUITS) {
        m_conduitEnabled[conduit] = true;
    }

    // Try to load existing settings if path is set
    if (!m_syncFolderPath.isEmpty()) {
        load();
    }
}

void Profile::setSyncFolderPath(const QString &path)
{
    m_syncFolderPath = path;
}

QString Profile::name() const
{
    if (!m_name.isEmpty()) {
        return m_name;
    }
    // Default to folder name
    if (!m_syncFolderPath.isEmpty()) {
        return QFileInfo(m_syncFolderPath).fileName();
    }
    return QString();
}

void Profile::setName(const QString &name)
{
    m_name = name;
}

bool Profile::isValid() const
{
    if (m_syncFolderPath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_syncFolderPath);
    return info.exists() && info.isDir() && info.isWritable();
}

bool Profile::exists() const
{
    return QFile::exists(configFilePath());
}

// ========== Device Settings ==========

QString Profile::devicePath() const
{
    return m_devicePath;
}

void Profile::setDevicePath(const QString &path)
{
    m_devicePath = path;
}

QString Profile::baudRate() const
{
    return m_baudRate;
}

void Profile::setBaudRate(const QString &rate)
{
    m_baudRate = rate;
}

DeviceFingerprint Profile::deviceFingerprint() const
{
    return m_deviceFingerprint;
}

void Profile::setDeviceFingerprint(const DeviceFingerprint &fingerprint)
{
    m_deviceFingerprint = fingerprint;
}

bool Profile::hasRegisteredDevice() const
{
    return m_deviceFingerprint.isValid();
}

// ========== Sync Settings ==========

QString Profile::conflictPolicy() const
{
    return m_conflictPolicy;
}

void Profile::setConflictPolicy(const QString &policy)
{
    m_conflictPolicy = policy;
}

bool Profile::conduitEnabled(const QString &conduitId) const
{
    return m_conduitEnabled.value(conduitId, true);
}

void Profile::setConduitEnabled(const QString &conduitId, bool enabled)
{
    m_conduitEnabled[conduitId] = enabled;
}

QStringList Profile::enabledConduits() const
{
    QStringList enabled;
    for (const QString &conduit : ALL_CONDUITS) {
        if (conduitEnabled(conduit)) {
            enabled << conduit;
        }
    }
    return enabled;
}

bool Profile::load()
{
    QString configPath = configFilePath();
    if (!QFile::exists(configPath)) {
        return false;
    }

    QSettings settings(configPath, QSettings::IniFormat);

    // Profile identity
    m_name = settings.value("profile/name", QString()).toString();

    // Device settings
    m_devicePath = settings.value("device/path", DEFAULT_DEVICE_PATH).toString();
    m_baudRate = settings.value("device/baudRate", DEFAULT_BAUD_RATE).toString();
    m_deviceFingerprint.userId = settings.value("device/userId", 0).toUInt();
    m_deviceFingerprint.userName = settings.value("device/userName", QString()).toString();

    // Sync settings
    m_conflictPolicy = settings.value("sync/conflictPolicy", DEFAULT_CONFLICT_POLICY).toString();

    // Conduit settings
    for (const QString &conduit : ALL_CONDUITS) {
        m_conduitEnabled[conduit] = settings.value(
            QString("conduits/%1/enabled").arg(conduit), true).toBool();
    }

    return true;
}

bool Profile::save()
{
    if (m_syncFolderPath.isEmpty()) {
        return false;
    }

    // Ensure directory exists
    QDir dir(m_syncFolderPath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            return false;
        }
    }

    QString configPath = configFilePath();
    QSettings settings(configPath, QSettings::IniFormat);

    // Profile identity
    if (!m_name.isEmpty()) {
        settings.setValue("profile/name", m_name);
    }

    // Device settings
    settings.setValue("device/path", m_devicePath);
    settings.setValue("device/baudRate", m_baudRate);
    if (m_deviceFingerprint.userId != 0) {
        settings.setValue("device/userId", m_deviceFingerprint.userId);
    }
    if (!m_deviceFingerprint.userName.isEmpty()) {
        settings.setValue("device/userName", m_deviceFingerprint.userName);
    }

    // Sync settings
    settings.setValue("sync/conflictPolicy", m_conflictPolicy);

    // Conduit settings
    for (const QString &conduit : ALL_CONDUITS) {
        settings.setValue(QString("conduits/%1/enabled").arg(conduit),
                          m_conduitEnabled.value(conduit, true));
    }

    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool Profile::initialize()
{
    if (m_syncFolderPath.isEmpty()) {
        return false;
    }

    QDir dir(m_syncFolderPath);

    // Create main directory
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            return false;
        }
    }

    // Create subdirectories
    dir.mkpath("memos");
    dir.mkpath("contacts");
    dir.mkpath("calendar");
    dir.mkpath("todos");
    dir.mkpath("install");
    dir.mkpath("install/installed");
    dir.mkpath(".state");

    // Save default settings
    return save();
}

QString Profile::configFilePath() const
{
    if (m_syncFolderPath.isEmpty()) {
        return QString();
    }
    return QDir(m_syncFolderPath).filePath(".qpilotsync.conf");
}

QString Profile::stateDirectoryPath() const
{
    if (m_syncFolderPath.isEmpty()) {
        return QString();
    }
    return QDir(m_syncFolderPath).filePath(".state");
}

QString Profile::installFolderPath() const
{
    if (m_syncFolderPath.isEmpty()) {
        return QString();
    }
    return QDir(m_syncFolderPath).filePath("install");
}
