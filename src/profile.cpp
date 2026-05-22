#include "profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QDateTime>

// K.8b T9/T10: accounts subgroup + sidecar migration
#include <KConfig>
#include <KConfigGroup>
#include "backendconfiguration.h"

const QString Profile::DEFAULT_CONFLICT_POLICY = "ask";
const QString Profile::DEFAULT_DEVICE_PATH = "/dev/ttyUSB0";
const QString Profile::DEFAULT_BAUD_RATE = "115200";


Profile::Profile(const QString &syncFolderPath)
    : m_syncFolderPath(syncFolderPath)
    , m_devicePath(DEFAULT_DEVICE_PATH)
    , m_baudRate(DEFAULT_BAUD_RATE)
    , m_conflictPolicy(DEFAULT_CONFLICT_POLICY)
{
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

QString Profile::id() const
{
    return m_id;
}

int Profile::schemaVersion() const
{
    return m_schemaVersion;
}

QString Profile::defaultPathForId(const QString &id)
{
    return QDir::homePath() + QStringLiteral("/.wildpalms/") + id;
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

QString Profile::conflictPromptStrategy() const
{
    return m_conflictPromptStrategy;
}

void Profile::setConflictPromptStrategy(const QString &strategy)
{
    m_conflictPromptStrategy = strategy;
}

QString Profile::conflictConnectionBehavior() const
{
    return m_conflictConnectionBehavior;
}

void Profile::setConflictConnectionBehavior(const QString &behavior)
{
    m_conflictConnectionBehavior = behavior;
}

int Profile::conflictTimeoutSeconds() const
{
    return m_conflictTimeoutSeconds;
}

void Profile::setConflictTimeoutSeconds(int seconds)
{
    m_conflictTimeoutSeconds = qBound(15, seconds, 300);
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
    m_conflictPromptStrategy = settings.value("sync/conflictPromptStrategy", "always_ask").toString();
    m_conflictConnectionBehavior = settings.value("sync/conflictConnectionBehavior", "keep_alive").toString();
    m_conflictTimeoutSeconds = settings.value("sync/conflictTimeoutSeconds", 60).toInt();

    // G.7 Task 54: SyncMappings — load raw JSON
    {
        m_syncMappingsJson = QJsonArray{};
        const QString jsonStr = settings.value(QStringLiteral("syncMappings/json")).toString();
        if (!jsonStr.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isArray())
                m_syncMappingsJson = doc.array();
        }
    }

    // K.8b T9: Accounts subgroup
    {
        m_accounts.clear();
        settings.beginGroup(QStringLiteral("accounts"));
        const QStringList acctIds = settings.childGroups();
        for (const QString &acctId : acctIds) {
            settings.beginGroup(acctId);
            Kalburator::Sync::BackendConfiguration bc;
            bc.id = acctId;
            bc.type = settings.value(QStringLiteral("type"), QString()).toString();
            bc.displayName = settings.value(QStringLiteral("displayName"), QString()).toString();
            bc.enabled = settings.value(QStringLiteral("enabled"), true).toBool();
            const QString paramsStr = settings.value(QStringLiteral("params")).toString();
            if (!paramsStr.isEmpty()) {
                const QJsonDocument doc = QJsonDocument::fromJson(paramsStr.toUtf8());
                if (doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                        bc.connectionParams[it.key()] = it.value().toVariant();
                    }
                }
            }
            m_accounts.append(bc);
            settings.endGroup();
        }
        settings.endGroup();
    }

    // K.8b T10: One-shot sidecar migration (added in T10)
    // If accounts is empty AND old .wildpalms.providers sidecar exists, migrate it.
    {
        const QString sidecarPath = QDir(m_syncFolderPath).filePath(QStringLiteral(".wildpalms.providers"));
        if (m_accounts.isEmpty() && QFile::exists(sidecarPath)) {
            KConfig oldCfg(sidecarPath, KConfig::SimpleConfig);
            KConfigGroup root = oldCfg.group(QStringLiteral("Providers"));
            const QStringList subGroups = root.groupList();
            for (const QString &id : subGroups) {
                const KConfigGroup g = root.group(id);
                Kalburator::Sync::BackendConfiguration bc;
                bc.id = id;
                bc.type = g.readEntry("kind", QString());
                bc.displayName = g.readEntry("displayName", QString());
                bc.enabled = true;
                const QStringList keys = g.keyList();
                for (const QString &k : keys) {
                    if (k == QLatin1String("kind") || k == QLatin1String("displayName"))
                        continue;
                    bc.connectionParams[k] = g.readEntry(k, QString());
                }
                m_accounts.append(bc);
            }
            if (!m_accounts.isEmpty()) {
                save(); // persist migrated accounts to .wildpalms.conf
                const QString stamp = QDateTime::currentDateTimeUtc().toString(
                    QStringLiteral("yyyyMMddTHHmmssZ"));
                QFile::rename(sidecarPath, sidecarPath + QStringLiteral(".migrated.") + stamp);
            }
        }
    }

    return true;
}

QJsonArray Profile::syncMappingsJson() const { return m_syncMappingsJson; }

void Profile::setSyncMappingsJson(const QJsonArray &json) { m_syncMappingsJson = json; }

// ========== Accounts (K.8b T9) ==========

QList<Kalburator::Sync::BackendConfiguration> Profile::accounts() const
{
    return m_accounts;
}

void Profile::saveAccount(const Kalburator::Sync::BackendConfiguration &cfg)
{
    for (auto &existing : m_accounts) {
        if (existing.id == cfg.id) {
            existing = cfg;
            return;
        }
    }
    m_accounts.append(cfg);
}

void Profile::removeAccount(const QString &id)
{
    m_accounts.removeIf([&id](const Kalburator::Sync::BackendConfiguration &c) {
        return c.id == id;
    });
}

void Profile::setAccounts(const QList<Kalburator::Sync::BackendConfiguration> &list)
{
    m_accounts = list;
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
    settings.setValue("sync/conflictPromptStrategy", m_conflictPromptStrategy);
    settings.setValue("sync/conflictConnectionBehavior", m_conflictConnectionBehavior);
    settings.setValue("sync/conflictTimeoutSeconds", m_conflictTimeoutSeconds);

    // G.7 Task 54: SyncMappings — stored as a JSON array
    if (!m_syncMappingsJson.isEmpty()) {
        QJsonDocument doc(m_syncMappingsJson);
        settings.setValue(QStringLiteral("syncMappings/json"),
                          QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    }

    // K.8b T9: Accounts subgroup — clear and rewrite
    settings.beginGroup(QStringLiteral("accounts"));
    settings.remove(QString()); // clear all existing entries
    for (const auto &acct : m_accounts) {
        settings.beginGroup(acct.id);
        settings.setValue(QStringLiteral("type"), acct.type);
        settings.setValue(QStringLiteral("displayName"), acct.displayName);
        settings.setValue(QStringLiteral("enabled"), acct.enabled);
        if (!acct.connectionParams.isEmpty()) {
            QJsonObject paramsObj;
            for (auto it = acct.connectionParams.constBegin();
                 it != acct.connectionParams.constEnd(); ++it) {
                paramsObj[it.key()] = QJsonValue::fromVariant(it.value());
            }
            QJsonDocument doc(paramsObj);
            settings.setValue(QStringLiteral("params"),
                              QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        }
        settings.endGroup();
    }
    settings.endGroup();

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
