#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERROW_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERROW_H

#include <QWidget>

class QComboBox;
class QLabel;

namespace WildPalms::Wizard {

struct WizardState;

/// One row on the Bindings page. Renders a label + QComboBox whose items
/// are "Local files" plus every domain-matching collection across the
/// connected WizardAccounts. Item data is QStringList{accountId,
/// collectionId} — both empty for Local files. Read-only collections are
/// listed but disabled.
class TargetPickerRow : public QWidget {
    Q_OBJECT
public:
    TargetPickerRow(const QString &pluginId,
                    WizardState *state,
                    QWidget *parent = nullptr);

    QString pluginId() const { return m_pluginId; }

    /// Repopulate from state->accounts, restore the current selection from
    /// the row's MappingSpec, and reset stale bindings to RawFiles.
    void rebuild();

signals:
    /// Empty ids == user picked "Local files".
    void bindingSelected(const QString &accountId, const QString &collectionId);

private:
    void onCurrentIndexChanged(int idx);

    QString      m_pluginId;
    WizardState *m_state;
    QComboBox   *m_combo {nullptr};
    QLabel      *m_hint  {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
