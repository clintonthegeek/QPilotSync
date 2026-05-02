#include "kalburatorinteractiveconflicthandler.h"
#include "conflictstore.h"

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
    Q_UNUSED(conflict);
    Q_UNUSED(policy);
    // Skeleton — Task 5 implements GUI-thread marshalling +
    // dialog. For now, defer everything.
    return ConflictDecision::Pending;
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
    Q_UNUSED(conflict);
    Q_UNUSED(policy);
    return ConflictDecision::Pending;
}
