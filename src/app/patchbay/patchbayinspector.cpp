// src/app/patchbay/patchbayinspector.cpp
#include "patchbayinspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

using WildPalms::Runtime::RouteStatus;

namespace WildPalms::AppPatchbay {

namespace {
QString statusText(RouteStatus st, const QString &category)
{
    switch (st) {
    case RouteStatus::Active:
        return QStringLiteral("Active.");
    case RouteStatus::WaitingForDevice:
        return QStringLiteral(
            "Waiting for device: category \"%1\" will be created on the "
            "Palm at the next HotSync.").arg(category);
    case RouteStatus::NoFreeSlot:
        return QStringLiteral(
            "No free category slot on the device — Palm databases hold at "
            "most 16 categories. Remove a category to make room.");
    case RouteStatus::NotARoute:
        return QStringLiteral(
            "This row is disabled, malformed, or references an unknown "
            "conduit; it will not run.");
    }
    return {};
}
} // namespace

PatchbayInspector::PatchbayInspector(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    auto *placeholder = new QLabel(
        QStringLiteral("Select a connection to edit its properties."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    m_stack->addWidget(placeholder);

    auto *form = new QWidget(this);
    auto *layout = new QFormLayout(form);
    m_mode = new QComboBox(form);
    m_mode->addItems({QStringLiteral("TwoWay"),
                      QStringLiteral("OneWayUpload"),
                      QStringLiteral("OneWayDownload")});
    m_policy = new QComboBox(form);
    m_policy->addItems({QStringLiteral("LastWriteWins"),
                        QStringLiteral("SourceWins"),
                        QStringLiteral("TargetWins"),
                        QStringLiteral("Duplicate"),
                        QStringLiteral("Skip"),
                        QStringLiteral("AskUser")});
    m_enabled = new QCheckBox(QStringLiteral("Enabled"), form);
    m_status = new QLabel(form);
    m_status->setWordWrap(true);
    layout->addRow(QStringLiteral("Sync mode"), m_mode);
    layout->addRow(QStringLiteral("Conflicts"), m_policy);
    layout->addRow(QString(), m_enabled);
    layout->addRow(QStringLiteral("Status"), m_status);
    m_stack->addWidget(form);

    auto onEdit = [this] { if (!m_loading) emitChanges(); };
    connect(m_mode, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_policy, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_enabled, &QCheckBox::toggled, this, onEdit);
}

void PatchbayInspector::setSelectedMapping(const QString &mappingId,
                                           const QJsonObject &json,
                                           RouteStatus status,
                                           const QString &categoryName)
{
    m_mappingId = mappingId;
    if (mappingId.isEmpty()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_loading = true;
    m_mode->setCurrentText(json.value(QLatin1String("mode")).toString());
    m_policy->setCurrentText(
        json.value(QLatin1String("conflictPolicy")).toString());
    m_enabled->setChecked(json.value(QLatin1String("enabled")).toBool(true));
    m_status->setText(statusText(status, categoryName));
    m_loading = false;
    m_stack->setCurrentIndex(1);
}

void PatchbayInspector::emitChanges()
{
    QJsonObject changes;
    changes[QLatin1String("mode")] = m_mode->currentText();
    changes[QLatin1String("conflictPolicy")] = m_policy->currentText();
    changes[QLatin1String("enabled")] = m_enabled->isChecked();
    emit mappingEdited(m_mappingId, changes);
}

} // namespace WildPalms::AppPatchbay
