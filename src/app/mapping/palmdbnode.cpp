#include "palmdbnode.h"

#include <QPainter>
#include <QGraphicsScene>
#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QFontMetricsF>
#include <QDebug>

namespace WildPalms::AppMapping {

PalmDbNode::PalmDbNode(const QString &dbName,
                       const QString &humanName,
                       const QString &domain,
                       const QStringList &slotNames,
                       QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_dbName(dbName)
    , m_humanName(humanName)
    , m_domain(domain)
{
    if (slotNames.size() != 16) {
        qWarning() << "[PalmDbNode] expected 16 slot names for"
                   << dbName << "got" << slotNames.size()
                   << "— falling back to empty list";
        m_slotNames = QStringList();
        for (int i = 0; i < 16; ++i) m_slotNames << QString();
    } else {
        m_slotNames = slotNames;
    }
    setFlag(ItemIsSelectable, false);
    setFlag(ItemIsMovable, false);
    rebuildLayout();
}

void PalmDbNode::rebuildLayout()
{
    m_activeSlots.clear();
    for (int slot = 0; slot < m_slotNames.size(); ++slot) {
        if (!m_slotNames.at(slot).isEmpty())
            m_activeSlots.append(slot);
    }
    m_hasContent = !m_activeSlots.isEmpty();
}

QRectF PalmDbNode::boundingRect() const
{
    const int rows = m_hasContent ? m_activeSlots.size() : 1; // placeholder row
    const qreal h = kHeaderHeight + rows * kRowHeight;
    return QRectF(0.0, 0.0, kNodeWidth, h);
}

QRectF PalmDbNode::anchorLocalRect(int slot) const
{
    if (!m_hasContent) return {};
    const int idx = m_activeSlots.indexOf(slot);
    if (idx < 0) return {};
    const qreal cy = kHeaderHeight + idx * kRowHeight + kRowHeight / 2.0;
    const qreal cx = kNodeWidth - 8.0;
    return QRectF(cx - kAnchorRadius, cy - kAnchorRadius,
                  kAnchorRadius * 2, kAnchorRadius * 2);
}

QPointF PalmDbNode::slotAnchorScenePos(int slot) const
{
    const QRectF r = anchorLocalRect(slot);
    if (r.isEmpty()) return QPointF();
    return mapToScene(r.center());
}

int PalmDbNode::slotAtScenePos(const QPointF &scenePos) const
{
    if (!m_hasContent) return -1;
    const QPointF local = mapFromScene(scenePos);
    for (int slot : m_activeSlots) {
        if (anchorLocalRect(slot).contains(local))
            return slot;
    }
    return -1;
}

void PalmDbNode::paint(QPainter *painter,
                       const QStyleOptionGraphicsItem * /*option*/,
                       QWidget * /*widget*/)
{
    const QPalette pal = QApplication::palette();
    const QRectF rect = boundingRect();

    // Body
    painter->setBrush(pal.color(QPalette::Base));
    painter->setPen(pal.color(QPalette::Mid));
    painter->drawRoundedRect(rect, 4.0, 4.0);

    // Header bar
    const QRectF headerRect(0.0, 0.0, kNodeWidth, kHeaderHeight);
    painter->setBrush(pal.color(QPalette::Highlight).darker(160));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(headerRect, 4.0, 4.0);
    // Square off the bottom of the rounded rect by overdrawing.
    painter->drawRect(QRectF(0.0, kHeaderHeight / 2.0,
                             kNodeWidth, kHeaderHeight / 2.0));

    painter->setPen(pal.color(QPalette::HighlightedText));
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->drawText(headerRect.adjusted(8, 0, -8, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, m_humanName);
    titleFont.setBold(false);
    painter->setFont(titleFont);

    // Rows
    painter->setPen(pal.color(QPalette::Text));
    if (!m_hasContent) {
        const QRectF row(0.0, kHeaderHeight, kNodeWidth, kRowHeight);
        QFont italic = painter->font();
        italic.setItalic(true);
        painter->setFont(italic);
        painter->setPen(pal.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(row.adjusted(8, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          QObject::tr("Sync once to discover categories"));
        return;
    }

    for (int i = 0; i < m_activeSlots.size(); ++i) {
        const int slot = m_activeSlots.at(i);
        const QRectF row(0.0, kHeaderHeight + i * kRowHeight,
                         kNodeWidth, kRowHeight);
        if (i % 2 == 1) {
            painter->fillRect(row, pal.color(QPalette::AlternateBase));
        }
        painter->setPen(pal.color(QPalette::Text));
        painter->drawText(row.adjusted(8, 0, -20, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          m_slotNames.at(slot));

        // Right-side anchor dot
        const QRectF a = anchorLocalRect(slot);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->setPen(pal.color(QPalette::Dark));
        painter->drawEllipse(a);
    }
}

} // namespace WildPalms::AppMapping
