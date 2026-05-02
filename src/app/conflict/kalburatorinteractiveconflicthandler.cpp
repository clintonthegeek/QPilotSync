#include "kalburatorinteractiveconflicthandler.h"
#include "conflictstore.h"
#include <QMetaObject>
#include <QThread>

using ConflictDecision =
    Kalburator::Sync::QSyncCore::ConflictDecision;
using ConflictRecord =
    Kalburator::Sync::QSyncCore::ConflictRecord;
using ConflictPolicy =
    Kalburator::Sync::QSyncCore::ConflictPolicy;

KalburatorInteractiveConflictHandler::KalburatorInteractiveConflictHandler(
    Kalburator::Sync::QSyncCore::ConflictStore *store,
    QWidget *parentWidget,
    QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_parentWidget(parentWidget)
{}

ConflictDecision
KalburatorInteractiveConflictHandler::handleConflict(
    ConflictRecord &conflict,
    const ConflictPolicy &policy)
{
    ++m_conflictsHandled;

    if (QThread::currentThread() == thread()) {
        return handleConflictOnGuiThread(conflict, policy);
    }

    ConflictDecision decision = ConflictDecision::Pending;
    QMetaObject::invokeMethod(
        this,
        [this, &conflict, &policy, &decision]() {
            decision = handleConflictOnGuiThread(conflict, policy);
        },
        Qt::BlockingQueuedConnection);
    return decision;
}

void KalburatorInteractiveConflictHandler::onSyncStart()
{
    m_localPending.clear();
    m_conflictsHandled = 0;
    m_conflictsDeferred = 0;
}

void KalburatorInteractiveConflictHandler::onSyncEnd(
    bool hadConflicts, bool allResolved)
{
    Q_UNUSED(hadConflicts);
    Q_UNUSED(allResolved);
}

ConflictDecision
KalburatorInteractiveConflictHandler::handleConflictOnGuiThread(
    ConflictRecord &conflict,
    const ConflictPolicy &policy)
{
    if (m_hook) {
        return m_hook(conflict, policy);
    }

    if (!m_parentWidget) {
        m_localPending.append(conflict);
        ++m_conflictsDeferred;
        return ConflictDecision::Pending;
    }

    // Production path: dialog wired in Task 6.
    m_localPending.append(conflict);
    ++m_conflictsDeferred;
    return ConflictDecision::Pending;
}
