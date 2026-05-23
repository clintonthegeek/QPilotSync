#ifndef WILDPALMS_APP_MAPPING_MAPPINGEDGE_H
#define WILDPALMS_APP_MAPPING_MAPPINGEDGE_H

#include <QGraphicsPathItem>
#include <QJsonObject>
#include <QString>

namespace WildPalms::AppMapping {

class PalmDbNode;
class ProviderNode;

/// Bezier-curve edge representing one SyncMapping between a PalmDbNode
/// slot port and a ProviderNode collection port. The full SyncMapping
/// (as JSON) is stored on the edge so the inspector panel can edit
/// fields without rebuilding the graph.
///
/// Lifetime: edges are owned by SyncMappingGraphView (via the
/// underlying QGraphicsScene). Source/target nodes are also owned by
/// the same view and are torn down together with the edges on
/// rebuild(). Callers must NOT retain `sourceNode()` / `targetNode()`
/// pointers across graph rebuilds — they're valid only for the
/// duration of the call.
class MappingEdge : public QGraphicsPathItem {
public:
    enum { Type = UserType + 3 };
    int type() const override { return Type; }

    enum class Visual { Default, Selected, Disabled, Stale };

    MappingEdge(PalmDbNode *sourceNode,
                int sourceSlot,
                ProviderNode *targetNode,
                const QString &targetCollectionId,
                const QJsonObject &mappingJson,
                QGraphicsItem *parent = nullptr);

    PalmDbNode  *sourceNode() const { return m_sourceNode; }
    int          sourceSlot() const { return m_sourceSlot; }
    ProviderNode *targetNode() const { return m_targetNode; }
    QString      targetCollectionId() const { return m_targetCollectionId; }
    QString      mappingId() const;

    QJsonObject  mappingJson() const { return m_mappingJson; }
    void         setMappingJson(const QJsonObject &json);

    void setVisual(Visual v);
    Visual visual() const { return m_visual; }

    /// Recompute the bezier path from current node anchor positions.
    /// Call after node positions change or after setMappingJson.
    void updateGeometry();

private:
    void applyPen();

    PalmDbNode   *m_sourceNode;
    int           m_sourceSlot;
    ProviderNode *m_targetNode;
    QString       m_targetCollectionId;
    QJsonObject   m_mappingJson;
    Visual        m_visual {Visual::Default};
};

} // namespace WildPalms::AppMapping

#endif
