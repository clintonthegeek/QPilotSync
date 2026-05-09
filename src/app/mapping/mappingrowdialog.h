#ifndef MAPPINGROWDIALOG_H
#define MAPPINGROWDIALOG_H

#include <QDialog>

// MappingRowDialog lives in WildPalmsAppMapping (separate static lib)
// instead of WildPalmsCore directly because WildPalmsCore PUBLIC-exposes
// src/core/ on the include path, where there is a *different*
// synctypes.h in the WP-local Sync:: namespace. By compiling this TU
// outside WildPalmsCore (no src/core/ on path) we get libkalburator's
// types/synctypes.h (Kalburator::Sync namespace) — the one we want.
#include "synctypes.h"

class QLineEdit;
class QComboBox;
class QCheckBox;

class MappingRowDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MappingRowDialog(QWidget *parent = nullptr);
    ~MappingRowDialog() override = default;

    // Populate the source-backend picker. Defaults to a single
    // "calendar-palm" entry if not called.
    void setSourceBackends(const QStringList &ids);

    // Populate the target-backend picker. Defaults to "rawfiles-cal" if
    // not called (preserves add-mode default for old callers).
    void setTargetBackends(const QStringList &ids);

    // Pre-populate the form from an existing mapping. Caller indicates
    // edit-mode by calling this; otherwise the dialog is in "Add" mode
    // and seeds a new uuid into id.
    void setMapping(const Kalburator::Sync::SyncMapping &mapping);

    // Snapshot of the form. Safe to call before exec() too — returns
    // the current widget state (edit-mode → original mapping;
    // add-mode → freshly seeded skeleton).
    Kalburator::Sync::SyncMapping mapping() const;

private:
    void buildUi();
    void applyMapping(const Kalburator::Sync::SyncMapping &m);

    QLineEdit  *m_idEdit        = nullptr;
    QComboBox  *m_sourceCombo   = nullptr;
    QComboBox  *m_targetCombo   = nullptr;
    QLineEdit  *m_sourceCalEdit = nullptr;
    QLineEdit  *m_targetCalEdit = nullptr;
    QComboBox  *m_modeCombo     = nullptr;
    QComboBox  *m_conflictCombo = nullptr;
    QCheckBox  *m_enabledCheck  = nullptr;
    bool        m_addMode       = true;
};

#endif // MAPPINGROWDIALOG_H
