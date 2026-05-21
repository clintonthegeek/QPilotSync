# Phase J — Implementation plan

**Companion to:** `2026-05-09-phase-j-wildpalms-providers-design.md`.
**Tag (planned):** `v0.30-phase-j-wildpalms-providers`.
**Authorization:** per `feedback_autonomous_phase_execution.md`,
the user has pre-approved this plan; execute tasks 1–10 without
per-task confirmation. STOP only if Task 4 or Task 7 surfaces a
genuine architectural ambiguity beyond the F1/F2/F3 envelope in
the design doc §5.

## Task 0 — Orient (5 min)

Read in this order:
1. `2026-05-09-phase-j-wildpalms-providers-design.md` (the spec).
2. `WildPalms/tests/runtime/tst_palm_runtime_hotsync.cpp` (the
   template).
3. `libkalburator/tests/sync/tst_caldav_provider.cpp` and
   `tst_carddav_capability_discovery.cpp` (FakeServer usage).
4. `WildPalms/src/runtime/palmruntime.cpp:270-345` (the
   default-mapping code F1 will edit).
5. `WildPalms/src/runtime/accountcontroller.cpp:90-118` (how
   `addProvider()` builds + connects a provider — the test
   reproduces this path without going through the dialog).

No code change in Task 0.

## Task 1 — CalDAV E2E test scaffold

Create `WildPalms/tests/runtime/tst_runtime_caldav_e2e.cpp`.
Add to `WildPalms/tests/runtime/CMakeLists.txt` next to
`tst_palm_runtime_hotsync` using the same helper macro
(`wildpalms_add_runtime_test` or whatever it uses — copy from
the hotsync entry).

Minimal first cut: a single test slot
`palm_to_caldav_propagates` that:
- Constructs `FakeCalDavServer` on stack; `listen()` on
  ephemeral port; `QVERIFY(server.isListening())`.
- Constructs `PalmRuntime` against `QTemporaryDir`.
- Registers a `MockBlobBackend` Palm-side seeded with one VEVENT
  (verbatim copy of the hotsync test's iCal body).
- Constructs a `BackendConfiguration` with `type = "caldav"`,
  `connectionParams["serverUrl"] = server.baseUrl().toString()`,
  username/password matching FakeCalDavServer's expected creds
  (look at `tst_caldav_provider.cpp` for the exact strings).
- Constructs a `CalDavProvider` directly (do NOT route through
  `AccountController` for now — that needs a `Profile`; reach
  for `AccountController` only if a later test needs it).
- `provider.load(cfg); provider.connect();` — wait for
  connection via `QTRY_VERIFY_WITH_TIMEOUT` on
  `provider.connectionState() == Connected` (5000ms).
- Wait for `provider.collections().size() > 0` (5000ms). Pick
  the first collection.
- Provider should have registered a `RemoteCalendarBackend`
  into `PalmRuntime::backendRegistry()` under composite id
  `<provider-id>:<collection-id>`. Verify with
  `runtime.backendRegistry().backendInstance(compositeId) !=
  nullptr`. **(If verification fails here, F2 investigation
  starts in Task 4.)**
- Build a `SyncMapping`: source = the Palm mock's id, target =
  the composite id; `setMappingsForTest({m})`.
- Run `runtime.runHotSync()` (or the runtime's appropriate
  one-shot entry — confirm by reading `palmruntime.h`); wait
  on the `QFuture<PalmRunResult>` with
  `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000)`.
- Assert: query the FakeCalDavServer's stored events for the
  collection; expect to see the seeded UID. (FakeCalDavServer's
  test API exposes this — check its header.)

Build, run, observe. **First-cut may fail; that's expected.**
Capture the failure mode and proceed to Task 2 / 4 as needed.

## Task 2 — CardDAV E2E test scaffold

Symmetric to Task 1 with `FakeCardDavServer`, `CardDavProvider`,
a vCard4 record body. Copy seed pattern from
`tst_carddav_capability_discovery.cpp`.

`tst_runtime_carddav_e2e.cpp` + `palm_to_carddav_propagates`
slot only at this point.

## Task 3 — Reverse-direction tests

Add `caldav_to_palm_propagates` to Task 1's executable and
`carddav_to_palm_propagates` to Task 2's executable. Pattern
matches forward direction but seeds the server side and asserts
on the Palm-side mock.

## Task 4 — F2 investigation: connect-before-sync

Re-run Tasks 1–3 with logging up. Observe whether the engine
handles "provider mapping for not-yet-registered backend"
gracefully or pathologically.

- (a) If the test passes consistently with the
  `QTRY_VERIFY_WITH_TIMEOUT(provider.collections().size() > 0,
  5000)` gate before `runHotSync()`: document the behavior in
  `04*-phase-j-status.md`. No code change. Skip Task 5.
- (b) If the test races / hangs / errors noisily: continue to
  Task 5.

Write the observation up in the phase-status doc draft now (a
few sentences); update at end of phase.

## Task 5 — F2 fix (only if (b) in Task 4)

Add `ProviderManager::isReady() const` returning true when every
held provider is in `Connected` or `Error` (terminal) state.
Add `ProviderManager::readyChanged(bool)` signal, emitted on
state transitions. Library-side change in
`libkalburator/src/sync/providermanager.{h,cpp}`.

Add a `WildPalms` runtime change in `palmruntime.cpp`: before
running first sync, if `m_accountController` is non-null and
`!m_accountController->providerManager()->isReady()`, defer the
sync via a one-shot `connect(...readyChanged...)`. Keep the
"sync without provider" path (no AccountController) untouched.

Add a libkalburator unit test for `isReady()` /
`readyChanged()` next to `tst_provider_manager`.

## Task 6 — F1: per-slot RawFile defaults

Edit `WildPalms/src/runtime/palmruntime.cpp:307`. Replace the
all-or-nothing `if (m_mappings.isEmpty())` with a per-slot loop:

```cpp
for (const auto &palmCol : palmCollections) {
    const bool alreadyBound = std::any_of(
        m_mappings.cbegin(), m_mappings.cend(),
        [&](const SyncMapping &m) {
            return m.sourceBackend == id
                && m.sourceCalendar == palmCol.id;
        });
    if (alreadyBound) continue;
    /* existing default-mapping construction goes here */
}
```

The existing inner block (RawFiles backend construction +
`SyncMapping` append) is unchanged — only the guard is rewritten.

Verify `tst_palm_runtime_default_mappings_only_when_empty`
still passes (it covers the "user has at least one mapping →
skip everything" case; the new logic still satisfies that for
the slot the user-mapping covers, and the test only seeds one
mapping for one slot, so the new behavior would create defaults
for *other* slots. Re-read the test post-fix and adjust the
assertion if it now needs to allow for N additional defaults,
but **prefer adjusting the test only minimally** — its intent
is "user mappings preserved," not "no defaults exist").

## Task 7 — F1 acceptance test

Add `default_mappings_per_slot_when_calendar_bound` to
`tst_runtime_caldav_e2e.cpp`. The test:
- Wires up a CalDAV mapping for the calendar slot only (no Palm
  device connect needed — bypass via test fixtures if cleaner).
- Has the Palm plugin set advertise three collections (calendar,
  memo, todo) — use mock plugin registration as the existing
  hotsync test does.
- Calls `connectDevice()` (or whatever path triggers the
  default-mapping logic).
- Asserts: the calendar mapping is the user-bound CalDAV one;
  memo and todo each got a `default-<plugin>-<col>` RawFiles
  mapping.

If this requires more `PalmRuntime` test surface (e.g. a way to
drive `connectDevice()` with a fake device that publishes those
collections), call that out and either reuse
`registerPluginForTest()` or add a minimal helper. **Do not
expand the runtime API beyond what the test needs.**

## Task 8 — F3 verification (no work expected)

If Tasks 1–3 already pass after Task 6 lands, F3 is satisfied
by construction (the engine's pipeline compile step would have
errored on a missing transformer). Document in the phase-status
doc that transformer registration was verified by integration
test.

If F3 *does* fail: the failure points to a missing
`registerTransformer` call in
`calendarbackendplugin.cpp::initializePlugin` or
`contactsbackendplugin.cpp::initializePlugin`. Add the missing
registration. Add a TODO note in `FINDINGS.md` since this would
be a non-obvious gap from Phase Ia/Ib's transcoder work.

## Task 9 — verify-all.sh + baseline refresh

Run `scripts/verify-all.sh`. Expect exit `3` (improvement —
new tests gained). Per autonomous-execution authorization,
refresh `baselines/wildpalms-worktree-ctest.txt` after
**confirming** the new tests are in the diff (and only the new
tests / expected sub-test growth are the source of the
improvement — flag if anything else flipped). Re-run
`verify-all.sh`; expect `0`.

## Task 10 — Documentation wrap-up (single commit)

Land in **one commit** on libkalburator (+ separate commits in
each affected consumer):

- `libkalburator/docs/phase0/04*-phase-j-status.md` — new file,
  status `landed YYYY-MM-DD`. Include F2 outcome (a vs. b) and
  F3 verification result.
- `libkalburator/docs/phase0/04w-deferred-work.md` — add Phase
  J retrospective for whatever closed; no new deferrals
  expected.
- `ROADMAP.md` — flip Phase J row to `✅ landed YYYY-MM-DD`,
  tag `v0.30-phase-j-wildpalms-providers`.
- `CURRENT-STATUS.md` — bump date; update "Where we are" /
  "Next" / "In flight" / "Recently committed"; flip Phase J
  to landed; identify the next phase (likely E.1 real-device
  verification gate, or close-out).
- `FINDINGS.md` — append any non-obvious finding from the
  audit (especially anything Task 4 surfaced about engine
  behavior under race conditions).

## Task 11 — Tag

```
git -C ~/dev/refactor-engine-merger/libkalburator tag \
    v0.30-phase-j-wildpalms-providers
```

Per autonomous-execution authorization, tag without separate
confirmation since the tag name is in the plan.

## Notes for the implementing agent

- Build cap: `-j 10` per memory `feedback_jobs_limit.md`.
- Don't edit pristine `~/dev/{libkalburator,PlanStan,WildPalms}`.
- Phase J's worktree is
  `~/dev/refactor-engine-merger/{libkalburator,WildPalms}`.
- WildPalms test executable additions go through the same
  CMake helper as existing tests; copy the
  `tst_palm_runtime_hotsync` entry verbatim and substitute
  names.
- If `runHotSync()` is the wrong entry (the runtime may have
  evolved to a different name post-Ic), grep for `QFuture<.*Run`
  in `palmruntime.h` to find the right one. Don't invent new
  surface; use what exists.
- The autonomous-execution authorization covers *this plan*. If
  the audit surfaces work outside Tasks 4–8's scope (e.g., a
  bug in `CalDavProvider`'s URL handling, a leak in
  `RemoteCalendarBackend`), STOP and surface to user.
