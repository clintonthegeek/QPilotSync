# Sub-Project C — Default Mapping Policy = LastWriteWins — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change `PalmRuntime::finishConnect`'s auto-created `SyncMapping`s to default to `ConflictResolution::LastWriteWins` instead of inheriting `AskUser`. Eliminates the silent-defer behavior for the 99% case while preserving per-mapping override via `MappingEditorDialog`.

**Architecture:** One-line config change in `PalmRuntime::finishConnect`. Auto-mappings created on first connect get `LastWriteWins`; user-customized mappings (loaded via `setSyncMappings` from a profile's `mappings.conf`) keep whatever the user set. A new test asserts the default; existing tests cover the override path.

**Tech Stack:** Qt6, C++17.

**Spec:** `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md` §4.3

**Build / test commands:**
- `cmake --build build-dev -j$(nproc)`
- `ctest --test-dir build-dev --output-on-failure -j$(nproc)`

**File inventory:**

Modified:
- `src/runtime/palmruntime.cpp` — one-line change at the default-mapping construction site (~line 342).
- `tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp` — extend with a conflict-policy assertion.

**Dependency:** Plan A (hash stability). Without stable hashes, LWW would treat every record as "both modified" each sync; the policy change is meaningless without A. Plan B is independent.

**Cross-repo:** none. No libkalburator changes.

---

## Task 1: Failing assertion for the new default

**Files:**
- Modify: `tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp`

- [ ] **Step 1: Read the existing test fixture**

```bash
sed -n '1,80p' /home/clinton/dev/WildPalms/tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp
```

Note the fixture pattern (how PalmRuntime is constructed, how mock device is wired, how mappings are inspected). Match it.

- [ ] **Step 2: Find the most relevant existing test**

```bash
grep -n "private slots:" /home/clinton/dev/WildPalms/tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp
grep -n "conflictPolicy\|SyncMapping" /home/clinton/dev/WildPalms/tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp
```

There may already be a test that walks the auto-created mappings. We add an assertion to it or write a sibling test.

- [ ] **Step 3: Add a new test slot**

In the `private slots:` block, add:

```cpp
    void defaultMappings_useLastWriteWinsPolicy();
```

- [ ] **Step 4: Add the test body**

Append before the `QTEST_*_MAIN` line. Adjust namespace prefixes + fixture wiring to match the existing tests:

```cpp
void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaultMappings_useLastWriteWinsPolicy()
{
    // Seed mock device with at least one record per Palm DB so
    // finishConnect creates default mappings.
    auto device = std::make_shared<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    WildPalms::PalmSync::PalmRecord pr;
    pr.recordId = 1;
    pr.category = 0;
    pr.data     = QByteArrayLiteral("seed");
    device->writeRecord(QStringLiteral("AddressDB"), pr);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());
    // The exact way to drive finishConnect varies — copy the pattern
    // from the existing test's first slot. Likely something like:
    //   runtime.connectDevice(makeFakeLink(device));
    // followed by a wait for readyForSync.

    // After finishConnect, inspect the auto-created mappings.
    const auto mappings = runtime.syncMappings();   // accessor on PalmRuntime
    QVERIFY(!mappings.isEmpty());
    for (const auto &m : mappings) {
        QCOMPARE(m.conflictPolicy,
                 Kalburator::Sync::ConflictResolution::LastWriteWins);
    }
}
```

If `runtime.syncMappings()` doesn't exist, the existing fixture must already have a way to inspect mappings — use that. Read the existing test to find the canonical access pattern (might be `runtime.engine().mappings()` or similar).

- [ ] **Step 5: Build, expect failure**

```bash
cmake --build build-dev --target tst_palm_runtime_default_mappings_only_when_empty
ctest --test-dir build-dev -R tst_palm_runtime_default_mappings_only_when_empty --output-on-failure 2>&1 | tail -15
```

Expected: `defaultMappings_useLastWriteWinsPolicy` FAILS — current default is `AskUser`, not `LastWriteWins`.

- [ ] **Step 6: Commit**

```bash
git add tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp
git commit -m "test: failing assertion for LastWriteWins default policy (red phase)"
```

---

## Task 2: One-line policy change

**Files:**
- Modify: `src/runtime/palmruntime.cpp`

- [ ] **Step 1: Locate the construction site**

```bash
grep -n "m.mode" /home/clinton/dev/WildPalms/src/runtime/palmruntime.cpp
```

Expected: a single hit around line 342 in `finishConnect`:

```cpp
m.mode           = Kalburator::Sync::SyncMode::TwoWay;
```

- [ ] **Step 2: Add the conflictPolicy line**

Replace the existing line (around line 342):

```cpp
            m.mode           = Kalburator::Sync::SyncMode::TwoWay;
            m.enabled        = true;
```

With:

```cpp
            m.mode           = Kalburator::Sync::SyncMode::TwoWay;
            m.conflictPolicy = Kalburator::Sync::ConflictResolution::LastWriteWins;
            m.enabled        = true;
```

The order doesn't strictly matter; placing `conflictPolicy` between `mode` and `enabled` keeps grouped settings adjacent.

- [ ] **Step 3: Build + run the test**

```bash
cmake --build build-dev --target tst_palm_runtime_default_mappings_only_when_empty
ctest --test-dir build-dev -R tst_palm_runtime_default_mappings_only_when_empty --output-on-failure
```

Expected: all tests in that suite PASS.

- [ ] **Step 4: Run sibling palm-runtime tests for regressions**

```bash
ctest --test-dir build-dev -R "tst_palm_runtime" --output-on-failure 2>&1 | tail -15
```

Expected: all pass. The runtime mode/hotsync/cancel/reload-mappings tests should not be affected by the policy default since they either set explicit policies or test mode/cancel paths orthogonal to conflict resolution.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/palmruntime.cpp
git commit -m "runtime: default auto-mapping policy LastWriteWins (was AskUser)

PalmRuntime::finishConnect auto-created mappings inherited the
SyncMapping default of AskUser, which combined with the engine's
Unmonitored mode silently deferred every conflict (finalSource and
finalTarget empty, applyBatch skipped, sync reports completed
with no work done — see palm-sync-honesty-design.md §1 Bug 2).
LastWriteWins resolves automatically using the per-side
lastModified timestamps. Users who want AskUser can still set it
per-mapping via MappingEditorDialog."
```

---

## Task 3: Full ctest + smoke

**Files:** none modified

- [ ] **Step 1: Full ctest**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | tail -5
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: 80+ tests pass. Default-mappings test now passes; everything else unchanged.

- [ ] **Step 2: Quick manual cross-check (no commit)**

```bash
grep -n "conflictPolicy" /home/clinton/dev/WildPalms/src/runtime/palmruntime.cpp
```

Expected: exactly one hit, on the new line. Confirms no other code in PalmRuntime sets the policy elsewhere.

```bash
grep -rn "conflictPolicy" /home/clinton/dev/WildPalms/src/widgets/dialogs/mappingeditordialog.{h,cpp} 2>/dev/null
```

Expected: hits in MappingEditorDialog showing it reads/writes per-mapping conflictPolicy — confirms the user-override path remains functional. No code change needed; just confirmation.

---

## Verification checklist

- [ ] `PalmRuntime::finishConnect` sets `m.conflictPolicy = LastWriteWins` on auto-mappings.
- [ ] `tst_palm_runtime_default_mappings_only_when_empty::defaultMappings_useLastWriteWinsPolicy` passes.
- [ ] No other tests regress.
- [ ] MappingEditorDialog still exposes conflictPolicy per-mapping (user override path untouched).
- [ ] Spec §4.3 requirements satisfied.

**Out of scope for this plan:**
- Migrating existing profiles (their `mappings.conf` already encodes a policy; if it's AskUser, the user must explicitly opt out via MappingEditorDialog). Documented as expected behavior; no migration helper.
- Conflict surfacing for explicit-AskUser mappings — that's Plan D.

**Next sub-project:** Plan D — Conflict surfacing wired (`docs/superpowers/plans/2026-05-22-palm-sync-D-conflict-surfacing.md`).
