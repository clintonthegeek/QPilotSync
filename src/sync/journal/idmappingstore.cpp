#include "idmappingstore.h"

#include <QJsonObject>

namespace WildPalms::Sync {

IDMappingStore::IDMappingStore(QObject *parent)
    : QObject(parent)
{
}

void IDMappingStore::mapIds(const RecordId &sourceId, const RecordId &targetId)
{
    // Remove any existing mappings for these IDs to maintain 1:1
    if (m_mappings.contains(sourceId)) {
        RecordId oldTargetId = m_mappings[sourceId].targetId;
        m_reverseMap.remove(oldTargetId);
    }
    if (m_reverseMap.contains(targetId)) {
        RecordId oldSourceId = m_reverseMap[targetId];
        m_mappings.remove(oldSourceId);
    }

    // Create new mapping
    IDMapping mapping;
    mapping.sourceId = sourceId;
    mapping.targetId = targetId;
    mapping.lastSynced = QDateTime::currentDateTime();

    m_mappings[sourceId] = mapping;
    m_reverseMap[targetId] = sourceId;

    emit mappingsChanged();
}

bool IDMappingStore::removeBySource(const RecordId &sourceId)
{
    if (m_mappings.contains(sourceId)) {
        RecordId targetId = m_mappings[sourceId].targetId;
        m_reverseMap.remove(targetId);
        m_mappings.remove(sourceId);
        emit mappingsChanged();
        return true;
    }
    return false;
}

bool IDMappingStore::removeByTarget(const RecordId &targetId)
{
    if (m_reverseMap.contains(targetId)) {
        RecordId sourceId = m_reverseMap[targetId];
        m_mappings.remove(sourceId);
        m_reverseMap.remove(targetId);
        emit mappingsChanged();
        return true;
    }
    return false;
}

RecordId IDMappingStore::targetForSource(const RecordId &sourceId) const
{
    if (m_mappings.contains(sourceId)) {
        return m_mappings[sourceId].targetId;
    }
    return RecordId();
}

RecordId IDMappingStore::sourceForTarget(const RecordId &targetId) const
{
    return m_reverseMap.value(targetId);
}

bool IDMappingStore::hasSourceMapping(const RecordId &sourceId) const
{
    return m_mappings.contains(sourceId);
}

bool IDMappingStore::hasTargetMapping(const RecordId &targetId) const
{
    return m_reverseMap.contains(targetId);
}

QStringList IDMappingStore::allSourceIds() const
{
    return m_mappings.keys();
}

QStringList IDMappingStore::allTargetIds() const
{
    return m_reverseMap.keys();
}

IDMapping IDMappingStore::getMapping(const RecordId &sourceId) const
{
    return m_mappings.value(sourceId);
}

void IDMappingStore::updateCategories(const RecordId &sourceId,
                                       const QString &sourceCategory,
                                       const QStringList &targetCategories)
{
    if (m_mappings.contains(sourceId)) {
        m_mappings[sourceId].sourceCategory = sourceCategory;
        m_mappings[sourceId].targetCategories = targetCategories;
        emit mappingsChanged();
    }
}

QJsonArray IDMappingStore::toJson() const
{
    QJsonArray array;
    for (const IDMapping &mapping : m_mappings) {
        array.append(mappingToJson(mapping));
    }
    return array;
}

int IDMappingStore::fromJson(const QJsonArray &array)
{
    clear();

    for (const QJsonValue &val : array) {
        IDMapping mapping = mappingFromJson(val.toObject());
        if (mapping.isValid()) {
            m_mappings[mapping.sourceId] = mapping;
            m_reverseMap[mapping.targetId] = mapping.sourceId;
        }
    }

    return m_mappings.size();
}

void IDMappingStore::clear()
{
    m_mappings.clear();
    m_reverseMap.clear();
    emit mappingsChanged();
}

QJsonObject IDMappingStore::mappingToJson(const IDMapping &mapping) const
{
    QJsonObject obj;
    obj["sourceId"] = mapping.sourceId;
    obj["targetId"] = mapping.targetId;
    obj["sourceCategory"] = mapping.sourceCategory;
    obj["targetCategories"] = QJsonArray::fromStringList(mapping.targetCategories);
    obj["lastSynced"] = mapping.lastSynced.toString(Qt::ISODate);
    obj["archived"] = mapping.archived;
    return obj;
}

IDMapping IDMappingStore::mappingFromJson(const QJsonObject &json) const
{
    IDMapping mapping;
    mapping.sourceId = json["sourceId"].toString();
    mapping.targetId = json["targetId"].toString();
    mapping.sourceCategory = json["sourceCategory"].toString();

    QJsonArray catArray = json["targetCategories"].toArray();
    for (const QJsonValue &val : catArray) {
        mapping.targetCategories << val.toString();
    }

    mapping.lastSynced = QDateTime::fromString(json["lastSynced"].toString(), Qt::ISODate);
    mapping.archived = json["archived"].toBool();
    return mapping;
}

} // namespace WildPalms::Sync
