# Conflict-handler port — design

**Status:** approved 2026-05-21.
**Parent:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (E.16 deferral (a)).
**Plan:** to be written next.

## Goal

Finish the QSyncCore → libkalburator conflict-system migration so
`src/sync/qsynccore/` can be deleted. Migrate the ~12 WP consumers
that still use `QSyncCore::*` types onto `Kalburator::Conflict::*` and
`Kalburator::Storage::*`; relocate `ConnectionBehavior` fully to
`PalmBackendConfig`; collapse the include-guard-collision bridge
(`conflictdialogbridge`, `palmruntimebridgeinstall`) that exists only
to keep the two duplicate type families apart.

After this lands, the `src/sync/qsynccore/` directory is gone, the
bridge is gone, and the WP-side conflict surface is one set of types,
one handler implementation, one home for the Palm-specific session
policy.

## Decision summary

- **Scope: broad audit (option A.broad in brainstorm).** Includes the
  type migration, the `ConnectionBehavior` relocation, the
  `IdMappingStore` → `IDMappingStore` casing fix, and the bridge
  collapse. All in one port; the conflict-system surface shrinks net.
- **Mechanic: big-bang in one commit (option A).** One landing does
  the whole migration atomically. WP types are byte-identical to
  upstream (the bridge proves this via `memcpy`), so the transition is
  a naming change plus the bridge deletion, not a semantic migration.
  The 74-ctest suite is the safety net. If a hunk causes unexpected
  trouble in implementation we can split into 2–3 commits in the same
  session, but the design target is one commit.
- **`src/sync/syncstate.{cpp,h}` stays in place.** It's still live
  code (mapping persistence) and is independent of the qsynccore
  delete. Migrating its `QSyncCore::IdMapping` usage to
  `Kalburator::Storage::IDMapping` is part of this port; relocating
  the files is not.
- **Zero libkalburator changes.** This port consumes the existing
  upstream surface (`Kalburator::Conflict::*`, `Kalburator::Storage::*`)
  and modifies only WP-local code. PlanStan is unaffected by
  construction. The libkalburator pretest policy
  (`feedback_planstan_pretest_for_upstream.md`) technically doesn't
  bind here; we still run PlanStan ctest before and after as a sanity
  check.
- **`KalburatorInteractiveConflictHandler` is already the only handler.**
  It was created in the M5a campaign and already derives from
  `Kalburator::Conflict::ConflictHandler`. No new handler class is
  introduced by this port; only its consumers change.

## Target architecture

```
        ┌─────────────────────────────────────────┐
        │  libkalburator (unchanged)              │
        │                                         │
        │  Kalburator::Conflict::                 │
        │    ConflictRecord, ConflictPolicy,      │
        │    ConflictStore, ConflictHandler,      │
        │    ConflictHandlerRegistry,             │
        │    ConflictManager,                     │
        │    AutoResolveStrategy / PromptStrategy │
        │    / FallbackBehavior / ConflictDecision│
        │                                         │
        │  Kalburator::Storage::                  │
        │    BaselineStore, IDMappingStore,       │
        │    IDMapping                            │
        └────────────────────┬────────────────────┘
                             │
                             ▼
        ┌─────────────────────────────────────────┐
        │  WildPalms                              │
        │                                         │
        │  src/app/conflict/                      │
        │    KalburatorInteractiveConflictHandler │
        │      ─ the only WP-side handler         │
        │      ─ derives from                     │
        │        Kalburator::Conflict::ConflictHandler│
        │      ─ optionally takes a               │
        │        PalmBackendConfig* (for          │
        │        ConnectionBehavior consult)      │
        │                                         │
        │  src/app/conflictdialog.{h,cpp}         │
        │    ConflictDialog — takes               │
        │      Kalburator::Conflict::ConflictRecord│
        │      & ConflictPolicy directly          │
        │                                         │
        │  src/widgets/dialogs/                   │
        │    conflictreviewdialog.{h,cpp} — same  │
        │                                         │
        │  src/palm/conflict/palmbackendconfig.h  │
        │    ConnectionBehavior + timeout —       │
        │      the ONE WP-side home for the       │
        │      Palm-specific session policy       │
        │                                         │
        │  src/plugins/{contacts,todos}/          │
        │    {Contacts,Todo}ConflictHandler — use │
        │      Kalburator::Conflict types directly│
        │                                         │
        │  src/sync/syncstate.{cpp,h}             │
        │    Stays where it is; now uses          │
        │      Kalburator::Storage::IDMapping     │
        │      for the bits it persists           │
        └─────────────────────────────────────────┘

DELETED by this port:
  src/sync/qsynccore/                            (8 files)
  src/app/conflict/conflictdialogbridge.{h,cpp}  (≈150 lines including comments)
  src/app/conflict/palmruntimebridgeinstall.cpp  (folded into PalmRuntime)
```

## Migration mechanics

### Step 1 — Namespace renames (consumer files)

Apply across the 11 src consumer files + 1 test file:

| From                              | To                                    |
|-----------------------------------|---------------------------------------|
| `QSyncCore::ConflictRecord`       | `Kalburator::Conflict::ConflictRecord` |
| `QSyncCore::RecordSnapshot`       | `Kalburator::Conflict::RecordSnapshot` |
| `QSyncCore::ConflictPolicy`       | `Kalburator::Conflict::ConflictPolicy` |
| `QSyncCore::ConflictStore`        | `Kalburator::Conflict::ConflictStore`  |
| `QSyncCore::ConflictHandler`      | `Kalburator::Conflict::ConflictHandler`|
| `QSyncCore::ConflictDecision`     | `Kalburator::Conflict::ConflictDecision`|
| `QSyncCore::AutoResolveStrategy`  | `Kalburator::Conflict::AutoResolveStrategy`|
| `QSyncCore::PromptStrategy`       | `Kalburator::Conflict::PromptStrategy` |
| `QSyncCore::FallbackBehavior`     | `Kalburator::Conflict::FallbackBehavior`|
| `QSyncCore::BaselineStore`        | `Kalburator::Storage::BaselineStore`   |
| `QSyncCore::IdMappingStore`       | `Kalburator::Storage::IDMappingStore`  |
| `QSyncCore::IdMapping`            | `Kalburator::Storage::IDMapping`       |
| `namespace QSyncCore { ... }`     | (remove forward decls; replace with `namespace Kalburator::Conflict` / `Storage` as appropriate) |
| `#include "sync/qsynccore/...h"`  | (remove; upstream header is reachable via existing include paths) |

The 12 consumer files are:

- `src/app/conflict/conflictdialogbridge.{cpp,h}` — deleted entirely in Step 4
- `src/app/conflictdialog.h`
- `src/app/conflict/kalburatorinteractiveconflicthandler.cpp`
- `src/app/conflictreviewwidget.h`
- `src/plugins/contacts/contactsconflicthandler.h`
- `src/plugins/todos/todoconflicthandler.h`
- `src/sync/syncstate.{cpp,h}`
- `src/widgets/dialogs/conflictreviewdialog.{cpp,h}`
- `tests/runtime/tst_palm_runtime_conflict_handler.cpp`

### Step 2 — `ConflictPolicy::connectionBehavior` references

Drop the references at the use sites:

- `src/app/conflict/conflictdialogbridge.cpp:33` — file dies in Step 4 anyway.
- `src/settingsdialog.cpp:341,358` — `m_profile->conflictConnectionBehavior()` continues to read/write a profile-level string; on apply, it now populates `PalmBackendConfig::connectionBehavior` instead of `ConflictPolicy::connectionBehavior`. The `ConnectionBehavior` symbol the settings dialog reads is now the one in `palmbackendconfig.h` (already exists; the parallel definition in `qsynccore/conflictpolicy.h` goes away with the directory delete).

### Step 3 — `ConflictDialog` / `ConflictReviewDialog` constructors

Retype:

- `ConflictDialog(const QSyncCore::ConflictRecord &, const QSyncCore::ConflictPolicy &, QWidget *)` → `ConflictDialog(const Kalburator::Conflict::ConflictRecord &, const Kalburator::Conflict::ConflictPolicy &, QWidget *)`
- Member fields `m_conflict`, `m_policy`, `m_decision` retype to upstream namespace.
- `ConflictReviewDialog(QSyncCore::ConflictStore *, …)` → `ConflictReviewDialog(Kalburator::Conflict::ConflictStore *, …)`

### Step 4 — Delete the bridge

Files removed:

- `src/app/conflict/conflictdialogbridge.h`
- `src/app/conflict/conflictdialogbridge.cpp`
- `src/app/conflict/palmruntimebridgeinstall.cpp`

Call-site replacements:

**In `kalburatorinteractiveconflicthandler.cpp:handleConflictOnGuiThread`** — replace `BridgePolicy` + `showAndGetDecision`:

```cpp
// before:
ConflictDialogBridge::BridgePolicy bp{ /* int casts */ };
int rawDecision = ConflictDialogBridge::showAndGetDecision(&wpRecord, bp, m_parentWidget);
return static_cast<Kalburator::Conflict::ConflictDecision>(rawDecision);

// after:
ConflictDialog dlg(conflict, policy, m_parentWidget);
dlg.exec();
return dlg.decision();
```

**In PalmRuntime's setup path** — replace `createAndInstall`:

```cpp
// before (palmruntimebridgeinstall.cpp):
auto *handler = ConflictDialogBridge::createAndInstall(runtime, parentWidget, qobjParent);
QObject::connect(handler, /* keepAlive */);

// after (PalmRuntime::installConflictHandler() or equivalent, folded inline):
auto *handler = new KalburatorInteractiveConflictHandler(
    m_conflictStore.get(), parentWidget, /* PalmBackendConfig if available */, qobjParent);
setConflictHandler(handler);
QObject::connect(handler, &KalburatorInteractiveConflictHandler::keepAliveRequested, /*…*/);
```

The exact home of the install logic (a method on PalmRuntime vs free function in kf6mainwindow) is decided during implementation. It needs to be on the GUI thread, with access to the conflict store and the parent widget — which PalmRuntime already has.

### Step 5 — Delete `src/sync/qsynccore/`

`git rm -r src/sync/qsynccore/` — eight files: `synccommon.h`, `conflictpolicy.{h,cpp}`, `conflictrecord.{h,cpp}`, `conflictstore.{h,cpp}`, `baselinestore.{h,cpp}`, `idmappingstore.{h,cpp}`.

Update `src/CMakeLists.txt` to drop these from the source list (currently lines 43–52).

### Step 6 — `syncstate.{cpp,h}`

`src/sync/syncstate.{cpp,h}` keeps its existing filenames and location, but its internal type references switch from `QSyncCore::IdMapping` to `Kalburator::Storage::IDMapping`. After this port, `src/sync/` contains only `syncstate.{cpp,h}` (plus `syncbackend.h`, which is a different concern).

### Step 7 — `IdMappingStore` → `IDMappingStore` casing

Upstream uses `IDMappingStore` (uppercase D); WP uses `IdMappingStore`. One additional sed across the same file set:

- `IdMappingStore` → `IDMappingStore`
- `IdMapping` (the struct in `synccommon.h`) → `IDMapping`

This is the only code-level change beyond the namespace renames.

## `ConnectionBehavior` relocation in detail

### Today (duplicate state)

- `QSyncCore::ConnectionBehavior` enum + `connectionBehaviorToString` / `…FromString` helpers in `src/sync/qsynccore/conflictpolicy.h`. Field: `QSyncCore::ConflictPolicy::connectionBehavior` + `connectionTimeoutSeconds`.
- `WildPalms::Palm::ConnectionBehavior` enum + same helpers in `src/palm/conflict/palmbackendconfig.h`. Field: `PalmBackendConfig::connectionBehavior` + `connectionTimeoutSeconds`.
- Profile (`settingsdialog.cpp:341,358`) reads/writes via `m_profile->conflictConnectionBehavior()`, currently feeding the QSyncCore side.
- `KalburatorInteractiveConflictHandler::shouldKeepConnectionAlive()` returns a private `m_keepAlive = true` bool; it does not currently consult either policy field.

### Target (single source)

- The `PalmBackendConfig` copy survives.
- The `QSyncCore` copy dies with `src/sync/qsynccore/`.
- The profile field still exists at QSettings level (no key-name change). On load, it populates `PalmBackendConfig::connectionBehavior` directly.
- `KalburatorInteractiveConflictHandler` constructor gains a `PalmBackendConfig*` parameter (non-owning, default `nullptr`). `shouldKeepConnectionAlive()` consults it: if set, return `cfg->connectionBehavior != DisconnectAndDefer`. If null, return the existing default (`true`).

### Wiring point

The single line that today reads "profile → `QSyncCore::ConflictPolicy::connectionBehavior`" becomes "profile → `PalmBackendConfig::connectionBehavior`". Located in the PalmRuntime / kf6mainwindow path that constructs `PalmBackendConfig` from profile settings — exact line found during implementation.

### Risk and mitigation

If a hidden sync-time read of `ConflictPolicy::connectionBehavior` exists somewhere not covered by the grep so far, it would silently drop after the migration. Mitigation: during implementation, grep for all reads of the field before deletion, confirm each is a UI-config-write rather than a sync-time-read. If any sync-time read exists, route through `PalmBackendConfig` instead.

## Bridge collapse: what dies, what replaces it

### Files deleted (≈150 lines net removal)

- `src/app/conflict/conflictdialogbridge.h` — `BridgePolicy` struct, `showAndGetDecision`, `createAndInstall`, `destroyHandler`, the explanatory wall-of-comments about include-guard collision.
- `src/app/conflict/conflictdialogbridge.cpp` — the `memcpy` between byte-identical structs, the `static_cast<int>` enum laundering, the temporary `QSyncCore::ConflictPolicy` reconstruction from `BridgePolicy`.
- `src/app/conflict/palmruntimebridgeinstall.cpp` — factory + signal wiring; folds into the PalmRuntime install path.

### Why this is safe after the qsynccore delete

The bridge exists *only* because both WP's `src/sync/qsynccore/` and libkalburator's `src/conflict/` define headers with the same include-guard names (`QSYNCCORE_CONFLICTRECORD_H`, etc.). A TU that includes both is silently broken by the second guard. With the WP copies gone, every TU sees one definition of every type. No memcpy hack, no `int` enum laundering, no `BridgePolicy` parallel struct.

### Net code change

≈150 lines deleted; ≈10 lines of direct-construction code added at the two former bridge-call sites. The bridge files are *only* this collision workaround; nothing in them is doing real work that needs to be preserved.

## Testing strategy + acceptance criteria

### Pre-port baseline (capture before any edits)

- WP ctest: 74/74 pass. Save the test list / result for diff.
- PlanStan ctest: capture current pass/fail counts as a baseline.
- No libkalburator rebuild needed since we are not touching upstream.

### During implementation (per-step checks)

- After Step 1 (namespace renames + casing fix): build must still succeed; WP ctest 74/74. The qsynccore/ directory still exists at this point; consumers just stopped using it.
- After Step 4 (bridge deleted, dialog rewired, palmruntimebridgeinstall folded in): build must still succeed; WP ctest 74/74. Specifically re-run the conflict-related tests: `tst_palm_runtime_conflict_handler`, `tst_contactsconflicthandler`, `tst_calendarconflicthandler`, `tst_todoconflicthandler`.
- After Step 5 (`src/sync/qsynccore/` deleted + CMakeLists trimmed): build must still succeed; WP ctest 74/74.

### Final acceptance

- WP ctest: 74/74, same set, same result. No test files added or removed by this port.
- PlanStan ctest: identical pass/fail to the pre-port baseline. Sanity check that nothing in WP-local churn somehow leaked into the libkalburator tree PlanStan pulls.
- Real-device smoke (manual, optional but recommended): run `./wildpalms` against a Palm m505, trigger a sync that has at least one expected conflict (e.g., modify one record both on-device and in a rawfiles store between syncs). Verify the dialog appears, the decision flows back to the engine, and the keep-alive `tickle` signal still emits.

### PlanStan-safety property

This port touches zero libkalburator code. The libkalburator pretest policy
(`feedback_planstan_pretest_for_upstream.md`) does not strictly bind here, but
we run PlanStan ctest pre and post as a low-cost sanity check that matches the
spirit of the coordination policy.

### Risk-bounded fallback

If any of (a) the `IdMappingStore` → `IDMappingStore` casing migration,
(b) the `ConnectionBehavior` wiring through `PalmBackendConfig`, or
(c) the bridge collapse causes test failures we can't immediately diagnose,
revert the offending hunk and split it into a follow-up commit. Approach A is
one commit by intent, but landing it as a quick sequence of 2–3 commits in the
same session is still cleaner than per-consumer drip-feed.

## Out of scope

- **Profile-settings UX refactor.** The `m_connectionBehaviorCombo` in
  `profilepropertiesdialog` and the `m_syncConnectionCombo` in `settingsdialog`
  keep their current shape. Only the *destination* of the value changes (from
  `ConflictPolicy` to `PalmBackendConfig`). A real settings-UX cleanup is a
  separate task.
- **`src/sync/syncstate.{cpp,h}` relocation.** Stays in `src/sync/`. Moving it
  is a separate concern.
- **`src/sync/syncbackend.h` cleanup.** That header is unrelated to the
  qsynccore family; out of scope.
- **PlanStan-side changes.** None. PlanStan is unaffected by this port.
- **New tests.** This port preserves the 74-ctest set; no new tests are
  added. The existing conflict-handler / plugin-handler tests prove the
  migration is type-correct and behaviour-preserving.

## Exit criteria

- `git ls-files src/sync/qsynccore/` returns empty.
- `git ls-files src/app/conflict/conflictdialogbridge.*` returns empty.
- `git ls-files src/app/conflict/palmruntimebridgeinstall.cpp` returns empty.
- `grep -r 'QSyncCore::' src/ tests/` returns no matches (in tracked files,
  ignoring docs/archived/).
- `grep -r 'IdMappingStore\|IdMapping\b' src/ tests/` returns no matches in
  type-name contexts (the renames are complete; only `IDMappingStore` /
  `IDMapping` should appear).
- WP ctest 74/74. PlanStan ctest unchanged.
- Phase-E spec's E.16 row deferral (a) marked ✅ in the row text.
- Integration plan's E.16 paragraph reflects "src/sync/qsynccore/ deleted;
  `src/sync/` reduced to syncstate".
