#include "mappingrowdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QUuid>
#include <QVBoxLayout>

namespace {
// Mode enum labels — must match Kalburator::Sync::SyncMode. Plan
// referenced "TwoWay/MirrorAtoB/MirrorBtoA" but the real enum is
// {Disabled, OneWayUpload, OneWayDownload, TwoWay}.
constexpr const char *kModeDisabled       = "Disabled";
constexpr const char *kModeOneWayUpload   = "OneWayUpload";
constexpr const char *kModeOneWayDownload = "OneWayDownload";
constexpr const char *kModeTwoWay         = "TwoWay";

// ConflictResolution enum labels — Plan referenced
// "Manual/UseSource/UseTarget/AskUser" but the real enum is
// {SourceWins, TargetWins, Duplicate, Skip, AskUser, LastWriteWins,
//  CustomMerge}. CustomMerge is intentionally omitted from the picker
// (it requires merge-rule configuration not exposed in this MVP UI).
constexpr const char *kPolicySourceWins    = "SourceWins";
constexpr const char *kPolicyTargetWins    = "TargetWins";
constexpr const char *kPolicyDuplicate     = "Duplicate";
constexpr const char *kPolicySkip          = "Skip";
constexpr const char *kPolicyAskUser       = "AskUser";
constexpr const char *kPolicyLastWriteWins = "LastWriteWins";

QString modeToString(Kalburator::Sync::SyncMode m) {
    using M = Kalburator::Sync::SyncMode;
    switch (m) {
        case M::Disabled:        return kModeDisabled;
        case M::OneWayUpload:    return kModeOneWayUpload;
        case M::OneWayDownload:  return kModeOneWayDownload;
        case M::TwoWay:          return kModeTwoWay;
    }
    return kModeTwoWay;
}

Kalburator::Sync::SyncMode modeFromString(const QString &s) {
    using M = Kalburator::Sync::SyncMode;
    if (s == kModeDisabled)       return M::Disabled;
    if (s == kModeOneWayUpload)   return M::OneWayUpload;
    if (s == kModeOneWayDownload) return M::OneWayDownload;
    return M::TwoWay;
}

QString policyToString(Kalburator::Sync::ConflictResolution p) {
    using P = Kalburator::Sync::ConflictResolution;
    switch (p) {
        case P::SourceWins:     return kPolicySourceWins;
        case P::TargetWins:     return kPolicyTargetWins;
        case P::Duplicate:      return kPolicyDuplicate;
        case P::Skip:           return kPolicySkip;
        case P::AskUser:        return kPolicyAskUser;
        case P::LastWriteWins:  return kPolicyLastWriteWins;
        case P::CustomMerge:    return kPolicyAskUser;  // not exposed
    }
    return kPolicyAskUser;
}

Kalburator::Sync::ConflictResolution policyFromString(const QString &s) {
    using P = Kalburator::Sync::ConflictResolution;
    if (s == kPolicySourceWins)    return P::SourceWins;
    if (s == kPolicyTargetWins)    return P::TargetWins;
    if (s == kPolicyDuplicate)     return P::Duplicate;
    if (s == kPolicySkip)          return P::Skip;
    if (s == kPolicyLastWriteWins) return P::LastWriteWins;
    return P::AskUser;
}
} // namespace

MappingRowDialog::MappingRowDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit mapping"));
    buildUi();

    // Default add-mode skeleton.
    Kalburator::Sync::SyncMapping skeleton;
    skeleton.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    skeleton.sourceBackend = QStringLiteral("calendar-palm");
    skeleton.targetBackend = QStringLiteral("rawfiles-cal");
    skeleton.mode = Kalburator::Sync::SyncMode::TwoWay;
    skeleton.conflictPolicy = Kalburator::Sync::ConflictResolution::AskUser;
    skeleton.lossPolicy = Kalburator::Sync::WhenLossWouldOccur::Warn;
    skeleton.enabled = true;
    applyMapping(skeleton);
}

void MappingRowDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    auto *form  = new QFormLayout();

    m_idEdit        = new QLineEdit(this);
    m_sourceCombo   = new QComboBox(this);
    m_sourceCombo->addItem(QStringLiteral("calendar-palm"));
    m_sourceCalEdit = new QLineEdit(this);
    m_targetCalEdit = new QLineEdit(this);
    m_modeCombo     = new QComboBox(this);
    m_modeCombo->addItems({kModeDisabled,
                           kModeOneWayUpload,
                           kModeOneWayDownload,
                           kModeTwoWay});
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItems({kPolicySourceWins,
                               kPolicyTargetWins,
                               kPolicyDuplicate,
                               kPolicySkip,
                               kPolicyAskUser,
                               kPolicyLastWriteWins});
    m_enabledCheck  = new QCheckBox(this);

    form->addRow(tr("ID"), m_idEdit);
    form->addRow(tr("Source backend"), m_sourceCombo);
    form->addRow(tr("Source collection"), m_sourceCalEdit);
    form->addRow(tr("Target collection"), m_targetCalEdit);
    form->addRow(tr("Mode"), m_modeCombo);
    form->addRow(tr("Conflict policy"), m_conflictCombo);
    form->addRow(tr("Enabled"), m_enabledCheck);

    outer->addLayout(form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

void MappingRowDialog::setSourceBackends(const QStringList &ids)
{
    m_sourceCombo->clear();
    m_sourceCombo->addItems(ids);
}

void MappingRowDialog::setMapping(const Kalburator::Sync::SyncMapping &m)
{
    m_addMode = false;
    applyMapping(m);
}

void MappingRowDialog::applyMapping(const Kalburator::Sync::SyncMapping &m)
{
    m_idEdit->setText(m.id);
    int srcIdx = m_sourceCombo->findText(m.sourceBackend);
    if (srcIdx >= 0) {
        m_sourceCombo->setCurrentIndex(srcIdx);
    } else {
        m_sourceCombo->addItem(m.sourceBackend);
        m_sourceCombo->setCurrentText(m.sourceBackend);
    }
    m_sourceCalEdit->setText(m.sourceCalendar);
    m_targetCalEdit->setText(m.targetCalendar);
    m_modeCombo->setCurrentText(modeToString(m.mode));
    m_conflictCombo->setCurrentText(policyToString(m.conflictPolicy));
    m_enabledCheck->setChecked(m.enabled);
}

Kalburator::Sync::SyncMapping MappingRowDialog::mapping() const
{
    Kalburator::Sync::SyncMapping m;
    m.id              = m_idEdit->text();
    m.sourceBackend   = m_sourceCombo->currentText();
    m.sourceCalendar  = m_sourceCalEdit->text();
    // RawFiles target locked per design spec §5.1; not exposed in MVP UI.
    m.targetBackend   = QStringLiteral("rawfiles-cal");
    m.targetCalendar  = m_targetCalEdit->text();
    m.mode            = modeFromString(m_modeCombo->currentText());
    m.conflictPolicy  = policyFromString(m_conflictCombo->currentText());
    // lossPolicy not exposed in MVP UI; default to Warn.
    m.lossPolicy      = Kalburator::Sync::WhenLossWouldOccur::Warn;
    m.enabled         = m_enabledCheck->isChecked();
    return m;
}
