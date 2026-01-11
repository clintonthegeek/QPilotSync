#ifndef QSYNCCORE_IDMAPPINGSTORE_H
#define QSYNCCORE_IDMAPPINGSTORE_H

/**
 * @file idmappingstore.h
 * @brief Bidirectional ID mapping store for sync operations
 *
 * Manages the correspondence between record identifiers in two
 * different data stores. Essential for incremental sync to know
 * which records correspond to each other.
 *
 * Example usage:
 *   - Palm record ID 12345 ↔ file "memos/meeting-notes.md"
 *   - Local file UUID ↔ cloud service record ID
 *
 * This class is designed to be:
 *   - Persistence-agnostic (storage handled by load/save callbacks)
 *   - Thread-safe for read operations
 *   - Suitable for extraction into QSyncCore shared library
 */

#include "synccommon.h"

#include <QObject>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

namespace QSyncCore {

/**
 * @brief Manages bidirectional ID mappings between two sync stores
 *
 * Provides O(1) lookup in both directions via dual hash maps.
 * Supports serialization to/from JSON for persistence.
 */
class IdMappingStore : public QObject
{
    Q_OBJECT

public:
    explicit IdMappingStore(QObject *parent = nullptr);
    ~IdMappingStore() override = default;

    // ========== Mapping Operations ==========

    /**
     * @brief Create or update a mapping between source and target IDs
     *
     * If either ID already has a mapping, the old mapping is removed
     * to maintain 1:1 correspondence.
     */
    void mapIds(const RecordId &sourceId, const RecordId &targetId);

    /**
     * @brief Remove a mapping by source ID
     * @return true if a mapping was removed
     */
    bool removeBySource(const RecordId &sourceId);

    /**
     * @brief Remove a mapping by target ID
     * @return true if a mapping was removed
     */
    bool removeByTarget(const RecordId &targetId);

    /**
     * @brief Get target ID for a source record
     * @return Target ID, or empty string if no mapping exists
     */
    RecordId targetForSource(const RecordId &sourceId) const;

    /**
     * @brief Get source ID for a target record
     * @return Source ID, or empty string if no mapping exists
     */
    RecordId sourceForTarget(const RecordId &targetId) const;

    /**
     * @brief Check if a source ID has a mapping
     */
    bool hasSourceMapping(const RecordId &sourceId) const;

    /**
     * @brief Check if a target ID has a mapping
     */
    bool hasTargetMapping(const RecordId &targetId) const;

    /**
     * @brief Get all source IDs in the store
     */
    QStringList allSourceIds() const;

    /**
     * @brief Get all target IDs in the store
     */
    QStringList allTargetIds() const;

    /**
     * @brief Get the full mapping entry for a source ID
     */
    IdMapping getMapping(const RecordId &sourceId) const;

    /**
     * @brief Get total number of mappings
     */
    int count() const { return m_mappings.size(); }

    /**
     * @brief Check if store has any mappings
     */
    bool isEmpty() const { return m_mappings.isEmpty(); }

    // ========== Category Support ==========

    /**
     * @brief Update category information for a mapping
     */
    void updateCategories(const RecordId &sourceId,
                          const QString &sourceCategory,
                          const QStringList &targetCategories);

    // ========== Serialization ==========

    /**
     * @brief Serialize all mappings to JSON
     */
    QJsonArray toJson() const;

    /**
     * @brief Load mappings from JSON
     * @param array JSON array of mapping objects
     * @return Number of mappings loaded
     */
    int fromJson(const QJsonArray &array);

    /**
     * @brief Clear all mappings
     */
    void clear();

signals:
    /**
     * @brief Emitted when mappings are modified
     */
    void mappingsChanged();

private:
    // Primary storage: source ID → full mapping
    QMap<RecordId, IdMapping> m_mappings;

    // Reverse lookup: target ID → source ID
    QMap<RecordId, RecordId> m_reverseMap;

    QJsonObject mappingToJson(const IdMapping &mapping) const;
    IdMapping mappingFromJson(const QJsonObject &json) const;
};

} // namespace QSyncCore

#endif // QSYNCCORE_IDMAPPINGSTORE_H
