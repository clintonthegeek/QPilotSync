// src/app/patchbay/syncpatchbayview.h
#pragma once

#include <QGraphicsView>
#include <QHash>

#include "patchbaytypes.h"

class QGraphicsProxyWidget;
class QContextMenuEvent;

namespace Graffodil {
class GraphScene;
class DefaultGraphTool;
class CreateEdgeTool;
class IGraphNode;
}

namespace WildPalms::AppPatchbay {

class PatchbayModel;
class PatchNodeItem;
class SignalPathWire;

class SyncPatchbayView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SyncPatchbayView(QWidget *parent = nullptr);
    ~SyncPatchbayView() override;

    void setModel(PatchbayModel *model);   ///< borrowed; triggers rebuild
    void rebuild();                        ///< re-sync scene from model

    // test seams (F.3 convention)
    int nodeCount() const;
    int wireCount() const;
    int strandCount() const;
    SignalPathWire *wireItem(const QString &mappingId) const;
    QPointF nodePos(const QString &nodeId) const;

    /// Test seam: drive the CreateEdgeTool result path directly.
    void requestEdgeForTest(const QString &sourceNodeId,
                            const QString &sourceAnchorId,
                            const QString &targetNodeId,
                            const QString &targetAnchorId);
    /// Delete all selected wires (Delete key path; also used by context menu).
    void deleteSelectedWires();

    /// Open an inline "+ category…" editor on the hub band for `domain`.
    void openCategoryEditor(const QString &domain);
    // test seams
    void openCategoryEditorForTest(const QString &domain) { openCategoryEditor(domain); }
    bool categoryEditorVisible() const;
    void commitCategoryEditorForTest(const QString &text);

signals:
    void wireSelected(const QString &mappingId);   ///< empty = deselected
    void addCategoryRequested(const QString &domain, const QPointF &scenePos);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void onEdgeRequested(Graffodil::IGraphNode *source,
                         const QString &sourceAnchorId,
                         Graffodil::IGraphNode *target,
                         const QString &targetAnchorId);
    void onSelectionChanged();
    void applyColumnLayout();

    PatchbayModel *m_model = nullptr;
    Graffodil::GraphScene *m_scene = nullptr;
    Graffodil::DefaultGraphTool *m_tool = nullptr;
    Graffodil::CreateEdgeTool *m_createTool = nullptr;
    QHash<QString, PatchNodeItem *> m_nodeItems;     // nodeId → item
    QHash<QString, SignalPathWire *> m_wireItems;    // mappingId → item
    QList<SignalPathWire *> m_strandItems;
    bool m_didInitialFit = false;

    QGraphicsProxyWidget *m_categoryEditor = nullptr;
    QString m_categoryEditorDomain;
};

} // namespace WildPalms::AppPatchbay
