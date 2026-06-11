// src/app/patchbay/patchnodeitem.cpp
#include "patchnodeitem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QVariantMap>

namespace WildPalms::AppPatchbay {

namespace {
QColor chromeFor(NodeKind k)
{
    switch (k) {
    case NodeKind::Palm:        return QColor(0x2a, 0x3a, 0x50);
    case NodeKind::Hub:         return QColor(0x35, 0x2e, 0x55);
    case NodeKind::Remote:      return QColor(0x2a, 0x4a, 0x2a);
    case NodeKind::GhostRemote: return QColor(0x4a, 0x2a, 0x2a);
    }
    return Qt::darkGray;
}
} // namespace

PatchNodeItem::PatchNodeItem(const NodeDesc &desc) : m_desc(desc)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setOpacity(m_desc.ghosted ? 0.45 : 1.0);
}

void PatchNodeItem::setDesc(const NodeDesc &desc)
{
    prepareGeometryChange();
    m_desc = desc;
    setOpacity(m_desc.ghosted ? 0.45 : 1.0);
    update();
}

bool PatchNodeItem::leftAnchors() const  { return m_desc.kind != NodeKind::Palm; }
bool PatchNodeItem::rightAnchors() const
{
    return m_desc.kind == NodeKind::Palm || m_desc.kind == NodeKind::Hub;
}

QList<PatchNodeItem::RowRef> PatchNodeItem::rows() const
{
    QList<RowRef> out;
    qreal y = kHeaderH;
    for (int b = 0; b < m_desc.bands.size(); ++b) {
        y += kBandHeaderH;
        for (int p = 0; p < m_desc.bands[b].ports.size(); ++p) {
            out.append({b, p, y});
            y += kRowH;
        }
        y += kBandPad;
    }
    return out;
}

qreal PatchNodeItem::contentHeight() const
{
    qreal y = kHeaderH;
    for (const auto &band : m_desc.bands)
        y += kBandHeaderH + band.ports.size() * kRowH + kBandPad;
    return y + 4.0;
}

QPointF PatchNodeItem::portRowCenter(const QString &portId) const
{
    for (const auto &r : rows())
        if (m_desc.bands[r.band].ports[r.port].id == portId)
            return QPointF(kWidth / 2.0, r.y + kRowH / 2.0);
    return QPointF(0, -1);
}

QString PatchNodeItem::portAt(const QPointF &itemPos) const
{
    for (const auto &r : rows())
        if (itemPos.y() >= r.y && itemPos.y() < r.y + kRowH)
            return m_desc.bands[r.band].ports[r.port].id;
    return {};
}

QList<Graffodil::Anchor> PatchNodeItem::anchors() const
{
    QList<Graffodil::Anchor> out;
    for (const auto &r : rows()) {
        const PortDesc &p = m_desc.bands[r.band].ports[r.port];
        if (p.kind == PortKind::AddCategory)
            continue;   // not connectable
        QVariantMap meta{
            {QStringLiteral("port"),   p.id},
            {QStringLiteral("kind"),   int(p.kind)},
            {QStringLiteral("domain"), p.domain},
            {QStringLiteral("node"),   m_desc.id},
        };
        const qreal cy = r.y + kRowH / 2.0;
        if (leftAnchors()) {
            Graffodil::Anchor a;
            a.id = p.id + QStringLiteral("@l");
            a.scenePos = mapToScene(QPointF(0.0, cy));
            a.outwardDirection = QPointF(-1.0, 0.0);
            a.metadata = meta;
            out << a;
        }
        if (rightAnchors()) {
            Graffodil::Anchor a;
            a.id = p.id + QStringLiteral("@r");
            a.scenePos = mapToScene(QPointF(kWidth, cy));
            a.outwardDirection = QPointF(1.0, 0.0);
            a.metadata = meta;
            out << a;
        }
    }
    // whole-DB anchor for ghost strands (palm only, band headers)
    if (m_desc.kind == NodeKind::Palm) {
        qreal y = kHeaderH;
        for (const auto &band : m_desc.bands) {
            // derive dbName from any slot port id "slot:<db>/<i>"; bands with
            // no claimed slots still get the anchor at the band header.
            QString db;
            if (!band.ports.isEmpty()) {
                const QString id = band.ports.first().id;   // slot:<db>/<i>
                db = id.mid(5, id.lastIndexOf(QLatin1Char('/')) - 5);
            } else {
                db = band.title.section(QStringLiteral(" — "), 1, 1);
            }
            Graffodil::Anchor a;
            a.id = QStringLiteral("db:%1@r").arg(db);
            a.scenePos = mapToScene(QPointF(kWidth, y + kBandHeaderH / 2.0));
            a.outwardDirection = QPointF(1.0, 0.0);
            a.metadata = QVariantMap{{QStringLiteral("port"),
                                      QStringLiteral("db:%1").arg(db)}};
            out << a;
            y += kBandHeaderH + band.ports.size() * kRowH + kBandPad;
        }
    }
    return out;
}

QRectF PatchNodeItem::nodeBoundingRect() const
{
    return mapToScene(boundingRect()).boundingRect();
}

QRectF PatchNodeItem::boundingRect() const
{
    return QRectF(0, 0, kWidth, contentHeight());
}

void PatchNodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *,
                          QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect();

    // body + header
    p->setPen(QPen(chromeFor(m_desc.kind).lighter(160), 1.2));
    p->setBrush(QColor(0x20, 0x22, 0x2b));
    p->drawRoundedRect(r, 8, 8);
    p->setBrush(chromeFor(m_desc.kind));
    p->setPen(Qt::NoPen);
    p->drawRoundedRect(QRectF(0, 0, kWidth, kHeaderH), 8, 8);
    p->setPen(QColor(0xd8, 0xdd, 0xe6));
    QFont f = p->font();
    f.setBold(true);
    p->setFont(f);
    p->drawText(QRectF(10, 0, kWidth - 64, kHeaderH),
                Qt::AlignVCenter | Qt::AlignLeft, m_desc.title);
    f.setBold(false);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p->setFont(f);
    p->setPen(QColor(0x9a, 0xa3, 0xb2));
    p->drawText(QRectF(10, 0, kWidth - 16, kHeaderH),
                Qt::AlignVCenter | Qt::AlignRight, m_desc.subtitle);

    // bands + rows
    qreal y = kHeaderH;
    for (const auto &band : m_desc.bands) {
        p->setPen(domainColor(band.domain).lighter(125));
        p->drawText(QRectF(10, y, kWidth - 16, kBandHeaderH),
                    Qt::AlignVCenter | Qt::AlignLeft, band.title.toUpper());
        y += kBandHeaderH;
        for (const auto &port : band.ports) {
            const bool isAdd = port.kind == PortKind::AddCategory;
            p->setPen(isAdd ? QColor(0x6f, 0x7a, 0x8a)
                            : QColor(0xcf, 0xd8, 0xe3));
            p->drawText(QRectF(16, y, kWidth - 40, kRowH),
                        Qt::AlignVCenter | Qt::AlignLeft, port.label);
            // port dots
            const qreal cy = y + kRowH / 2.0;
            if (!isAdd) {
                QColor dot = domainColor(port.domain);
                if (port.waiting)    dot = QColor(0xd9, 0xa4, 0x5b);
                if (port.noFreeSlot) dot = QColor(0xc2, 0x54, 0x50);
                p->setBrush(dot);
                p->setPen(Qt::NoPen);
                if (leftAnchors())
                    p->drawEllipse(QPointF(0.0, cy), 4, 4);
                if (rightAnchors())
                    p->drawEllipse(QPointF(kWidth, cy), 4, 4);
            }
            y += kRowH;
        }
        y += kBandPad;
    }
}

void PatchNodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    const QString port = portAt(event->pos());
    if (port.startsWith(QLatin1String("add:"))) {
        emit addCategoryClicked(port.mid(4), event->scenePos());
        event->accept();
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

} // namespace WildPalms::AppPatchbay
