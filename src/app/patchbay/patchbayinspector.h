// src/app/patchbay/patchbayinspector.h
#pragma once

#include <QJsonObject>
#include <QWidget>

#include "runtime/routemapping.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QStackedWidget;

namespace WildPalms::AppPatchbay {

/// Right-side inspector (spec §7.2): mode, conflict policy, enabled,
/// plus a human explanation of the RouteStatus.
class PatchbayInspector : public QWidget {
    Q_OBJECT
public:
    explicit PatchbayInspector(QWidget *parent = nullptr);

    /// Empty mappingId → placeholder page.
    void setSelectedMapping(const QString &mappingId, const QJsonObject &json,
                            WildPalms::Runtime::RouteStatus status,
                            const QString &categoryName);

signals:
    /// Field edits; the page forwards to PatchbayModel::updateMapping.
    void mappingEdited(const QString &mappingId, const QJsonObject &changes);

private:
    void emitChanges();

    QString m_mappingId;
    bool m_loading = false;
    QStackedWidget *m_stack = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_policy = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace WildPalms::AppPatchbay
