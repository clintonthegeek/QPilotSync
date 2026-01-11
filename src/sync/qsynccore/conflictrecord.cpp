#include "conflictrecord.h"

#include <QUuid>
#include <QJsonArray>

namespace QSyncCore {

// ========== RecordSnapshot ==========

QJsonObject RecordSnapshot::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["description"] = description;
    obj["content"] = QString::fromUtf8(content.toBase64());
    obj["contentHash"] = contentHash;
    obj["contentType"] = contentType;
    obj["lastModified"] = lastModified.toString(Qt::ISODate);
    obj["category"] = category;
    obj["metadata"] = QJsonObject::fromVariantMap(metadata);
    return obj;
}

RecordSnapshot RecordSnapshot::fromJson(const QJsonObject &json)
{
    RecordSnapshot snapshot;
    snapshot.id = json["id"].toString();
    snapshot.description = json["description"].toString();
    snapshot.content = QByteArray::fromBase64(json["content"].toString().toUtf8());
    snapshot.contentHash = json["contentHash"].toString();
    snapshot.contentType = json["contentType"].toString();
    snapshot.lastModified = QDateTime::fromString(json["lastModified"].toString(), Qt::ISODate);
    snapshot.category = json["category"].toString();
    snapshot.metadata = json["metadata"].toObject().toVariantMap();
    return snapshot;
}

// ========== ConflictRecord ==========

QString ConflictRecord::summary() const
{
    QString typeStr;
    switch (type) {
        case ConflictType::BothModified:
            typeStr = "Both modified";
            break;
        case ConflictType::ModifiedVsDeleted:
            typeStr = "Modified vs Deleted";
            break;
        case ConflictType::DeletedVsModified:
            typeStr = "Deleted vs Modified";
            break;
        case ConflictType::DuplicateDetected:
            typeStr = "Duplicate";
            break;
        case ConflictType::TypeMismatch:
            typeStr = "Type mismatch";
            break;
    }

    QString desc = source.description;
    if (desc.isEmpty()) {
        desc = target.description;
    }
    if (desc.isEmpty()) {
        desc = QString("Record %1").arg(source.id.isEmpty() ? target.id : source.id);
    }

    return QString("%1: %2").arg(typeStr, desc);
}

void ConflictRecord::assessComplexity()
{
    // Simple heuristics for complexity assessment
    // This can be overridden by conduit-specific logic

    if (type == ConflictType::ModifiedVsDeleted ||
        type == ConflictType::DeletedVsModified) {
        // Deletions are always complex decisions
        complexity = ConflictComplexity::Complex;
        return;
    }

    if (source.content.isEmpty() || target.content.isEmpty()) {
        complexity = ConflictComplexity::Complex;
        return;
    }

    // Compare content sizes
    int sizeDiff = qAbs(source.content.size() - target.content.size());
    int maxSize = qMax(source.content.size(), target.content.size());

    if (maxSize == 0) {
        complexity = ConflictComplexity::Simple;
        return;
    }

    double changeRatio = static_cast<double>(sizeDiff) / maxSize;

    if (changeRatio < 0.05) {
        // Less than 5% size change - probably simple
        complexity = ConflictComplexity::Simple;
    } else if (changeRatio < 0.30) {
        // 5-30% change - moderate
        complexity = ConflictComplexity::Moderate;
    } else {
        // More than 30% change - complex
        complexity = ConflictComplexity::Complex;
    }
}

QJsonObject ConflictRecord::toJson() const
{
    QJsonObject obj;
    obj["conflictId"] = conflictId;
    obj["conduitId"] = conduitId;
    obj["type"] = conflictTypeToString(type);
    obj["complexity"] = static_cast<int>(complexity);
    obj["source"] = source.toJson();
    obj["target"] = target.toJson();
    obj["detectedAt"] = detectedAt.toString(Qt::ISODate);
    obj["syncSessionId"] = syncSessionId;
    obj["decision"] = conflictDecisionToString(decision);
    obj["resolvedAt"] = resolvedAt.toString(Qt::ISODate);
    obj["resolvedBy"] = resolvedBy;
    obj["mergedContent"] = QString::fromUtf8(mergedContent.toBase64());
    obj["applied"] = applied;
    obj["applyError"] = applyError;
    return obj;
}

ConflictRecord ConflictRecord::fromJson(const QJsonObject &json)
{
    ConflictRecord record;
    record.conflictId = json["conflictId"].toString();
    record.conduitId = json["conduitId"].toString();
    record.type = conflictTypeFromString(json["type"].toString());
    record.complexity = static_cast<ConflictComplexity>(json["complexity"].toInt());
    record.source = RecordSnapshot::fromJson(json["source"].toObject());
    record.target = RecordSnapshot::fromJson(json["target"].toObject());
    record.detectedAt = QDateTime::fromString(json["detectedAt"].toString(), Qt::ISODate);
    record.syncSessionId = json["syncSessionId"].toString();
    record.decision = conflictDecisionFromString(json["decision"].toString());
    record.resolvedAt = QDateTime::fromString(json["resolvedAt"].toString(), Qt::ISODate);
    record.resolvedBy = json["resolvedBy"].toString();
    record.mergedContent = QByteArray::fromBase64(json["mergedContent"].toString().toUtf8());
    record.applied = json["applied"].toBool();
    record.applyError = json["applyError"].toString();
    return record;
}

QString ConflictRecord::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ========== Conversion Functions ==========

QString conflictTypeToString(ConflictType type)
{
    switch (type) {
        case ConflictType::BothModified:      return "BothModified";
        case ConflictType::ModifiedVsDeleted: return "ModifiedVsDeleted";
        case ConflictType::DeletedVsModified: return "DeletedVsModified";
        case ConflictType::DuplicateDetected: return "DuplicateDetected";
        case ConflictType::TypeMismatch:      return "TypeMismatch";
    }
    return "Unknown";
}

ConflictType conflictTypeFromString(const QString &str)
{
    if (str == "BothModified")      return ConflictType::BothModified;
    if (str == "ModifiedVsDeleted") return ConflictType::ModifiedVsDeleted;
    if (str == "DeletedVsModified") return ConflictType::DeletedVsModified;
    if (str == "DuplicateDetected") return ConflictType::DuplicateDetected;
    if (str == "TypeMismatch")      return ConflictType::TypeMismatch;
    return ConflictType::BothModified;
}

QString conflictDecisionToString(ConflictDecision decision)
{
    switch (decision) {
        case ConflictDecision::Pending:    return "Pending";
        case ConflictDecision::UseSource:  return "UseSource";
        case ConflictDecision::UseTarget:  return "UseTarget";
        case ConflictDecision::UseBoth:    return "UseBoth";
        case ConflictDecision::Merge:      return "Merge";
        case ConflictDecision::Skip:       return "Skip";
        case ConflictDecision::DeleteBoth: return "DeleteBoth";
    }
    return "Pending";
}

ConflictDecision conflictDecisionFromString(const QString &str)
{
    if (str == "Pending")    return ConflictDecision::Pending;
    if (str == "UseSource")  return ConflictDecision::UseSource;
    if (str == "UseTarget")  return ConflictDecision::UseTarget;
    if (str == "UseBoth")    return ConflictDecision::UseBoth;
    if (str == "Merge")      return ConflictDecision::Merge;
    if (str == "Skip")       return ConflictDecision::Skip;
    if (str == "DeleteBoth") return ConflictDecision::DeleteBoth;
    return ConflictDecision::Pending;
}

} // namespace QSyncCore
