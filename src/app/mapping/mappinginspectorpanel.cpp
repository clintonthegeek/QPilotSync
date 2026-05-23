#include "mappinginspectorpanel.h"

#include <array>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace WildPalms::AppMapping {

namespace {
struct ModeEntry { const char *value; const char *label; };
constexpr std::array<ModeEntry, 4> kModes = {{
    {"TwoWay",          "Two-way"},
    {"OneWayUpload",    "Palm → Provider"},
    {"OneWayDownload",  "Provider → Palm"},
    {"Disabled",        "Disabled"},
}};

constexpr std::array<const char*, 6> kPolicies = {
    "SourceWins", "TargetWins", "Duplicate",
    "Skip", "AskUser", "LastWriteWins",
};
} // namespace

MappingInspectorPanel::MappingInspectorPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void MappingInspectorPanel::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    m_placeholder = new QLabel(
        tr("Select a connection to edit its properties."), m_stack);
    m_placeholder->setAlignment(Qt::AlignCenter);
    QFont f = m_placeholder->font();
    f.setItalic(true);
    m_placeholder->setFont(f);
    m_stack->addWidget(m_placeholder);

    m_editor = new QWidget(m_stack);
    auto *form = new QFormLayout(m_editor);
    form->setContentsMargins(0, 0, 0, 0);

    m_modeCombo = new QComboBox(m_editor);
    for (const auto &m : kModes)
        m_modeCombo->addItem(QString::fromLatin1(m.label), QString::fromLatin1(m.value));

    m_policyCombo = new QComboBox(m_editor);
    for (const auto *p : kPolicies)
        m_policyCombo->addItem(QString::fromLatin1(p), QString::fromLatin1(p));

    m_enabledCheck = new QCheckBox(m_editor);

    form->addRow(tr("Sync Mode"),       m_modeCombo);
    form->addRow(tr("Conflict Policy"), m_policyCombo);
    form->addRow(tr("Enabled"),         m_enabledCheck);

    m_stack->addWidget(m_editor);
    m_stack->setCurrentWidget(m_placeholder);

    connect(m_modeCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,           &MappingInspectorPanel::emitChange);
    connect(m_policyCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,           &MappingInspectorPanel::emitChange);
    connect(m_enabledCheck, &QCheckBox::toggled,
            this,           &MappingInspectorPanel::emitChange);
}

void MappingInspectorPanel::setSelectedMapping(const QString &mappingId,
                                                const QJsonObject &json)
{
    m_currentMappingId = mappingId;
    m_currentJson      = json;

    if (mappingId.isEmpty()) {
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }

    m_suppressEmit = true;
    const QString modeVal =
        json.value(QStringLiteral("mode")).toString(QStringLiteral("TwoWay"));
    const int modeIdx = m_modeCombo->findData(modeVal);
    m_modeCombo->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);

    QString policyVal = json.value(QStringLiteral("conflictResolution")).toString();
    if (policyVal.isEmpty())
        policyVal = json.value(QStringLiteral("conflictPolicy")).toString();
    if (policyVal.isEmpty())
        policyVal = QStringLiteral("AskUser");
    const int policyIdx = m_policyCombo->findData(policyVal);
    m_policyCombo->setCurrentIndex(policyIdx >= 0 ? policyIdx : 4);

    m_enabledCheck->setChecked(json.value(QStringLiteral("enabled")).toBool());

    m_stack->setCurrentWidget(m_editor);
    m_suppressEmit = false;
}

void MappingInspectorPanel::emitChange()
{
    if (m_suppressEmit) return;
    if (m_currentMappingId.isEmpty()) return;

    QJsonObject updated = m_currentJson;
    updated[QStringLiteral("mode")] =
        m_modeCombo->currentData().toString();
    const QString policy = m_policyCombo->currentData().toString();
    updated[QStringLiteral("conflictResolution")] = policy;
    updated[QStringLiteral("conflictPolicy")]     = policy;
    updated[QStringLiteral("enabled")] = m_enabledCheck->isChecked();

    m_currentJson = updated;
    Q_EMIT mappingEdited(m_currentMappingId, updated);
}

} // namespace WildPalms::AppMapping
