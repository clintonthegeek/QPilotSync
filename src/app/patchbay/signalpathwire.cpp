// src/app/patchbay/signalpathwire.cpp
#include "signalpathwire.h"

#include <QPainter>

#include <graffodil/EdgePathStrategy.h>

namespace WildPalms::AppPatchbay {

SignalPathWire::SignalPathWire(Role role,
                               Graffodil::IGraphNode *source,
                               const QString &sourceAnchorId,
                               Graffodil::IGraphNode *target,
                               const QString &targetAnchorId,
                               const QString &domain)
    : Graffodil::GraphEdgeItem(source, sourceAnchorId, target, targetAnchorId,
                               std::make_unique<Graffodil::BezierPathStrategy>())
    , m_role(role)
    , m_domain(domain)
{
    if (role == Role::Strand) {
        // not selectable, no bead, sits under wires
        graphicsItem()->setFlag(QGraphicsItem::ItemIsSelectable, false);
        graphicsItem()->setZValue(-2.0);
        setHitWidth(0.0);
    }
    applyPen();
}

void SignalPathWire::setWireState(WireState state)
{
    m_wireState = state;
    applyPen();
    graphicsItem()->update();
}

void SignalPathWire::setBead(const QString &glyph)
{
    Graffodil::EdgeLabelStyle style;
    style.color = m_wireState == WireState::Broken
        ? QColor(0xf0, 0xa8, 0xa5) : QColor(0x9f, 0xd8, 0xa8);
    style.background = QColor(0x22, 0x2a, 0x36);
    style.backgroundPadding = 4.0;
    setLabelStyle(style);
    setLabel(glyph);
}

void SignalPathWire::setStrandState(StrandState state, bool wholeDomain)
{
    m_strandState = state;
    m_wholeDomain = wholeDomain;
    applyPen();
    graphicsItem()->update();
}

void SignalPathWire::applyPen()
{
    QColor c = domainColor(m_domain);
    QPen pen(c, 2.5);

    if (m_role == Role::Strand) {
        pen.setWidthF(m_wholeDomain ? 3.0 : 2.0);
        if (m_strandState == StrandState::Ghost) {
            pen.setStyle(Qt::DashLine);
            c.setAlphaF(0.45);
            pen.setColor(c);
        }
        setPen(pen);
        return;
    }

    switch (m_wireState) {
    case WireState::Disabled:
        pen.setColor(QColor(0x56, 0x60, 0x70));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(2.0);
        break;
    case WireState::Broken:
        pen.setColor(QColor(0xc2, 0x54, 0x50));
        break;
    default:
        break;
    }
    setPen(pen);
}

void SignalPathWire::paint(QPainter *painter,
                           const QStyleOptionGraphicsItem *option,
                           QWidget *widget)
{
    Graffodil::GraphEdgeItem::paint(painter, option, widget);

    // chevrons for one-way wires (spec §6.2)
    if (m_role != Role::Wire)
        return;
    if (m_wireState != WireState::OneWayUpload
        && m_wireState != WireState::OneWayDownload)
        return;

    const QPainterPath p = path();
    if (p.isEmpty())
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(domainColor(m_domain).lighter(135), 2.0);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (qreal t : {0.3, 0.5, 0.7}) {
        const QPointF pt = p.pointAtPercent(t);
        qreal angle = -p.angleAtPercent(t);
        // upload: hub → remote = along the path; download: against it
        if (m_wireState == WireState::OneWayDownload)
            angle += 180.0;
        painter->save();
        painter->translate(pt);
        painter->rotate(angle);
        painter->drawPolyline(QPolygonF{
            QPointF(-4, -5), QPointF(4, 0), QPointF(-4, 5)});
        painter->restore();
    }
}

} // namespace WildPalms::AppPatchbay
