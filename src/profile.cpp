#include "profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>

const QString Profile::DEFAULT_CONFLICT_POLICY = "ask";
const QString Profile::DEFAULT_DEVICE_PATH = "/dev/ttyUSB0";
const QString Profile::DEFAULT_BAUD_RATE = "115200";
const QStringList Profile::ALL_CONDUITS = {"memos", "contacts", "calendar", "todos", "webcalendar"};

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

ConnectionMode Profile::connectionMode() const
{
    return m_connectionMode;
}

void Profile::setConnectionMode(ConnectionMode mode)
{
    m_connectionMode = mode;
}

bool Profile::autoSyncOnConnect() const
{
    return m_autoSyncOnConnect;
}

void Profile::setAutoSyncOnConnect(bool enabled)
{
    m_autoSyncOnConnect = enabled;
}

QString Profile::defaultSyncType() const
{
    return m_defaultSyncType;
}

void Profile::setDefaultSyncType(const QString &type)
{
    m_defaultSyncType = type;
}

QDateTime Profile::lastSyncTime() const
{
    return m_lastSyncTime;
}

void Profile::setLastSyncTime(const QDateTime &time)
{
    m_lastSyncTime = time;
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

QString Profile::conflictAutoResolve() const
{
    return m_conflictAutoResolve;
}

void Profile::setConflictAutoResolve(const QString &strategy)
{
    m_conflictAutoResolve = strategy;
}

QString Profile::conflictFallback() const
{
    return m_conflictFallback;
}

void Profile::setConflictFallback(const QString &fallback)
{
    m_conflictFallback = fallback;
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

QJsonObject Profile::conduitSettings(const QString &conduitId) const
{
    return m_conduitSettings.value(conduitId);
}

void Profile::setConduitSettings(const QString &conduitId, const QJsonObject &settings)
{
    m_conduitSettings[conduitId] = settings;
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
    m_deviceFingerprint.usbSerialNumber = settings.value("device/usbSerialNumber", QString()).toString();
    m_deviceFingerprint.modelName = settings.value("device/modelName", QString()).toString();
    m_deviceFingerprint.manufacturer = settings.value("device/manufacturer", QString()).toString();
    m_deviceFingerprint.romVersion = settings.value("device/romVersion", 0).toUInt();
    m_deviceFingerprint.productId = settings.value("device/productId", QString()).toString();
    m_deviceFingerprint.romSize = settings.value("device/romSize", 0).toULongLong();
    m_deviceFingerprint.ramSize = settings.value("device/ramSize", 0).toULongLong();
    m_deviceFingerprint.ramFree = settings.value("device/ramFree", 0).toULongLong();

    // Connection mode (default to KeepAlive for development)
    QString modeStr = settings.value("device/connectionMode", "keepalive").toString();
    if (modeStr == "disconnect") {
        m_connectionMode = ConnectionMode::DisconnectAfterSync;
    } else {
        m_connectionMode = ConnectionMode::KeepAlive;
    }

    // Auto-sync settings
    m_autoSyncOnConnect = settings.value("device/autoSyncOnConnect", false).toBool();
    m_defaultSyncType = settings.value("device/defaultSyncType", "hotsync").toString();

    // Sync metadata
    QString lastSyncStr = settings.value("sync/lastSyncTime", QString()).toString();
    if (!lastSyncStr.isEmpty()) {
        m_lastSyncTime = QDateTime::fromString(lastSyncStr, Qt::ISODate);
    }

    // Sync settings
    m_conflictPolicy = settings.value("sync/conflictPolicy", DEFAULT_CONFLICT_POLICY).toString();
    m_conflictAutoResolve = settings.value("sync/conflictAutoResolve", "none").toString();
    m_conflictFallback = settings.value("sync/conflictFallback", "defer").toString();

    // Conduit settings
    for (const QString &conduit : ALL_CONDUITS) {
        m_conduitEnabled[conduit] = settings.value(
            QString("conduits/%1/enabled").arg(conduit), true).toBool();

        // Load conduit-specific settings as JSON
        QString settingsStr = settings.value(
            QString("conduits/%1/settings").arg(conduit)).toString();
        if (!settingsStr.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(settingsStr.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                m_conduitSettings[conduit] = doc.object();
            }
        }
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
    if (!m_deviceFingerprint.usbSerialNumber.isEmpty()) {
        settings.setValue("device/usbSerialNumber", m_deviceFingerprint.usbSerialNumber);
    }
    if (!m_deviceFingerprint.modelName.isEmpty()) {
        settings.setValue("device/modelName", m_deviceFingerprint.modelName);
    }
    if (!m_deviceFingerprint.manufacturer.isEmpty()) {
        settings.setValue("device/manufacturer", m_deviceFingerprint.manufacturer);
    }
    if (m_deviceFingerprint.romVersion != 0) {
        settings.setValue("device/romVersion", m_deviceFingerprint.romVersion);
    }
    if (!m_deviceFingerprint.productId.isEmpty()) {
        settings.setValue("device/productId", m_deviceFingerprint.productId);
    }
    if (m_deviceFingerprint.romSize != 0) {
        settings.setValue("device/romSize", m_deviceFingerprint.romSize);
    }
    if (m_deviceFingerprint.ramSize != 0) {
        settings.setValue("device/ramSize", m_deviceFingerprint.ramSize);
    }
    if (m_deviceFingerprint.ramFree != 0) {
        settings.setValue("device/ramFree", m_deviceFingerprint.ramFree);
    }

    // Connection mode
    settings.setValue("device/connectionMode",
        m_connectionMode == ConnectionMode::DisconnectAfterSync ? "disconnect" : "keepalive");

    // Auto-sync settings
    settings.setValue("device/autoSyncOnConnect", m_autoSyncOnConnect);
    settings.setValue("device/defaultSyncType", m_defaultSyncType);

    // Sync metadata
    if (m_lastSyncTime.isValid()) {
        settings.setValue("sync/lastSyncTime", m_lastSyncTime.toString(Qt::ISODate));
    }

    // Sync settings
    settings.setValue("sync/conflictPolicy", m_conflictPolicy);
    settings.setValue("sync/conflictAutoResolve", m_conflictAutoResolve);
    settings.setValue("sync/conflictFallback", m_conflictFallback);

    // Conduit settings
    for (const QString &conduit : ALL_CONDUITS) {
        settings.setValue(QString("conduits/%1/enabled").arg(conduit),
                          m_conduitEnabled.value(conduit, true));

        // Save conduit-specific settings as JSON string
        if (m_conduitSettings.contains(conduit)) {
            QJsonDocument doc(m_conduitSettings[conduit]);
            settings.setValue(QString("conduits/%1/settings").arg(conduit),
                              QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        }
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
    return QDir(m_syncFolderPath).filePath(".wildpalms.conf");
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
