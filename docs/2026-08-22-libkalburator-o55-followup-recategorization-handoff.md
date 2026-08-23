# Handoff: O55 follow-up — recategorization scenario still fails at v1.00 (alias/baseline non-consolidation + destructive op under unresolved AskUser)

> **Resolution (2026-08-22, CLOSED):** accepted lib-side as FINDINGS **O56**,
> fixed at lib `b847ab8` / tag **v1.01**. Both asks confirmed: (1) the pass-2
> conflict was phantom — bidirectional (crossed) alias rows made the join
> resolve source and target onto different keys; fixed with anchor-stable id
> aliasing. (2) The hub-row deletion was a real invariant break — a non-conflict
> Delete op applied in-line while its sibling deferred; fixed with an
> all-or-nothing write hold for mapping-runs that defer unresolved AskUser
> conflicts. WP pin bumped v1.00 → v1.01; test passes unmodified; ctest
> 130/130. Response: libkalburator
> `docs/2026-08-22-o56-recategorization-followup-response.md`.

- **From:** WildPalms `main` + pin bump to lib **v1.00**
- **To:** libkalburator
- **Date:** 2026-08-22
- **Re:** your `2026-08-22-o55-hub-record-id-aliasing-response.md` (O55 closed)

## Summary

O55's aliasing fix works — `tst_palm_runtime_route_first_sync` passes
unmodified, thank you. But the second test named in the original handoff,
`tst_palm_runtime_route_recategorization`, still fails at v1.00 with what looks
like an O55-class residue: after a successful propagation pass, the NEXT pass
reports an **AskUser unresolved conflict (empty errorMessage)** for the
palm↔hub calendar leg, and the hub record **vanishes anyway** — so the down-
stream route leg then deletes the record from its remote. End state: data loss
again (hub collection empty, both route remotes empty).

## Scenario (what the test does)

1. Fresh profile. Seed DatebookDB record (id 201) in category slot "Work".
2. HotSync #1 — converges; hub + WorkCal remote get the record.
3. Test edits the HUB record directly (recategorize Work → Home) via
   `GenericSqliteBackend::updateRecord`, same record id, new contentHash.
4. HotSync #2 (fixpoint, maxPasses=3):
   - **Pass 1 — everything correct:** palm↔hub pushes the recategorized record
     to the palm (+1 created on palm; new device-assigned id), route-Work
     deletes from WorkCal (-1), route-Home creates on HomeCal (+1).
   - **Pass 2 — `auto_wp-calendar_sync1` fails:** `success=false`,
     `errorMessage=""`, `unresolvedConflicts.size()==1`, zero stats.
     Then `auto_wp-route-u-home_sync1` **deletes its record from HomeCal**
     (its FilteredCollectionBackend view went empty — the hub record is gone).

## End-state evidence (profile `.state/` DBs after the run)

Hub `calendar` table: **0 rows** (the record vanished during pass 2, despite
the AskUser conflict being unresolved — no resolution was injected).

`blob_baselines_v3` — ONE logical record, TWO unconsolidated rows:

```
auto_wp-calendar_sync1 | palm:datebook:201            | 0c2787f1 | 0c2787f1
auto_wp-calendar_sync1 | calendar\x01palm:datebook:201 | 24544419 | d7ed48b8   <- asymmetric
auto_wp-route-u-work_sync1 | calendar\x01palm:datebook:201 | 24544419 | 24544419
auto_wp-route-u-home_sync1 | calendar\x01palm:datebook:201 | 24544419 | 24544419
```

`blob_id_aliases` — BOTH directions stored for the same mapping:

```
auto_wp-calendar_sync1 | calendar\x01palm:datebook:201 -> palm:datebook:201
auto_wp-calendar_sync1 | palm:datebook:201 -> calendar\x01palm:datebook:201
```

## Reading

- Pass 1's post-write baseline/alias bookkeeping did not consolidate the two
  native id forms into one canonical baseline row: pass 2's diff still sees
  the record under two ids with mismatched per-side hashes → BothModified-
  shaped conflict (AskUser → unresolved → `success=false`, empty message).
- Something in pass 2 nonetheless **applied a destructive op to the hub**
  before/independent of the unresolved conflict — the hub row is gone, and the
  downstream route leg faithfully propagated that disappearance to HomeCal.
  Under AskUser-with-no-resolution nothing should have moved.
- The crossed (bidirectional) alias rows may be benign by design, but combined
  with the dual baseline rows they look like the same "two id forms treated as
  two records" signature O55 set out to kill — this time via the baseline
  layer rather than the diff join.

## Repro

```
ctest --test-dir build -R tst_palm_runtime_route_recategorization
# (WP main @ post-v1.00-pin; fresh profile state built per run — not the
#  poisoned-profile case from your response doc)
```

Per-mapping probe output available on request; happy to run any lib-side
instrumentation against this scenario.

## Asks

1. Confirm whether pass 2's conflict classification here is correct (we believe
   both sides were NOT modified — palm got the record in pass 1 and its
   baseline should reflect the read-back hash).
2. The hub-row deletion under an unresolved AskUser conflict looks like a
   separate invariant break ("no data movement when a conflict is pending").
3. If useful, we can bisect v0.99..v1.00 or test candidate fixes against this
   test within the day.
