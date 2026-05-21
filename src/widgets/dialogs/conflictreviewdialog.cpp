#include "conflictreviewdialog.h"

#include "../../app/conflictreviewwidget.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

ConflictReviewDialog::ConflictReviewDialog(Kalburator::Conflict::ConflictStore *store,
                                             ConduitLookupFn conduitLookup,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Review Conflicts"));
    setMinimumSize(900, 600);
    resize(1000, 700);

    auto *layout = new QVBoxLayout(this);

    m_reviewWidget = new ConflictReviewWidget(store, this);
    if (conduitLookup)
        m_reviewWidget->setConduitLookup(std::move(conduitLookup));
    layout->addWidget(m_reviewWidget, 1);

    auto *buttonBox = new QDialogButtonBox(this);
    auto *closeBtn = buttonBox->addButton(QDialogButtonBox::Close);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(buttonBox);

    // Forward apply signal
    connect(m_reviewWidget, &ConflictReviewWidget::applyResolutionsRequested,
            this, &ConflictReviewDialog::applyResolutionsRequested);
}

int ConflictReviewDialog::pendingCount() const
{
    return m_reviewWidget->pendingCount();
}

int ConflictReviewDialog::resolvedCount() const
{
    return m_reviewWidget->resolvedCount();
}
