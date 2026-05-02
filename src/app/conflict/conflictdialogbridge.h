#ifndef CONFLICTDIALOGBRIDGE_H
#define CONFLICTDIALOGBRIDGE_H

// Plain-function bridge that opens ConflictDialog without requiring the caller
// to include WP-local QSyncCore headers.  The collision arises because both
// libkalburator and WildPalms define conflictrecord.h / conflictpolicy.h with
// the *same* include-guard names (QSYNCCORE_CONFLICTRECORD_H, etc.).  A TU
// that includes kalburatorinteractiveconflicthandler.h (→ libkalburator
// headers) cannot also include conflictdialog.h (→ WP-local headers) — the
// second set would be silently swallowed by the guard, causing type mismatches.
//
// This bridge keeps the two header worlds in separate TUs.
//
// Signatures use only primitive types so this header is safe to include
// alongside libkalburator headers.

class QWidget;

namespace ConflictDialogBridge {

// ---------------------------------------------------------------------------
// Policy bridge struct — carries the common ConflictPolicy fields using only
// primitive (non-Qt) types so this header imposes no include dependencies.
//
// Ordinal values of AutoResolveStrategy / PromptStrategy / FallbackBehavior /
// ConflictDecision enumerators are identical in both namespaces (verified in
// Task 6 investigation), so we pass them as int and static_cast on both sides.
// ---------------------------------------------------------------------------
struct BridgePolicy {
    int  autoResolve;              // AutoResolveStrategy
    int  promptStrategy;           // PromptStrategy
    int  promptTimeoutSeconds;
    int  timeoutDecision;          // ConflictDecision
    int  fallback;                 // FallbackBehavior
    bool allowBatchReview;
    bool showPreviewBeforeSync;
    int  maxAutoResolvePerSync;
    bool requireConfirmForDeletes;
    bool logAllDecisions;
};

// ---------------------------------------------------------------------------
// exec() — open ConflictDialog modally.
//
// wpRecord: pointer to a ConflictRecord whose layout is byte-for-byte
//           identical to QSyncCore::ConflictRecord (same compiler, same field
//           order, same Qt types).  The bridge casts it to
//           const QSyncCore::ConflictRecord*.
//
// policy:   plain BridgePolicy struct; the bridge reconstructs a
//           QSyncCore::ConflictPolicy from it, avoiding the layout-size
//           mismatch (WP policy has two extra Palm-specific fields).
//
// Returns QDialog::Accepted (1) or QDialog::Rejected (0).
// After Accepted, call getDecision() to retrieve the user's choice.
// ---------------------------------------------------------------------------
int exec(const void       *wpRecord,   // const QSyncCore::ConflictRecord *
         const BridgePolicy &policy,
         QWidget           *parent);

// Must be called only after exec() returned QDialog::Accepted.
// Returns static_cast<int>(QSyncCore::ConflictDecision) — cast-safe to
// Kalburator::Sync::QSyncCore::ConflictDecision since ordinals are identical.
int getDecision();

} // namespace ConflictDialogBridge

#endif // CONFLICTDIALOGBRIDGE_H
