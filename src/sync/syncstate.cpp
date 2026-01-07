#include "syncstate.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDebug>

namespace Sync {

SyncState::SyncState(const QString &userName,
                     const QString &conduitId,
                     QObject *parent)
    : QObject(parent)
    , m_userName(userName)
    , m_conduitId(conduitId)
{
    // State directory: ~/.qpilotsync/<username>/<conduit>/
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_stateDir = QDir(baseDir).filePath(m_userName + "/" + m_conduitId);

    ensureStateDir();
}

SyncState::~SyncState()
{
    // Auto-save on destruction
    save();
}

void SyncState::ensureStateDir()
{
    QDir dir(m_stateDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit errorOccurred(QString("Failed to create state directory: %1").arg(m_stateDir));
        }
    }

    // Also ensure baseline subdirectory exists
    QString baselineDir = dir.filePath("baseline");
    QDir(baselineDir).mkpath(".");
}

// ========== ID Mapping Operations ==========

void SyncState::mapIds(const QString &palmId, const QString &pcId)
{
    // Remove any existing mappings for these IDs
    if (m_mappings.contains(palmId)) {
        QString oldPcId = m_mappings[palmId].pcId;
        m_pcToPalmMap.remove(oldPcId);
    }
    if (m_pcToPalmMap.contains(pcId)) {
        QString oldPalmId = m_pcToPalmMap[pcId];
        m_mappings.remove(oldPalmId);
    }

    // Create new mapping
    IDMapping mapping;
    mapping.palmId = palmId;
    mapping.pcId = pcId;
    mapping.lastSynced = QDateTime::currentDateTime();

    m_mappings[palmId] = mapping;
    m_pcToPalmMap[pcId] = palmId;

    emit stateChanged();
}

void SyncState::removePalmMapping(const QString &palmId)
{
    if (m_mappings.contains(palmId)) {
        QString pcId = m_mappings[palmId].pcId;
        m_pcToPalmMap.remove(pcId);
        m_mappings.remove(palmId);
        emit stateChanged();
    }
}

void SyncState::removePCMapping(const QString &pcId)
{
    if (m_pcToPalmMap.contains(pcId)) {
        QString palmId = m_pcToPalmMap[pcId];
        m_mappings.remove(palmId);
        m_pcToPalmMap.remove(pcId);
        emit stateChanged();
    }
}

QString SyncState::pcIdForPalm(const QString &palmId) const
{
    if (m_mappings.contains(palmId)) {
        return m_mappings[palmId].pcId;
    }
    return QString();
}

QString SyncState::palmIdForPC(const QString &pcId) const
{
    return m_pcToPalmMap.value(pcId);
}

bool SyncState::hasPalmMapping(const QString &palmId) const
{
    return m_mappings.contains(palmId);
}

bool SyncState::hasPCMapping(const QString &pcId) const
{
    return m_pcToPalmMap.contains(pcId);
}

QStringList SyncState::allPalmIds() const
{
    return m_mappings.keys();
}

QStringList SyncState::allPCIds() const
{
    return m_pcToPalmMap.keys();
}

IDMapping SyncState::getMapping(const QString &palmId) const
{
    return m_mappings.value(palmId);
}

void SyncState::updateCategories(const QString &palmId,
                                  const QString &palmCategory,
                                  const QStringList &pcCategories)
{
    if (m_mappings.contains(palmId)) {
        m_mappings[palmId].palmCategory = palmCategory;
        m_mappings[palmId].pcCategories = pcCategories;
        emit stateChanged();
    }
}

// ========== Baseline Operations ==========

QString SyncState::baselinePath() const
{
    return QDir(m_stateDir).filePath("baseline");
}

void SyncState::saveBaseline(const QMap<QString, QString> &pcFileHashes)
{
    m_baselineHashes = pcFileHashes;
    emit stateChanged();
}

QString SyncState::baselineHash(const QString &pcId) const
{
    return m_baselineHashes.value(pcId);
}

bool SyncState::hasFileChanged(const QString &pcId, const QString &currentHash) const
{
    if (!m_baselineHashes.contains(pcId)) {
        return true;  // New file
    }
    return m_baselineHashes[pcId] != currentHash;
}

// ========== Sync Metadata ==========

QDateTime SyncState::lastSyncTime() const
{
    return m_lastSyncTime;
}

void SyncState::setLastSyncTime(const QDateTime &time)
{
    m_lastSyncTime = time;
    emit stateChanged();
}

QString SyncState::lastSyncPC() const
{
    return m_lastSyncPC;
}

void SyncState::setLastSyncPC(const QString &pcName)
{
    m_lastSyncPC = pcName;
    emit stateChanged();
}

bool SyncState::isFirstSync() const
{
    return m_mappings.isEmpty() && !m_lastSyncTime.isValid();
}

bool SyncState::validateMappings(const QStringList &palmIds) const
{
    // All Palm IDs should have mappings
    for (const QString &id : palmIds) {
        if (!m_mappings.contains(id)) {
            return false;
        }
    }

    // Mapping count should match
    if (m_mappings.size() != palmIds.size()) {
        return false;
    }

    return true;
}

// ========== Persistence ==========

bool SyncState::load()
{
    QString mappingsFile = QDir(m_stateDir).filePath("mappings.json");

    QFile file(mappingsFile);
    if (!file.exists()) {
        // No previous state - this is fine for first sync
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QString("Failed to open mappings file: %1").arg(mappingsFile));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("Failed to parse mappings: %1").arg(parseError.errorString()));
        return false;
    }

    QJsonObject root = doc.object();

    // Load metadata
    m_lastSyncTime = QDateTime::fromString(root["lastSyncTime"].toString(), Qt::ISODate);
    m_lastSyncPC = root["lastSyncPC"].toString();

    // Load mappings
    m_mappings.clear();
    m_pcToPalmMap.clear();

    QJsonArray mappingsArray = root["mappings"].toArray();
    for (const QJsonValue &val : mappingsArray) {
        IDMapping mapping = mappingFromJson(val.toObject());
        m_mappings[mapping.palmId] = mapping;
        m_pcToPalmMap[mapping.pcId] = mapping.palmId;
    }

    // Load baseline hashes
    m_baselineHashes.clear();
    QJsonObject baselineObj = root["baseline"].toObject();
    for (auto it = baselineObj.begin(); it != baselineObj.end(); ++it) {
        m_baselineHashes[it.key()] = it.value().toString();
    }

    qDebug() << "[SyncState] Loaded" << m_mappings.size() << "mappings for" << m_conduitId;
    return true;
}

bool SyncState::save()
{
    ensureStateDir();

    QString mappingsFile = QDir(m_stateDir).filePath("mappings.json");

    QJsonObject root;

    // Save metadata
    root["userName"] = m_userName;
    root["conduitId"] = m_conduitId;
    root["lastSyncTime"] = m_lastSyncTime.toString(Qt::ISODate);
    root["lastSyncPC"] = m_lastSyncPC;
    root["version"] = 1;

    // Save mappings
    QJsonArray mappingsArray;
    for (const IDMapping &mapping : m_mappings) {
        mappingsArray.append(mappingToJson(mapping));
    }
    root["mappings"] = mappingsArray;

    // Save baseline hashes
    QJsonObject baselineObj;
    for (auto it = m_baselineHashes.begin(); it != m_baselineHashes.end(); ++it) {
        baselineObj[it.key()] = it.value();
    }
    root["baseline"] = baselineObj;

    // Write to file
    QFile file(mappingsFile);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString("Failed to save mappings: %1").arg(mappingsFile));
        return false;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    qDebug() << "[SyncState] Saved" << m_mappings.size() << "mappings for" << m_conduitId;
    return true;
}

void SyncState::clear()
{
    m_mappings.clear();
    m_pcToPalmMap.clear();
    m_baselineHashes.clear();
    m_lastSyncTime = QDateTime();
    m_lastSyncPC.clear();
    emit stateChanged();
}

QString SyncState::statePath() const
{
    return m_stateDir;
}

QJsonObject SyncState::mappingToJson(const IDMapping &mapping) const
{
    QJsonObject obj;
    obj["palmId"] = mapping.palmId;
    obj["pcId"] = mapping.pcId;
    obj["palmCategory"] = mapping.palmCategory;
    obj["pcCategories"] = QJsonArray::fromStringList(mapping.pcCategories);
    obj["lastSynced"] = mapping.lastSynced.toString(Qt::ISODate);
    obj["archived"] = mapping.archived;
    return obj;
}

IDMapping SyncState::mappingFromJson(const QJsonObject &json) const
{
    IDMapping mapping;
    mapping.palmId = json["palmId"].toString();
    mapping.pcId = json["pcId"].toString();
    mapping.palmCategory = json["palmCategory"].toString();

    QJsonArray catArray = json["pcCategories"].toArray();
    for (const QJsonValue &val : catArray) {
        mapping.pcCategories << val.toString();
    }

    mapping.lastSynced = QDateTime::fromString(json["lastSynced"].toString(), Qt::ISODate);
    mapping.archived = json["archived"].toBool();
    return mapping;
}

} // namespace Sync
