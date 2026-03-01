#include "interactiveconflicthandler.h"
#include "conflictdialog.h"

#include <QApplication>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

using namespace QSyncCore;

InteractiveConflictHandler::InteractiveConflictHandler(ConflictStore *store,
                                                        QWidget *parentWidget,
                                                        QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_parentWidget(parentWidget)
    , m_conflictsHandled(0)
    , m_conflictsDeferred(0)
    , m_autoResolvedCount(0)
    , m_applyToAllDecision(ConflictDecision::Pending)
    , m_keepAlive(true)
{
}

void InteractiveConflictHandler::onSyncStart()
{
    m_conflictsHandled = 0;
    m_conflictsDeferred = 0;
    m_autoResolvedCount = 0;
    m_applyToAllDecision = ConflictDecision::Pending;
    m_localPending.clear();
    m_keepAlive = true;
}

void InteractiveConflictHandler::onSyncEnd(bool hadConflicts, bool allResolved)
{
    if (hadConflicts) {
        qDebug() << "[ConflictHandler] Sync ended with"
                 << m_conflictsHandled << "conflicts handled,"
                 << m_conflictsDeferred << "deferred,"
                 << m_autoResolvedCount << "auto-resolved";

        if (!allResolved && m_conflictsDeferred > 0) {
            qDebug() << "[ConflictHandler]" << m_conflictsDeferred
                     << "conflicts need review in batch UI";
        }
    }
}

ConflictDecision InteractiveConflictHandler::handleConflict(ConflictRecord &conflict,
                                                             const ConflictPolicy &policy)
{
    m_conflictsHandled++;

    // 1. Check if "apply to all" is active from previous decision (no GUI needed)
    if (m_applyToAllDecision != ConflictDecision::Pending) {
        conflict.decision = m_applyToAllDecision;
        conflict.resolvedAt = QDateTime::currentDateTime();
        conflict.resolvedBy = "user:applyToAll";
        qDebug() << "[ConflictHandler] Applied 'to all' decision:" << conflictDecisionToString(m_applyToAllDecision);
        return m_applyToAllDecision;
    }

    // 2. Check for automatic resolution (no GUI needed)
    if (policy.shouldAutoResolve(conflict)) {
        if (policy.maxAutoResolvePerSync > 0 && m_autoResolvedCount >= policy.maxAutoResolvePerSync) {
            qDebug() << "[ConflictHandler] Auto-resolve limit reached, deferring";
        } else {
            ConflictDecision decision = policy.getAutoDecision(conflict);
            if (decision != ConflictDecision::Pending) {
                conflict.decision = decision;
                conflict.resolvedAt = QDateTime::currentDateTime();
                conflict.resolvedBy = QString("policy:%1")
                    .arg(autoResolveStrategyToString(policy.autoResolve));
                m_autoResolvedCount++;
                qDebug() << "[ConflictHandler] Auto-resolved:" << conflictDecisionToString(decision);
                return decision;
            }
        }
    }

    // 3. If not on GUI thread, bounce via BlockingQueuedConnection
    //    Sync runs on a worker thread; ConflictDialog must show on the GUI thread.
    if (QThread::currentThread() != qApp->thread()) {
        ConflictDecision result = ConflictDecision::Pending;
        QMetaObject::invokeMethod(this, [this, &conflict, &policy, &result]() {
            result = handleConflictOnGuiThread(conflict, policy);
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    return handleConflictOnGuiThread(conflict, policy);
}

ConflictDecision InteractiveConflictHandler::handleConflictOnGuiThread(
    ConflictRecord &conflict, const ConflictPolicy &policy)
{
    // Check if we should prompt
    if (!canPrompt()) {
        return handleFallback(conflict, policy);
    }

    if (!policy.shouldPrompt(conflict)) {
        return handleFallback(conflict, policy);
    }

    // Show dialog on GUI thread
    m_keepAlive = (policy.connectionBehavior == ConnectionBehavior::KeepAlive ||
                   policy.connectionBehavior == ConnectionBehavior::TimeoutThenDefer);

    ConflictDialog dialog(conflict, policy, m_conduitLookup, m_parentWidget);

    // Connect tickle signal
    connect(&dialog, &ConflictDialog::keepAliveRequested,
            this, &InteractiveConflictHandler::keepAliveRequested);

    emit conflictProgress(m_conflictsHandled, m_conflictsHandled, conflict.summary());

    dialog.exec();

    ConflictDecision decision = dialog.decision();

    // Handle "apply to all"
    if (dialog.applyToAll() && decision != ConflictDecision::Pending) {
        m_applyToAllDecision = decision;
        qDebug() << "[ConflictHandler] 'Apply to all' set to:" << conflictDecisionToString(decision);
    }

    // Update conflict record
    if (decision == ConflictDecision::Pending) {
        // Deferred
        m_conflictsDeferred++;
        m_localPending.append(conflict);
        if (m_store) {
            m_store->addConflict(conflict);
        }
        qDebug() << "[ConflictHandler] Deferred for later review";

        // If policy says disconnect on defer, update keep-alive
        if (policy.connectionBehavior == ConnectionBehavior::DisconnectAndDefer) {
            m_keepAlive = false;
        }
    } else {
        conflict.decision = decision;
        conflict.resolvedAt = QDateTime::currentDateTime();
        conflict.resolvedBy = "user";
        qDebug() << "[ConflictHandler] User decided:" << conflictDecisionToString(decision);
    }

    return decision;
}

ConflictDecision InteractiveConflictHandler::handleFallback(ConflictRecord &conflict,
                                                             const ConflictPolicy &policy)
{
    switch (policy.fallback) {
        case FallbackBehavior::Defer:
            conflict.decision = ConflictDecision::Pending;
            m_conflictsDeferred++;
            m_localPending.append(conflict);
            if (m_store) {
                m_store->addConflict(conflict);
            }
            return ConflictDecision::Pending;

        case FallbackBehavior::Skip:
            conflict.decision = ConflictDecision::Skip;
            conflict.resolvedAt = QDateTime::currentDateTime();
            conflict.resolvedBy = "fallback:skip";
            return ConflictDecision::Skip;

        case FallbackBehavior::UseDefault:
            {
                ConflictDecision decision = policy.getAutoDecision(conflict);
                if (decision == ConflictDecision::Pending) {
                    decision = ConflictDecision::Skip;
                }
                conflict.decision = decision;
                conflict.resolvedAt = QDateTime::currentDateTime();
                conflict.resolvedBy = "fallback:default";
                return decision;
            }

        case FallbackBehavior::Abort:
            // Caller should handle this
            conflict.decision = ConflictDecision::Skip;
            return ConflictDecision::Skip;
    }

    return ConflictDecision::Pending;
}

QList<ConflictRecord> InteractiveConflictHandler::pendingConflicts() const
{
    return m_localPending;
}

void InteractiveConflictHandler::clearApplyToAll()
{
    m_applyToAllDecision = ConflictDecision::Pending;
}
