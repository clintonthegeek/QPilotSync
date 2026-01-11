#include "conflictdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QTimer>
#include <QSplitter>
#include <QGroupBox>
#include <QCloseEvent>

using namespace QSyncCore;

ConflictDialog::ConflictDialog(const ConflictRecord &conflict,
                               const ConflictPolicy &policy,
                               QWidget *parent)
    : QDialog(parent)
    , m_conflict(conflict)
    , m_policy(policy)
    , m_decision(ConflictDecision::Pending)
    , m_applyToAll(false)
    , m_timeoutTimer(nullptr)
    , m_tickleTimer(nullptr)
    , m_remainingSeconds(0)
{
    setWindowTitle(tr("Resolve Conflict"));
    setMinimumSize(800, 600);

    setupUI();
    displayConflict();
}

ConflictDialog::~ConflictDialog()
{
    stopTimeout();
}

void ConflictDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Summary at top
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 10px;");
    mainLayout->addWidget(m_summaryLabel);

    // Side-by-side comparison
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    // Source side (e.g., Palm)
    QGroupBox *sourceGroup = new QGroupBox(tr("Source (Palm/Local)"), this);
    QVBoxLayout *sourceLayout = new QVBoxLayout(sourceGroup);
    m_sourceInfoLabel = new QLabel(this);
    m_sourceInfoLabel->setWordWrap(true);
    m_sourceInfoLabel->setStyleSheet("color: #666; font-size: 11px;");
    sourceLayout->addWidget(m_sourceInfoLabel);
    m_sourceText = new QTextEdit(this);
    m_sourceText->setReadOnly(true);
    m_sourceText->setFont(QFont("monospace", 10));
    sourceLayout->addWidget(m_sourceText);
    splitter->addWidget(sourceGroup);

    // Target side (e.g., PC)
    QGroupBox *targetGroup = new QGroupBox(tr("Target (PC/Cloud)"), this);
    QVBoxLayout *targetLayout = new QVBoxLayout(targetGroup);
    m_targetInfoLabel = new QLabel(this);
    m_targetInfoLabel->setWordWrap(true);
    m_targetInfoLabel->setStyleSheet("color: #666; font-size: 11px;");
    targetLayout->addWidget(m_targetInfoLabel);
    m_targetText = new QTextEdit(this);
    m_targetText->setReadOnly(true);
    m_targetText->setFont(QFont("monospace", 10));
    targetLayout->addWidget(m_targetText);
    splitter->addWidget(targetGroup);

    mainLayout->addWidget(splitter, 1);

    // Timeout progress bar
    m_timeoutBar = new QProgressBar(this);
    m_timeoutBar->setTextVisible(true);
    m_timeoutBar->setFormat(tr("Time remaining: %v seconds"));
    m_timeoutBar->setVisible(false);
    mainLayout->addWidget(m_timeoutBar);

    // Apply to all checkbox
    m_applyToAllCheck = new QCheckBox(tr("Apply this choice to all remaining conflicts"), this);
    mainLayout->addWidget(m_applyToAllCheck);

    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    m_useSourceBtn = new QPushButton(tr("Use Source"), this);
    m_useSourceBtn->setIcon(QIcon::fromTheme("go-previous"));
    m_useSourceBtn->setToolTip(tr("Keep the source version, overwrite target"));
    connect(m_useSourceBtn, &QPushButton::clicked, this, &ConflictDialog::onUseSource);
    buttonLayout->addWidget(m_useSourceBtn);

    m_useTargetBtn = new QPushButton(tr("Use Target"), this);
    m_useTargetBtn->setIcon(QIcon::fromTheme("go-next"));
    m_useTargetBtn->setToolTip(tr("Keep the target version, overwrite source"));
    connect(m_useTargetBtn, &QPushButton::clicked, this, &ConflictDialog::onUseTarget);
    buttonLayout->addWidget(m_useTargetBtn);

    m_useBothBtn = new QPushButton(tr("Keep Both"), this);
    m_useBothBtn->setIcon(QIcon::fromTheme("list-add"));
    m_useBothBtn->setToolTip(tr("Create duplicates, keeping both versions"));
    connect(m_useBothBtn, &QPushButton::clicked, this, &ConflictDialog::onUseBoth);
    buttonLayout->addWidget(m_useBothBtn);

    buttonLayout->addStretch();

    m_skipBtn = new QPushButton(tr("Skip"), this);
    m_skipBtn->setToolTip(tr("Leave both unchanged for now"));
    connect(m_skipBtn, &QPushButton::clicked, this, &ConflictDialog::onSkip);
    buttonLayout->addWidget(m_skipBtn);

    m_deferBtn = new QPushButton(tr("Review Later"), this);
    m_deferBtn->setIcon(QIcon::fromTheme("appointment-new"));
    m_deferBtn->setToolTip(tr("Save for batch review after sync"));
    connect(m_deferBtn, &QPushButton::clicked, this, &ConflictDialog::onDefer);
    buttonLayout->addWidget(m_deferBtn);

    mainLayout->addLayout(buttonLayout);

    // Hide defer if not allowed by policy
    m_deferBtn->setVisible(m_policy.allowBatchReview);

    // Disable "use both" for delete conflicts
    if (m_conflict.type == ConflictType::ModifiedVsDeleted ||
        m_conflict.type == ConflictType::DeletedVsModified) {
        m_useBothBtn->setEnabled(false);
        m_useBothBtn->setToolTip(tr("Cannot duplicate when one version is deleted"));
    }
}

void ConflictDialog::displayConflict()
{
    // Summary
    QString typeDesc;
    switch (m_conflict.type) {
        case ConflictType::BothModified:
            typeDesc = tr("Both sides modified this record");
            break;
        case ConflictType::ModifiedVsDeleted:
            typeDesc = tr("Source modified, target deleted");
            break;
        case ConflictType::DeletedVsModified:
            typeDesc = tr("Source deleted, target modified");
            break;
        case ConflictType::DuplicateDetected:
            typeDesc = tr("Duplicate content detected");
            break;
        case ConflictType::TypeMismatch:
            typeDesc = tr("Record type mismatch");
            break;
    }

    QString complexityDesc;
    switch (m_conflict.complexity) {
        case ConflictComplexity::Simple:
            complexityDesc = tr("Simple change");
            break;
        case ConflictComplexity::Moderate:
            complexityDesc = tr("Moderate change");
            break;
        case ConflictComplexity::Complex:
            complexityDesc = tr("Complex change - review carefully");
            break;
    }

    m_summaryLabel->setText(QString("<b>%1</b><br>%2 - %3")
        .arg(m_conflict.summary())
        .arg(typeDesc)
        .arg(complexityDesc));

    // Display each side
    displayRecord(m_conflict.source, m_sourceText, m_sourceInfoLabel);
    displayRecord(m_conflict.target, m_targetText, m_targetInfoLabel);
}

void ConflictDialog::displayRecord(const RecordSnapshot &record,
                                    QTextEdit *textEdit,
                                    QLabel *infoLabel)
{
    // Info line
    QString info;
    if (record.isDeleted()) {
        info = tr("<b style='color:red;'>DELETED</b>");
        textEdit->setPlainText(tr("(Record has been deleted)"));
        textEdit->setStyleSheet("background-color: #ffe0e0;");
    } else if (record.isEmpty()) {
        info = tr("<b style='color:orange;'>NEW</b>");
        textEdit->setPlainText(tr("(Record does not exist on this side)"));
        textEdit->setStyleSheet("background-color: #fff0e0;");
    } else {
        info = QString("ID: %1 | Modified: %2 | Size: %3 bytes")
            .arg(record.id)
            .arg(record.lastModified.toString("yyyy-MM-dd hh:mm"))
            .arg(record.content.size());

        if (!record.category.isEmpty()) {
            info += QString(" | Category: %1").arg(record.category);
        }

        // Display content as text
        // Try to interpret as UTF-8 text, fallback to hex dump for binary
        QString contentStr = QString::fromUtf8(record.content);

        // Check if it looks like valid text
        bool isText = true;
        for (int i = 0; i < qMin(1000, contentStr.length()); i++) {
            QChar c = contentStr[i];
            if (!c.isPrint() && !c.isSpace() && c != '\n' && c != '\r' && c != '\t') {
                isText = false;
                break;
            }
        }

        if (isText) {
            textEdit->setPlainText(contentStr);
            textEdit->setStyleSheet("");
        } else {
            // Hex dump for binary
            QString hex;
            for (int i = 0; i < record.content.size(); i++) {
                if (i > 0 && i % 16 == 0) hex += '\n';
                else if (i > 0 && i % 8 == 0) hex += "  ";
                else if (i > 0) hex += ' ';
                hex += QString("%1").arg((unsigned char)record.content[i], 2, 16, QChar('0'));
            }
            textEdit->setPlainText(hex);
            textEdit->setStyleSheet("font-family: monospace; background-color: #f0f0f0;");
            info += " | <i>Binary content</i>";
        }
    }

    infoLabel->setText(info);
}

void ConflictDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    startTimeout();
}

void ConflictDialog::closeEvent(QCloseEvent *event)
{
    stopTimeout();

    // If closing without decision, treat as defer if allowed, else skip
    if (m_decision == ConflictDecision::Pending) {
        if (m_policy.allowBatchReview) {
            m_decision = ConflictDecision::Pending;  // Will be deferred
        } else {
            m_decision = ConflictDecision::Skip;
        }
    }

    QDialog::closeEvent(event);
}

void ConflictDialog::startTimeout()
{
    if (m_policy.promptTimeoutSeconds <= 0) return;

    m_remainingSeconds = m_policy.promptTimeoutSeconds;
    m_timeoutBar->setMaximum(m_remainingSeconds);
    m_timeoutBar->setValue(m_remainingSeconds);
    m_timeoutBar->setVisible(true);

    m_timeoutTimer = new QTimer(this);
    connect(m_timeoutTimer, &QTimer::timeout, this, &ConflictDialog::onTimeout);
    m_timeoutTimer->start(1000);

    // Also start tickle timer to keep connection alive
    m_tickleTimer = new QTimer(this);
    connect(m_tickleTimer, &QTimer::timeout, this, &ConflictDialog::onTickle);
    m_tickleTimer->start(5000);  // Tickle every 5 seconds
}

void ConflictDialog::stopTimeout()
{
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
        delete m_timeoutTimer;
        m_timeoutTimer = nullptr;
    }
    if (m_tickleTimer) {
        m_tickleTimer->stop();
        delete m_tickleTimer;
        m_tickleTimer = nullptr;
    }
}

void ConflictDialog::onTimeout()
{
    m_remainingSeconds--;
    m_timeoutBar->setValue(m_remainingSeconds);

    if (m_remainingSeconds <= 0) {
        stopTimeout();
        m_decision = m_policy.timeoutDecision;
        m_applyToAll = false;
        accept();
    }
}

void ConflictDialog::onTickle()
{
    emit keepAliveRequested();
}

void ConflictDialog::onUseSource()
{
    stopTimeout();
    m_decision = ConflictDecision::UseSource;
    m_applyToAll = m_applyToAllCheck->isChecked();
    accept();
}

void ConflictDialog::onUseTarget()
{
    stopTimeout();
    m_decision = ConflictDecision::UseTarget;
    m_applyToAll = m_applyToAllCheck->isChecked();
    accept();
}

void ConflictDialog::onUseBoth()
{
    stopTimeout();
    m_decision = ConflictDecision::UseBoth;
    m_applyToAll = m_applyToAllCheck->isChecked();
    accept();
}

void ConflictDialog::onSkip()
{
    stopTimeout();
    m_decision = ConflictDecision::Skip;
    m_applyToAll = m_applyToAllCheck->isChecked();
    accept();
}

void ConflictDialog::onDefer()
{
    stopTimeout();
    m_decision = ConflictDecision::Pending;
    m_applyToAll = false;
    accept();
}
