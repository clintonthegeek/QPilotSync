#include "clobberdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace WildPalms::Runtime {

struct ClobberDialog::Impl {
    DomainMappings input;
    QMap<QString, QCheckBox*> checkboxes;
};

ClobberDialog::ClobberDialog(const DomainMappings &mappings, QWidget *parent)
    : QDialog(parent), d(std::make_unique<Impl>())
{
    d->input = mappings;
    setWindowTitle(tr("Clobber Palm from PC"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Select which Palm conduits to wipe and re-push from desktop:"),
        this));

    for (auto it = d->input.constBegin(); it != d->input.constEnd(); ++it) {
        auto *cb = new QCheckBox(
            tr("%1 — %2 mapping(s)")
                .arg(it.key())
                .arg(it.value().size()),
            this);
        layout->addWidget(cb);
        d->checkboxes.insert(it.key(), cb);
    }

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Clobber"));
    layout->addWidget(bb);

    connect(bb, &QDialogButtonBox::accepted, this, &ClobberDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &ClobberDialog::reject);
}

ClobberDialog::~ClobberDialog() = default;

void ClobberDialog::setDomainChecked(const QString &domain, bool checked)
{
    if (auto *cb = d->checkboxes.value(domain)) cb->setChecked(checked);
}

QList<QString> ClobberDialog::selectedMappingIds() const
{
    QList<QString> result;
    for (auto it = d->input.constBegin(); it != d->input.constEnd(); ++it) {
        if (d->checkboxes.value(it.key())->isChecked())
            result.append(it.value());
    }
    return result;
}

void ClobberDialog::accept()
{
    const int n = selectedMappingIds().size();
    if (n == 0) {
        QDialog::reject();
        return;
    }
    const auto button = QMessageBox::warning(
        this, tr("Clobber Palm from PC"),
        tr("This will delete %n Palm database(s) and replace them with "
           "desktop data. The Palm-side data being deleted is NOT backed "
           "up. Continue?", "", n),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (button == QMessageBox::Yes) QDialog::accept();
}

} // namespace WildPalms::Runtime
