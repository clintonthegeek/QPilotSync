#ifndef WILDPALMS_APP_MAPPING_MAPPINGINSPECTORPANEL_H
#define WILDPALMS_APP_MAPPING_MAPPINGINSPECTORPANEL_H

#include <QWidget>
#include <QJsonObject>

class QComboBox;
class QCheckBox;
class QLabel;
class QStackedWidget;

namespace WildPalms::AppMapping {

class SyncMappingGraphView;

/// Inspector strip pinned to the bottom of SyncMappingsPage. Shows
/// Sync Mode + Conflict Policy + Enabled controls for the selected
/// edge. When no edge is selected, shows a "Select a connection to
/// edit its properties." placeholder.
///
/// On user edits the panel calls SyncMappingGraphView::updateMapping()
/// (added on demand) — for F.3 T8 we instead emit a signal that the
/// page wraps and dispatches. The panel never reaches into the view's
/// edge list directly.
class MappingInspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit MappingInspectorPanel(QWidget *parent = nullptr);

    /// Show controls for the selected mapping. Empty `mappingId` →
    /// placeholder.
    void setSelectedMapping(const QString &mappingId, const QJsonObject &json);

signals:
    /// Emitted when the user edits any field. The page subscribes and
    /// writes the change back into the graph view's mapping list.
    void mappingEdited(const QString &mappingId, const QJsonObject &updatedJson);

private slots:
    void emitChange();

private:
    void buildUi();

    QStackedWidget *m_stack {nullptr};
    QLabel         *m_placeholder {nullptr};
    QWidget        *m_editor {nullptr};

    QComboBox *m_modeCombo {nullptr};
    QComboBox *m_policyCombo {nullptr};
    QCheckBox *m_enabledCheck {nullptr};

    QString     m_currentMappingId;
    QJsonObject m_currentJson;
    bool        m_suppressEmit {false};
};

} // namespace WildPalms::AppMapping

#endif
