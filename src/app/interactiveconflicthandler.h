#ifndef INTERACTIVECONFLICTHANDLER_H
#define INTERACTIVECONFLICTHANDLER_H

/**
 * @file interactiveconflicthandler.h
 * @brief Interactive conflict handler with UI dialogs
 *
 * Implements ConflictHandler to show dialogs during sync and
 * optionally defer conflicts for batch review.
 */

#include "sync/qsynccore/conflictpolicy.h"
#include "sync/qsynccore/conflictstore.h"

#include <QObject>
#include <QPointer>

class QWidget;
class ConflictDialog;

/**
 * @brief Interactive conflict handler with dialog support
 *
 * Shows ConflictDialog for each conflict during sync.
 * Supports "apply to all" and deferred resolution.
 */
class InteractiveConflictHandler : public QObject, public QSyncCore::ConflictHandler
{
    Q_OBJECT

public:
    /**
     * @brief Construct an interactive handler
     * @param store Store for deferred conflicts (may be null)
     * @param parent Parent widget for dialogs
     */
    explicit InteractiveConflictHandler(QSyncCore::ConflictStore *store = nullptr,
                                        QWidget *parentWidget = nullptr,
                                        QObject *parent = nullptr);

    ~InteractiveConflictHandler() override = default;

    // ConflictHandler interface
    QSyncCore::ConflictDecision handleConflict(QSyncCore::ConflictRecord &conflict,
                                                const QSyncCore::ConflictPolicy &policy) override;

    void onSyncStart() override;
    void onSyncEnd(bool hadConflicts, bool allResolved) override;
    bool canPrompt() const override { return m_parentWidget != nullptr; }
    bool shouldKeepConnectionAlive() const override { return m_keepAlive; }
    QList<QSyncCore::ConflictRecord> pendingConflicts() const override;

    /**
     * @brief Set the parent widget for dialogs
     */
    void setParentWidget(QWidget *widget) { m_parentWidget = widget; }

    /**
     * @brief Get count of conflicts handled this session
     */
    int conflictsHandled() const { return m_conflictsHandled; }

    /**
     * @brief Get count of conflicts deferred this session
     */
    int conflictsDeferred() const { return m_conflictsDeferred; }

    /**
     * @brief Check if "apply to all" is active
     */
    bool hasApplyToAll() const { return m_applyToAllDecision != QSyncCore::ConflictDecision::Pending; }

    /**
     * @brief Get the "apply to all" decision
     */
    QSyncCore::ConflictDecision applyToAllDecision() const { return m_applyToAllDecision; }

    /**
     * @brief Clear the "apply to all" setting
     */
    void clearApplyToAll();

signals:
    /**
     * @brief Request to tickle the device connection
     */
    void keepAliveRequested();

    /**
     * @brief Conflict resolution progress
     */
    void conflictProgress(int current, int total, const QString &description);

private:
    QSyncCore::ConflictStore *m_store;
    QWidget *m_parentWidget;

    int m_conflictsHandled;
    int m_conflictsDeferred;
    int m_autoResolvedCount;

    // "Apply to all" state
    QSyncCore::ConflictDecision m_applyToAllDecision;

    // Keep connection alive during dialog
    bool m_keepAlive;

    // Locally deferred conflicts (not stored yet)
    QList<QSyncCore::ConflictRecord> m_localPending;

    QSyncCore::ConflictDecision handleFallback(QSyncCore::ConflictRecord &conflict,
                                                const QSyncCore::ConflictPolicy &policy);
};

#endif // INTERACTIVECONFLICTHANDLER_H
