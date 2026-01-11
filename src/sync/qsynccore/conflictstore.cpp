#include "conflictstore.h"

#include <QJsonArray>

namespace QSyncCore {

ConflictStore::ConflictStore(QObject *parent)
    : QObject(parent)
{
}

// ========== Adding Conflicts ==========

QString ConflictStore::addConflict(const ConflictRecord &conflict)
{
    ConflictRecord toAdd = conflict;

    // Generate ID if not provided
    if (toAdd.conflictId.isEmpty()) {
        toAdd.conflictId = ConflictRecord::generateId();
    }

    // Set detection time if not set
    if (!toAdd.detectedAt.isValid()) {
        toAdd.detectedAt = QDateTime::currentDateTime();
    }

    m_conflicts[toAdd.conflictId] = toAdd;

    emit conflictsAdded(1);
    emit conflictsChanged();

    return toAdd.conflictId;
}

void ConflictStore::addConflicts(const QList<ConflictRecord> &conflicts)
{
    if (conflicts.isEmpty()) return;

    for (const ConflictRecord &conflict : conflicts) {
        ConflictRecord toAdd = conflict;

        if (toAdd.conflictId.isEmpty()) {
            toAdd.conflictId = ConflictRecord::generateId();
        }
        if (!toAdd.detectedAt.isValid()) {
            toAdd.detectedAt = QDateTime::currentDateTime();
        }

        m_conflicts[toAdd.conflictId] = toAdd;
    }

    emit conflictsAdded(conflicts.size());
    emit conflictsChanged();
}

// ========== Querying Conflicts ==========

ConflictRecord ConflictStore::getConflict(const QString &conflictId) const
{
    return m_conflicts.value(conflictId);
}

QList<ConflictRecord> ConflictStore::allConflicts() const
{
    return m_conflicts.values();
}

QList<ConflictRecord> ConflictStore::conflictsForConduit(const QString &conduitId) const
{
    QList<ConflictRecord> result;
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.conduitId == conduitId) {
            result.append(conflict);
        }
    }
    return result;
}

QList<ConflictRecord> ConflictStore::pendingConflicts() const
{
    QList<ConflictRecord> result;
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.isPending()) {
            result.append(conflict);
        }
    }
    return result;
}

QList<ConflictRecord> ConflictStore::pendingConflictsForConduit(const QString &conduitId) const
{
    QList<ConflictRecord> result;
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.conduitId == conduitId && conflict.isPending()) {
            result.append(conflict);
        }
    }
    return result;
}

QList<ConflictRecord> ConflictStore::resolvedUnappliedConflicts() const
{
    QList<ConflictRecord> result;
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.needsApply()) {
            result.append(conflict);
        }
    }
    return result;
}

QList<ConflictRecord> ConflictStore::resolvedUnappliedConflictsForConduit(const QString &conduitId) const
{
    QList<ConflictRecord> result;
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.conduitId == conduitId && conflict.needsApply()) {
            result.append(conflict);
        }
    }
    return result;
}

bool ConflictStore::hasPendingConflicts() const
{
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.isPending()) {
            return true;
        }
    }
    return false;
}

int ConflictStore::pendingCount() const
{
    int count = 0;
    for (const ConflictRecord &conflict : m_conflicts) {
        if (conflict.isPending()) {
            count++;
        }
    }
    return count;
}

// ========== Resolving Conflicts ==========

void ConflictStore::resolveConflict(const QString &conflictId,
                                     ConflictDecision decision,
                                     const QString &resolvedBy,
                                     const QByteArray &mergedContent)
{
    if (!m_conflicts.contains(conflictId)) return;

    ConflictRecord &conflict = m_conflicts[conflictId];
    conflict.decision = decision;
    conflict.resolvedAt = QDateTime::currentDateTime();
    conflict.resolvedBy = resolvedBy;
    conflict.mergedContent = mergedContent;

    emit conflictResolved(conflictId, decision);
    emit conflictsChanged();
}

void ConflictStore::markApplied(const QString &conflictId, bool success,
                                 const QString &errorMessage)
{
    if (!m_conflicts.contains(conflictId)) return;

    ConflictRecord &conflict = m_conflicts[conflictId];
    conflict.applied = success;
    if (!success) {
        conflict.applyError = errorMessage;
    }

    emit conflictsChanged();
}

void ConflictStore::resolveAll(ConflictDecision decision, const QString &resolvedBy)
{
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_conflicts.begin(); it != m_conflicts.end(); ++it) {
        if (it->isPending()) {
            it->decision = decision;
            it->resolvedAt = now;
            it->resolvedBy = resolvedBy;
        }
    }

    emit conflictsChanged();
}

void ConflictStore::resetToPending(const QString &conflictId)
{
    if (!m_conflicts.contains(conflictId)) return;

    ConflictRecord &conflict = m_conflicts[conflictId];
    conflict.decision = ConflictDecision::Pending;
    conflict.resolvedAt = QDateTime();
    conflict.resolvedBy.clear();
    conflict.mergedContent.clear();
    conflict.applied = false;
    conflict.applyError.clear();

    emit conflictsChanged();
}

// ========== Removing Conflicts ==========

void ConflictStore::removeConflict(const QString &conflictId)
{
    if (m_conflicts.remove(conflictId) > 0) {
        emit conflictsChanged();
    }
}

void ConflictStore::removeConflictsForConduit(const QString &conduitId)
{
    bool removed = false;
    auto it = m_conflicts.begin();
    while (it != m_conflicts.end()) {
        if (it->conduitId == conduitId) {
            it = m_conflicts.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        emit conflictsChanged();
    }
}

void ConflictStore::removeAppliedConflicts()
{
    bool removed = false;
    auto it = m_conflicts.begin();
    while (it != m_conflicts.end()) {
        if (it->applied) {
            it = m_conflicts.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        emit conflictsChanged();
    }
}

void ConflictStore::clear()
{
    if (!m_conflicts.isEmpty()) {
        m_conflicts.clear();
        emit conflictsChanged();
    }
}

// ========== Persistence ==========

QJsonArray ConflictStore::toJson() const
{
    QJsonArray array;
    for (const ConflictRecord &conflict : m_conflicts) {
        array.append(conflict.toJson());
    }
    return array;
}

int ConflictStore::fromJson(const QJsonArray &array)
{
    m_conflicts.clear();

    for (const QJsonValue &val : array) {
        ConflictRecord conflict = ConflictRecord::fromJson(val.toObject());
        if (!conflict.conflictId.isEmpty()) {
            m_conflicts[conflict.conflictId] = conflict;
        }
    }

    if (!m_conflicts.isEmpty()) {
        emit conflictsChanged();
    }

    return m_conflicts.size();
}

} // namespace QSyncCore
