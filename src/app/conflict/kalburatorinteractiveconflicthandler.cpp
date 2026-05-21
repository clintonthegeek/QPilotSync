#include "kalburatorinteractiveconflicthandler.h"
#include "conflictdialogbridge.h"
#include "conflictstore.h"
#include <QMetaObject>
#include <QThread>
#include <QTimer>

using ConflictDecision =
    Kalburator::Conflict::ConflictDecision;
using ConflictRecord =
    Kalburator::Conflict::ConflictRecord;
using ConflictPolicy =
    Kalburator::Conflict::ConflictPolicy;

// ---------------------------------------------------------------------------
// Namespace-translation helpers
//
// ConflictRecord: both structs are field-for-field identical (same field
// names, same Qt types, same order) modulo namespace.  The bridge TU casts a
// Kalburator::Conflict::ConflictRecord* to QSyncCore::ConflictRecord*;
// this is safe because the layouts are bit-for-bit identical (same compiler,
// same headers, same Qt types).
//
// ConflictPolicy: the WP-local struct has two extra Palm-specific fields
// (connectionBehavior, connectionTimeoutSeconds) that were stripped from
// libkalburator during Phase B.  To avoid a layout-size mismatch (casting a
// smaller Kalburator struct to the larger WP type would read past the end), we
// pass policy via ConflictDialogBridge::BridgePolicy — a plain-C POD struct
// that carries the common fields as ints/bools, with no Qt types in the
// signature.  The bridge TU reconstructs a QSyncCore::ConflictPolicy from it.
//
// All enumerators have the same ordinal positions in both namespaces (verified
// by inspection of both headers in Task 6).
// ---------------------------------------------------------------------------
namespace {

using KalRecord = Kalburator::Conflict::ConflictRecord;
using KalPolicy = Kalburator::Conflict::ConflictPolicy;

// Pass the record as a const KalRecord& (layout-identical to WP type).
// The bridge receives the address and casts to const QSyncCore::ConflictRecord*.
const KalRecord &asWildPalmsRecord(const KalRecord &src)
{
    // No translation needed — the structs are identical modulo namespace.
    return src;
}

// Pack the common ConflictPolicy fields into the plain-C bridge struct.
ConflictDialogBridge::BridgePolicy toBridgePolicy(const KalPolicy &src)
{
    ConflictDialogBridge::BridgePolicy bp;
    bp.autoResolve              = static_cast<int>(src.autoResolve);
    bp.promptStrategy           = static_cast<int>(src.promptStrategy);
    bp.promptTimeoutSeconds     = src.promptTimeoutSeconds;
    bp.timeoutDecision          = static_cast<int>(src.timeoutDecision);
    bp.fallback                 = static_cast<int>(src.fallback);
    bp.allowBatchReview         = src.allowBatchReview;
    bp.showPreviewBeforeSync    = src.showPreviewBeforeSync;
    bp.maxAutoResolvePerSync    = src.maxAutoResolvePerSync;
    bp.requireConfirmForDeletes = src.requireConfirmForDeletes;
    bp.logAllDecisions          = src.logAllDecisions;
    return bp;
}

ConflictDecision fromBridgeDecision(int d)
{
    return static_cast<ConflictDecision>(d);
}

} // anonymous namespace

KalburatorInteractiveConflictHandler::KalburatorInteractiveConflictHandler(
    Kalburator::Conflict::ConflictStore *store,
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
    // Reference captures are safe: BlockingQueuedConnection blocks this thread
    // until the slot returns, so the captured locals remain alive throughout.
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

    // Production path: open ConflictDialog modally via the bridge
    // (direct include of conflictdialog.h is not possible here — it would
    // pull in WP-local QSyncCore headers whose include guards collide with
    // the libkalburator headers already included above).

    const KalRecord &wpRecord = asWildPalmsRecord(conflict);
    const ConflictDialogBridge::BridgePolicy bp = toBridgePolicy(policy);

    // Tickle the device link while the user thinks (15 s cadence matches
    // the legacy InteractiveConflictHandler keep-alive pattern).
    QTimer keepAlive;
    keepAlive.setInterval(15 * 1000);
    connect(&keepAlive, &QTimer::timeout,
            this, &KalburatorInteractiveConflictHandler::keepAliveRequested);
    keepAlive.start();

    const int rawDecision = ConflictDialogBridge::showAndGetDecision(
        static_cast<const void *>(&wpRecord), bp, m_parentWidget);

    keepAlive.stop();

    if (rawDecision == static_cast<int>(ConflictDecision::Pending)) {
        m_localPending.append(conflict);
        ++m_conflictsDeferred;
        return ConflictDecision::Pending;
    }

    return fromBridgeDecision(rawDecision);
}
