#ifndef WILDPALMS_APP_MAPPING_PALMDBNODE_H
#define WILDPALMS_APP_MAPPING_PALMDBNODE_H

#include <QGraphicsItem>
#include <QString>
#include <QStringList>
#include <QRectF>
#include <QVector>

namespace WildPalms::AppMapping {

/// QGraphicsItem rendering a single Palm database (DatebookDB,
/// AddressDB, MemoDB, ToDoDB) with one port row per populated
/// category slot. Right-side anchors carry the slot index.
///
/// The node is rendered with a coloured title bar (theme-derived) and
/// stacks port rows below. Slots are read from the constructor-supplied
/// 16-entry list; empty entries are skipped on render (slot 0 is shown
/// as "Unfiled"). If all entries are empty a single disabled placeholder
/// row is shown reading "Sync once to discover categories".
class PalmDbNode : public QGraphicsItem {
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    /// dbName: "DatebookDB" / "AddressDB" / "MemoDB" / "ToDoDB"
    /// humanName: title bar text (e.g. "Calendar — DatebookDB")
    /// domain: one of "calendar" / "contacts" / "memos" / "todos"
    /// slotNames: exactly 16 entries; empty means slot unnamed
    PalmDbNode(const QString &dbName,
               const QString &humanName,
               const QString &domain,
               const QStringList &slotNames,
               QGraphicsItem *parent = nullptr);

    QString dbName()  const { return m_dbName; }
    QString domain()  const { return m_domain; }

    /// Scene position of the right-side anchor for `slot`, or invalid
    /// QPointF if slot is unpopulated.
    QPointF slotAnchorScenePos(int slot) const;

    /// Slot index (0..15) for the right-side anchor at scene position,
    /// or -1 if no anchor is there.
    int slotAtScenePos(const QPointF &scenePos) const;

    /// Width / height for layout by the parent view.
    QRectF boundingRect() const override;

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    /// Convenience for the view: list of active slot indices in
    /// ascending order (slot 0 is included only when its name is
    /// non-empty OR domain is shown — see implementation).
    QList<int> activeSlots() const { return m_activeSlots; }

private:
    void rebuildLayout();
    QRectF anchorLocalRect(int slot) const;

    QString     m_dbName;
    QString     m_humanName;
    QString     m_domain;
    QStringList m_slotNames;   // 16 entries
    QList<int>  m_activeSlots; // indices into m_slotNames where name non-empty
    bool        m_hasContent {false}; // false = show placeholder row

    static constexpr qreal kNodeWidth     = 200.0;
    static constexpr qreal kHeaderHeight  = 28.0;
    static constexpr qreal kRowHeight     = 22.0;
    static constexpr qreal kAnchorRadius  = 5.0;
};

} // namespace WildPalms::AppMapping

#endif
