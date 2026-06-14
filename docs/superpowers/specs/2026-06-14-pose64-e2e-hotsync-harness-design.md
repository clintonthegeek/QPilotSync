# POSE64-backed end-to-end HotSync fidelity harness — design (Phase 1 skeleton)

**Date:** 2026-06-14
**Status:** Design approved; implementation plan pending.
**Scope:** WildPalms-only, test-tree-only. **No `src/` change** for the skeleton. No
libkalburator change. POSE64 is consumed as an external binary (sibling repo `~/dev/POSE64`),
not modified.
**Roadmap:** stands up the first real-device end-to-end test surface. Every one of WildPalms'
126 ctests currently mocks the Palm (`MockKPilotLink` / `MockPalmDatabaseAccess`), so the
entire real pilot-link **DLP wire is untested**. This harness fills that gap and becomes the
vehicle that later retires the long "hardware-pending / user-smoke-test-pending" backlog in
`CLAUDE.md` (clobber Task 12, category-reconciler first live `writeAppBlock`, multi-hop
skip-unchanged on-device log, contacts id-prefix re-test, …).

---

## Problem

WildPalms syncs a real Palm OS device over a serial cradle via pilot-link
(`KPilotDeviceLink` → `pi_bind`/`pi_listen`/`pi_accept_to` → `dlp_*`). That entire layer —
CMP/PADP/DLP marshaling, record pack/unpack, AppInfo/category writes, `dlp_FindDBInfo`
modnum reads that drive skip-unchanged — only runs against hardware. The test suite
substitutes it wholesale with in-memory mocks. So:

- Bugs in the real wire (encoding, AppInfo, connection lifecycle) are invisible until a
  human plugs in a Palm and runs a manual smoke test.
- The backlog of "PENDING on a real Palm" items grows because each needs a hands-on
  hardware session to verify.
- "Sync fidelity" — does a calendar event's start/end/description/category/alarm actually
  survive the canonical→Palm-wire→on-device round trip — is never measured automatically.

POSE64 (sibling repo, v0.9.1, 2026-06-14) now removes the hardware dependency. As of its
**Phase 4.5 / GATE 4** (verified 2026-06-12, re-verified 2026-06-14) it performs a full
HotSync against pilot-link over an emulated serial port, headless, reproducibly
(10/10 ×2 soak on an idle host).

## Goals

1. Drive a **real HotSync** between a headless POSE64-emulated Palm and a headless
   `PalmRuntime`, over POSE64's **real PTY/DLP** serial link — exercising the exact code
   path the mocks replace.
2. Assert **record-level fidelity** of what landed on the device, using a decoder
   **independent of WildPalms' own encoder** (pilot-link's `unpack_*`), so the test cannot
   be fooled by a matched encode+decode bug.
3. Establish the reusable **seed → run → export → decode → assert** oracle and the
   two-process orchestration that the later fidelity *matrix* and *three-tier* phases build on.
4. Stay **opt-in and non-disruptive**: plain `ctest` is unchanged (126 pass + the new test
   skips when the emulator/baseline are absent); the real run is `ctest -L device-e2e` on a
   machine where POSE64 lives.

## Non-goals (Phase 1)

- The full fidelity **matrix** (all conduits × sync modes × edit/conflict/delete/recategorize
  patterns). Phase 1 is one scenario, one conduit, one direction.
- The **three-tier** remote leg (DAV/Akonadi). Phase 1 is **Palm ↔ Hub only**; the hub is
  the real `GenericSqliteBackend`, seeded directly. No remote server is stood up.
- Asserting the **skip-unchanged / modnum** behavior (Goal-2 of the multi-hop feature).
  Deferred to a later phase that adds log/modnum assertions.
- Any **`src/` change**. If a later scenario needs a control knob (longer `pi_accept_to`
  timeout, a sentinel device fingerprint to bypass the GUI mismatch dialog), it is added
  then, scoped to that phase.
- Wiring this into shared CI. ROMs are machine-local and gitignored; Phase 1 targets the dev
  box. The env-var design leaves a clean future CI-lane path.

---

## Key facts that make this cheap (verified during brainstorming recon)

**POSE64 side**

- The emulated Palm serial port is an ordinary **PTY** (`posix_openpt`/`grantpt`/`unlockpt`/
  `ptsname` → `/dev/pts/N`). It speaks real DLP/CMP/PADP; **pilot-link talks to it directly,
  no protocol bridge.** (`src/platform/EmTransportSerialUnix.cpp:614–668`.)
- The slave path is discoverable at runtime via ReControl `info` (appends
  `pty=/dev/pts/N` once the transport is installed —
  `src/core/ReControlCmds_Session.cpp:466–468`) and printed to stderr at startup.
- **ReControl** is a line-based TCP protocol (default port 6416, `--port <n>`), single active
  session, `OK`/`ERR` replies, `.`-terminated multiline bodies. Relevant commands:
  `info`, `install <prc|pdb>`, `export <dbname> <path>` (writes the *actual* on-device DB to
  a host file via the ROM `SavePalmFile`), `apps`, `delete <dbname>`, `reset [soft|hard]`,
  `load <psf>` / `save <psf>`, `button cradle tap`, `state`, `ui`, `tap-id <id>`.
- **Headless:** `QT_QPA_PLATFORM=offscreen`. A Python reference harness already exists
  (`tests/lib/harness.py`, `tests/lib/recontrol_client.py`); we re-implement the minimal
  client surface in C++.
- **Verified HotSync procedure (GATE 4):** *attach first, then tap* — open the slave port and
  enter the listener, **then** `button cradle tap`. The guest CMP listen window is ~1.2 s.
- **Known flake to handle:** a modal "HotSync Problem" form (id `12000`) can swallow re-taps;
  POSE64's own smoke test retries once (dismiss via `tap-id 12004`).

**WildPalms side**

- `PalmRuntime::connectDevice(QStringList)` → `PalmDeviceAccess::doConnect` → constructs
  `KPilotDeviceLink(devicePaths)` on a worker thread. The device path is **not hardcoded**.
- **Critical:** `ConnectionWorker::doConnect` skips the multi-port probe when the list has
  exactly one element and goes straight to `pi_bind`/`pi_listen`/`pi_accept_to`
  (`src/palm/kpilotdevicelink.cpp:189–196`). Passing a **single-element list
  `["/dev/pts/N"]`** therefore reproduces pilot-xfer's exact, GATE-4-verified flow and
  **avoids the risky probe close/reopen dance entirely** — no `src/` change needed.
- A headless sync is: construct `PalmRuntime(profileDir)` → `connectDevice([pty])` → await
  `connectionComplete` (auto-runs `finishConnect`, which wires the real
  `PilotLinkPalmDatabaseAccess` into the conduit backends and generates the palm↔hub
  mappings) → `hotSync()` returns `QFuture<PalmRunResult>`.
- `PalmRuntime` already builds a **real** `GenericSqliteBackend` hub at `.state/hub.db` and
  loads the four conduit plugins in its constructor; **AccountController / Akonadi are not
  required** for a Palm↔hub run.
- Test seams exist if needed: `registerBackendInstanceForTest`, `setMappingsForTest`,
  `setLinkFactoryForTest` — but the skeleton uses the real path end to end.
- WildPalms already links pilot-link, so `unpack_Appointment` (and `Address/ToDo/Memo`) are
  available to the oracle.

---

## Architecture

Two processes, one test-driver process:

```
ctest: tst_device_e2e_hotsync   (single process, Qt event loop)
 ├─ EmulatorFixture                          ├─ PalmRuntime (real, headless)
 │   QProcess: pose64 --offscreen            │   real GenericSqliteBackend hub (.state/hub.db)
 │     -psf <baseline.psf> --port <p>        │   4 conduit plugins; NO Akonadi/AccountController
 │   ReControlClient (QTcpSocket)            │   connectDevice(["/dev/pts/N"])  ← 1 elem ⇒ probe
 │     info / install / export / apps /      │      skipped ⇒ pi_bind direct (GATE-4 path)
 │     reset / load / button cradle tap      │
 └────────────────────────────────────────────────────────────────────────────────────────
 Orchestration (per scenario):
  1. emu.load(baseline);  pty = emu.info().pty           # never cache pty across a load
  2. seed real hub:  hub backend createRecord(canonical calendar event)
  3. rt.connectDevice([pty])                              # WP opens pty, blocks in pi_accept_to
  4. emu.button("cradle","tap")                           # guest CMP volley meets the listener
  5. await connectionComplete  → finishConnect auto-wires palm↔hub calendar mapping
  6. fut = rt.hotSync();  QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), …)
  7. emu.export("DatebookDB", tmp.pdb)
  8. recs = PilotLinkDecoder::readAppointments(tmp.pdb)   # independent oracle
  9. ASSERT recs contains the seeded event (start, end, description, category, alarm)
```

Steps 3–6 are the real pilot-link DLP wire. Step 2 seeds the **real** hub so the mapping
under test is the genuine palm↔hub calendar leg.

### Components (all new, under `tests/device-e2e/`)

1. **`ReControlClient`** (`recontrolclient.{h,cpp}`) — `QTcpSocket` wrapper over POSE64's
   line protocol. `connectTo(port)`; `Reply command(QString)` where
   `Reply{ bool ok; QString line; QStringList body; }` parses single-line `OK …`/`ERR …`
   and `.`-terminated multiline. Convenience: `info()` (parses `pty=`), `installFile(path)`,
   `exportDb(name, hostPath)`, `apps()`, `reset(kind)`, `load(psf)`, `button(name, action)`,
   `state()`, `ui()`. Per-command read timeout (configurable; `export`/`install` longer).
   Mirrors `recontrol_client.py` semantics. ~150 lines.

2. **`EmulatorFixture`** (`emulatorfixture.{h,cpp}`) — owns the POSE64 `QProcess`. Launches
   `pose64` with `QT_QPA_PLATFORM=offscreen`, `-psf <baseline>`, `--port <p>` (pick a free
   port to allow parallel suites later). Polls `state` until ready. Exposes the
   `ReControlClient`. `loadBaseline()` issues `load <baseline>` and **re-queries `info` for
   the (possibly new) pty**. Teardown sends `quit`, waits, kills if needed, captures stderr
   on failure. Resolves `WILDPALMS_POSE64_BIN` and `WILDPALMS_PALM_BASELINE_PSF` from the
   environment; if either is unset or missing, the test `QSKIP`s with a clear message.
   ~200 lines.

3. **`PilotLinkDecoder`** (`pilotlinkdecoder.{h,cpp}`) — the **independent oracle**. Reads a
   `.pdb` (pi-file format via pilot-link's `pi-file.h`) and decodes records with
   pilot-link's `unpack_Appointment` (Phase 1) — `unpack_Address` / `unpack_ToDo` /
   `unpack_Memo` added per conduit later. Returns plain value structs (e.g. `DecodedEvent`)
   the test asserts against. Never calls WildPalms code. ~150 lines (grows per conduit).

4. **`tst_device_e2e_hotsync.cpp`** — the QtTest. `init()` builds a `QTemporaryDir` profile;
   constructs `PalmRuntime`; the test method runs the 9-step orchestration and asserts.
   Reliability helpers (below) live here or in `EmulatorFixture`.

5. **`tests/device-e2e/CMakeLists.txt`** — defines the target, links pilot-link + the
   WildPalms runtime libraries, registers the test with CTest **label `device-e2e`**, copies
   any fixture data. Hooked from `tests/CMakeLists.txt`.

### Lifecycle & isolation

- **Baseline `.psf`:** a calibrated, HotSync-ready m515 session with empty PIM databases,
  launcher reachable, past the hard-reset confirmation screen. The repo already ships
  `freshm515.psf`; the plan's first task **validates it is suitable** (boots headless,
  `state` ready, `info` reports a pty, a bare pilot-xfer/`apps` round trip works). If it is
  not, the plan adds a one-time, documented baseline-creation script and checks the result
  in (or points the env var at it).
- **Per-test isolation:** each test `load`s the baseline (fresh RAM + known device), then
  **re-queries the pty** (a session `load` reinstalls the serial transport and may mint a
  new `/dev/pts/N` — the pty path is never cached across a load). Each test also gets a fresh
  `QTemporaryDir` profile (own `.state/hub.db`, baseline store, `palm-revisions.ini`).
  One POSE64 child process is shared for the whole test executable (process spawn is the
  expensive part); `load` between tests is the reset.
- **Reliability discipline (carried over from POSE64's GATE-4 experience):**
  - *Attach-then-tap* ordering is mandatory (open the pty / enter `pi_accept_to` before
    `button cradle tap`).
  - *Retry-once* on the modal "HotSync Problem" form: after a failed connect, check `ui` for
    form id `12000`; if present, dismiss (`tap-id 12004`) and retry the connect+tap once.
  - *Crash visibility:* if `state` reports `blocked_on_ui` or the process exits, fail the
    test with the captured `dialog` + `backtrace` + stderr — never hang. All awaits are
    bounded by `QTRY_VERIFY_WITH_TIMEOUT`.

### Build, gating, risks

- **Gating:** the target **always builds** (so it can't bitrot against `src/` changes), but
  `QSKIP`s at runtime when the env vars are absent. `ctest` → 126 pass + 1 skip;
  `ctest -L device-e2e` → runs the real thing where POSE64 + baseline exist. No CI emulator
  dependency in Phase 1.
- **Risks & mitigations:**
  | Risk | Mitigation |
  |---|---|
  | `pi_accept_to` timeout vs tap timing | Tap immediately after `connectDevice`; bound/extend the accept window; retry-once. |
  | PTY churn across `load` | Always re-query `info`; never cache the pty path. |
  | `freshm515.psf` not a usable baseline | Validated in plan Task 1; fallback = documented creation script. |
  | pilot-link `unpack_*` struct drift | Use the pilot-link version WildPalms already pins; appointment unpack is stable. |
  | Emulator nondeterminism / open landmines | Phase-1 is headless (landmines #5/#6 are display-gated); bounded awaits + crash capture + retry-once turn flakes into actionable failures, not hangs. |

---

## First scenario (the one green test)

**`hub → Palm, calendar, clean first HotSync`** — mirrors the real pending hardware smoke
test ("populated calendar → datebook, one HotSync should land events"):

1. Seed the real hub's calendar collection with **one** canonical event (fixed
   start/end/description, a category, an alarm) — constructed the same way the existing
   runtime tests build canonical records, but written to the real `GenericSqliteBackend`.
2. Run the orchestration; **one** `hotSync()`.
3. `export DatebookDB`; decode with `unpack_Appointment`.
4. **Assert:** exactly the seeded event is present on the Palm, with start, end, description,
   category, and alarm matching. (Record count and key fields — the fidelity check.)

The immediate next scenario (still Phase 1-adjacent, easy once the skeleton exists) is the
reverse — `Palm → hub`: `install` a `DatebookDB` with a known event, `hotSync`, assert it
landed in `hub.db` — which exercises the read path and proves the `install` seed mechanism
alongside the `export` readout.

## What this unlocks (later sub-projects, each its own spec → plan)

- **Fidelity matrix:** a scenario table parametrized over the same fixture —
  conduits {calendar, contacts, memo, todos} × modes {hotSync, fullSync, clobber, copy} ×
  patterns {seed-hub, seed-palm, both-edited→conflict, delete-one-side, recategorize}. This
  is the body of "really try all variations and evaluate sync fidelity," and it retires the
  hardware-pending backlog item by item.
- **Three-tier:** add a remote tier (`FakeCalDavServer` / `LocalFolder`) to assert
  Remote↔Hub↔Palm propagation in a single HotSync, plus the multi-hop **skip-unchanged**
  log/modnum criterion (Goal-2 of the multi-hop feature).
- **Category reconciler / AppInfo:** assert the first live `writeAppBlock` creates the
  expected category slots on the device (closes a substrate-A hardware gap).
