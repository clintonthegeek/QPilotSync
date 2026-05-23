#include "providernode.h"

#include <QPainter>
#include <QApplication>
#include <QPalette>
#include <QFont>

namespace WildPalms::AppMapping {

ProviderNode::ProviderNode(const QString &providerId,
                           const QString &displayName,
                           const QList<Kalburator::Sync::CollectionInfo> &collections,
                           const QString &busyText,
                           QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_providerId(providerId)
    , m_displayName(displayName)
    , m_collections(collections)
    , m_busyText(busyText)
{
    setFlag(ItemIsSelectable, false);
    setFlag(ItemIsMovable, false);
}

QRectF ProviderNode::boundingRect() const
{
    const int rows = m_busyText.isEmpty()
        ? std::max<int>(1, m_collections.size())
        : 1;
    return QRectF(0.0, 0.0, kNodeWidth,
                  kHeaderHeight + rows * kRowHeight);
}

QRectF ProviderNode::anchorLocalRect(int row) const
{
    if (!m_busyText.isEmpty()) return {};
    if (row < 0 || row >= m_collections.size()) return {};
    const qreal cy = kHeaderHeight + row * kRowHeight + kRowHeight / 2.0;
    const qreal cx = 8.0;
    return QRectF(cx - kAnchorRadius, cy - kAnchorRadius,
                  kAnchorRadius * 2, kAnchorRadius * 2);
}

QString ProviderNode::collectionDomain(const QString &collectionId) const
{
    for (const auto &c : m_collections) {
        if (c.id == collectionId)
            return c.type.isEmpty() ? QStringLiteral("unknown") : c.type;
    }
    return QStringLiteral("unknown");
}

QPointF ProviderNode::collectionAnchorScenePos(const QString &collectionId) const
{
    if (!m_busyText.isEmpty()) return QPointF();
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).id == collectionId) {
            const QRectF r = anchorLocalRect(i);
            return mapToScene(r.center());
        }
    }
    return QPointF();
}

QString ProviderNode::collectionAtScenePos(const QPointF &scenePos) const
{
    if (!m_busyText.isEmpty()) return {};
    const QPointF local = mapFromScene(scenePos);
    for (int i = 0; i < m_collections.size(); ++i) {
        if (anchorLocalRect(i).contains(local))
            return m_collections.at(i).id;
    }
    return {};
}

void ProviderNode::paint(QPainter *painter,
                         const QStyleOptionGraphicsItem * /*option*/,
                         QWidget * /*widget*/)
{
    const QPalette pal = QApplication::palette();
    const QRectF rect = boundingRect();

    painter->setBrush(pal.color(QPalette::Base));
    painter->setPen(pal.color(QPalette::Mid));
    painter->drawRoundedRect(rect, 4.0, 4.0);

    const QRectF headerRect(0.0, 0.0, kNodeWidth, kHeaderHeight);
    // Green-ish header to visually distinguish from Palm nodes (which use
    // Highlight-dark in T4).
    QColor headerCol(0x2a, 0x4a, 0x2a);
    painter->setBrush(headerCol);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(headerRect, 4.0, 4.0);
    painter->drawRect(QRectF(0.0, kHeaderHeight / 2.0,
                             kNodeWidth, kHeaderHeight / 2.0));

    painter->setPen(pal.color(QPalette::HighlightedText));
    QFont f = painter->font();
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(headerRect.adjusted(8, 0, -8, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, m_displayName);
    f.setBold(false);
    painter->setFont(f);

    if (!m_busyText.isEmpty()) {
        const QRectF row(0.0, kHeaderHeight, kNodeWidth, kRowHeight);
        QFont italic = painter->font();
        italic.setItalic(true);
        painter->setFont(italic);
        painter->setPen(pal.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(row.adjusted(8, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft, m_busyText);
        return;
    }

    for (int i = 0; i < m_collections.size(); ++i) {
        const auto &c = m_collections.at(i);
        const QRectF row(0.0, kHeaderHeight + i * kRowHeight,
                         kNodeWidth, kRowHeight);
        if (i % 2 == 1)
            painter->fillRect(row, pal.color(QPalette::AlternateBase));

        painter->setPen(pal.color(QPalette::Text));
        painter->drawText(row.adjusted(20, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft, c.name);

        const QRectF a = anchorLocalRect(i);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->setPen(pal.color(QPalette::Dark));
        painter->drawEllipse(a);
    }
}

} // namespace WildPalms::AppMapping
