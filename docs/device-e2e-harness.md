# Device-e2e HotSync harness (POSE64)

Opt-in end-to-end tests that drive a real HotSync against a headless POSE64
emulator over a real pty/DLP link, asserting record-level fidelity with an
independent pilot-link decoder. This is the first test surface that exercises
WildPalms' real pilot-link DLP wire (every other test mocks the device).

## Run

    WILDPALMS_POSE64_BIN=/home/clinton/dev/POSE64/build/pose64 \
    WILDPALMS_PALM_BASELINE_PSF=/home/clinton/dev/POSE64/baseline-datebook-m515.psf \
    ctest --test-dir build -L device-e2e -V

Without the two env vars the integration test (`tst_device_e2e_hotsync`) `QSKIP`s,
so plain `ctest` is unaffected. The component unit tests (`tst_recontrol_client`,
`tst_pilotlink_decoder`, `tst_canon_calendar_seed`) always run.

## Baseline .psf

The baseline must be a calibrated, HotSync-ready m515 session whose `apps all`
list includes `DatebookDB`. A fresh m515 image only creates `DatebookDB` once the
Date Book app is actually used, so bake a baseline by installing an empty one:

    tests/device-e2e/scripts/make-baseline.sh \
        /home/clinton/dev/POSE64/build/pose64 \
        /home/clinton/dev/POSE64/freshm515.psf \
        /home/clinton/dev/POSE64/baseline-datebook-m515.psf

The baseline .psf and ROM images are machine-local (not in this repo).

## Architecture

See `docs/superpowers/specs/2026-06-14-pose64-e2e-hotsync-harness-design.md` and
`docs/superpowers/plans/2026-06-14-pose64-e2e-hotsync-harness.md`. Phase 1 covers
hub->Palm calendar (clean first HotSync).

## Roadmap (Phase 2 = fidelity matrix, Phase 3 = three-tier)

The authoritative, detailed roadmap lives in `CLAUDE.md` →
"POSE64 e2e harness: Phase 2 (fidelity matrix) + Phase 3 (three-tier)". Summary of the
**fidelity matrix** axes the harness scales to:

- **Conduits (4):** calendar (`DatebookDB`), contacts (`AddressDB`), memo (`MemoDB`), todos (`ToDoDB`)
  — each adds a `buildCanon<Domain>Event` seed helper and a pilot-link decoder
  (`unpack_Address`/`unpack_ToDo`/`unpack_Memo`).
- **Sync modes (5):** hotSync, fullSync, copyPalmToPC, copyPCToPalm, clobberSync.
- **Patterns (per conduit × mode):** seed-on-hub→Palm; seed-on-Palm→hub; both-edited→conflict;
  delete→tombstone + mass-delete guard; recategorize→named-category routing (+ first live
  `writeAppBlock`); unchanged-second-pass→skip-unchanged log; per-domain field coverage.

Make the integration test data-driven (`QTest::addRow`) so each cell is a named row; extend
`scripts/make-baseline.sh` + `mkdatebook.c` to bake empty `AddressDB`/`MemoDB`/`ToDoDB` (and a
category AppInfo block for the recategorize pattern). **Phase 3** adds a remote tier
(`FakeCalDavServer` / `LocalFolderContribution`) to verify Remote↔Hub↔Palm propagation in one
HotSync on real hardware.

## Components (`tests/device-e2e/`)

- `ReControlClient` - QTcpSocket client for POSE64's ReControl line protocol.
- `EmulatorFixture` - launches/controls the headless `pose64` child (pty, export, reset).
- `PilotLinkDecoder` - decodes an exported DatebookDB `.pdb` via pilot-link `unpack_Appointment` (independent of WP's encoder).
- `canonseed` - builds a canon calendar event for seeding the hub.

## Known findings surfaced by the harness (follow-ups, not yet fixed)

- The contacts conduit write-back reports "Write to contacts failed" when syncing
  against a baseline whose AddressDB has pre-seeded records.
- The canon->Palm calendar **alarm** transcode is lossy: a seeded alarm does not
  reach the device (engine logs `onWorkerTranscodingWarning warnings: QList("alarms")`).
  The integration test records the alarm outcome informationally rather than asserting it.
