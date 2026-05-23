#include "mappingedge.h"
#include "palmdbnode.h"
#include "providernode.h"

#include <QPainterPath>
#include <QPen>
#include <QApplication>
#include <QPalette>

namespace WildPalms::AppMapping {

MappingEdge::MappingEdge(PalmDbNode *sourceNode,
                         int sourceSlot,
                         ProviderNode *targetNode,
                         const QString &targetCollectionId,
                         const QJsonObject &mappingJson,
                         QGraphicsItem *parent)
    : QGraphicsPathItem(parent)
    , m_sourceNode(sourceNode)
    , m_sourceSlot(sourceSlot)
    , m_targetNode(targetNode)
    , m_targetCollectionId(targetCollectionId)
    , m_mappingJson(mappingJson)
{
    setFlag(ItemIsSelectable, true);
    setZValue(-1.0);   // draw under nodes
    updateGeometry();
    applyPen();
}

QString MappingEdge::mappingId() const
{
    return m_mappingJson.value(QStringLiteral("id")).toString();
}

void MappingEdge::setMappingJson(const QJsonObject &json)
{
    m_mappingJson = json;
    applyPen();
    update();
}

void MappingEdge::setVisual(Visual v)
{
    m_visual = v;
    applyPen();
    update();
}

void MappingEdge::updateGeometry()
{
    if (!m_sourceNode || !m_targetNode) return;
    const QPointF a = m_sourceNode->slotAnchorScenePos(m_sourceSlot);
    const QPointF b = m_targetNode->collectionAnchorScenePos(m_targetCollectionId);
    // PalmDbNode::slotAnchorScenePos() and
    // ProviderNode::collectionAnchorScenePos() return a default-constructed
    // QPointF (0,0) as their "not found" sentinel. SyncMappingGraphView
    // positions all nodes at x>=30, so a real anchor is never at (0,0) and
    // isNull() reliably detects the sentinel.
    if (a.isNull() || b.isNull()) return;

    const qreal dx = std::abs(b.x() - a.x());
    const qreal c  = std::max<qreal>(40.0, dx * 0.5);

    QPainterPath path;
    path.moveTo(a);
    path.cubicTo(a.x() + c, a.y(),
                 b.x() - c, b.y(),
                 b.x(),     b.y());
    setPath(path);
}

void MappingEdge::applyPen()
{
    const QPalette pal = QApplication::palette();
    QPen pen;
    pen.setWidthF(2.0);

    const bool enabled = m_mappingJson.value(QStringLiteral("enabled")).toBool();

    switch (m_visual) {
    case Visual::Selected:
        pen.setColor(pal.color(QPalette::Highlight));
        pen.setWidthF(3.0);
        break;
    case Visual::Stale:
        pen.setColor(QColor(0xc0, 0x70, 0x00));   // orange
        pen.setStyle(Qt::DashLine);
        break;
    case Visual::Disabled:
        pen.setColor(pal.color(QPalette::Mid));
        pen.setStyle(Qt::DashLine);
        break;
    case Visual::Default:
    default:
        pen.setColor(pal.color(QPalette::Text));
        break;
    }

    // Disabled mapping always renders dashed and faded regardless of
    // selection state.
    // Stale visual subsumes the disabled-mapping overlay: its orange dash
    // is more informative than the gray fade and shouldn't be tinted away.
    if (!enabled && m_visual != Visual::Stale) {
        pen.setStyle(Qt::DashLine);
        QColor c = pen.color();
        c.setAlphaF(0.5);
        pen.setColor(c);
    }

    setPen(pen);
}

} // namespace WildPalms::AppMapping
