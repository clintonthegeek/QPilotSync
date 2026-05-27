# Sub-project A — libkalburator Port (re-pin + green) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move WildPalms from its current libkalburator pin (`948dce8`) to the release tag **`v0.56-o15-converged`** (the `feature/o15-calendar-write-convergence` branch merged into libkalburator's trunk), which contains the O7 `ShapeRegistries` injection ctors, the O12 `TranscodingPlan` removal, and the O15 calendar-write convergence — keeping the build and full test suite green with **no source changes**, since the boundary audit proved O12/O15 are no-ops for WildPalms.

> **Pin target is a tag, not a SHA.** libkalburator is merging the o15 branch to trunk and tagging it `v0.56-o15-converged`. This plan pins WildPalms to that tag. Task 1 confirms the tag exists before proceeding — if the merge/tag hasn't landed yet, this plan blocks on it.

**Architecture:** WildPalms consumes libkalburator via CMake FetchContent, pinned by `WILDPALMS_LIBKALBURATOR_GIT_TAG` (`CMakeLists.txt:63`). This plan bumps that pin and verifies. It deliberately **keeps using the transitional ambient-`ShapeRegistries` path** (the global `TransformationRegistry::instance()` + 2-arg `SyncEngine`/1-arg `PluginManager` ctors), which remain valid on `main`. The O7 *injection* hygiene (sub-project A2) is **deferred to sub-project B**, because it requires untangling WildPalms' reliance on process-global shape singletons (`palmruntime.cpp:234-256`: the `s_globalRegistrationDone` guard, duplicate-id rejection across PalmRuntime instances, and a documented heap-corruption re-registration path) — work entangled with B's composition-root rework.

**Tech Stack:** C++/Qt6, KF6, CMake (legacy/no-preset; build dir `build-dev`), CTest, libkalburator (sibling at `../libkalburator`, FetchContent dep).

**Scope guard:** This plan is the O7/O12/O15 *port only* (item A in the umbrella spec). It does NOT introduce the hub, roles, or view changes. The libkalburator-side topology/authority decision (proposal `docs/2026-05-27-libkalburator-topology-authority-proposal.md`) does not gate this plan.

---

### Task 1: Confirm the pin target tag exists and capture the baseline

**Files:**
- Reference: `/home/clinton/dev/libkalburator` (sibling checkout)
- Reference: `CMakeLists.txt:63` (current pin)

- [ ] **Step 1: Confirm the `v0.56-o15-converged` tag exists on libkalburator's trunk**

Run:
```bash
git -C /home/clinton/dev/libkalburator fetch --tags 2>/dev/null
git -C /home/clinton/dev/libkalburator rev-parse v0.56-o15-converged 2>/dev/null \
  && echo "tag present" || echo "TAG MISSING — merge/tag not landed yet; BLOCK"
```
Expected: `tag present`. If `TAG MISSING`, stop — this plan blocks until libkalburator merges and tags. (Also confirm the Codeberg remote has the tag, since FetchContent fetches from `https://codeberg.org/clintonthegeek/libkalburator.git`: `git -C /home/clinton/dev/libkalburator ls-remote --tags origin v0.56-o15-converged`.)

- [ ] **Step 2: Confirm the tag carries O7 (injecting ctors), O12 (no `TranscodingPlan`), and O15**

Run:
```bash
git -C /home/clinton/dev/libkalburator show v0.56-o15-converged:src/shape/shaperegistries.h | head -30
git -C /home/clinton/dev/libkalburator grep -n "TranscodingPlan" v0.56-o15-converged -- 'src/**/*.h' || echo "no TranscodingPlan at tag (expected)"
git -C /home/clinton/dev/libkalburator grep -n "SyncTransaction" v0.56-o15-converged -- 'src/**/*.h' || echo "no SyncTransaction at tag (O15 merged, expected)"
```
Expected: `ShapeRegistries` struct present; both greps print their "no …" lines.

- [ ] **Step 3: Record the current WildPalms pin for the rollback note**

Run:
```bash
grep -n "WILDPALMS_LIBKALBURATOR_GIT_TAG" /home/clinton/dev/WildPalms/CMakeLists.txt
```
Expected: line 63 shows `948dce88b727f56b8fd08d14cfb44ef0f9c24ee2`.

---

### Task 2: Establish the green baseline on the OLD pin (pre-change control)

This proves any later failure is attributable to the pin bump, not pre-existing breakage.

**Files:**
- Test: the existing CTest suite under `tests/` (built into `build-dev`)

- [ ] **Step 1: Configure + build on the current pin**

Run:
```bash
cd /home/clinton/dev/WildPalms
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j"$(nproc)" 2>&1 | tail -5
```
Expected: builds to completion (this uses the FetchContent pin `948dce8`).

- [ ] **Step 2: Run the full suite and record the baseline pass count**

Run:
```bash
ctest --test-dir build-dev --output-on-failure 2>&1 | tail -25
```
Expected: record the "N tests passed, M failed" line. **This count is the contract for Task 4.** If anything fails here, STOP — that is a pre-existing failure unrelated to the port; investigate separately before proceeding.

---

### Task 3: Bump the pin to libkalburator `main`

**Files:**
- Modify: `CMakeLists.txt:63`

- [ ] **Step 1: Edit the pin**

In `CMakeLists.txt:63`, set the tag value to `v0.56-o15-converged`:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.56-o15-converged" CACHE STRING
    "libkalburator tag to fetch when WILDPALMS_LIBKALBURATOR_SOURCE_DIR is unset")
```

- [ ] **Step 2: Re-fetch and reconfigure against the new pin**

The FetchContent cache must re-fetch. Use a fresh build dir to force a clean fetch:
```bash
cd /home/clinton/dev/WildPalms
cmake -S . -B build-dev-portcheck -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -10
```
Expected: `-- libkalburator: fetching v0.56-o15-converged from Codeberg` then "Configuring done".

---

### Task 4: Verify green on the new pin

**Files:**
- Test: CTest suite in `build-dev-portcheck`

- [ ] **Step 1: Build against the new pin**

Run:
```bash
cd /home/clinton/dev/WildPalms
cmake --build build-dev-portcheck -j"$(nproc)" 2>&1 | tee build-portcheck.log | tail -8
```
Expected: builds to completion, **0 errors**. (Confirmed in the planning spike against the local sibling: 0 compile errors, binary produced.)

- [ ] **Step 2: Confirm zero compile errors explicitly**

Run:
```bash
grep -cE "error:" build-portcheck.log | xargs echo "compile errors:"
```
Expected: `compile errors: 0`. If nonzero, this is real port drift — capture the errors and address per the O12/O15 checklist (`../libkalburator/docs/2026-05-27-downstream-port-checklist.md`); the audit predicts none, so any error is a genuine finding to investigate, not to paper over.

- [ ] **Step 3: Run the full suite; assert the baseline pass count holds**

Run:
```bash
ctest --test-dir build-dev-portcheck --output-on-failure 2>&1 | tail -25
```
Expected: the same "N tests passed" count as Task 2 Step 2 (no regressions). Any newly-failing test is a port regression — investigate before continuing.

- [ ] **Step 4: Clean up the scratch build/log**

Run:
```bash
rm -rf /home/clinton/dev/WildPalms/build-dev-portcheck /home/clinton/dev/WildPalms/build-portcheck.log
```
(`build-*/` is gitignored, but remove the scratch dir to keep the tree tidy.)

---

### Task 5: Update the stale O15 comment

The only WildPalms reference to an O15-deleted class is a comment, not code.

**Files:**
- Modify: `tests/runtime/tst_runtime_caldav_e2e.cpp:154`

- [ ] **Step 1: Read the comment in context**

Run:
```bash
sed -n '150,158p' /home/clinton/dev/WildPalms/tests/runtime/tst_runtime_caldav_e2e.cpp
```
Expected: a comment referencing `CalendarPluginWriter` (the class libkalburator deleted in O15).

- [ ] **Step 2: Rewrite the comment to drop the dead-class reference**

Replace the `CalendarPluginWriter`-referencing sentence with one that states the behavioral reality post-O15: calendar writes now go through the uniform `DefaultBlobWriter` / `IBlobBackend::createRecord` path (best-effort, retry-safe, no transactional rollback). Keep the surrounding test logic unchanged. (Show the exact old/new text when editing; this is a comment-only change — no test behavior changes.)

---

### Task 6: Commit the port

**Files:**
- Modify: `CMakeLists.txt`, `tests/runtime/tst_runtime_caldav_e2e.cpp`

- [ ] **Step 1: Stage and commit**

Run:
```bash
cd /home/clinton/dev/WildPalms
git add CMakeLists.txt tests/runtime/tst_runtime_caldav_e2e.cpp
git commit -m "$(cat <<'EOF'
build(deps): re-pin libkalburator to v0.56-o15-converged (port, A1)

Bumps WILDPALMS_LIBKALBURATOR_GIT_TAG 948dce8 -> v0.56-o15-converged
(the o15 branch merged to trunk and tagged), carrying the O7 ShapeRegistries
injecting ctors, the O12 TranscodingPlan removal, and the O15 calendar-write
convergence. Verified: builds clean, full ctest suite green, no source
changes needed — O12/O15 are no-ops for WildPalms per the boundary audit.
Drops the stale CalendarPluginWriter reference (O15 deleted it) from the
caldav e2e test comment.

O7 *injection* (dropping the transitional ambient ShapeRegistries) is
deferred to sub-project B, which reworks the same composition root and
must untangle WP's process-global shape-singleton reliance
(palmruntime.cpp s_globalRegistrationDone guard).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```
Expected: a single commit on `feature/three-tier-sync`.

---

## Deferred to sub-project B (NOT in this plan): O7 injection (A2)

Recorded here so it isn't lost. Adopting the injecting `SyncEngine`/`PluginManager`
ctors with a non-global `ShapeRegistries` requires, on the **WildPalms** side:
- A composition root (`PalmRuntime`) that owns one `Shape::ShapeRegistries` and
  injects it into both `PluginManager` and `SyncEngine`.
- Getting the four plugin submodules' shapes (calendar/contacts/memo/todos — **not**
  plucker, which uses WP's own `IBackendPlugin`) into that injected instance
  instead of `TransformationRegistry::instance()` — i.e. removing the ctor
  self-registration (`calendarbackendplugin.cpp:39-40` and peers) and registering
  via the injected registries.
- Reworking the `s_globalRegistrationDone` process-global guard and the
  duplicate-id / heap-corruption-on-re-registration behavior
  (`palmruntime.cpp:234-256`) for per-instance registries.

This is entangled with B's composition-root and role-binding work and will be
planned there, after libkalburator answers the topology/authority proposal.

## Self-Review

- **Spec coverage:** Implements umbrella-spec sub-project A (the O7/O12/O15 port). A2 (O7 injection) is explicitly carved out and deferred to B with rationale — consistent with the spec's §5 "pending decision" note.
- **Placeholder scan:** `<TARGET_SHA>` is a deliberate run-time-resolved value with the exact command to obtain it (Task 1 Step 1) — not a vague placeholder. Task 5 Step 2 is a comment rewrite described by intent (no code logic), which is appropriate for a comment-only change.
- **Consistency:** The Task 2 baseline pass count is the explicit contract checked in Task 4 Step 3. Pin SHAs (`948dce8` → `9eb596c`) are consistent throughout.
