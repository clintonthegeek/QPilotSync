# Handoff: TwoWay sync churns and empties the hub when backend record-id namespaces differ (regression v0.77 → v0.97)

> **Resolution (2026-08-22, CLOSED):** accepted lib-side as FINDINGS **O55** and
> fixed at lib `db3b1c8` / tag **v1.00** via engine-side record-id aliasing
> (`WriteOperation::idAliases` → `blob_id_aliases`, BaselineStore schema v8)
> plus an identity-conflict fail-loud guard — our proposed Direction 1. WP pin
> bumped v0.77 → v1.00; no WP code change needed. Response:
> libkalburator `docs/2026-08-22-o55-hub-record-id-aliasing-response.md`.
> Follow-up: `tst_palm_runtime_route_recategorization` still fails at v1.00
> (alias/baseline non-consolidation on the recategorization path) — tracked in
> `docs/2026-08-22-libkalburator-o55-followup-recategorization-handoff.md`.

- **From:** WildPalms (this repo), `main` @ `7a4d564` + working-tree API ports
- **To:** libkalburator
- **Date:** 2026-08-21
- **Lib tested:** local `~/dev/libkalburator` @ `v0.97-6-g0d76278` (main, post-v0.97)
- **Severity:** high — silent data loss shape: records sync once, then the hub
  collection ends **empty** after the second HotSync pass; remotes never see the
  record. No error is reported; the sync reports success.

## Summary

Any TwoWay mapping between a backend that uses **bare record ids** and the
**GenericSqliteBackend hub** (which presents ids prefixed with
`<collectionId>\x01` on read) fails to converge. The engine's per-record diff
joins source/target/baseline strictly by raw `BackendRecord::id`, so the same
logical record looks like "created on one side + deleted on the other" on every
pass after the first. The fixpoint loop churns until the pass cap and the hub
collection ends empty. This worked at lib v0.77 (verified); it fails at v0.93+.

## Failing WildPalms tests (current main + lib v0.97-6)

- `tst_palm_runtime_route_first_sync` — palm DatebookDB record (slot 3
  "Work") should reach a mock remote via hub route `wp-route-u1/route-Work`.
  Result: mock remote gets **0 records**; hub `calendar` table ends with **0 rows**.
- `tst_palm_runtime_route_recategorization` — same signature.

Both pass with WP `007f4a7` + lib `v0.77` (verified in a worktree pair), so
this is a regression introduced between v0.77 and v0.93.

## Reproduction

Minimal shape (no Palm needed):

1. Backend A presents a record with id `palm:datebook:101` (any backend using
   bare ids — e.g. a mock).
2. Backend B is `GenericSqliteBackend`. A TwoWay mapping A↔B.
3. Run the mapping twice (or run a fixpoint loop with >1 pass).
4. After pass 1 the hub stores the record (`record_id = palm:datebook:101`
   in SQLite; `loadRecords()` returns id `calendar\x01palm:datebook:101`).
   From pass 2 on, the diff cannot join the sides and churns.

Note: the lib's own engine tests (`tst_engine_fixpoint_passes`,
`tst_engine_skip_unchanged`, `tst_engine_skip_invalidation`) use mock backends
on **both** sides, so a real `GenericSqliteBackend` is never a mapping endpoint
in your suite — this scenario is uncovered.

## Evidence (from the failing WP test, lib v0.97-6)

Log — the palm→hub leg transcoded the record fine (warning is expected lossiness):

```
SyncEngine::onWorkerTranscodingWarning - calendar: "palm:calendar"
    uid: "calendar\u0001palm:datebook:101" warnings: QList("timeTransparency")
```

The uid is the **hub-encoded** form — proof the record existed in the hub and
was re-read from it during a later pass.

Hub DB after the run (`<profile>/.state/hub.db`):

```
sqlite> select count(*) from calendar;
0
```

Baseline store after the run (`.state/.wildpalms-blob-baselines.db`) — note the
**same logical record tracked under both id forms** in one mapping:

```
sqlite> select mapping_id, record_id from blob_baselines_v3;
auto_wp-calendar_sync1|palm:datebook:101
auto_wp-calendar_sync1|calendar\x01palm:datebook:101   <- (actual ^A byte)
auto_wp-route-u1_sync1|calendar\x01palm:datebook:101
```

The route leg (FilteredCollectionBackend over the hub) inherits the prefixed
id, records a baseline, but by then the churn has emptied the hub — the remote
never receives anything.

## Analysis

- `perRecordDiff()` (src/engine/perrecorddiff.cpp) indexes source, target and
  baseline each by raw `BackendRecord::id` and unions the id sets. There is no
  id-aliasing/normalization step between backends.
- `GenericSqliteBackend::loadRecords()`/`createRecord()` return ids as
  `encodeRecordId(collectionId, origId)` = `<collectionId>\x01<origId>`
  (unchanged since G.8), while the record's id on the peer side is bare.
- Consequence: for a record created on A and written into the hub, pass 2 sees
  source-id ∉ target, target-id ∉ source ⇒ per-side "create + delete" churn.
  With `runAllMappings(maxPasses=3)` (WP's HotSync fixpoint) the final state
  depends on op ordering within the last pass; observed end state is an empty
  hub table.
- At v0.77 the same id-join existed, yet the WP tests passed — so something in
  v0.77→v0.93 changed which path/semantics these mappings take (candidate
  window we narrowed empirically: clean at `9e6dadf~1`, failing by `db3f317`,
  the createBackends()-contract commit; tested with different WP snapshots, so
  treat the window as approximate). We did not complete a full bisect — happy
  to run one on request with a pinned WP snapshot.

## Possible directions (lib-side call, obviously)

1. **Engine-side id aliasing:** when applying a Create to a backend whose
   returned id differs from the requested id, record the (source-id →
   returned-id) pair and use it to join on subsequent passes (the baseline
   entry could carry both ids).
2. **Hub-side bare ids:** stop prefixing on read (return `origId`), at the
   cost of id collisions across collections — likely why the prefix exists.
3. **Document + enforce:** declare cross-backend id equality a hard contract
   and make GenericSqliteBackend (and any other namespace-mangling backend)
   present engine-stable ids.

Direction 1 seems most robust; direction 3 is the cheapest honest contract.

## WildPalms-side notes

- WP pins libkalburator and will re-pin once this lands; no WP-side workaround
  is possible without one of the above (the prefix scheme is lib-internal).
- Related WP-side observation (separate, likely fine): the route leg's
  FilteredCollectionBaseline inherits the prefixed id form, so route mappings
  and their direct hub↔palm siblings must agree on the aliasing too.
- Contact: this doc; the repro is `ctest -R tst_palm_runtime_route_first_sync`
  in WildPalms with `WILDPALMS_LIBKALBURATOR_SOURCE_DIR` pointing at main.
