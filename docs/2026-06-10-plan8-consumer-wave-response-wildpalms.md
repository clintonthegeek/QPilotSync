# WildPalms response — Plan 8 consumer wave (ISyncHost shims + runSyncFuture retirement)

**Date:** 2026-06-10
**From:** WildPalms dev
**To:** libkalburator dev (architectural-redress campaign, Plan 8)
**Answers:** libkalburator `docs/2026-06-10-plan8-wildpalms-consumer-wave-handoff.md`
**Status:** **COMPLETE — WildPalms is runSyncFuture-clean. Step 3 is unblocked
from our side.**
**WildPalms tip:** local `main` (commits `d68fa5d` → `4dc3537`), pin **v0.69**,
ctest **120/120**.

---

## What landed, per the handoff's sequencing table

| Handoff item | Status | Commit |
|---|---|---|
| A.0 pin bump v0.66 → v0.69 | Done | `d68fa5d` |
| A.1 `PalmSyncHost` overrides deleted → base defaults + `setBackendRegistry()` | Done | `e5d2820` |
| A.2 `SyncHost_WP` | Kept, per recommendation | — |
| A.3 test hosts | Untouched, per handoff | — |
| B.1 `runAllMappings` → subset `SyncRequest` | Done | `4dc3537` |
| B.2 `runMirror` → single `SyncRequest` | Done | `4dc3537` |
| B.3 cancel-path test | Done (two tests, both paths) | `4dc3537` |

Build is clean of `runSyncFuture` deprecation warnings from WildPalms code.
The remaining warnings in our build log are lib-internal
(`examples/reference_consumer/main.cpp:300`, `src/sync/syncruncoordinator.cpp:60`)
— your own step-3 migration debt, listed here only so you don't grep our logs
and wonder.

## B.3 confirmation — the part you asked us to verify

We took the recommended robust pattern, on **both** sites (your parenthetical
asked B.1 to get the same check; it had the same exposure):

- Result delivery moved out of `.then()` into the K.8b T16 cancellation
  watcher's `finished` slot (fires on completion AND cancel). The caller-facing
  `QFuture<PalmRunResult>` is promise-backed and now always finishes.
- Reads use `engineFuture.resultAt(0)` guarded by `resultCount() > 0` — on the
  single-mapping path a canceled canonical future carries **no result at all**
  (your own FINDINGS caveat; the B.3 suggestion of `resultAt(0)` alone would
  assert there). When the result is missing and `isCanceled()`, we synthesize
  `cancelled=true`. You may want to fold that guard into the lib's own step-3
  single-mapping migration.
- New regression tests in `tests/runtime/tst_palm_runtime_cancel_sync.cpp`:
  `cancelSync_midRun_mirror_emitsRunFinished` (B.2 path) and
  `cancelSync_midRun_hotSync_emitsRunFinished` (B.1 path). A
  slow-`loadRecords` mock makes the cancel window deterministic. Both failed
  against the `.then()` delivery (spy never fired) and pass against the
  watcher delivery. B.4 conventions followed (`QTRY_*`, `resultAt(0)`).

Behavioral note for the record: pre-migration, a cancelled run's result was
simply never delivered (that was the bug); post-migration a cancelled run
reports `success=false` with `errorMessage="Sync cancelled"` through
`runFinished` and the returned future.

## §6 answer — no need to pull the dual future-interface collapse forward

The watcher-based workaround was straightforward in our shape (the watcher
already existed for cancel propagation). We have no urgent need for the
collapse; sequence it on your own schedule. The `resultCount()==0` guard above
is the only wart worth carrying into that design.

## §5 push status

Acknowledged. The push of WildPalms `main` (now ~100 commits ahead of origin,
including the v0.67-era and v0.69 pin bumps) is queued with the user — it is
their call per repo policy, flagged in this session.
