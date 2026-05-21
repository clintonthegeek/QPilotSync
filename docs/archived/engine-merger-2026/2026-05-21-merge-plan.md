# Merge Plan — `refactor/engine-merger` → `master`/`main`

**Status:** ✅ landed 2026-05-21 (Tasks 0–9 complete; FetchContent cutover deferred to a separate Phase 3b session per the plan's "out of scope" section)
**Author:** 2026-05-21, post-Phase-P
**Predecessors:** `2026-05-20-phase-p-merge-readiness-design.md`, tag `v0.52-phase-p-merge-ready`

**Execution post-mortem (2026-05-21):** Plan executed end-to-end in one session. Three surprises beyond the plan's scope:

1. **WildPalms had load-bearing uncommitted work pre-merge.** The 2026-05-16 flake fix (`WILDPALMS_QTEST_MAIN`/`WILDPALMS_QTEST_GUILESS_MAIN` macros + `tests/wildpalms_qtest_main.h`) was applied to the worktree but never committed. The 77/77 green test count depended on it. Committed as `806793d` before tagging WildPalms.
2. **WildPalms had 783 accidentally-tracked `build-dev/*` artifacts.** Untracked via `git rm --cached -r build-dev Testing` + matching `.gitignore` adds (mirror of main's commit). Committed as `afa2727`.
3. **WildPalms tracked `pilot-link` + `pilot-link-git` as self-referencing symlinks** (since `a62a81f`). Pre-merge, developers manually overlaid real local directories; the merge of refactor → main on pristine wrote the broken self-symlinks into the working tree, destroying the local source. Resolved by adding a system-package fallback to `lib/CMakeLists.txt` (commit `ed74132` on refactor, cherry-picked as `d099bcb` on main). Now: vendored if `pilot-link/configure.in` exists, else uses `/usr/lib/libpisock.so` + `/usr/include/pi-*.h`.

PlanStan merge conflicts resolved as the plan anticipated; the only surprise was a silent semantic break: master's `BlockActions` construction in `createPlanningSubsystem` crashes with SIGSEGV when CollectionController is constructed without a `QGuiApplication` (which refactor's `QTEST_GUILESS_MAIN` unit tests do). Fixed by wrapping the block in `if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()))`. Documented in FINDINGS.

Final test posture: libkalburator 103/103, WildPalms 77/77, PlanStan 122/146 (+17 from master's tests, -2 deleted gantt helpers; 24 fail all pre-existing). `verify-all.sh` all-green.

This is the playbook for landing the engine-merger campaign back on
`master`/`main` in all three repos. **A fresh agent should be able to
execute this plan end-to-end** with the user available for the smoke
step and for resolving genuine architectural ambiguities.

Phase P closed the *technical* gaps. This plan handles the *merge*
gaps — specifically, the fact that PlanStan `master` was developed in
parallel for the duration of the campaign and now has 114 commits the
refactor branch hasn't seen.

## Divergence audit (the actual state)

Run from the coordination folder:

```bash
cd PlanStan      && git rev-list --left-right --count master...refactor/engine-merger
cd ../libkalburator && git rev-list --left-right --count main...refactor/engine-merger
cd ../WildPalms     && git rev-list --left-right --count main...refactor/engine-merger
```

Current state (2026-05-21):

| Repo | `master/main` ahead | `refactor` ahead | Difficulty |
|---|---|---|---|
| **libkalburator** | **0** | 423 | Pure fast-forward |
| **WildPalms** | **1** | 163 | Near-FF; 1 trivial doc commit |
| **PlanStan** | **114** | 139 | **Real merge** |

### libkalburator (trivial)

`main`'s last commit IS the merge base. Zero parallel work. Tag
`v0.52-phase-p-merge-ready` lives on `refactor/engine-merger`'s HEAD;
fast-forwarding `main` to that tag is the entire merge.

### WildPalms (near-trivial)

Only commit on `main` since the merge base: `1ed5d36 chore: add
session plan doc, update gitignore (build-dev/, Testing/, circular
symlink removed)` (2026-05-20). Risk: if `refactor/engine-merger`
also touched `.gitignore`, that's the only possible conflict. WildPalms
saw zero Phase P changes; the campaign work on WildPalms ended at
`v0.27-phase-ia-vcard4-canonical` (May 8), then idle code-wise with
plumbing follow-ups.

**WildPalms tag decision (recorded here):** Tag `v0.52-phase-p-merge-ready`
on WildPalms' `refactor/engine-merger` HEAD too, for cross-repo
symmetry. It's a marker meaning "this commit was the WildPalms head
when the campaign concluded," not a claim of Phase P content.

### PlanStan (the real work)

114 master commits + 139 refactor commits + 15 files in the
intersection. The PlanStan divergence is summarized below; full survey
is the source of truth (run the Explore agent against PlanStan master
again if facts have shifted).

#### Master's parallel themes (8 clusters, ~114 commits)

1. **Block Actions + Context Menu Foundation** (~15) — unified block
   actions, context menu builder, drag-drop data, BlockSelectionService.
   Routes `block.decomposeSeed` through CollectionController.
2. **DecomposeOrchestrator integration** (~8) — task-views owns
   orchestrator, wires scope/modality deps, lifecycle, real-scene tests.
3. **CurrentScopeService infrastructure** (~5) — new view-infra service
   layer; PSS publishes through scope service; declarative view
   modalities.
4. **Graph layout policy & refactor** (~14) — sticky positions, explicit
   relayout, `syncFromStore`/`runFullLayout` public, mirrorXPositions,
   neighbor-anchor placement.
5. **Developer/testing tools** (~5) — developer menu (load/save
   fixture), BlockFixture + BlockJson, `BlockStoreSql::clearAllBlocks`.
6. **Gantt legacy cleanup → BlockGantt promotion** (~5) — deleted
   legacy incidence-based Gantt + helpers; BlockGantt is sole impl.
7. **Calendar/agenda views updates** (~6) — agenda calendar refactor,
   day/week panel updates, `IDecomposeViewModalities` interface added.
8. **Docs + dogfood content** (~8) — Stage 5 dogfood, structural
   overview, sample fixtures.

#### Conflict-candidate files (intersection of touched files)

15 files in the intersection. Hot zones:

| File | Master's changes | Refactor's changes | Difficulty |
|---|---|---|---|
| `src/controllers/collectioncontroller.cpp/h` | DecomposeOrchestrator + CurrentScopeService wiring; BlockSelectionService routing; baseline-sync pipeline | Phase O/P rewrote: connectBackendSignals, recordChanged, mirrorProviderBackends, providerOwner | **High — line-by-line merge** |
| `src/app/mainwindow.cpp/h` | Developer menu, BlockActions globally installed, panel rename `unscheduled_tasks` → `block_planning` | NewCollectionDialog wiring, wizard chrome entry point | **Medium** — separate sections of the file |
| `libs/view-infrastructure/include/viewtype.h` + `viewtyperegistry.cpp` | `IDecomposeViewModalities` added; `semanticlevel`/`egostate` moved here from graph | Probably untouched on refactor (Phase O/P didn't touch this) | **Low** — refactor likely doesn't conflict; verify with `git log --oneline $(git merge-base master refactor/engine-merger)..refactor/engine-merger -- libs/view-infrastructure/` |
| `src/views/tooldockmanager.cpp`, `viewmanager.cpp` | (review) | (review) | **Low–Medium** |
| `src/dialogs/settingsdialog.cpp` | (review) | (review) | **Low** |
| `src/app/planstanui.rc.in` | menu/toolbar XML | menu/toolbar XML | **Low** if changes are in different sections |
| `src/CMakeLists.txt` | Deleted 6+ Gantt/widget classes (dockitemwidget, unscheduledfilterproxy, ganttviewpanel, etc.) | Added new e2e-wizard test dir, AppController services, etc. | **Medium** — additive on refactor side, deletive on master; refactor must adopt deletions |
| `tests/{core,docking,graph,planning}/CMakeLists.txt` | New test registrations | New test registrations | **Medium** — append-style conflicts, resolve by merging both lists |

**Hot files refactor touched that master DID NOT** (clean merge):
`appcontroller.{cpp,h}`, `synctopologyviewpanel.{cpp,h}`, all of
`src/sync/topology/*`. ~70% of Phase O/P's surface lands cleanly.

**Master DELETED, refactor MODIFIED — none** (per survey).
**Refactor DELETED, master MODIFIED — none** (per survey).
No deletion conflicts. Both branches added many new files (38 unique
on refactor, ~20 on master) — those land cleanly side-by-side.

#### Surprise structural changes on master to watch for

- `unscheduled_tasks` layout panel renamed to `block_planning`. Refactor
  may reference the old id.
- Legacy Gantt classes deleted (ganttviewpanel, ganttschedulinghelper,
  etc.). Refactor must not still `#include` them.
- `IDecomposeViewModalities` interface — refactor's `SyncTopologyViewPanel`
  doesn't implement it (it predates this interface). Decide post-merge
  whether the topology panel SHOULD implement it (probably not — it
  doesn't host decomposable blocks).
- `semanticlevel`/`egostate` moved from `libs/graph` to
  `libs/view-infrastructure`. Refactor's includes still point at
  `libs/graph` — find-and-replace the include paths.

## Goal

After this plan executes:

- `libkalburator/main` HEAD == `v0.52-phase-p-merge-ready`
- `WildPalms/main` HEAD == `v0.52-phase-p-merge-ready` (post-tag-on-merge)
- `PlanStan/master` HEAD contains all 139 refactor commits merged in,
  conflicts resolved, tests green
- `verify-all.sh` (pointed at the new master HEADs, not the worktrees)
  exits 0
- Manual smoke (per Phase P design §"Manual smoke") passes against the
  Radicale dev server
- `refactor/engine-merger` branch in each repo: KEPT (not deleted)
  until the post-merge soak period concludes

## Strategy: merge master INTO refactor first, then fast-forward master

The natural reflex is "merge refactor into master." That's WRONG for
this campaign. The right move is:

```
On refactor/engine-merger:  git merge master  → resolve conflicts here
                            verify-all green
                            (optional) tag v0.52.1-post-master-merge
On master:                  git merge --ff-only refactor/engine-merger
```

**Why this order:**

1. **Master is sacrosanct.** Once a bad commit lands on master,
   reverting on a shared branch is awful. Doing the conflict resolution
   on `refactor/engine-merger` means a wrecked attempt is just `git
   reset --hard pre-merge-master-into-refactor`.
2. **Tests run with the merge state BEFORE master is touched.** If a
   merge-conflict resolution introduces a regression, you find it on
   refactor branch and fix it before master sees anything.
3. **The final master update is a fast-forward** — no merge commit
   noise on master's first-parent history; the campaign tag is the
   anchor.
4. **Bisects on master remain useful.** A 139-commit merge commit on
   master would obscure the per-phase tags; the fast-forward approach
   preserves them as first-class history.

This applies to **PlanStan** (the only repo where there's anything to
merge in). libkalburator and WildPalms don't have a meaningful
`master/main` history to merge in — for those, the simple direction
works fine.

## Pre-merge gate (mandatory, before ANY merge starts)

1. **Manual smoke** — per `2026-05-20-phase-p-merge-readiness-design.md`
   §"Manual smoke checklist". User-driven, against `localhost:5232`.
   Recently verified working on real Nextcloud (12-calendar wizard
   run, 2026-05-21). If the user has done this in the past 48h, the
   gate is met; otherwise re-run.
2. **`verify-all.sh` exits 0** on the worktrees as they stand. Last
   recorded run: 2026-05-21, all three projects matching baseline.
3. **Working trees clean.** `git status -s` shows only the known
   untracked items (per CURRENT-STATUS).
4. **No tag drift.** Confirm `v0.52-phase-p-merge-ready` is on the
   expected commit in libkalburator + PlanStan. WildPalms doesn't have
   the tag yet — Task 0 below tags it.

## Steps

### Task 0 — Tag WildPalms `v0.52-phase-p-merge-ready`

For cross-repo symmetry. Documents "this was the WildPalms head at
campaign-end." Pre-authorized per the merge plan.

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git tag -a v0.52-phase-p-merge-ready -m "Campaign-end tag for refactor/engine-merger.

WildPalms saw no Phase P changes; this tag marks the WildPalms head as
of the campaign's conclusion for cross-repo symmetry. WildPalms's last
code work in the campaign was v0.27-phase-ia-vcard4-canonical (May 8);
follow-up plumbing commits since then are tracked in this tag."
git tag --list | sort -V | tail -3   # confirm
```

### Task 1 — libkalburator fast-forward to main

Zero conflicts; pure fast-forward.

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git checkout main      # switches THIS worktree; if refactor checkout
                       #   is needed in parallel, use a second worktree
git merge --ff-only refactor/engine-merger
git log --oneline -5   # confirm HEAD == v0.52-phase-p-merge-ready
git checkout refactor/engine-merger   # restore the worktree's branch
```

**If `--ff-only` fails:** investigate — main shouldn't have moved.
Don't add `--ff` without understanding why.

**Verify:**

```bash
cmake --build build -j 10
ctest --test-dir build --output-on-failure
```

Should pass 103/103.

### Task 2 — WildPalms near-fast-forward to main

1 commit to merge. If `.gitignore` doesn't conflict, this is also
effectively trivial.

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git checkout main
git merge --no-ff refactor/engine-merger   # creates a merge commit
                                             #   joining the 1-commit
                                             #   main history with the
                                             #   163-commit campaign
# Resolve .gitignore conflicts if any (likely just lines to keep both)
# If no conflicts, merge commit lands automatically.
git log --oneline -3
git checkout refactor/engine-merger
```

**Verify:**

```bash
cmake --build build -j 10
ctest --test-dir build --output-on-failure
```

Should pass 77/77.

### Task 3 — PlanStan: merge master INTO refactor

The real work. Done on the `refactor/engine-merger` branch so that
master stays untouched until tests pass.

```bash
cd ~/dev/refactor-engine-merger/PlanStan
# Confirm we're on refactor branch (we should be — this is the worktree's branch)
git status
git branch --show-current   # → refactor/engine-merger

# Pull master's commits into refactor — this is where conflicts happen
git merge --no-ff master   # 114 commits coming in; expect conflicts
```

**Conflict resolution playbook** (handle files in this order — the
order minimizes cascade-conflicts):

#### 3a. Build system + structural deletions first

```bash
# Master deleted Gantt/widget classes; refactor's CMakeLists.txt entries
# for them must be removed. Get master's CMakeLists.txt edits:
git checkout --theirs src/CMakeLists.txt              # accept master's
                                                       #   deletions
# Then re-apply any refactor-side additions (e2e-wizard, AppController,
# new source files). Either by editing manually or using `git diff
# refactor/engine-merger -- src/CMakeLists.txt` for reference.
```

After resolving, build a smoke `cmake -B build-dev` and confirm
nothing is missing. Iterate until configure passes.

#### 3b. libs/view-infrastructure (interface changes)

`viewtype.h` + `viewtyperegistry.cpp` likely accept master's version
wholesale (IDecomposeViewModalities + semanticlevel/egostate moves)
unless refactor added its own viewtypes for the topology panel.

```bash
git diff $(git merge-base master refactor/engine-merger)..refactor/engine-merger -- libs/view-infrastructure
# If output is empty: accept master's version: git checkout --theirs libs/view-infrastructure
# If non-empty: line-by-line merge.
```

If `semanticlevel`/`egostate` moved from `libs/graph` to
`libs/view-infrastructure`, grep refactor's diff for the old include
path and rewrite:

```bash
git grep -l 'include.*"semanticlevel.h"\|include.*"egostate.h"' src/ libs/
# For each match, change include from libs/graph path to libs/view-infrastructure path
```

#### 3c. CollectionController — the hardest file

Master adds:
- `CurrentScopeService` ownership
- `DecomposeOrchestrator` ownership
- `BlockSelectionService` routing
- Multi-backend baseline sync wiring (commit `fe8c4c2e`)
- Removal of `reconcileProjectLogicalCalendars` (commit `282fdd03`)

Refactor adds:
- `connectBackendSignals` helper (Phase P T3)
- `recordChanged` body (Phase P T4)
- `m_signalsConnectedBackends` member
- `mirrorProviderBackends` (Phase O.7.2)
- Many other Phase O changes

Both sides extensively rewired this file. **Manual line-by-line merge
is the only honest approach.** Strategy:

1. Open `git diff master refactor/engine-merger -- src/controllers/collectioncontroller.{cpp,h}` in a wide terminal.
2. Mark master's blocks "M" and refactor's blocks "R" in a scratch buffer.
3. Where master adds members/methods that refactor doesn't touch (CurrentScopeService, DecomposeOrchestrator, BlockSelectionService) — keep master's additions verbatim.
4. Where refactor adds members/methods that master doesn't touch (connectBackendSignals, recordChanged, mirrorProviderBackends helpers, m_signalsConnectedBackends) — keep refactor's additions verbatim.
5. Where both modified the same method (likely `setUpServices`, constructor, `loadCollectionFromFile`): hand-merge, preferring whichever side's logic is more recent OR composing both.
6. After merging, immediately build `cmake --build build-dev -j 10 --target PlanStanCore`. Don't move on until it compiles.
7. Run `ctest -R tst_collectioncontroller --output-on-failure`. Don't move on until they pass.

**Estimated time for collectioncontroller alone: 60–90 minutes.**

#### 3d. mainwindow.cpp

Master changes:
- Developer menu (load/save fixture) — additive
- BlockActions globally installed — additive
- Panel id rename `unscheduled_tasks` → `block_planning`

Refactor changes:
- `NewCollectionDialog` wiring (Phase O.5)
- Wizard chrome integration

The changes are in different sections of mainwindow.cpp; conflicts
should be additive (both add new menu items, both add new slots).
Adopt the panel rename: search refactor's diff for `unscheduled_tasks`
and replace with `block_planning`.

#### 3e. Remaining intersection files

`tooldockmanager.cpp`, `viewmanager.cpp`, `settingsdialog.cpp`,
`planstanui.rc.in` — work through one at a time, in any order.

#### 3f. Test CMakeLists.txt files

Both branches added test registrations. Conflicts will be
adjacent-line additions. Just keep both sets of `add_executable` /
`add_test` calls.

```bash
# After merge:
cmake -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON  # full reconfigure
cmake --build build-dev -j 10
ctest --test-dir build-dev --output-on-failure
```

Tests should pass — both sides' tests now exist together. Combined
count probably ~125 passing (105 from refactor + ~20 new from master)
out of ~150 total (24 pre-existing fails + a few master-side
env-gated).

### Task 4 — Refresh baseline post-merge-into-refactor

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
# If exit 3 (improvement, expected since master added passing tests):
cp baselines/planstan-worktree-ctest.txt baselines/planstan-worktree-ctest.txt.bak.pre-master-merge
cp baselines/planstan-worktree-ctest.txt.last baselines/planstan-worktree-ctest.txt
./scripts/verify-all.sh   # confirm exit 0
git add baselines/
git commit -m "merge: refresh PlanStan baseline post-master-merge-into-refactor"
```

### Task 5 — (Optional, recommended) Tag the merge state

```bash
cd ~/dev/refactor-engine-merger/PlanStan
git tag -a v0.52.1-master-merged -m "PlanStan refactor/engine-merger with master merged in.

Master's 114 parallel commits (DecomposeOrchestrator, CurrentScopeService,
BlockActions, BlockGantt promotion, etc.) merged into refactor branch.
verify-all green. Tests pass. Smoke checked.

The next step is the trivial fast-forward of master to this commit."
```

### Task 6 — Manual smoke #2

Re-run the smoke checklist with the merged state. CRITICAL: this is
where master's parallel work might subtly break Phase P's flows.
Particularly:

- Wizard create + events visible (Phase P signature)
- Decompose orchestration on a block (master signature)
- Both flows operating in the same session

If either is broken, that's a merge bug — fix on the refactor branch,
re-tag if needed. Don't proceed until smoke is fully green.

### Task 7 — Fast-forward master

```bash
cd ~/dev/refactor-engine-merger/PlanStan
git checkout master
git merge --ff-only refactor/engine-merger
git log --oneline -3  # confirm HEAD includes both campaign work + master's
                      #   parallel work, with the campaign tag in history
git checkout refactor/engine-merger  # restore worktree
```

### Task 8 — Cross-repo synchronization

Now all three repos' `master`/`main` HEADs hold the campaign work:

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh   # final cross-repo gate
```

If green: merge is **landed**. Update docs:

- `CURRENT-STATUS.md` — "Where we are" gets a "✅ Campaign merged to
  master 2026-05-XX" entry; "Next" becomes Phase 3b
- `ROADMAP.md` — add an end-of-campaign row noting the merge
- `2026-05-21-merge-plan.md` (this file) — status flipped to
  `✅ landed YYYY-MM-DD`

### Task 9 — Branch hygiene

KEEP `refactor/engine-merger` for at least 30 days post-merge. If a
regression surfaces, having the unsquashed branch makes bisection
infinitely easier.

After 30+ days of clean master operation:

```bash
# In each of the three repos:
git push origin --delete refactor/engine-merger   # delete remote
git branch -D refactor/engine-merger              # delete local
```

But this is a future-user decision, not a fresh-agent decision.

## What this plan does NOT do (out of scope)

- **Phase 3b — FetchContent cutover.** Both PlanStan and WildPalms
  currently consume libkalburator via `add_subdirectory(../libkalburator
  ...)` (the sibling-worktree layout). Phase 3b replaces this with
  `FetchContent_Declare(libkalburator URL ...)` pinned to a libkalburator
  release tag. Mechanical steps sketched below.
- **Pushing tags to remote.** The campaign tags exist locally on
  `refactor/engine-merger`. Pushing to origin is a user-driven
  decision (typically `git push origin --tags v0.X-...` per tag for
  visibility, NOT `--tags` for everything).

## Phase 3b sketch (for the doc trail; not executed here)

Goal: PlanStan and WildPalms consume libkalburator as a first-class
FetchContent dependency at a pinned release tag, instead of needing a
sibling `libkalburator/` directory on disk.

Mechanical steps per consumer:

```cmake
# Remove from CMakeLists.txt:
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libkalburator
                 libkalburator EXCLUDE_FROM_ALL)

# Add:
include(FetchContent)
FetchContent_Declare(
    libkalburator
    GIT_REPOSITORY https://codeberg.org/clintonthegeek/libkalburator.git
    GIT_TAG        v0.52-phase-p-merge-ready   # or whatever release tag
)
FetchContent_MakeAvailable(libkalburator)
```

Prerequisites:
1. libkalburator must be pushed to its codeberg/github remote (per
   `~/dev/CLAUDE.md` "The One Rule")
2. The release tag must exist on the remote (not just local)
3. libkalburator's exported targets must be the same name PlanStan
   uses (`Kalburator::Sync`, etc. — verify with the existing
   `add_subdirectory` build)

After Phase 3b:
- `~/dev/refactor-engine-merger/` folder can be removed (it was a
  campaign-only construct)
- The pristine `~/dev/PlanStan/`, `~/dev/libkalburator/`,
  `~/dev/WildPalms/` checkouts return to being the canonical ones
- libkalburator can be developed independently, tagged independently,
  consumed at pinned versions

This is its own work session — probably 2–4 hours including the
remote push + first FetchContent build + the inevitable
target-name-mismatch debugging.

## What a fresh agent does FIRST

If a fresh agent arrives at this folder with the prompt "start the
merge" or "continue the merge":

1. Read `CURRENT-STATUS.md` — confirm Phase P landed
2. Read this file
3. Re-run the divergence audit at the top of this doc — facts may
   have shifted
4. Confirm `verify-all.sh` exits 0 in the current state
5. If pre-merge gate is met (smoke + green), start at Task 0
6. If pre-merge gate is NOT met, surface the gap to the user before
   proceeding

## Self-review (this plan)

- ✅ Divergence audit recorded with concrete numbers
- ✅ Merge order specified with rationale
- ✅ Strategy choice (merge-master-into-refactor-first) justified
- ✅ Per-file conflict playbook for PlanStan's hot files
- ✅ Pre-merge gate enumerated
- ✅ WildPalms tag decision recorded (tag for symmetry)
- ✅ Phase 3b sketched (not executed)
- ✅ Branch hygiene addressed (keep 30 days)
- ✅ Surfaces ambiguity (e.g. "IDecomposeViewModalities — refactor's
  topology panel probably shouldn't implement it") rather than
  hand-waving
- ✅ Estimated effort (PlanStan merge: 2–3 hours focused work)

**What this plan trusts the human for:**
- Manual smoke pass/fail judgment
- Resolving genuine architectural ambiguities surfaced during the
  CollectionController merge (e.g. "does CurrentScopeService own
  ProviderManager now, or vice versa?")
- The Phase 3b execution decision

**What this plan does NOT trust the human to remember:**
- The branch order
- The merge-master-into-refactor strategy
- That `--ff-only` is the right flag for libkalburator
- The dev-artifact exclusions (`/home/clinton/fsdfasg/` style)
- The need to tag WildPalms for symmetry
- The Phase 3b prerequisite list

All of those are above.
