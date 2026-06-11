// src/app/patchbay/patchbaymodel.h
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include "patchbaytypes.h"
#include "runtime/routemapping.h"   // RouteStatus

namespace WildPalms::AppPatchbay {

/// Pure data layer of the Sync Patchbay (spec §4): turns Profile rows +
/// route statuses + provider state into node/port/wire/strand descriptions.
/// No QGraphicsView dependency; fully unit-testable.
class PatchbayModel : public QObject {
    Q_OBJECT
public:
    struct ProviderEntry {
        QString providerId;
        QString displayName;
        QString busyText;   ///< empty → connected (F.3 convention)
        QList<Kalburator::Sync::CollectionInfo> collections;
    };

    struct Inputs {
        QJsonArray mappings;                            ///< Profile::syncMappingsJson()
        QList<ConduitFacts> conduits;
        QHash<QString, QStringList> slotSnapshot;       ///< dbName → 16 names
        QHash<QString, QStringList> desiredCategories;  ///< dbName → names
        QList<ProviderEntry> providers;
        QHash<QString, WildPalms::Runtime::RouteStatus> routeStatuses;
        bool deviceConnected = false;
        QString deviceName;
    };

    explicit PatchbayModel(QObject *parent = nullptr);

    void setInputs(const Inputs &inputs);   ///< rebuilds everything, emits rebuilt()

    QList<NodeDesc> nodes() const { return m_nodes; }
    QList<WireDesc> wires() const { return m_wires; }
    QList<StrandDesc> strands() const { return m_strands; }
    QJsonArray mappings() const { return m_inputs.mappings; }

    // ── edit operations (Task 9) ──────────────────────────────────────
    /// Returns the new mapping id, or empty on validation failure
    /// (unknown domain/conduit, incompatible collection, duplicate).
    QString addMapping(const QString &hubPortId, const QString &providerId,
                       const QString &collectionId);
    bool removeMapping(const QString &mappingId);
    /// Merge `changes` into the row (mode/conflictPolicy/enabled edits).
    bool updateMapping(const QString &mappingId, const QJsonObject &changes);
    bool addCategory(const QString &domain, const QString &name);
    /// Fails if any mapping row still references the category.
    bool removeCategory(const QString &domain, const QString &name);

    // lookup helpers (inspector / view)
    QJsonObject mappingById(const QString &mappingId) const;
    WildPalms::Runtime::RouteStatus statusFor(const QString &mappingId) const;

signals:
    void rebuilt();
    void mappingsChanged(const QJsonArray &mappings);
    /// dbName + full new desired list (write through to Profile).
    void desiredCategoriesChanged(const QString &dbName, const QStringList &names);

private:
    void rebuild();
    void rebuildRemotes();
    void rebuildWiresAndStrands();
    const ConduitFacts *conduitForDomain(const QString &domain) const;
    const ConduitFacts *conduitForId(const QString &conduitId) const;
    /// Category names for a hub band: desiredCategories ∪ names parsed from
    /// rows, original order, "Unfiled" excluded, case-insensitive dedup.
    QStringList hubCategoryNames(const ConduitFacts &c) const;

    Inputs m_inputs;
    QList<NodeDesc> m_nodes;
    QList<WireDesc> m_wires;
    QList<StrandDesc> m_strands;
};

} // namespace WildPalms::AppPatchbay
