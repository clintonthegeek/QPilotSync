#include "conflictreviewwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QMessageBox>

using namespace QSyncCore;

ConflictReviewWidget::ConflictReviewWidget(ConflictStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setupUI();
    refresh();

    // Connect to store changes
    connect(m_store, &ConflictStore::conflictsChanged,
            this, &ConflictReviewWidget::refresh);
}

ConflictReviewWidget::~ConflictReviewWidget()
{
}

void ConflictReviewWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Header with filter
    QHBoxLayout *headerLayout = new QHBoxLayout();

    QLabel *titleLabel = new QLabel(tr("<h2>Pending Conflicts</h2>"), this);
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    QLabel *filterLabel = new QLabel(tr("Show:"), this);
    headerLayout->addWidget(filterLabel);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem(tr("All Conflicts"), "all");
    m_filterCombo->addItem(tr("Pending Only"), "pending");
    m_filterCombo->addItem(tr("Resolved Only"), "resolved");
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConflictReviewWidget::onFilterChanged);
    headerLayout->addWidget(m_filterCombo);

    mainLayout->addLayout(headerLayout);

    // Main splitter
    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left side: conflict list
    QWidget *listPanel = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);

    m_conflictList = new QListWidget(this);
    m_conflictList->setMinimumWidth(250);
    connect(m_conflictList, &QListWidget::currentItemChanged,
            this, &ConflictReviewWidget::onConflictSelected);
    listLayout->addWidget(m_conflictList);

    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("padding: 5px; background-color: #f0f0f0;");
    listLayout->addWidget(m_statusLabel);

    mainSplitter->addWidget(listPanel);

    // Right side: details and actions
    QWidget *detailsPanel = new QWidget(this);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsPanel);
    detailsLayout->setContentsMargins(0, 0, 0, 0);

    // Summary
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet("font-size: 14px; padding: 10px; background-color: #e8e8e8;");
    detailsLayout->addWidget(m_summaryLabel);

    // Side-by-side view
    QSplitter *compareSplitter = new QSplitter(Qt::Horizontal, this);

    m_sourceGroup = new QGroupBox(tr("Source"), this);
    QVBoxLayout *sourceLayout = new QVBoxLayout(m_sourceGroup);
    m_sourceInfoLabel = new QLabel(this);
    m_sourceInfoLabel->setWordWrap(true);
    m_sourceInfoLabel->setStyleSheet("color: #666; font-size: 11px;");
    sourceLayout->addWidget(m_sourceInfoLabel);
    m_sourceText = new QTextEdit(this);
    m_sourceText->setReadOnly(true);
    m_sourceText->setFont(QFont("monospace", 10));
    sourceLayout->addWidget(m_sourceText);
    compareSplitter->addWidget(m_sourceGroup);

    m_targetGroup = new QGroupBox(tr("Target"), this);
    QVBoxLayout *targetLayout = new QVBoxLayout(m_targetGroup);
    m_targetInfoLabel = new QLabel(this);
    m_targetInfoLabel->setWordWrap(true);
    m_targetInfoLabel->setStyleSheet("color: #666; font-size: 11px;");
    targetLayout->addWidget(m_targetInfoLabel);
    m_targetText = new QTextEdit(this);
    m_targetText->setReadOnly(true);
    m_targetText->setFont(QFont("monospace", 10));
    targetLayout->addWidget(m_targetText);
    compareSplitter->addWidget(m_targetGroup);

    detailsLayout->addWidget(compareSplitter, 1);

    // Individual action buttons
    QHBoxLayout *actionLayout = new QHBoxLayout();

    m_useSourceBtn = new QPushButton(tr("Use Source"), this);
    m_useSourceBtn->setIcon(QIcon::fromTheme("go-previous"));
    connect(m_useSourceBtn, &QPushButton::clicked, this, &ConflictReviewWidget::onUseSource);
    actionLayout->addWidget(m_useSourceBtn);

    m_useTargetBtn = new QPushButton(tr("Use Target"), this);
    m_useTargetBtn->setIcon(QIcon::fromTheme("go-next"));
    connect(m_useTargetBtn, &QPushButton::clicked, this, &ConflictReviewWidget::onUseTarget);
    actionLayout->addWidget(m_useTargetBtn);

    m_useBothBtn = new QPushButton(tr("Duplicate Both"), this);
    m_useBothBtn->setIcon(QIcon::fromTheme("list-add"));
    connect(m_useBothBtn, &QPushButton::clicked, this, &ConflictReviewWidget::onUseBoth);
    actionLayout->addWidget(m_useBothBtn);

    m_skipBtn = new QPushButton(tr("Skip"), this);
    connect(m_skipBtn, &QPushButton::clicked, this, &ConflictReviewWidget::onSkip);
    actionLayout->addWidget(m_skipBtn);

    actionLayout->addStretch();

    m_resetBtn = new QPushButton(tr("Reset to Pending"), this);
    m_resetBtn->setIcon(QIcon::fromTheme("edit-undo"));
    connect(m_resetBtn, &QPushButton::clicked, this, &ConflictReviewWidget::onResetToPending);
    actionLayout->addWidget(m_resetBtn);

    detailsLayout->addLayout(actionLayout);

    mainSplitter->addWidget(detailsPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);

    mainLayout->addWidget(mainSplitter, 1);

    // Batch action buttons at bottom
    QHBoxLayout *batchLayout = new QHBoxLayout();

    m_resolveAllSourceBtn = new QPushButton(tr("Resolve All: Use Source"), this);
    connect(m_resolveAllSourceBtn, &QPushButton::clicked,
            this, &ConflictReviewWidget::onResolveAllSource);
    batchLayout->addWidget(m_resolveAllSourceBtn);

    m_resolveAllTargetBtn = new QPushButton(tr("Resolve All: Use Target"), this);
    connect(m_resolveAllTargetBtn, &QPushButton::clicked,
            this, &ConflictReviewWidget::onResolveAllTarget);
    batchLayout->addWidget(m_resolveAllTargetBtn);

    m_clearResolvedBtn = new QPushButton(tr("Clear Resolved"), this);
    m_clearResolvedBtn->setIcon(QIcon::fromTheme("edit-clear"));
    connect(m_clearResolvedBtn, &QPushButton::clicked,
            this, &ConflictReviewWidget::onClearResolved);
    batchLayout->addWidget(m_clearResolvedBtn);

    batchLayout->addStretch();

    m_applyBtn = new QPushButton(tr("Apply Resolutions (Sync)"), this);
    m_applyBtn->setIcon(QIcon::fromTheme("view-refresh"));
    m_applyBtn->setStyleSheet("font-weight: bold;");
    connect(m_applyBtn, &QPushButton::clicked,
            this, &ConflictReviewWidget::applyResolutionsRequested);
    batchLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(batchLayout);

    // Initial state
    updateButtons();
}

void ConflictReviewWidget::refresh()
{
    updateConflictList();
    updateButtons();
    emit conflictsChanged();
}

int ConflictReviewWidget::pendingCount() const
{
    return m_store->pendingCount();
}

int ConflictReviewWidget::resolvedCount() const
{
    return m_store->resolvedUnappliedConflicts().size();
}

void ConflictReviewWidget::updateConflictList()
{
    m_conflictList->clear();
    m_currentConflictId.clear();

    QString filter = m_filterCombo->currentData().toString();

    QList<ConflictRecord> conflicts;
    if (filter == "pending") {
        conflicts = m_store->pendingConflicts();
    } else if (filter == "resolved") {
        conflicts = m_store->resolvedUnappliedConflicts();
    } else {
        conflicts = m_store->allConflicts();
    }

    for (const ConflictRecord &conflict : conflicts) {
        QListWidgetItem *item = new QListWidgetItem(m_conflictList);

        QString text = conflict.summary();
        if (!conflict.isPending()) {
            text = QString("[%1] %2").arg(decisionToString(conflict.decision)).arg(text);
        }
        item->setText(text);
        item->setData(Qt::UserRole, conflict.conflictId);
        item->setIcon(decisionToIcon(conflict.decision));

        // Color code by status
        if (conflict.isPending()) {
            item->setBackground(QColor(255, 255, 200));  // Yellow for pending
        } else if (conflict.applied) {
            item->setBackground(QColor(200, 255, 200));  // Green for applied
        } else {
            item->setBackground(QColor(200, 220, 255));  // Blue for resolved
        }
    }

    // Update status
    int pending = m_store->pendingCount();
    int resolved = resolvedCount();
    int total = m_store->count();

    m_statusLabel->setText(tr("%1 conflicts: %2 pending, %3 resolved")
        .arg(total).arg(pending).arg(resolved));

    // Clear details if nothing selected
    if (m_conflictList->count() == 0) {
        m_summaryLabel->setText(tr("No conflicts to display"));
        m_sourceText->clear();
        m_targetText->clear();
        m_sourceInfoLabel->clear();
        m_targetInfoLabel->clear();
    }
}

void ConflictReviewWidget::onConflictSelected(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);

    if (!current) {
        m_currentConflictId.clear();
        updateButtons();
        return;
    }

    m_currentConflictId = current->data(Qt::UserRole).toString();
    ConflictRecord conflict = m_store->getConflict(m_currentConflictId);

    if (!conflict.conflictId.isEmpty()) {
        displayConflict(conflict);
    }

    updateButtons();
}

void ConflictReviewWidget::displayConflict(const ConflictRecord &conflict)
{
    // Summary
    QString typeDesc;
    switch (conflict.type) {
        case ConflictType::BothModified:
            typeDesc = tr("Both modified");
            break;
        case ConflictType::ModifiedVsDeleted:
            typeDesc = tr("Source modified, target deleted");
            break;
        case ConflictType::DeletedVsModified:
            typeDesc = tr("Source deleted, target modified");
            break;
        case ConflictType::DuplicateDetected:
            typeDesc = tr("Duplicate detected");
            break;
        case ConflictType::TypeMismatch:
            typeDesc = tr("Type mismatch");
            break;
    }

    QString statusText;
    if (conflict.isPending()) {
        statusText = tr("<span style='color:orange;'>Pending</span>");
    } else {
        statusText = QString("<span style='color:blue;'>%1</span> by %2")
            .arg(decisionToString(conflict.decision))
            .arg(conflict.resolvedBy);
    }

    m_summaryLabel->setText(QString("<b>%1</b> [%2]<br>%3 | Detected: %4<br>Status: %5")
        .arg(conflict.conduitId)
        .arg(typeDesc)
        .arg(conflict.source.description.isEmpty() ? conflict.target.description : conflict.source.description)
        .arg(conflict.detectedAt.toString("yyyy-MM-dd hh:mm"))
        .arg(statusText));

    // Display records
    auto displayRecord = [](const RecordSnapshot &record, QTextEdit *text, QLabel *info) {
        if (record.isDeleted()) {
            info->setText(tr("<b style='color:red;'>DELETED</b>"));
            text->setPlainText(tr("(Record has been deleted)"));
            text->setStyleSheet("background-color: #ffe0e0;");
        } else if (record.isEmpty()) {
            info->setText(tr("<b style='color:orange;'>NEW</b>"));
            text->setPlainText(tr("(Does not exist)"));
            text->setStyleSheet("background-color: #fff0e0;");
        } else {
            QString infoStr = QString("ID: %1 | Modified: %2")
                .arg(record.id)
                .arg(record.lastModified.toString("yyyy-MM-dd hh:mm"));
            if (!record.category.isEmpty()) {
                infoStr += QString(" | %1").arg(record.category);
            }
            info->setText(infoStr);

            // Display as text
            QString content = QString::fromUtf8(record.content);
            text->setPlainText(content);
            text->setStyleSheet("");
        }
    };

    displayRecord(conflict.source, m_sourceText, m_sourceInfoLabel);
    displayRecord(conflict.target, m_targetText, m_targetInfoLabel);
}

void ConflictReviewWidget::updateButtons()
{
    bool hasSelection = !m_currentConflictId.isEmpty();
    bool hasPending = m_store->pendingCount() > 0;
    bool hasResolved = resolvedCount() > 0;

    ConflictRecord current;
    if (hasSelection) {
        current = m_store->getConflict(m_currentConflictId);
    }

    bool isPending = hasSelection && current.isPending();
    bool isDeleteConflict = hasSelection &&
        (current.type == ConflictType::ModifiedVsDeleted ||
         current.type == ConflictType::DeletedVsModified);

    // Individual buttons
    m_useSourceBtn->setEnabled(isPending);
    m_useTargetBtn->setEnabled(isPending);
    m_useBothBtn->setEnabled(isPending && !isDeleteConflict);
    m_skipBtn->setEnabled(isPending);
    m_resetBtn->setEnabled(hasSelection && !current.isPending() && !current.applied);

    // Batch buttons
    m_resolveAllSourceBtn->setEnabled(hasPending);
    m_resolveAllTargetBtn->setEnabled(hasPending);
    m_clearResolvedBtn->setEnabled(hasResolved);
    m_applyBtn->setEnabled(hasResolved);
}

void ConflictReviewWidget::resolveCurrentConflict(ConflictDecision decision)
{
    if (m_currentConflictId.isEmpty()) return;

    m_store->resolveConflict(m_currentConflictId, decision, "user");
    refresh();
}

void ConflictReviewWidget::onUseSource()
{
    resolveCurrentConflict(ConflictDecision::UseSource);
}

void ConflictReviewWidget::onUseTarget()
{
    resolveCurrentConflict(ConflictDecision::UseTarget);
}

void ConflictReviewWidget::onUseBoth()
{
    resolveCurrentConflict(ConflictDecision::UseBoth);
}

void ConflictReviewWidget::onSkip()
{
    resolveCurrentConflict(ConflictDecision::Skip);
}

void ConflictReviewWidget::onResetToPending()
{
    if (m_currentConflictId.isEmpty()) return;

    m_store->resetToPending(m_currentConflictId);
    refresh();
}

void ConflictReviewWidget::onResolveAllSource()
{
    int count = m_store->pendingCount();
    if (count == 0) return;

    auto reply = QMessageBox::question(this,
        tr("Resolve All Conflicts"),
        tr("This will resolve %1 conflicts using the Source version.\n\n"
           "Are you sure?").arg(count),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_store->resolveAll(ConflictDecision::UseSource, "user:batch");
        refresh();
    }
}

void ConflictReviewWidget::onResolveAllTarget()
{
    int count = m_store->pendingCount();
    if (count == 0) return;

    auto reply = QMessageBox::question(this,
        tr("Resolve All Conflicts"),
        tr("This will resolve %1 conflicts using the Target version.\n\n"
           "Are you sure?").arg(count),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_store->resolveAll(ConflictDecision::UseTarget, "user:batch");
        refresh();
    }
}

void ConflictReviewWidget::onClearResolved()
{
    int count = resolvedCount();
    if (count == 0) return;

    auto reply = QMessageBox::question(this,
        tr("Clear Resolved Conflicts"),
        tr("This will remove %1 resolved conflicts from the list.\n\n"
           "Note: If resolutions haven't been applied via sync, "
           "they will be lost.\n\n"
           "Continue?").arg(count),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_store->removeAppliedConflicts();

        // Also remove resolved but unapplied if user confirmed
        for (const ConflictRecord &c : m_store->resolvedUnappliedConflicts()) {
            m_store->removeConflict(c.conflictId);
        }
        refresh();
    }
}

void ConflictReviewWidget::onFilterChanged(int index)
{
    Q_UNUSED(index);
    updateConflictList();
}

QString ConflictReviewWidget::decisionToString(ConflictDecision decision) const
{
    switch (decision) {
        case ConflictDecision::Pending:    return tr("Pending");
        case ConflictDecision::UseSource:  return tr("Use Source");
        case ConflictDecision::UseTarget:  return tr("Use Target");
        case ConflictDecision::UseBoth:    return tr("Duplicate Both");
        case ConflictDecision::Merge:      return tr("Merged");
        case ConflictDecision::Skip:       return tr("Skip");
        case ConflictDecision::DeleteBoth: return tr("Delete Both");
    }
    return tr("Unknown");
}

QIcon ConflictReviewWidget::decisionToIcon(ConflictDecision decision) const
{
    switch (decision) {
        case ConflictDecision::Pending:
            return QIcon::fromTheme("dialog-question");
        case ConflictDecision::UseSource:
            return QIcon::fromTheme("go-previous");
        case ConflictDecision::UseTarget:
            return QIcon::fromTheme("go-next");
        case ConflictDecision::UseBoth:
            return QIcon::fromTheme("list-add");
        case ConflictDecision::Skip:
            return QIcon::fromTheme("dialog-cancel");
        default:
            return QIcon();
    }
}
