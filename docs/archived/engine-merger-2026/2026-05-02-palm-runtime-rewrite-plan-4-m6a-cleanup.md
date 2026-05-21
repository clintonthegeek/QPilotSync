# M6a — Orphan Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Predecessor:** Plan 3 / M5 (M5a + M5b + M5c landed 2026-05-02,
   tags `v0.18` / `v0.19` / `v0.20`).
**Successor:** Plan 5 / M6b (move device-connection lifecycle into
   `PalmRuntime`; delete `DeviceSession` + `DeviceWorker` +
   `TickleWorker`; rewire `KF6MainWindow`). Requires its own
   brainstorm — design choices (threading model, signal surface)
   need fresh thought, not stale assumptions.
**Status:** Plan written 2026-05-02. Implementation pending.

---

**Goal:** Delete two orphaned code islands that the M5 series left
behind — the V1 plucker plugin (already excluded from build) and
the unused `HotSyncCoordinator` class — without changing any
runtime behavior.

**Architecture:** Pure deletion. No new code. No interface changes.
Both targets are already disconnected from live code paths:
plucker's `add_subdirectory(plucker)` lines are commented out in
both `src/plugins/CMakeLists.txt` and `tests/plugins/CMakeLists.txt`,
and `HotSyncCoordinator` has zero call sites (verified by
`grep -rn "new HotSyncCoordinator\|HotSyncCoordinator("` matching
only its own definition).

**Tech Stack:** Bash (`rm -rf`, `git rm`), CMake edits, single repo
(WildPalms only — libkalburator and PlanStan untouched).

---

## Why this is its own sub-phase (not folded into M6b)

M6 per the original design (`2026-05-01-palm-runtime-rewrite-design.md`
§M6) bundles three things: (1) delete plucker, (2) delete
`SyncRunner_wp`/`DeviceSession`/`DeviceWorker`/`TickleWorker`, and
(3) rip mode-dispatch out of `KF6MainWindow`.

After M5c the picture changed:

- `SyncRunner_wp` is already gone (M5c pulled it forward).
- Mode dispatch in `KF6MainWindow` is already replaced — `onHotSync`
  / `onFullSync` / `onCopyPalmToPC` / `onCopyPCToPalm` / `onBackup` /
  `onRestore` already call `m_palmRuntime->X()`.
- `DeviceSession` + `DeviceWorker` + `TickleWorker` are still live
  for **device connection lifecycle** (not sync execution). Deleting
  them requires `PalmRuntime` to grow `connectDevice(QStringList
  paths)` that owns the `KPilotDeviceLink` itself. That's a real
  refactor with threading-model design decisions.

So M6 actually splits cleanly:

- **M6a (this plan)** — pure deletion of orphans. No design needed.
- **M6b (next plan)** — DeviceSession migration. Needs brainstorm.

Doing M6a as its own commit/tag means the orphan cleanup ships
immediately and isn't held up behind M6b's design work.

---

## Out of scope

- `DeviceSession` / `DeviceWorker` / `TickleWorker` — M6b.
- `DeviceSession::requestSync` + `DeviceWorker::doSync` (dead code
  path with no callers) — would be deleted here on principle, but
  the entire enclosing classes vanish in M6b regardless. Skipping
  to avoid wasted churn.
- `AutoSyncOrchestrator` — narrowed to USB-serial profile lookup
  in M5b. Not listed in design doc M6 scope. Leave alone.
- Anything in libkalburator or PlanStan.

---

## File Structure

**Files to delete (recursively):**

- `WildPalms/src/plugins/plucker/` — entire directory (7 plugin
  source files + `parser/PyPlucker/` + `viewer/` + `CMakeLists.txt`
  + `plucker-backend-plugin.json` + `pluckerconfig.{cpp,h}`).
- `WildPalms/tests/plugins/plucker/` — entire directory (5 test
  source files + `CMakeLists.txt` + `fixtures/`).
- `WildPalms/src/runtime/hotsynccoordinator.h`
- `WildPalms/src/runtime/hotsynccoordinator.cpp`

**Files to modify:**

- `WildPalms/src/plugins/CMakeLists.txt` — remove the
  commented-out `# add_subdirectory(plucker)` block + its
  preceding M6-TODO comment.
- `WildPalms/tests/plugins/CMakeLists.txt` — remove the
  commented-out `# add_subdirectory(plucker)` block + its
  preceding M6-TODO comment.
- `WildPalms/src/runtime/CMakeLists.txt` — remove the
  `hotsynccoordinator.h` / `hotsynccoordinator.cpp` lines and
  the surrounding `# G.7 — HotSyncCoordinator replaces ...`
  comment from the `target_sources(WildPalmsRuntime PRIVATE ...)`
  block.

**Files to update at end (status / index):**

- `refactor-engine-merger/CURRENT-STATUS.md`
- `WildPalms/CLAUDE.md` — only if it references plucker or
  HotSyncCoordinator (verify; do not modify if no references).
- `refactor-engine-merger/FINDINGS.md` — append only if anything
  non-obvious surfaces during execution.

---

## Verification gate

`scripts/verify-all.sh` from the coordination folder root must
exit `0` (matches baseline). No real-device test required —
neither deletion target affects runtime behavior.

Test posture before M6a (per `CURRENT-STATUS.md` 2026-05-02):

- libkalburator: 54/54 pass
- PlanStan: 90/114 pass (24 pre-existing env failures)
- WildPalms: 75/75 pass

Test posture after M6a (expected):

- libkalburator: **54/54 pass** (unchanged — no libkalburator changes)
- PlanStan: **90/114 pass** (unchanged — no PlanStan changes)
- WildPalms: **75/75 pass** (unchanged — deleted plucker tests
  weren't in the build; HotSyncCoordinator has no tests to delete)

If any number changes, **stop and investigate** before proceeding.

---

### Task 1: Delete plucker source tree + CMake reference

**Files:**
- Delete: `WildPalms/src/plugins/plucker/` (directory + all contents)
- Modify: `WildPalms/src/plugins/CMakeLists.txt`

- [ ] **Step 1: Confirm the plucker tree is not built**

Run: `cd WildPalms && grep -n "add_subdirectory(plucker)" src/plugins/CMakeLists.txt`
Expected: only commented matches (lines starting with `#`).

If any uncommented `add_subdirectory(plucker)` exists, **stop**.
The premise that plucker is build-disabled is false — investigate
before deleting.

- [ ] **Step 2: Delete the source directory**

Run: `cd WildPalms && rm -rf src/plugins/plucker`

- [ ] **Step 3: Remove the M6-TODO comment block from src/plugins/CMakeLists.txt**

Open `WildPalms/src/plugins/CMakeLists.txt`. Find the trailing
block:

```cmake
# Plucker plugin is V1 (not migrated to IBackendPluginV2). Its .so trips
# a double-free in static destructors when discovered by
# KPluginMetaData::findPlugins, breaking the M5c plugin-page smoke test.
# Excluded from the build; M6 will delete the plucker plugin source tree
# entirely (see 2026-05-02 M5c plan / FINDINGS.md).
# add_subdirectory(plucker)
```

Delete the entire block (all 6 lines). The file should end after
the last live `add_subdirectory(...)` call.

- [ ] **Step 4: Verify CMake configures cleanly**

Run: `cd WildPalms && cmake --build build-dev --target WildPalmsCore -- -k0 2>&1 | tail -20`

If the project uses CMake presets that require a re-configure
when CMakeLists.txt changes, run:

`cd WildPalms && cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -10`

Expected: configure succeeds, no references to `src/plugins/plucker`
in the output.

- [ ] **Step 5: Commit**

```bash
cd WildPalms
git add -A src/plugins
git commit -m "M6a Task 1: delete plucker plugin source tree

Plucker was V1 (never migrated to IBackendPluginV2) and tripped a
static-destructor double-free in KPluginMetaData::findPlugins that
broke the M5c plugin-page smoke test. Already build-excluded since
M5c via commented add_subdirectory; this commit removes the source
tree itself + its now-orphaned add_subdirectory comment block."
```

---

### Task 2: Delete plucker tests + CMake reference

**Files:**
- Delete: `WildPalms/tests/plugins/plucker/` (directory + all contents)
- Modify: `WildPalms/tests/plugins/CMakeLists.txt`

- [ ] **Step 1: Confirm the plucker tests are not built**

Run: `cd WildPalms && grep -n "add_subdirectory(plucker)" tests/plugins/CMakeLists.txt`
Expected: only commented matches (lines starting with `#`).

- [ ] **Step 2: Delete the test directory**

Run: `cd WildPalms && rm -rf tests/plugins/plucker`

- [ ] **Step 3: Remove the M6-TODO comment block from tests/plugins/CMakeLists.txt**

Open `WildPalms/tests/plugins/CMakeLists.txt`. Find the trailing
block:

```cmake
# Plucker plugin tests are commented out — the plugin itself is excluded
# from the build (see src/plugins/CMakeLists.txt). M6 deletes both.
# add_subdirectory(plucker)
```

Delete the entire block (all 3 lines). The file should end after
the last live `add_subdirectory(...)` call.

- [ ] **Step 4: Verify CMake re-configures cleanly**

Run: `cd WildPalms && cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -10`

Expected: configure succeeds, no references to `tests/plugins/plucker`.

- [ ] **Step 5: Commit**

```bash
cd WildPalms
git add -A tests/plugins
git commit -m "M6a Task 2: delete plucker plugin tests

Pair with M6a Task 1 (source tree deletion). Tests were already
commented out of tests/plugins/CMakeLists.txt since M5c."
```

---

### Task 3: Delete HotSyncCoordinator + CMake reference

**Files:**
- Delete: `WildPalms/src/runtime/hotsynccoordinator.h`
- Delete: `WildPalms/src/runtime/hotsynccoordinator.cpp`
- Modify: `WildPalms/src/runtime/CMakeLists.txt`

- [ ] **Step 1: Confirm HotSyncCoordinator is unused**

Run:
```bash
cd WildPalms && grep -rn "HotSyncCoordinator" --include="*.h" --include="*.cpp" --include="CMakeLists.txt" 2>/dev/null | grep -v build/
```

Expected matches (only):
- `src/runtime/hotsynccoordinator.h:30:class HotSyncCoordinator : public QObject`
- `src/runtime/hotsynccoordinator.h:34:    HotSyncCoordinator(...)`
- `src/runtime/hotsynccoordinator.cpp:1:#include "hotsynccoordinator.h"`
- `src/runtime/hotsynccoordinator.cpp:11:HotSyncCoordinator::HotSyncCoordinator(...)`
- `src/runtime/hotsynccoordinator.cpp:22,24,27,49,63` (member-fn definitions)
- `src/runtime/CMakeLists.txt:42-44` (the `target_sources` block + comment)
- `src/palm/contacts/palmcontactsbackend.h:18` (a doc comment only —
  see Step 2)

If `grep -rn "new HotSyncCoordinator\|HotSyncCoordinator("` shows
**any** match outside `hotsynccoordinator.cpp` itself, **stop**.
Something instantiates it; M6a's premise is wrong and the deletion
is unsafe.

- [ ] **Step 2: Fix the stale doc-comment reference in palmcontactsbackend.h**

Open `WildPalms/src/palm/contacts/palmcontactsbackend.h:18` and
read the comment. It currently says (verbatim):

> ```
>  * and is used by HotSyncCoordinator to identify the Palm resource.
> ```

Replace `HotSyncCoordinator` with `PalmRuntime` so the comment
reflects current reality:

```
 * and is used by PalmRuntime to identify the Palm resource.
```

This is a one-word edit. Don't rewrite the surrounding paragraph.

- [ ] **Step 3: Delete the source files**

Run:
```bash
cd WildPalms && rm src/runtime/hotsynccoordinator.h src/runtime/hotsynccoordinator.cpp
```

- [ ] **Step 4: Remove the target_sources entry from src/runtime/CMakeLists.txt**

Open `WildPalms/src/runtime/CMakeLists.txt`. Find the block
inside `target_sources(WildPalmsRuntime PRIVATE ...)`:

```cmake
    # G.7 — HotSyncCoordinator replaces SyncRunner_wp (Task 55 deletes the old files).
    hotsynccoordinator.h
    hotsynccoordinator.cpp
```

Delete all three lines. The next comment in the block (the
`# M2 Task 2/3 — PalmDeviceAccess` block, currently at line 46)
becomes the first entry of the `target_sources` body.

- [ ] **Step 5: Re-configure and build WildPalmsRuntime**

Run:
```bash
cd WildPalms && cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -10
cd WildPalms && cmake --build build-dev --target WildPalmsRuntime 2>&1 | tail -30
```

Expected: configure + build both succeed. No references to
`hotsynccoordinator` remain in `build-dev/src/runtime/`.

- [ ] **Step 6: Confirm no stale moc artifacts reference HotSyncCoordinator**

Run:
```bash
cd WildPalms && grep -rn "hotsynccoordinator" build-dev/src/runtime/ 2>/dev/null | head
```

Expected: empty output (the `WildPalmsRuntime_autogen/mocs_compilation.cpp`
file should have been regenerated and no longer reference
`moc_hotsynccoordinator.cpp`).

If any matches remain, run a clean rebuild of the runtime target:
```bash
cd WildPalms && cmake --build build-dev --target WildPalmsRuntime --clean-first 2>&1 | tail -20
```
and re-check.

- [ ] **Step 7: Commit**

```bash
cd WildPalms
git add -A src/runtime src/palm/contacts/palmcontactsbackend.h
git commit -m "M6a Task 3: delete orphaned HotSyncCoordinator

HotSyncCoordinator was introduced in G.7 (Task 48-49) as a
replacement for SyncRunner_wp's device-event reaction layer. The
Palm runtime rewrite (M2-M5) made it obsolete: PalmRuntime::hotSync
is now driven directly by KF6MainWindow's onHotSync handler and
no code instantiates HotSyncCoordinator anywhere.

Verified zero call sites: grep -rn 'new HotSyncCoordinator|HotSyncCoordinator('
matches only its own constructor definition.

Also fixes a stale doc-comment reference in palmcontactsbackend.h
(s/HotSyncCoordinator/PalmRuntime/)."
```

---

### Task 4: Run verify-all.sh

**Files:** none modified.

- [ ] **Step 1: Run cross-repo verification**

Run: `cd /home/clinton/dev/refactor-engine-merger && ./scripts/verify-all.sh 2>&1 | tail -40`

Expected: exit code `0` (all builds + tests match baseline).

- [ ] **Step 2: Interpret the exit code**

| Exit | Meaning | Action |
| ---- | ------- | ------ |
| 0    | Match baseline. | Proceed to Task 5. |
| 1    | Configure or build failure. | **Stop.** Read the script's last 60 lines of output, find the failing target, fix the underlying issue, re-run. Do NOT skip ahead. |
| 2    | Test regression (pass→fail somewhere). | **Stop.** A previously-passing test now fails — M6a was supposed to be behavior-preserving. Investigate which test, which repo, which commit caused it. Likely candidates: a stale `#include "hotsynccoordinator.h"` somewhere, or a CMake target that quietly depended on the deleted files. |
| 3    | Test improvement (fail→pass somewhere). | **Stop.** Unexpected — investigate before refreshing baseline. Could be a flaky test that happened to pass this run; do not treat as a real fix without confirming. |

- [ ] **Step 3: If exit was 0, confirm test counts**

Run:
```bash
cd /home/clinton/dev/refactor-engine-merger
grep -E "^(libkalburator|PlanStan|WildPalms): " baselines/*-worktree-ctest.txt 2>/dev/null | tail -10
```

Or if `verify-all.sh` printed a summary, read it. Expected counts
exactly:

- libkalburator: 54/54
- PlanStan: 90/114 (24 pre-existing env failures)
- WildPalms: 75/75

If any count differs, **stop** even if exit was 0 — a baseline
file may itself be stale.

---

### Task 5: Tag v0.21-phase-m6a-cleanup on WildPalms

**Files:** none modified.

- [ ] **Step 1: Confirm we are on the right branch + worktree**

Run:
```bash
cd WildPalms && git branch --show-current
```
Expected: `refactor/engine-merger`. (Per project convention this
worktree should always be on that branch — if not, **stop**.)

Run:
```bash
cd WildPalms && git log --oneline -3
```
Expected: HEAD is the M6a Task 3 commit; previous commits are
M6a Task 2 and M6a Task 1.

- [ ] **Step 2: Ask user before tagging**

`git tag` is on the destructive-operations list per
`refactor-engine-merger/CLAUDE.md` ("Ground rules → Destructive
operations"). The user must run the tag command themselves OR
explicitly authorize this session.

Print to the user:

> M6a tasks 1-3 landed and verify-all.sh exits 0. Ready to tag
> `v0.21-phase-m6a-cleanup` on WildPalms HEAD
> (`<short-sha-of-task-3-commit>`). Want me to run the tag
> command, or will you?

Wait for user response. If user runs it themselves, skip to Task 6.
If user authorizes this session, run:

```bash
cd WildPalms && git tag -a v0.21-phase-m6a-cleanup -m "M6a — orphan cleanup

Deleted plucker plugin source tree + tests (was build-excluded
since M5c) and the orphaned HotSyncCoordinator class (introduced
G.7, never instantiated, made obsolete by Palm runtime rewrite).

No behavior change. verify-all.sh clean. Test posture:
- libkalburator: 54/54
- PlanStan: 90/114 (pre-existing env failures)
- WildPalms: 75/75"
```

---

### Task 6: Update CURRENT-STATUS.md

**Files:**
- Modify: `refactor-engine-merger/CURRENT-STATUS.md`

- [ ] **Step 1: Read the current file**

Read `refactor-engine-merger/CURRENT-STATUS.md` (whole file). It is
≤ 150 lines.

- [ ] **Step 2: Bump the date line**

Change the first non-heading line:

```
**Last updated:** 2026-05-02 (M5c complete — _v2 tests rewritten + MVP-guard option deleted; 75/75 tests green)
```

to (substitute today's actual date if not 2026-05-02):

```
**Last updated:** 2026-05-02 (M6a complete — plucker tree + HotSyncCoordinator deleted; verify-all clean)
```

- [ ] **Step 3: Add a new "Where we are" entry between M5c and "Next"**

Find the M5c paragraph (begins with `✅ **Palm runtime rewrite —
Plan 3c / M5c**`). Immediately after that paragraph closes (after
the `WildPalms: 70→75 tests.` line), insert a blank line and then:

```markdown
✅ **Palm runtime rewrite — Plan 4 / M6a** — orphan cleanup.
   Deleted `src/plugins/plucker/` (V1 plugin, double-free in
   static destructors per FINDINGS, build-excluded since M5c) and
   `tests/plugins/plucker/`. Deleted `src/runtime/hotsynccoordinator.{h,cpp}`
   (G.7 artifact, never instantiated anywhere — `PalmRuntime::hotSync`
   replaced its role). No behavior change; verify-all.sh exit 0.
   Tag `v0.21-phase-m6a-cleanup`. WildPalms: 75/75 (unchanged).
```

- [ ] **Step 4: Update the "Next" section**

Find the existing "Next" block:

```markdown
## Next

⬜ **Plan 5 / M6** — delete plucker plugin source tree; merge
   `palm-rewrite` to `refactor/engine-merger`.
```

Replace with:

```markdown
## Next

⬜ **Plan 5 / M6b** — move device-connection lifecycle into
   `PalmRuntime`; delete `DeviceSession` + `DeviceWorker` +
   `TickleWorker`; rewire `KF6MainWindow` to drive PalmRuntime
   directly. Needs a brainstorm before plan-writing — threading
   model and PalmRuntime signal surface are open design questions.
   Real-device verification gate.

⬜ **M7** — merge `palm-rewrite` (WildPalms branch) to
   `refactor/engine-merger`; cross-repo verify-all clean; tag
   `v0.23-palm-rewrite` on WildPalms HEAD.
```

- [ ] **Step 5: Update "Recently committed (WildPalms)"**

Prepend three lines to the top of the WildPalms commit list (most
recent first), substituting the actual short SHAs of the M6a
commits:

```
<sha-task-3>  M6a Task 3: delete orphaned HotSyncCoordinator
<sha-task-2>  M6a Task 2: delete plucker plugin tests
<sha-task-1>  M6a Task 1: delete plucker plugin source tree
```

To get the SHAs:
```bash
cd WildPalms && git log --oneline -3 --format="%h  %s"
```

- [ ] **Step 6: Update test posture if changed**

The `## Test posture (...)` section header should be bumped:

```markdown
## Test posture (2026-05-02, post-M6a)
```

The body lines (54/54, 90/114, 75/75) should be **unchanged** — if
they aren't, M6a's "no behavior change" claim is wrong. Investigate
before saving.

- [ ] **Step 7: Sanity-check the file is still ≤ 150 lines**

Run: `wc -l refactor-engine-merger/CURRENT-STATUS.md`

Per CLAUDE.md: "Keep the file ≤ 150 lines so it's quick to skim."
If the file grew past 150, prune older entries from "Recently
committed" sections (oldest first) until under the cap. Do not
prune the M5c or M6a entries; they're still load-bearing for the
next agent.

- [ ] **Step 8: Commit (in the coordination folder, NOT a worktree)**

The coordination folder is not a git repo (per CLAUDE.md), so
there is nothing to commit. The `CURRENT-STATUS.md` update lives
on the working tree of `~/dev/refactor-engine-merger/` itself.

Verify:
```bash
cd /home/clinton/dev/refactor-engine-merger && git rev-parse --is-inside-work-tree 2>&1
```
Expected: `fatal: not a git repository...` — confirming no commit
is needed.

If somehow this *is* a repo (CLAUDE.md was wrong or someone
init'd it), **stop and ask the user** before committing — the
project's policy is that this folder is coordination-only.

---

### Task 7: Append FINDINGS only if something non-obvious surfaced

**Files:**
- Modify (conditional): `refactor-engine-merger/FINDINGS.md`

- [ ] **Step 1: Decide whether to append**

The plan anticipated:
- plucker double-free is already documented in FINDINGS (per
  M5c plan / src/plugins/CMakeLists.txt comment) — no new entry.
- HotSyncCoordinator orphaned status is captured in the M6a
  commit message + this plan — no new entry needed.

**Append to FINDINGS only if** during execution you discovered
something the next agent would need to know that isn't already
in:
- A commit message
- This plan
- Existing FINDINGS entries
- The status doc

Examples that *would* warrant an entry:
- A second hidden caller of `HotSyncCoordinator` was found and
  removed (means the "orphaned" claim was nuanced).
- The CMake re-configure exposed a build ordering issue that
  needed a targeted fix.
- A previously-passing test went red on a plain-vanilla
  re-configure (suggests a flaky baseline).

If none apply, **skip this task**. Don't write empty entries.

- [ ] **Step 2 (if appending): Add the entry**

Open `refactor-engine-merger/FINDINGS.md` and append a new entry
at the bottom of the file using the file's existing format (read
the most recent existing entry first to match style).

- [ ] **Step 3 (if appending): No commit needed**

The coordination folder is not a git repo (see Task 6 Step 8).

---

## Self-review checklist

Before declaring M6a complete, the executor should mentally run
these checks:

1. **Spec coverage:** Plan covered (a) plucker source tree, (b)
   plucker tests, (c) HotSyncCoordinator class, (d) verify-all
   gate, (e) tag, (f) status doc. ✓

2. **Out-of-scope discipline:** No task touched DeviceSession,
   DeviceWorker, TickleWorker, AutoSyncOrchestrator, KF6MainWindow,
   or anything in libkalburator/PlanStan. If an executor was tempted
   to "just clean up DeviceSession::requestSync while we're here,"
   they should resist — that belongs in M6b.

3. **Behavior preservation:** Test counts unchanged (54/54, 90/114,
   75/75). If any number flipped, M6a's premise is wrong and the
   commit should be reverted, not patched over.

4. **Reversibility:** Every commit is independently revertable.
   `git revert v0.21-phase-m6a-cleanup~..v0.21-phase-m6a-cleanup`
   would restore the deleted files cleanly.

---

## Estimated effort

~30 minutes for a focused executor. Three commits + one tag + one
status-doc edit. No design decisions, no real-device testing, no
cross-repo coordination.

If execution takes more than 90 minutes, something surprising has
happened — pause and re-read the audit findings in the parent
session before continuing.
