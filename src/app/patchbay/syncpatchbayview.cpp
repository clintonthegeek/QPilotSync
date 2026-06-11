// src/app/patchbay/syncpatchbayview.cpp
#include "syncpatchbayview.h"

#include <QGraphicsItem>

#include <graffodil/GraphScene.h>
#include <graffodil/DefaultGraphTool.h>
#include <graffodil/CreateEdgeTool.h>
#include <graffodil/Types.h>

#include "patchbaymodel.h"
#include "patchnodeitem.h"
#include "signalpathwire.h"

namespace WildPalms::AppPatchbay {

namespace {
constexpr qreal kPalmX = 40.0;
constexpr qreal kHubX = 460.0;
constexpr qreal kRemoteX = 900.0;
constexpr qreal kTopY = 40.0;
constexpr qreal kStackGap = 24.0;
} // namespace

SyncPatchbayView::SyncPatchbayView(QWidget *parent) : QGraphicsView(parent)
{
    m_scene = new Graffodil::GraphScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(QColor(0x16, 0x16, 0x1d));

    m_tool = new Graffodil::DefaultGraphTool(this);
    m_createTool = new Graffodil::CreateEdgeTool(this);
    m_tool->addAnchorRoute(m_createTool, 12.0);
    m_scene->setActiveTool(m_tool);

    connect(m_createTool, &Graffodil::CreateEdgeTool::edgeRequested,
            this, &SyncPatchbayView::onEdgeRequested);
    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &SyncPatchbayView::onSelectionChanged);
}

SyncPatchbayView::~SyncPatchbayView() = default;

void SyncPatchbayView::setModel(PatchbayModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
        connect(m_model, &PatchbayModel::rebuilt,
                this, &SyncPatchbayView::rebuild);
    rebuild();
}

void SyncPatchbayView::rebuild()
{
    // tear down previous items (scene registry does not own them)
    const auto cleared = m_scene->clearGraph();
    for (auto *e : cleared.edges) delete e->graphicsItem();
    for (auto *n : cleared.nodes) delete n->graphicsItem();
    m_nodeItems.clear();
    m_wireItems.clear();
    m_strandItems.clear();
    if (!m_model)
        return;

    // nodes
    for (const auto &desc : m_model->nodes()) {
        auto *item = new PatchNodeItem(desc);
        m_scene->addNode(item);
        m_nodeItems.insert(desc.id, item);
        connect(item, &PatchNodeItem::addCategoryClicked,
                this, &SyncPatchbayView::addCategoryRequested);
    }

    applyColumnLayout();

    // strands (under wires)
    auto *hub = m_nodeItems.value(QStringLiteral("hub"));
    auto *palm = m_nodeItems.value(QStringLiteral("palm"));
    if (hub && palm) {
        for (const auto &s : m_model->strands()) {
            auto *e = new SignalPathWire(SignalPathWire::Role::Strand,
                                         palm, s.palmPortId + QStringLiteral("@r"),
                                         hub, s.hubPortId + QStringLiteral("@l"),
                                         s.domain);
            e->setStrandState(s.state, s.wholeDomain);
            m_scene->addEdge(e);
            e->adjust();
            m_strandItems << e;
        }
    }

    // wires
    for (const auto &w : m_model->wires()) {
        auto *target = m_nodeItems.value(w.targetNodeId);
        if (!hub || !target)
            continue;
        auto *e = new SignalPathWire(SignalPathWire::Role::Wire,
                                     hub, w.sourcePortId + QStringLiteral("@r"),
                                     target, w.targetPortId + QStringLiteral("@l"),
                                     w.domain);
        e->setWireState(w.state);
        if (!w.beadGlyph.isEmpty())
            e->setBead(w.beadGlyph);
        m_scene->addEdge(e);
        e->adjust();
        m_wireItems.insert(w.mappingId, e);
    }

    m_scene->fitSceneRectToContent(120.0);
    if (!m_didInitialFit && !m_nodeItems.isEmpty()) {
        fitInView(m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40),
                  Qt::KeepAspectRatio);
        m_didInitialFit = true;
    }
}

void SyncPatchbayView::applyColumnLayout()
{
    Graffodil::LayoutResult layout;
    qreal palmY = kTopY, hubY = kTopY, remoteY = kTopY;
    // deterministic stacking in model order (spec §6 — manual LayoutResult)
    for (const auto &desc : m_model->nodes()) {
        auto *item = m_nodeItems.value(desc.id);
        if (!item) continue;
        switch (desc.kind) {
        case NodeKind::Palm:
            layout.nodePositions.insert(desc.id, QPointF(kPalmX, palmY));
            palmY += item->contentHeight() + kStackGap;
            break;
        case NodeKind::Hub:
            layout.nodePositions.insert(desc.id, QPointF(kHubX, hubY));
            hubY += item->contentHeight() + kStackGap;
            break;
        case NodeKind::Remote:
        case NodeKind::GhostRemote:
            layout.nodePositions.insert(desc.id, QPointF(kRemoteX, remoteY));
            remoteY += item->contentHeight() + kStackGap;
            break;
        }
    }
    m_scene->applyLayout(layout);
}

void SyncPatchbayView::onEdgeRequested(Graffodil::IGraphNode *source,
                                       const QString &sourceAnchorId,
                                       Graffodil::IGraphNode *target,
                                       const QString &targetAnchorId)
{
    if (!m_model || !source || !target)
        return;
    // normalize: hub end + remote end, either drag direction (spec §7.1)
    auto portOf = [](const QString &anchorId) {
        QString p = anchorId;
        if (p.endsWith(QLatin1String("@l")) || p.endsWith(QLatin1String("@r")))
            p.chop(2);
        return p;
    };
    QString hubPort, remoteNodeId, remotePort;
    auto classify = [&](Graffodil::IGraphNode *n, const QString &anchorId) {
        const QString port = portOf(anchorId);
        if (n->nodeId() == QLatin1String("hub")
            && (port.startsWith(QLatin1String("dom:"))
                || port.startsWith(QLatin1String("cat:"))))
            hubPort = port;
        else if (n->nodeId().startsWith(QLatin1String("remote:"))
                 && port.startsWith(QLatin1String("col:"))) {
            remoteNodeId = n->nodeId();
            remotePort = port;
        }
    };
    classify(source, sourceAnchorId);
    classify(target, targetAnchorId);
    if (hubPort.isEmpty() || remotePort.isEmpty())
        return;   // palm-tier or invalid endpoints: not user-wirable (spec §3)

    // "col:<collectionId>|<domain>" → collectionId; "remote:<id>" → providerId
    const QString providerId = remoteNodeId.mid(7);
    const QString body = remotePort.mid(4);
    const QString collectionId = body.left(body.lastIndexOf(QLatin1Char('|')));
    m_model->addMapping(hubPort, providerId, collectionId);
    // model rebuild redraws the scene; invalid drops are silently ignored
    // (addMapping validates domain compatibility + duplicates)
}

void SyncPatchbayView::onSelectionChanged()
{
    for (auto it = m_wireItems.constBegin(); it != m_wireItems.constEnd(); ++it) {
        if (it.value()->graphicsItem()->isSelected()) {
            emit wireSelected(it.key());
            return;
        }
    }
    emit wireSelected(QString());
}

int SyncPatchbayView::nodeCount() const { return m_nodeItems.size(); }
int SyncPatchbayView::wireCount() const { return m_wireItems.size(); }
int SyncPatchbayView::strandCount() const { return m_strandItems.size(); }

SignalPathWire *SyncPatchbayView::wireItem(const QString &mappingId) const
{
    return m_wireItems.value(mappingId);
}

QPointF SyncPatchbayView::nodePos(const QString &nodeId) const
{
    auto *item = m_nodeItems.value(nodeId);
    return item ? item->pos() : QPointF();
}

} // namespace WildPalms::AppPatchbay
