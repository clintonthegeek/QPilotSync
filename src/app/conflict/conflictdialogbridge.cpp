// conflictdialogbridge.cpp — WP-local namespace only.
//
// IMPORTANT: Do NOT include any libkalburator headers here.  This TU exists
// precisely to keep WP-local QSyncCore headers (pulled in by conflictdialog.h)
// separate from the libkalburator QSyncCore headers used in
// kalburatorinteractiveconflicthandler.cpp.

#include "conflictdialogbridge.h"

// WP-local app header — pulls in sync/qsynccore/conflictrecord.h etc.
// That is intentional and safe here; no libkalburator headers are present.
#include "../conflictdialog.h"

#include <QDialog>

namespace ConflictDialogBridge {

namespace {
// Stores the decision from the most-recently-exec'd dialog so getDecision()
// can retrieve it.  Single-threaded (GUI thread only); always called in an
// exec()/getDecision() pair, so a static is safe.
static QSyncCore::ConflictDecision s_lastDecision =
    QSyncCore::ConflictDecision::Pending;

QSyncCore::ConflictPolicy policyFromBridge(const BridgePolicy &bp)
{
    QSyncCore::ConflictPolicy p;
    p.autoResolve              = static_cast<QSyncCore::AutoResolveStrategy>(bp.autoResolve);
    p.promptStrategy           = static_cast<QSyncCore::PromptStrategy>(bp.promptStrategy);
    p.promptTimeoutSeconds     = bp.promptTimeoutSeconds;
    p.timeoutDecision          = static_cast<QSyncCore::ConflictDecision>(bp.timeoutDecision);
    p.fallback                 = static_cast<QSyncCore::FallbackBehavior>(bp.fallback);
    p.allowBatchReview         = bp.allowBatchReview;
    p.showPreviewBeforeSync    = bp.showPreviewBeforeSync;
    p.maxAutoResolvePerSync    = bp.maxAutoResolvePerSync;
    p.requireConfirmForDeletes = bp.requireConfirmForDeletes;
    p.logAllDecisions          = bp.logAllDecisions;
    // WP-only fields connectionBehavior / connectionTimeoutSeconds left at
    // their default-constructed values — irrelevant for dialog display.
    return p;
}
} // anonymous namespace

int exec(const void        *wpRecord,
         const BridgePolicy &policy,
         QWidget            *parent)
{
    // wpRecord points to a Kalburator::Sync::QSyncCore::ConflictRecord whose
    // layout is byte-for-byte identical to QSyncCore::ConflictRecord (same
    // compiler, same field order, same Qt types, same namespace-stripped
    // struct definition).  The cast is therefore well-defined.
    const auto &record =
        *static_cast<const QSyncCore::ConflictRecord *>(wpRecord);

    const QSyncCore::ConflictPolicy wpPolicy = policyFromBridge(policy);

    ConflictDialog dlg(record, wpPolicy, nullptr, parent);
    const int code = dlg.exec();
    s_lastDecision = dlg.decision();
    return code;
}

int getDecision()
{
    return static_cast<int>(s_lastDecision);
}

} // namespace ConflictDialogBridge
