#include "mappingeditordialog.h"

#include "mappingrowdialog.h"
#include "synctypes.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace {
constexpr int kColId      = 0;
constexpr int kColSource  = 1;
constexpr int kColTarget  = 2;
constexpr int kColMode    = 3;
constexpr int kColPolicy  = 4;
constexpr int kColEnabled = 5;
constexpr int kColCount   = 6;
constexpr int kRoleJson   = Qt::UserRole + 1;
} // namespace

MappingEditorDialog::MappingEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Configure mappings"));
    resize(800, 400);
    buildUi();
}

void MappingEditorDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    m_model = new QStandardItemModel(0, kColCount, this);
    m_model->setHeaderData(kColId,      Qt::Horizontal, tr("ID"));
    m_model->setHeaderData(kColSource,  Qt::Horizontal, tr("Source"));
    m_model->setHeaderData(kColTarget,  Qt::Horizontal, tr("Target"));
    m_model->setHeaderData(kColMode,    Qt::Horizontal, tr("Mode"));
    m_model->setHeaderData(kColPolicy,  Qt::Horizontal, tr("Conflict"));
    m_model->setHeaderData(kColEnabled, Qt::Horizontal, tr("Enabled"));

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    outer->addWidget(m_tableView);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn    = new QPushButton(tr("Add..."), this);
    auto *editBtn   = new QPushButton(tr("Edit..."), this);
    auto *deleteBtn = new QPushButton(tr("Delete"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch();
    outer->addLayout(btnRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(addBtn,    &QPushButton::clicked,
            this,      &MappingEditorDialog::onAddClicked);
    connect(editBtn,   &QPushButton::clicked,
            this,      &MappingEditorDialog::onEditClicked);
    connect(deleteBtn, &QPushButton::clicked,
            this,      &MappingEditorDialog::onDeleteClicked);
}

void MappingEditorDialog::setMappings(const QJsonArray &json)
{
    m_model->removeRows(0, m_model->rowCount());
    for (const auto &v : json) {
        if (v.isObject())
            appendRow(v.toObject());
    }
}

QJsonArray MappingEditorDialog::mappings() const
{
    QJsonArray out;
    for (int row = 0; row < m_model->rowCount(); ++row)
        out.append(rowToJson(row));
    return out;
}

void MappingEditorDialog::removeRowForTest(int row)
{
    if (row >= 0 && row < m_model->rowCount())
        m_model->removeRow(row);
}

void MappingEditorDialog::appendRow(const QJsonObject &mapping)
{
    const int row = m_model->rowCount();
    m_model->insertRow(row);
    setRowFromJson(row, mapping);
}

void MappingEditorDialog::setRowFromJson(int row, const QJsonObject &json)
{
    auto setCol = [&](int col, const QString &text) {
        m_model->setData(m_model->index(row, col), text);
    };
    setCol(kColId,      json.value(QStringLiteral("id")).toString());
    setCol(kColSource,  json.value(QStringLiteral("sourceCalendar")).toString());
    setCol(kColTarget,  json.value(QStringLiteral("targetCalendar")).toString());
    setCol(kColMode,    json.value(QStringLiteral("mode")).toString());
    // libkalburator's syncMappingToJson writes the conflict policy under
    // "conflictResolution" (NOT "conflictPolicy" as the original M5b plan
    // assumed). Read both keys for forward compat with hand-written test
    // fixtures that used the older key.
    QString policy = json.value(QStringLiteral("conflictResolution")).toString();
    if (policy.isEmpty())
        policy = json.value(QStringLiteral("conflictPolicy")).toString();
    setCol(kColPolicy,  policy);
    setCol(kColEnabled, json.value(QStringLiteral("enabled")).toBool() ? tr("Yes") : tr("No"));
    // Stash the full JSON in row's id column UserRole so we can round-trip
    // fields not shown in the table (sourceBackend, targetBackend, lossPolicy).
    m_model->setData(m_model->index(row, kColId), json, kRoleJson);
}

QJsonObject MappingEditorDialog::rowToJson(int row) const
{
    QJsonObject json = m_model->data(m_model->index(row, kColId), kRoleJson)
                              .toJsonObject();
    // Update fields visible in the table (in case user edited via dialog).
    json[QStringLiteral("id")]             = m_model->data(m_model->index(row, kColId)).toString();
    json[QStringLiteral("sourceCalendar")] = m_model->data(m_model->index(row, kColSource)).toString();
    json[QStringLiteral("targetCalendar")] = m_model->data(m_model->index(row, kColTarget)).toString();
    json[QStringLiteral("mode")]           = m_model->data(m_model->index(row, kColMode)).toString();
    // Keep both keys in sync to round-trip cleanly via libkalburator's
    // syncMappingFromJson (which reads "conflictResolution") while still
    // honoring fixtures using the original "conflictPolicy" key.
    const QString policy = m_model->data(m_model->index(row, kColPolicy)).toString();
    json[QStringLiteral("conflictResolution")] = policy;
    json[QStringLiteral("conflictPolicy")]     = policy;
    json[QStringLiteral("enabled")]        = m_model->data(m_model->index(row, kColEnabled)).toString() == tr("Yes");
    return json;
}

void MappingEditorDialog::setKnownBackends(const QStringList &ids)
{
    m_knownBackends = ids;
}

void MappingEditorDialog::onAddClicked()
{
    MappingRowDialog dlg(this);
    if (!m_knownBackends.isEmpty()) {
        dlg.setSourceBackends(m_knownBackends);
        dlg.setTargetBackends(m_knownBackends);
    }
    if (dlg.exec() != QDialog::Accepted)
        return;
    appendRow(Kalburator::Sync::syncMappingToJson(dlg.mapping()));
}

void MappingEditorDialog::onEditClicked()
{
    auto sel = m_tableView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const int row = sel.first().row();

    QJsonObject json = rowToJson(row);
    Kalburator::Sync::SyncMapping current =
        Kalburator::Sync::syncMappingFromJson(json);

    MappingRowDialog dlg(this);
    if (!m_knownBackends.isEmpty()) {
        dlg.setSourceBackends(m_knownBackends);
        dlg.setTargetBackends(m_knownBackends);
    }
    dlg.setMapping(current);
    if (dlg.exec() != QDialog::Accepted)
        return;
    setRowFromJson(row, Kalburator::Sync::syncMappingToJson(dlg.mapping()));
}

void MappingEditorDialog::onDeleteClicked()
{
    auto sel = m_tableView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    m_model->removeRow(sel.first().row());
}
