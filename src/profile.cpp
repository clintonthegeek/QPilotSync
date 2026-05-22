#include "profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
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
    if (m_syncFolderPath.isEmpty())
        return false;

    QDir dir(m_syncFolderPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    if (!saveProfileConf())  return false;
    if (!saveAccountsConf()) return false;
    if (!saveMappingsConf()) return false;
    return true;
}

bool Profile::saveProfileConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/profile.conf");
    QSettings s(path, QSettings::IniFormat);

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    s.setValue(QStringLiteral("profile/id"),
               m_id.isEmpty() ? QFileInfo(m_syncFolderPath).fileName() : m_id);
    s.setValue(QStringLiteral("profile/name"), m_name);

    s.setValue(QStringLiteral("device/path"),    m_devicePath);
    s.setValue(QStringLiteral("device/baudRate"), m_baudRate);
    s.setValue(QStringLiteral("device/connectionMode"),
               m_connectionMode == ConnectionMode::DisconnectAfterSync
                   ? QStringLiteral("disconnect")
                   : QStringLiteral("keepalive"));
    s.setValue(QStringLiteral("device/autoSyncOnConnect"), m_autoSyncOnConnect);
    s.setValue(QStringLiteral("device/defaultSyncType"),    m_defaultSyncType);
    s.setValue(QStringLiteral("device/userId"),           m_deviceFingerprint.userId);
    s.setValue(QStringLiteral("device/userName"),         m_deviceFingerprint.userName);
    s.setValue(QStringLiteral("device/usbSerialNumber"),  m_deviceFingerprint.usbSerialNumber);
    s.setValue(QStringLiteral("device/modelName"),        m_deviceFingerprint.modelName);
    s.setValue(QStringLiteral("device/manufacturer"),     m_deviceFingerprint.manufacturer);
    s.setValue(QStringLiteral("device/romVersion"),       m_deviceFingerprint.romVersion);
    s.setValue(QStringLiteral("device/productId"),        m_deviceFingerprint.productId);
    s.setValue(QStringLiteral("device/romSize"),
               QString::number(m_deviceFingerprint.romSize));
    s.setValue(QStringLiteral("device/ramSize"),
               QString::number(m_deviceFingerprint.ramSize));
    s.setValue(QStringLiteral("device/ramFree"),
               QString::number(m_deviceFingerprint.ramFree));

    if (m_lastSyncTime.isValid())
        s.setValue(QStringLiteral("sync/lastSyncTime"),
                   m_lastSyncTime.toUTC().toString(Qt::ISODate));
    s.setValue(QStringLiteral("sync/conflictPolicy"),          m_conflictPolicy);
    s.setValue(QStringLiteral("sync/conflictAutoResolve"),     m_conflictAutoResolve);
    s.setValue(QStringLiteral("sync/conflictFallback"),        m_conflictFallback);
    s.setValue(QStringLiteral("sync/conflictPromptStrategy"),  m_conflictPromptStrategy);
    s.setValue(QStringLiteral("sync/conflictConnectionBehavior"),
               m_conflictConnectionBehavior);
    s.setValue(QStringLiteral("sync/conflictTimeoutSeconds"),  m_conflictTimeoutSeconds);

    s.sync();
    return s.status() == QSettings::NoError;
}

bool Profile::saveAccountsConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/accounts.conf");
    QSettings s(path, QSettings::IniFormat);
    s.clear();   // F.1a: full rewrite, no merge.

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    for (const auto &a : m_accounts) {
        const QString group = QStringLiteral("account-") + sanitizeKConfigGroupId(a.id);
        s.setValue(group + QStringLiteral("/type"),        a.type);
        s.setValue(group + QStringLiteral("/displayName"), a.displayName);
        s.setValue(group + QStringLiteral("/enabled"),     a.enabled);

        const QString paramsGroup = group + QStringLiteral("/params");
        for (auto it = a.connectionParams.constBegin();
             it != a.connectionParams.constEnd(); ++it) {
            s.setValue(paramsGroup + QStringLiteral("/") + it.key(), it.value());
        }
    }

    s.sync();
    return s.status() == QSettings::NoError;
}

bool Profile::saveMappingsConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/mappings.conf");
    QSettings s(path, QSettings::IniFormat);
    s.clear();

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    QSet<QString> usedGroups;
    for (const auto &v : m_syncMappingsJson) {
        if (!v.isObject()) continue;
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;

        QString group = QStringLiteral("mapping-") + sanitizeKConfigGroupId(id);
        int n = 2;
        while (usedGroups.contains(group)) {
            group = QStringLiteral("mapping-") + sanitizeKConfigGroupId(id)
                  + QStringLiteral("-") + QString::number(n++);
        }
        usedGroups.insert(group);

        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            const QJsonValue val = it.value();
            if (val.isString())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toString());
            else if (val.isBool())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toBool());
            else if (val.isDouble())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toDouble());
            else {
                const QJsonDocument doc = val.isObject()
                    ? QJsonDocument(val.toObject())
                    : QJsonDocument(val.toArray());
                s.setValue(group + QStringLiteral("/") + it.key(),
                           doc.toJson(QJsonDocument::Compact));
            }
        }
    }

    s.sync();
    return s.status() == QSettings::NoError;
}

QString Profile::sanitizeKConfigGroupId(const QString &id) const
{
    QString out = id;
    out.replace(QLatin1Char('/'), QLatin1Char('_'));
    out.replace(QLatin1Char('['), QLatin1Char('_'));
    out.replace(QLatin1Char(']'), QLatin1Char('_'));
    return out;
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
