# Phase E.15b — fullsync → runtime Relocation Design

**Status:** Draft, 2026-04-26
**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.15b
**Predecessor:** Phase E.15a (Install action plugin landed 2026-04-26)
**Successor:** Phase E.16 (legacy `IConduit` family deletion)

## Goal

Mechanical relocation deferred from E.8: move `src/fullsync/`'s
five host-interface `.cpp/.h` pairs into `src/runtime/`, fold the
`WildPalmsFullSync` static library into `WildPalmsRuntime`, and
delete the now-empty `src/fullsync/` subtree. No behavioural change;
no new abstractions; no new tests. Existing 3 `test_fullsync_*`
tests keep passing.

The deferral note in `src/runtime/CMakeLists.txt:7-8` (and the
matching note in `src/CMakeLists.txt:255-258`) gets retired.

## Decisions

### Decision #1 — `git mv`, not copy-and-delete

Use `git mv` on each of the five file pairs. Preserves git history
through the rename. Files retain blame across the move. Same approach
as any clean-rename refactor.

### Decision #2 — `_wp` filename suffix stays

Files keep `_wp` suffix (`calendarcollection_wp.cpp`, etc.). The
suffix exists because the type names (`CalendarCollection`,
`ConflictResolver`, `SyncConfigStore`, `SyncHost`,
`ConflictPresenter`) collide with libkalburator's. Removing the
suffix would force renaming the types or relying on namespacing —
larger blast radius for no win.

### Decision #3 — Header guards rename to `WILDPALMS_RUNTIME_*`

The five headers currently use `WILDPALMS_FULLSYNC_*` guards; rename
to `WILDPALMS_RUNTIME_*`. Mechanical: each file gets exactly two
edits (`#ifndef` + `#define` lines). No callers reference the guard
strings.

### Decision #4 — `syncconfigstore_wp.cpp` QSettings group keys keep their `"fullsync/..."` literals

The file uses three QSettings keys:

```cpp
constexpr auto kLogicalCalendarsGroup = "fullsync/logicalCalendars";
constexpr auto kBackendConfigsGroup   = "fullsync/backendConfigs";
constexpr auto kSyncMappingsKey       = "fullsync/syncMappings";
```

These are persistent user-facing config keys. Renaming would migrate
runtime behavior (existing user configs disappear under new keys).
Out of scope for a mechanical relocation; keep verbatim. A future
migration can rename + provide a one-shot upgrade path.

### Decision #5 — `WildPalmsRuntime` gains public `Kalburator::Sync` link

`WildPalmsRuntime` currently has only a PRIVATE include path on
`Kalburator::Sync` (added in E.15a for `installsourcecollector.cpp`).
The fullsync code implements `ISyncHost` etc. with virtual methods,
so vtable emission needs an actual link. Promote to PUBLIC link.

This pulls `Kalburator::Sync` transitively into anything linking
`WildPalmsRuntime` — but every existing consumer (V2 plugin .so files,
`WildPalmsCore` via the INTERFACE link line) already links
`Kalburator::Sync` directly. No new transitive surface.

### Decision #6 — `add_fullsync_test()` helper renamed to `add_runtime_test()`

The test helper at `tests/CMakeLists.txt:132-146` is a four-line
function that links `Kalburator::Sync + WildPalmsFullSync`. Replace
with the same shape linking `WildPalmsRuntime`; rename function
accordingly. Each call site (3 lines below the function defn) gets
one find/replace. Keeps the helper's purpose obvious.

## Files touched

```
DELETED:
  src/fullsync/CMakeLists.txt
  src/fullsync/                    (entire directory after files move out)

MOVED (git mv) — src/fullsync/X → src/runtime/X:
  calendarcollection_wp.{h,cpp}
  conflictpresenter_wp.{h,cpp}
  conflictresolver_wp.{h,cpp}
  syncconfigstore_wp.{h,cpp}
  synchost_wp.{h,cpp}

MODIFIED:
  src/CMakeLists.txt                  drop add_subdirectory(fullsync) +
                                       remove deferral comments at lines 134, 257
  src/runtime/CMakeLists.txt          add 5 source pairs to SOURCES; promote
                                       Kalburator::Sync from PRIVATE include
                                       to PUBLIC link; drop deferral comment at line 7
  tests/CMakeLists.txt                rename add_fullsync_test → add_runtime_test;
                                       link WildPalmsRuntime; update 3 call sites

EDIT (per file, in moved location):
  src/runtime/calendarcollection_wp.h    header guard rename
  src/runtime/conflictpresenter_wp.h     header guard rename
  src/runtime/conflictresolver_wp.h      header guard rename
  src/runtime/syncconfigstore_wp.h       header guard rename
  src/runtime/synchost_wp.h              header guard rename
```

## Acceptance criteria

- [ ] `cmake --preset dev` configures cleanly.
- [ ] `cmake --build build-dev` succeeds; `WildPalmsFullSync` target no
      longer exists; `WildPalmsRuntime` gains the moved sources.
- [ ] `ctest --test-dir build-dev` passes 76/76 (same as post-E.15a).
- [ ] `git log --follow src/runtime/synchost_wp.h` shows history
      preserved through the move.
- [ ] `grep -r "WildPalmsFullSync\|src/fullsync" src tests` returns no
      results.
- [ ] Memory index gains an E.15b entry; parent spec row flipped to ✅.

## Spec exit gate

Mechanical correctness — `ctest` passes, library graph is one node
smaller, no consumer code outside `tests/CMakeLists.txt` and
`src/CMakeLists.txt` had to change.

## Scope excluded

- **Renaming the `_wp` suffix** (Decision #2).
- **Migrating QSettings keys** away from `"fullsync/..."` (Decision #4).
- **Renaming the types** (`CalendarCollection`, `SyncHost`, etc.) —
  out of scope; large blast radius.
- **Any new tests.** The 3 existing `test_fullsync_*.cpp` cover the
  semantics; rename their files would just churn history.

---

**Author:** Claude (E.15b session, 2026-04-26)
