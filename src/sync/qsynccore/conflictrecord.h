#ifndef QSYNCCORE_CONFLICTRECORD_H
#define QSYNCCORE_CONFLICTRECORD_H

/**
 * @file conflictrecord.h
 * @brief Data structures for capturing and tracking sync conflicts
 *
 * A conflict occurs when both sides of a sync have modified the same
 * record since the last sync. This file provides structures to capture
 * the conflict state for immediate or deferred resolution.
 *
 * Design goals:
 *   - Platform-agnostic (works for Palm↔PC, Local↔Cloud, etc.)
 *   - Sufficient detail for informed user decisions
 *   - Serializable for deferred resolution across sessions
 *   - Support for different data types (text, structured, binary)
 */

#include "synccommon.h"

#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QJsonObject>
#include <QVariantMap>

namespace QSyncCore {

/**
 * @brief Type of conflict detected
 */
enum class ConflictType
{
    BothModified,       ///< Both sides modified the same record
    ModifiedVsDeleted,  ///< One side modified, other deleted
    DeletedVsModified,  ///< Opposite of above (source deleted, target modified)
    DuplicateDetected,  ///< Same content exists with different IDs
    TypeMismatch        ///< Record type changed (rare edge case)
};

/**
 * @brief Complexity assessment for conflict resolution
 *
 * Used by hybrid resolution strategies to decide whether to
 * prompt immediately or defer for detailed review.
 */
enum class ConflictComplexity
{
    Simple,     ///< Minor change, easy to decide (e.g., whitespace, timestamp)
    Moderate,   ///< Clear change, reviewable quickly (e.g., single field edit)
    Complex     ///< Significant changes, needs careful review (e.g., restructure)
};

/**
 * @brief Resolution chosen for a conflict
 */
enum class ConflictDecision
{
    Pending,        ///< Not yet decided
    UseSource,      ///< Keep source version, overwrite target
    UseTarget,      ///< Keep target version, overwrite source
    UseBoth,        ///< Duplicate - keep both versions
    Merge,          ///< Merged version (if merge was possible)
    Skip,           ///< Skip this record, leave both unchanged
    DeleteBoth      ///< Delete from both sides (rare)
};

/**
 * @brief Snapshot of a record's state at conflict time
 *
 * Captures enough information to display the record to the user
 * and apply the resolution later.
 */
struct RecordSnapshot
{
    RecordId id;                    ///< Record identifier in its store
    QString description;            ///< Human-readable summary (title, first line, etc.)
    QByteArray content;             ///< Raw content for display/comparison
    QString contentHash;            ///< Hash for change detection
    QString contentType;            ///< MIME type hint (text/plain, text/vcard, etc.)
    QDateTime lastModified;         ///< When this version was last changed
    QString category;               ///< Category/folder if applicable
    QVariantMap metadata;           ///< Additional type-specific data

    bool isEmpty() const { return id.isEmpty(); }
    bool isDeleted() const { return content.isEmpty() && !id.isEmpty(); }

    QJsonObject toJson() const;
    static RecordSnapshot fromJson(const QJsonObject &json);
};

/**
 * @brief Complete conflict record for resolution
 *
 * Captures all information needed to:
 *   1. Display the conflict to the user
 *   2. Apply the chosen resolution
 *   3. Persist for deferred resolution
 */
struct ConflictRecord
{
    QString conflictId;             ///< Unique ID for this conflict instance
    QString conduitId;              ///< Which conduit detected this (memos, contacts, etc.)
    ConflictType type = ConflictType::BothModified;
    ConflictComplexity complexity = ConflictComplexity::Moderate;

    RecordSnapshot source;          ///< Source side (e.g., Palm device)
    RecordSnapshot target;          ///< Target side (e.g., PC files)

    QDateTime detectedAt;           ///< When conflict was detected
    QString syncSessionId;          ///< Which sync session found this

    // Resolution state
    ConflictDecision decision = ConflictDecision::Pending;
    QDateTime resolvedAt;           ///< When user/policy made decision
    QString resolvedBy;             ///< "user", "policy:SourceWins", etc.
    QByteArray mergedContent;       ///< If decision is Merge, the merged result

    // For deferred resolution
    bool applied = false;           ///< Has this resolution been applied?
    QString applyError;             ///< Error message if apply failed

    bool isPending() const { return decision == ConflictDecision::Pending; }
    bool isResolved() const { return decision != ConflictDecision::Pending; }
    bool needsApply() const { return isResolved() && !applied; }

    /**
     * @brief Get a human-readable summary of this conflict
     */
    QString summary() const;

    /**
     * @brief Calculate complexity based on content differences
     */
    void assessComplexity();

    QJsonObject toJson() const;
    static ConflictRecord fromJson(const QJsonObject &json);

    /**
     * @brief Generate a unique conflict ID
     */
    static QString generateId();
};

/**
 * @brief Convert ConflictType to string
 */
QString conflictTypeToString(ConflictType type);
ConflictType conflictTypeFromString(const QString &str);

/**
 * @brief Convert ConflictDecision to string
 */
QString conflictDecisionToString(ConflictDecision decision);
ConflictDecision conflictDecisionFromString(const QString &str);

} // namespace QSyncCore

#endif // QSYNCCORE_CONFLICTRECORD_H
