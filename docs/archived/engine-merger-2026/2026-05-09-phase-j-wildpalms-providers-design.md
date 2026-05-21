# Phase J — WildPalms calendar + contacts via providers (E2E audit)

**Status:** ⏳ designed 2026-05-09. Plan companion:
`2026-05-09-phase-j-wildpalms-providers-plan.md`.
**Tag (planned):** `v0.30-phase-j-wildpalms-providers`.
**Gates:** Phase Ic landed (✅ 2026-05-09).

## 1. Why this phase exists

Phase Ic shipped the WildPalms accounts UX: `AccountController`,
`AccountsPage`, `AddAccountDialog` (CalDAV + CardDAV kinds),
`MappingPromptDialog`, and the extended `MappingRowDialog` /
`MappingEditorDialog`. The plumbing exists; nothing has yet
**driven a real Palm ↔ DAV sync end-to-end through the runtime**.

Phase J is an **audit phase**. It codifies E2E behavior as
integration tests against fake DAV servers, then closes whatever
gaps those tests surface. No new architecture. No new UI.

The original ROADMAP placeholder for J ("WildPalms migrates other
domains to providers") is partly stale: Phase Ic already shipped
the AccountsPage + default-mapping pieces. What's left is
verification, plus any plumbing fixes the verification reveals.

Memo and todo stay RawFile-only. Designing memo / todo providers
is out of scope; tracked in `04w-deferred-work.md` C-section if
ever revisited.

## 2. Goal

Two integration tests, green:

- **`tst_runtime_caldav_e2e`** — Palm calendar ↔ FakeCalDavServer
  through `PalmRuntime` + `AccountController` + `CalDavProvider`.
- **`tst_runtime_carddav_e2e`** — symmetric, with
  FakeCardDavServer + `CardDavProvider` + the contacts plugin.

…and any fixes those tests demand. Probable fix list (best
estimate; the audit drives the actual list):

- F1. `PalmRuntime::connectDevice` default-mapping logic at
  `palmruntime.cpp:307` is all-or-nothing
  (`if (m_mappings.isEmpty())`). When a user has provider-bound
  mappings for *some* Palm slots but no mappings for others (e.g.
  calendar bound to CalDAV; memo / todo unbound), today's check
  skips RawFile defaults entirely. Phase J makes this **per-slot**:
  generate a RawFiles default for each Palm collection that has no
  mapping yet; leave existing mappings untouched.
- F2. **Connect-before-sync ordering.** `ProviderManager::connectAll()`
  is async; provider-supplied composite-id backends register into
  `BackendRegistry` only after discovery succeeds. If
  `PalmRuntime::start()` runs a sync before the provider's
  collections register, the mapping resolves to "missing backend."
  Phase J either (a) confirms the engine already skips missing
  mappings cleanly with a clear log line, or (b) adds a
  ready-gate. Decision deferred to plan; depends on what the
  audit shows.
- F3. **Transcoding registration.** Verify `(calendar, palm) ↔
  (calendar, ical)` and `(contacts, palm) ↔ (contacts, vcard4)`
  Pipelines compile in both directions when the runtime is wired
  through providers. The plugins register their transformers on
  load; this should already work, but the integration test is the
  first time it runs in the runtime context.

If the audit finds something genuinely outside this list, surface
to user before fixing — Phase J's scope is verification, not new
architecture.

## 3. Out of scope

- Memo / todo providers. RawFiles for those slots, indefinitely.
- Real-device verification (E.1 in `04w-deferred-work.md`).
- New UX. The Phase Ic UI is the contract Phase J verifies, not
  changes.
- KWallet (B.4), CTag (B.2), ETag at engine level (B.1), RFC 6764
  auto-discovery (B.3), Nextcloud-style multi-protocol provider
  (B.5), vCard version-negotiation hardening (B.6) — all remain
  deferred per `04w`.
- Akonadi backend (C.1) — separate phase if ever pursued.

## 4. Test architecture

### 4.1 Where the tests live

`WildPalms/tests/runtime/tst_runtime_caldav_e2e.cpp` and
`tst_runtime_carddav_e2e.cpp`. Add both to
`WildPalms/tests/runtime/CMakeLists.txt` next to existing
`tst_palm_runtime_hotsync` / `tst_palm_runtime_default_mappings_only_when_empty`.

### 4.2 Fake server reuse

`libkalburator/tests/sync/fakecaldavserver.{h,cpp}` and
`fakecarddavserver.{h,cpp}` already exist (used by
`tst_caldav_provider`, `tst_caldav_config_widget`,
`tst_carddav_capability_discovery`). They are header + cpp pairs
with no dedicated static lib — each existing test compiles them
directly. Phase J does the same: WildPalms test executables
list these `.cpp` files in their `add_executable` SOURCES.

If linking the libkalburator test sources from a sibling repo's
build tree creates path / include grief, the alternative is to
promote the fakes to a small shared static lib
(`kalburator_dav_test_fakes`) at the libkalburator side and link
both libkalburator's existing tests and WildPalms's new ones to
it. Keep this fallback in mind; don't preemptively land it.

### 4.3 Test shape (model: `tst_palm_runtime_hotsync`)

The hotsync test (`tst_palm_runtime_hotsync.cpp`) is the
template:

1. Build a `PalmRuntime` against a temp profile.
2. Register a Palm-side blob backend via
   `registerBlobBackendForTest()` (a `MockBlobBackend` seeded with
   one or two records).
3. **(New for Phase J)** Stand up `FakeCalDavServer` (resp.
   `FakeCardDavServer`); construct a `BackendConfiguration`
   pointing at `server.baseUrl()`; construct a `CalDavProvider`
   (resp. `CardDavProvider`); load + connect via the same path
   `AccountController` uses. The composite-id backend registers
   itself.
4. Construct a `SyncMapping` whose source is the Palm-side mock
   and target is the provider-supplied composite id; call
   `setMappingsForTest({...})`.
5. Run `runtime.runHotSync()` (or whatever the runtime's
   one-shot entry point is — confirm during plan); wait on the
   returned `QFuture<PalmRunResult>` with
   `QTRY_VERIFY_WITH_TIMEOUT`.
6. Assert: server-side state contains the Palm-originated record;
   Palm-side state contains anything the test seeded server-side.

### 4.4 Coverage matrix per executable

`tst_runtime_caldav_e2e`:
- `palm_to_caldav_propagates` — one event Palm-side, empty
  CalDAV; after sync, server has the event.
- `caldav_to_palm_propagates` — one event server-side, empty
  Palm; after sync, Palm-side mock has the record.
- `bidirectional_no_conflict` — disjoint records on both sides
  merge.
- `default_mappings_per_slot_when_calendar_bound` — exercises
  fix F1: bind a CalDAV mapping for calendar; assert
  `PalmRuntime::connectDevice()` still produces RawFile defaults
  for memo + todo slots. (Test guards F1 even if no other
  refactor lands.)

`tst_runtime_carddav_e2e`:
- `palm_to_carddav_propagates`.
- `carddav_to_palm_propagates`.
- `bidirectional_no_conflict`.

If any of the above proves harder to write than expected (e.g.
the runtime's one-shot entry shape doesn't fit), drop the bi-
directional case from each executable and keep the two
unidirectional + the default-mapping case. Goal is coverage
sufficient to prove the wire works; not exhaustive matrix.

### 4.5 Fixture lifetime

`FakeCalDavServer` / `FakeCardDavServer` are `QTcpServer`
subclasses; they `listen()` on an ephemeral port and
`baseUrl()` returns `http://127.0.0.1:<port>/`. Construct on the
stack inside each test; destruct triggers `close()`. No
cross-test state.

`PalmRuntime` is also stack / `QTemporaryDir`-rooted per
existing tests.

`AccountController` requires a `Profile *` — the tests can use
the same `Profile` constructor pattern Phase Ic widget tests use
(`tst_account_controller.cpp` is the reference).

## 5. Likely fixes & acceptance criteria

### F1. Per-slot RawFile defaults

**Code:** `WildPalms/src/runtime/palmruntime.cpp:307`.

Today:
```cpp
if (m_mappings.isEmpty()) {
    for (const auto &palmCol : palmCollections) { /* default mapping */ }
}
```

Replace with per-slot logic: for each `palmCol` in
`palmCollections`, only auto-create a RawFiles default mapping if
no existing `m_mappings` entry has
`m.sourceBackend == id && m.sourceCalendar == palmCol.id`.

**Acceptance:** the new test
`default_mappings_per_slot_when_calendar_bound` is the
acceptance bar. No regression in
`tst_palm_runtime_default_mappings_only_when_empty`.

### F2. Connect-before-sync ordering

**Investigation step in plan, not a pre-baked fix.** Plan
includes a task to write the test once, observe the engine's
behavior, and decide:

- (a) If the engine already logs "no backend registered for id X"
  and skips the mapping cleanly: document the behavior; no code
  change. Tests gate provider connect with
  `QTRY_VERIFY_WITH_TIMEOUT(provider.collections().size() > 0,
  5000)` before triggering sync.
- (b) If the engine fails noisily or hangs: introduce
  `ProviderManager::isReady()` / `readyChanged()` and have
  `PalmRuntime` defer first sync until ready (or at least until
  every provider in `AccountController::providers()` is in
  `Connected` or `Error` state). Decision lands in plan after
  observing the test.

**Acceptance:** all four `tst_runtime_caldav_e2e` cases run
deterministically without races. The chosen approach (a) or (b)
is documented in the phase-status doc.

### F3. Transcoder registration verification

**Code:** `WildPalms/src/plugins/calendar/calendarbackendplugin.cpp`
+ `contactsbackendplugin.cpp` register transformers on load.

**Acceptance:** the integration tests pass. If a transformer is
missing, the engine's pipeline-compile step will error
("`no pipeline from (palm) to (ical)`"), making the gap obvious.
Fix at the registration site.

### Test posture target

- WildPalms: 80 → 82 test executables (the two new E2E ones).
  Sub-test growth ≥ 6 (4 + 3 = 7 minimum per §4.4).
- libkalburator: unchanged.
- PlanStan: unchanged.
- `verify-all.sh` returns `0` (or `3` improvement → baseline
  refresh during phase wrap-up).

## 6. Risks

- **R1. Linking libkalburator test sources from WildPalms's build
  tree.** The fakes live in `libkalburator/tests/sync/` and use
  `#include "..."` paths relative to that directory. If
  reproducing those include paths in WildPalms's CMakeLists is
  fragile, fall back to the static-lib option (§4.2). Mitigate
  by trying the direct-source approach first — it's how
  libkalburator's own tests do it.

- **R2. Provider construction needs a real `Profile`.** Phase Ic's
  widget tests work around this; the new tests follow the same
  pattern. Cost is per-test setup boilerplate, not architectural.

- **R3. CalDavProvider's discovery hits an HTTP server that's
  also producing PROPFIND-formatted XML.** FakeCalDavServer's
  PROPFIND already passes
  `tst_caldav_capability_discovery` and `tst_caldav_provider`,
  so this risk is low — but new test paths might exercise
  PROPFIND scenarios the existing tests don't. Mitigate by
  copying the seed/teardown pattern verbatim from
  `tst_caldav_provider.cpp`.

- **R4. Audit surfaces something we didn't anticipate.** This is
  the whole point of an audit phase — embrace it. If a finding
  exceeds "small fix in one file," surface to user before
  expanding scope. The autonomous-execution authorization covers
  the planned tasks, not unbounded scope creep.

## 7. Tag, deferrals, exit

**Tag:** `v0.30-phase-j-wildpalms-providers` on libkalburator's
HEAD per ROADMAP convention. Land after both consumers green.

**Deferrals on completion:**
- `04w-deferred-work.md` — gets a Phase J retrospective entry
  for whatever F1/F2/F3 resolved; no new deferrals expected.
- If F2 lands as approach (b), it closes a small gap in
  `ProviderManager`'s contract; mention in
  `libkalburator/docs/phase0/04*-phase-j-status.md`.

**Exit checklist:**
1. Library code: any F2 ProviderManager-readiness API in
   libkalburator (if (b) chosen).
2. Library tests: green (`tests/sync/` unaffected expected).
3. Consumers: PlanStan unchanged; WildPalms +2 test executables,
   F1 fix in `palmruntime.cpp`.
4. `verify-all.sh` clean.
5. Tag `v0.30-phase-j-wildpalms-providers`.
6. `CURRENT-STATUS.md` updated.
7. Phase-status doc
   `libkalburator/docs/phase0/04*-phase-j-status.md` flipped to
   "landed YYYY-MM-DD".
8. `FINDINGS.md` appended with non-obvious learnings (likely:
   whatever F2 turned into; any test-fixture quirks).

## 8. Cross-references

- `2026-05-09-phase-ic-wildpalms-accounts-ux-design.md` — Phase
  Ic context this phase verifies.
- `2026-05-08-phase-ib-carddav-transport-design.md` — CardDAV
  transport.
- `2026-05-06-phase-h-providers-design.md` — IProvider
  abstraction.
- `WildPalms/tests/runtime/tst_palm_runtime_hotsync.cpp` —
  template integration test.
- `libkalburator/tests/sync/tst_caldav_provider.cpp` —
  FakeCalDavServer usage pattern.
- `libkalburator/docs/phase0/04w-deferred-work.md` — items C.1
  (Akonadi), B.\* (transport extras) deliberately not pulled in.
