#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

/**
 * @file conflictdialog.h
 * @brief Dialog for resolving a single sync conflict
 *
 * Shows source and target versions side-by-side using QTextEdit,
 * allowing the user to choose which version to keep.
 *
 * This is a generic implementation using text display.
 * Future versions can override displayRecord() to show type-specific views.
 */

#include <QDialog>
#include "sync/qsynccore/conflictrecord.h"
#include "sync/qsynccore/conflictpolicy.h"

class QTextEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QTimer;
class QProgressBar;

/**
 * @brief Dialog for resolving a single sync conflict
 */
class ConflictDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConflictDialog(const QSyncCore::ConflictRecord &conflict,
                            const QSyncCore::ConflictPolicy &policy,
                            QWidget *parent = nullptr);
    ~ConflictDialog() override;

    /**
     * @brief Get the user's decision
     */
    QSyncCore::ConflictDecision decision() const { return m_decision; }

    /**
     * @brief Should this decision apply to all remaining conflicts?
     */
    bool applyToAll() const { return m_applyToAll; }

    /**
     * @brief Was the conflict deferred?
     */
    bool wasDeferred() const { return m_decision == QSyncCore::ConflictDecision::Pending; }

signals:
    /**
     * @brief Request to keep connection alive (tickle)
     */
    void keepAliveRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onUseSource();
    void onUseTarget();
    void onUseBoth();
    void onSkip();
    void onDefer();
    void onTimeout();
    void onTickle();

private:
    void setupUI();
    void displayConflict();
    void startTimeout();
    void stopTimeout();

    /**
     * @brief Display record content in a text edit
     *
     * Override in subclasses for type-specific display.
     */
    virtual void displayRecord(const QSyncCore::RecordSnapshot &record,
                               QTextEdit *textEdit,
                               QLabel *infoLabel);

    QSyncCore::ConflictRecord m_conflict;
    QSyncCore::ConflictPolicy m_policy;
    QSyncCore::ConflictDecision m_decision;
    bool m_applyToAll;

    // UI elements
    QLabel *m_summaryLabel;
    QLabel *m_sourceInfoLabel;
    QLabel *m_targetInfoLabel;
    QTextEdit *m_sourceText;
    QTextEdit *m_targetText;

    QPushButton *m_useSourceBtn;
    QPushButton *m_useTargetBtn;
    QPushButton *m_useBothBtn;
    QPushButton *m_skipBtn;
    QPushButton *m_deferBtn;
    QCheckBox *m_applyToAllCheck;

    // Timeout handling
    QTimer *m_timeoutTimer;
    QTimer *m_tickleTimer;
    QProgressBar *m_timeoutBar;
    int m_remainingSeconds;
};

#endif // CONFLICTDIALOG_H
