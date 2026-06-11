// src/app/patchbay/patchnodeitem.h
#pragma once

#include <QGraphicsObject>

#include <graffodil/IGraphNode.h>
#include <graffodil/Types.h>

#include "patchbaytypes.h"

namespace WildPalms::AppPatchbay {

/// Generic patchbay node: renders a NodeDesc (header + bands + port rows)
/// and exposes Graffodil anchors per port. Anchor id grammar:
/// "<portId>@l" / "<portId>@r" — which sides exist depends on NodeKind:
///   Palm → @r only; Hub → @l and @r; Remote/GhostRemote → @l only.
/// Anchor.metadata is a QVariantMap {"port": portId, "kind": int(PortKind),
/// "domain": domain, "node": nodeId} used by the view for drop validation.
class PatchNodeItem : public QGraphicsObject, public Graffodil::IGraphNode {
    Q_OBJECT
public:
    explicit PatchNodeItem(const NodeDesc &desc);

    void setDesc(const NodeDesc &desc);
    NodeDesc desc() const { return m_desc; }

    // geometry constants (shared with the view's layout math)
    static constexpr qreal kWidth = 200.0;
    static constexpr qreal kHeaderH = 28.0;
    static constexpr qreal kBandHeaderH = 20.0;
    static constexpr qreal kRowH = 22.0;
    static constexpr qreal kBandPad = 6.0;

    qreal contentHeight() const;             ///< full node height for layout
    /// Item-coords center of a port's row; the view uses this for the
    /// inline category editor; -1 y if port unknown.
    QPointF portRowCenter(const QString &portId) const;
    /// Port at an item-coords position (for AddCategory click routing).
    QString portAt(const QPointF &itemPos) const;

    // ── IGraphNode ────────────────────────────────────────────────────
    QString nodeId() const override { return m_desc.id; }
    QList<Graffodil::Anchor> anchors() const override;
    QRectF nodeBoundingRect() const override;
    QGraphicsItem *graphicsItem() override { return this; }

    // ── QGraphicsItem ────────────────────────────────────────────────
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt,
               QWidget *w) override;

signals:
    /// Click landed on an AddCategory ghost row.
    void addCategoryClicked(const QString &domain, const QPointF &scenePos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    struct RowRef { int band; int port; qreal y; };   // y = row top, item coords
    QList<RowRef> rows() const;
    bool leftAnchors() const;
    bool rightAnchors() const;

    NodeDesc m_desc;
};

} // namespace WildPalms::AppPatchbay
