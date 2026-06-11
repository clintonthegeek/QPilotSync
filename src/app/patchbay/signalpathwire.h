// src/app/patchbay/signalpathwire.h
#pragma once

#include <graffodil/GraphEdgeItem.h>

#include "patchbaytypes.h"

namespace WildPalms::AppPatchbay {

/// Signal-path edge (spec §6): domain-colored stroke; chevrons for one-way
/// direction; dashed/desaturated when disabled; red when broken; glyph bead
/// via Graffodil's edge label. Also renders read-only palm strands.
class SignalPathWire : public Graffodil::GraphEdgeItem {
public:
    enum class Role { Wire, Strand };

    SignalPathWire(Role role,
                   Graffodil::IGraphNode *source, const QString &sourceAnchorId,
                   Graffodil::IGraphNode *target, const QString &targetAnchorId,
                   const QString &domain);

    Role role() const { return m_role; }
    QString domain() const { return m_domain; }

    /// Wire role only.
    void setWireState(WireState state);
    WireState wireState() const { return m_wireState; }
    void setBead(const QString &glyph);

    /// Strand role only.
    void setStrandState(StrandState state, bool wholeDomain);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    void applyPen();

    Role m_role;
    QString m_domain;
    WireState m_wireState = WireState::TwoWay;
    StrandState m_strandState = StrandState::Solid;
    bool m_wholeDomain = false;
};

} // namespace WildPalms::AppPatchbay
