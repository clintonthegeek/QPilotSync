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
    , m_idMappings(new WildPalms::Sync::IDMappingStore(this))
    , m_baseline(new WildPalms::Sync::BaselineStore(this))
    , m_conflicts(new Kalburator::Conflict::ConflictStore(this))
{
    // Forward change signals
    connect(m_idMappings, &WildPalms::Sync::IDMappingStore::mappingsChanged,
            this, &SyncState::stateChanged);
    connect(m_baseline, &WildPalms::Sync::BaselineStore::baselineChanged,
            this, &SyncState::stateChanged);
    connect(m_conflicts, &Kalburator::Conflict::ConflictStore::conflictsChanged,
            this, &SyncState::stateChanged);
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
    m_idMappings->mapIds(palmId, pcId);
}

void SyncState::removePalmMapping(const QString &palmId)
{
    m_idMappings->removeBySource(palmId);
}

void SyncState::removePCMapping(const QString &pcId)
{
    m_idMappings->removeByTarget(pcId);
}

QString SyncState::pcIdForPalm(const QString &palmId) const
{
    return m_idMappings->targetForSource(palmId);
}

QString SyncState::palmIdForPC(const QString &pcId) const
{
    return m_idMappings->sourceForTarget(pcId);
}

bool SyncState::hasPalmMapping(const QString &palmId) const
{
    return m_idMappings->hasSourceMapping(palmId);
}

bool SyncState::hasPCMapping(const QString &pcId) const
{
    return m_idMappings->hasTargetMapping(pcId);
}

QStringList SyncState::allPalmIds() const
{
    return m_idMappings->allSourceIds();
}

QStringList SyncState::allPCIds() const
{
    return m_idMappings->allTargetIds();
}

IDMapping SyncState::getMapping(const QString &palmId) const
{
    // Convert from WildPalms::Sync::IDMapping to Sync::IDMapping
    WildPalms::Sync::IDMapping coreMapping = m_idMappings->getMapping(palmId);

    IDMapping mapping;
    mapping.palmId = coreMapping.sourceId;
    mapping.pcId = coreMapping.targetId;
    mapping.palmCategory = coreMapping.sourceCategory;
    mapping.pcCategories = coreMapping.targetCategories;
    mapping.lastSynced = coreMapping.lastSynced;
    mapping.archived = coreMapping.archived;

    return mapping;
}

void SyncState::updateCategories(const QString &palmId,
                                  const QString &palmCategory,
                                  const QStringList &pcCategories)
{
    m_idMappings->updateCategories(palmId, palmCategory, pcCategories);
}

// ========== Baseline Operations ==========

QString SyncState::baselinePath() const
{
    return QDir(m_stateDir).filePath("baseline");
}

void SyncState::saveBaseline(const QMap<QString, QString> &pcFileHashes)
{
    m_baseline->saveBaseline(pcFileHashes);
}

QString SyncState::baselineHash(const QString &pcId) const
{
    return m_baseline->hash(pcId);
}

bool SyncState::hasFileChanged(const QString &pcId, const QString &currentHash) const
{
    return m_baseline->hasChanged(pcId, currentHash);
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
    return m_idMappings->isEmpty() && !m_lastSyncTime.isValid();
}

bool SyncState::validateMappings(const QStringList &palmIds) const
{
    // All Palm IDs should have mappings
    for (const QString &id : palmIds) {
        if (!m_idMappings->hasSourceMapping(id)) {
            return false;
        }
    }

    // Mapping count should match
    if (m_idMappings->count() != palmIds.size()) {
        return false;
    }

    return true;
}

// ========== Conflict Operations ==========

bool SyncState::hasPendingConflicts() const
{
    return m_conflicts->hasPendingConflicts();
}

int SyncState::pendingConflictCount() const
{
    return m_conflicts->pendingCount();
}

QList<Kalburator::Conflict::ConflictRecord> SyncState::pendingConflicts() const
{
    return m_conflicts->pendingConflicts();
}

void SyncState::clearPendingConflicts()
{
    m_conflicts->clear();
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

    // Load mappings via IDMappingStore
    // Need to convert from legacy format to new format
    QJsonArray mappingsArray = root["mappings"].toArray();
    if (!mappingsArray.isEmpty()) {
        // Convert legacy format (palmId/pcId) to new format (sourceId/targetId)
        QJsonArray convertedArray;
        for (const QJsonValue &val : mappingsArray) {
            QJsonObject oldObj = val.toObject();
            QJsonObject newObj;
            newObj["sourceId"] = oldObj["palmId"];
            newObj["targetId"] = oldObj["pcId"];
            newObj["sourceCategory"] = oldObj["palmCategory"];
            newObj["targetCategories"] = oldObj["pcCategories"];
            newObj["lastSynced"] = oldObj["lastSynced"];
            newObj["archived"] = oldObj["archived"];
            convertedArray.append(newObj);
        }
        m_idMappings->fromJson(convertedArray);
    }

    // Load baseline hashes via BaselineStore
    QJsonObject baselineObj = root["baseline"].toObject();
    if (!baselineObj.isEmpty()) {
        m_baseline->fromJson(baselineObj);
    }

    // Load pending conflicts via ConflictStore
    QJsonArray conflictsArray = root["conflicts"].toArray();
    if (!conflictsArray.isEmpty()) {
        m_conflicts->fromJson(conflictsArray);
    }

    qDebug() << "[SyncState] Loaded" << m_idMappings->count() << "mappings,"
             << m_conflicts->pendingCount() << "pending conflicts for" << m_conduitId;
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
    root["version"] = 2;  // Version 2 uses libkalburator format

    // Save mappings - convert from libkalburator format to legacy format for compatibility
    QJsonArray coreArray = m_idMappings->toJson();
    QJsonArray legacyArray;
    for (const QJsonValue &val : coreArray) {
        QJsonObject coreObj = val.toObject();
        QJsonObject legacyObj;
        legacyObj["palmId"] = coreObj["sourceId"];
        legacyObj["pcId"] = coreObj["targetId"];
        legacyObj["palmCategory"] = coreObj["sourceCategory"];
        legacyObj["pcCategories"] = coreObj["targetCategories"];
        legacyObj["lastSynced"] = coreObj["lastSynced"];
        legacyObj["archived"] = coreObj["archived"];
        legacyArray.append(legacyObj);
    }
    root["mappings"] = legacyArray;

    // Save baseline hashes via BaselineStore
    root["baseline"] = m_baseline->toJson();

    // Save pending conflicts via ConflictStore
    root["conflicts"] = m_conflicts->toJson();

    // Write to file
    QFile file(mappingsFile);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString("Failed to save mappings: %1").arg(mappingsFile));
        return false;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    qDebug() << "[SyncState] Saved" << m_idMappings->count() << "mappings,"
             << m_conflicts->pendingCount() << "pending conflicts for" << m_conduitId;
    return true;
}

void SyncState::clear()
{
    m_idMappings->clear();
    m_baseline->clear();
    m_conflicts->clear();
    m_lastSyncTime = QDateTime();
    m_lastSyncPC.clear();
    emit stateChanged();
}

QString SyncState::statePath() const
{
    return m_stateDir;
}

void SyncState::setStateDirectory(const QString &baseDir)
{
    m_stateDir = QDir(baseDir).filePath(m_userName + "/" + m_conduitId);
    ensureStateDir();
}

} // namespace Sync
