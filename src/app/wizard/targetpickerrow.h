#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERROW_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERROW_H

#include <QWidget>
#include <QStringList>

class QComboBox;
class QLabel;

namespace WildPalms::Wizard {

struct WizardState;

/// One row on the TargetPickerPage. Knows its pluginId and the list of
/// account kinds compatible with that pluginId. Renders a label +
/// QComboBox. The page populates the combo by calling rebuild() with
/// the current WizardState's accounts.
class TargetPickerRow : public QWidget {
    Q_OBJECT
public:
    TargetPickerRow(const QString &pluginId,
                    const QStringList &compatibleKinds,
                    WizardState *state,
                    QWidget *parent = nullptr);

    QString pluginId() const { return m_pluginId; }
    QStringList compatibleKinds() const { return m_compatibleKinds; }

    /// Repopulate the combo from m_state->accounts. Called by the
    /// page on initializePage() and after addNewAccount/selectExistingAccount.
    void rebuild();

signals:
    /// Emitted when the user picks "Add new <kind>…" from the dropdown.
    void addNewRequested(const QString &kind);

    /// Emitted when the user picks an existing WizardAccount by id.
    /// Empty id == user picked "Local files" (RawFiles).
    void existingSelected(const QString &accountId);

private:
    void onCurrentIndexChanged(int idx);

    QString      m_pluginId;
    QStringList  m_compatibleKinds;
    WizardState *m_state;
    QComboBox   *m_combo {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
