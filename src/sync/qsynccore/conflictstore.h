#ifndef QSYNCCORE_CONFLICTSTORE_H
#define QSYNCCORE_CONFLICTSTORE_H

/**
 * @file conflictstore.h
 * @brief Persistent storage for sync conflicts awaiting resolution
 *
 * Stores conflicts that need user review between sync sessions.
 * Supports deferred resolution workflows where users can:
 *   1. Disconnect from device after capturing conflicts
 *   2. Review conflicts at leisure using batch UI
 *   3. Apply resolutions in next sync session
 *
 * Storage is JSON-based for easy inspection and debugging.
 */

#include "conflictrecord.h"

#include <QObject>
#include <QList>
#include <QMap>

namespace QSyncCore {

/**
 * @brief Manages persistent storage of sync conflicts
 *
 * Conflicts are organized by conduit ID for efficient filtering.
 * Each conflict has a unique ID for tracking through resolution.
 */
class ConflictStore : public QObject
{
    Q_OBJECT

public:
    explicit ConflictStore(QObject *parent = nullptr);
    ~ConflictStore() override = default;

    // ========== Adding Conflicts ==========

    /**
     * @brief Add a new conflict to the store
     * @return The conflict ID assigned
     */
    QString addConflict(const ConflictRecord &conflict);

    /**
     * @brief Add multiple conflicts at once
     */
    void addConflicts(const QList<ConflictRecord> &conflicts);

    // ========== Querying Conflicts ==========

    /**
     * @brief Get a conflict by its ID
     */
    ConflictRecord getConflict(const QString &conflictId) const;

    /**
     * @brief Get all conflicts (across all conduits)
     */
    QList<ConflictRecord> allConflicts() const;

    /**
     * @brief Get conflicts for a specific conduit
     */
    QList<ConflictRecord> conflictsForConduit(const QString &conduitId) const;

    /**
     * @brief Get only pending (unresolved) conflicts
     */
    QList<ConflictRecord> pendingConflicts() const;

    /**
     * @brief Get only pending conflicts for a conduit
     */
    QList<ConflictRecord> pendingConflictsForConduit(const QString &conduitId) const;

    /**
     * @brief Get conflicts that have been resolved but not yet applied
     */
    QList<ConflictRecord> resolvedUnappliedConflicts() const;

    /**
     * @brief Get resolved unapplied conflicts for a specific conduit
     */
    QList<ConflictRecord> resolvedUnappliedConflictsForConduit(const QString &conduitId) const;

    /**
     * @brief Check if there are any pending conflicts
     */
    bool hasPendingConflicts() const;

    /**
     * @brief Count pending conflicts
     */
    int pendingCount() const;

    /**
     * @brief Count all conflicts
     */
    int count() const { return m_conflicts.size(); }

    // ========== Resolving Conflicts ==========

    /**
     * @brief Update a conflict's resolution
     *
     * Does not apply the resolution - just records the decision.
     */
    void resolveConflict(const QString &conflictId,
                         ConflictDecision decision,
                         const QString &resolvedBy = "user",
                         const QByteArray &mergedContent = QByteArray());

    /**
     * @brief Mark a conflict as applied (resolution was executed)
     */
    void markApplied(const QString &conflictId, bool success = true,
                     const QString &errorMessage = QString());

    /**
     * @brief Batch resolve all pending conflicts with same decision
     */
    void resolveAll(ConflictDecision decision, const QString &resolvedBy);

    /**
     * @brief Reset a conflict back to pending (undo resolution)
     */
    void resetToPending(const QString &conflictId);

    // ========== Removing Conflicts ==========

    /**
     * @brief Remove a single conflict
     */
    void removeConflict(const QString &conflictId);

    /**
     * @brief Remove all conflicts for a conduit
     */
    void removeConflictsForConduit(const QString &conduitId);

    /**
     * @brief Remove all applied conflicts (cleanup after sync)
     */
    void removeAppliedConflicts();

    /**
     * @brief Clear all conflicts
     */
    void clear();

    // ========== Persistence ==========

    /**
     * @brief Serialize all conflicts to JSON
     */
    QJsonArray toJson() const;

    /**
     * @brief Load conflicts from JSON
     * @return Number of conflicts loaded
     */
    int fromJson(const QJsonArray &array);

signals:
    /**
     * @brief Emitted when conflicts are added
     */
    void conflictsAdded(int count);

    /**
     * @brief Emitted when a conflict is resolved
     */
    void conflictResolved(const QString &conflictId, ConflictDecision decision);

    /**
     * @brief Emitted when conflicts change
     */
    void conflictsChanged();

private:
    // Conflict ID → ConflictRecord
    QMap<QString, ConflictRecord> m_conflicts;
};

} // namespace QSyncCore

#endif // QSYNCCORE_CONFLICTSTORE_H
