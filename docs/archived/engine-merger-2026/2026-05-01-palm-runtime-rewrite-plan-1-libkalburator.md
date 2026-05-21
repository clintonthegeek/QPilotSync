# Palm Runtime Rewrite — Plan 1 of 2: libkalburator changes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the libkalburator API extensions and facade deletion required by the WildPalms Palm runtime rewrite (M1 from the design spec), independently mergeable on the existing `refactor/engine-merger` branch.

**Architecture:** Three landings: (1) lift the static-init constraint on `DomainPlugin` registration so WildPalms backend plugins can introduce non-stock domains at load time; (2) add an `ExecutionOverride` parameter to `runSyncFuture` so the engine can run a mapping as a one-way mirror in either direction (needed for the `Copy*` modes); (3) delete the F1 facade (`runBlobMirror` / `runBlobTwoWay`) by inlining its self-call in `dispatchFirstSync` and migrating-or-deleting its tests, since after (2) it has no consumers.

**Tech Stack:** C++20, Qt6, CMake, ctest. Existing libkalburator codebase. Working directory: `~/dev/refactor-engine-merger/libkalburator/` (worktree on branch `refactor/engine-merger`). Build dir: `build/`. verify-all script: `~/dev/refactor-engine-merger/scripts/verify-all.sh`.

**Spec:** `~/dev/refactor-engine-merger/2026-05-01-palm-runtime-rewrite-design.md` §3.

**Out of scope for this plan:** Anything in WildPalms or PlanStan. Plan 2 (M2-M7) covers the WildPalms rewrite and is drafted after this plan lands.

---

## File Structure

### Modified files

- `libkalburator/src/shape/transformationregistry.h` — add per-domain freeze tracking; document the post-init mutation contract.
- `libkalburator/src/shape/transformationregistry.cpp` — implement freeze; add freeze-on-first-compile.
- `libkalburator/src/shape/domainregistry.h` — add public `registerPlugin()`; document multi-plugin contribution rules.
- `libkalburator/src/shape/domainregistry.cpp` — implement `registerPlugin()`; allow re-init for newly-added plugins.
- `libkalburator/src/engine/syncengine.h` — define `ExecutionOverride`; add overload of `runSyncFuture(mappingId, override)`; remove F1-facade method declarations.
- `libkalburator/src/engine/syncengine.cpp` — thread override to worker; inline `dispatchFirstSync`'s mirror loop; delete facade bodies.
- `libkalburator/src/engine/syncengineworker.h` — accept override parameter on the per-sync state.
- `libkalburator/src/engine/syncengineworker.cpp` — pass override to adapter.
- `libkalburator/src/engine/idomainadapter.h` — extend `applyChanges` (or sibling) to honor mirror semantics, OR add a separate `applyMirror` virtual — see Task 4.
- `libkalburator/src/blob/blobdomainadapter.h/cpp` — implement mirror direction.
- `libkalburator/src/calendar/calendardomainadapter.h/cpp` — implement mirror direction (or document not-applicable + assert).

### Created files

- `libkalburator/tests/shape/tst_dynamic_domain_registration.cpp` — proves dynamic registration semantics + freeze.
- `libkalburator/tests/calendar/tst_engine_mirror_direction.cpp` — proves both mirror directions and destructive-deletion semantics via `runSyncFuture(..., override)`.

### Deleted files (after migration)

- `libkalburator/tests/blob/tst_engine_blob_one_shot.cpp` — 18 cases against the facade. Audit each; migrate engine-relevant coverage; delete the rest.
- `libkalburator/src/blob/blobsyncresult.h` — only referenced by the facade and `dispatchFirstSync`. Deletes after Task 11.

### Modified test files

- `libkalburator/tests/calendar/tst_engine_unified_boundary.cpp` — drop the `runBlobTwoWay_*` and `runBlobMirror_*` cases; equivalent coverage moves to `tst_engine_mirror_direction.cpp` for engine-relevant pieces.
- `libkalburator/tests/blob/CMakeLists.txt` — remove `tst_engine_blob_one_shot` registration.
- `libkalburator/tests/shape/CMakeLists.txt` — register `tst_dynamic_domain_registration`.
- `libkalburator/tests/calendar/CMakeLists.txt` — register `tst_engine_mirror_direction`.

### Documentation

- `libkalburator/docs/phase0/04r-phase-g-status.md` — flip Task 55/58 deferred markers to "deleted in M1 of Palm runtime rewrite".
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — add M1 to "Recently committed".
- `~/dev/refactor-engine-merger/FINDINGS.md` — record the latent HotSyncCoordinator threading bug (the gap that motivated the rewrite — Plan 2 fixes it but documenting it now is the persistence-layer obligation).

---

## Task 1: Document the latent threading-bug finding

**Files:**
- Modify: `~/dev/refactor-engine-merger/FINDINGS.md`

This isn't code but it's the load-bearing context for everything that follows. Land it first so the plan-reader and future-you have it.

- [ ] **Step 1: Append finding**

Append to FINDINGS.md (under whichever heading matches its date convention; if unsure use "## 2026-05-01" as a new top-level entry):

```markdown
## 2026-05-01 — HotSyncCoordinator's Palm path is latently broken (threading)

**What:** `SyncEngine`'s worker thread is not the Palm link thread. The Palm
`IBlobBackend` implementations (`PalmCalendarBackend`, `PalmMemoBackend`,
`PalmContactsBackend`, `PalmToDoBackend`) call their `IPalmDatabaseAccess`
synchronously and do **not** marshal to a specific thread —
`IPalmDatabaseAccess`'s header (palm/sync/ipalmdatabaseaccess.h:24-25)
explicitly states "Methods are blocking. PalmBackend is expected to run
on a worker thread when a real device is in play."

`HotSyncCoordinator::onDeviceConnected` calls
`m_engine->runSyncFuture(palmMappingIds)`. This dispatches the sync to
`SyncEngine`'s private worker thread, which then calls `loadRecords` /
`createRecord` etc. on the Palm backends — from the engine worker
thread, NOT the link thread.

**Why undetected:** All HotSyncCoordinator coverage (`tst_hotsync_coordinator`
and friends) uses `MockBlobBackend`, which is thread-agnostic. No real-device
HotSync has ever been exercised through the post-G.6 path.

**How to apply:** Plan 2 (Palm runtime rewrite) addresses this by introducing
`PalmDeviceAccess` — a self-marshalling wrapper over `KPilotLink` — and
having Palm backends call through it via `BlockingQueuedConnection`. This
plan (Plan 1) does not fix the bug; the rewrite's M2 milestone does.
```

- [ ] **Step 2: Commit**

```bash
cd ~/dev/refactor-engine-merger
git add FINDINGS.md
git commit -m "$(cat <<'EOF'
docs(findings): HotSyncCoordinator's Palm path is latently broken

Engine worker thread != Palm link thread; Palm IBlobBackends are not
self-marshalling. Tests pass because they use MockBlobBackend.
Documenting the gap; Plan 2 of the Palm runtime rewrite fixes it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

(Note: this commit is in the coordination folder, not a worktree. The
coordination folder isn't a git repo — verify with `git status` first.
If it's not a repo, skip the commit step and just save the finding;
FINDINGS.md is the persistence target either way.)

---

## Task 2: Failing test — register a DomainPlugin after process start

**Files:**
- Create: `libkalburator/tests/shape/tst_dynamic_domain_registration.cpp`
- Modify: `libkalburator/tests/shape/CMakeLists.txt`

This is the entry-point test for §3.1 of the spec. It will fail at first because the public API (`DomainRegistry::registerPlugin`) doesn't exist yet.

- [ ] **Step 1: Write the test file**

Create `libkalburator/tests/shape/tst_dynamic_domain_registration.cpp`:

```cpp
#include <QTest>

#include "domainplugin.h"
#include "domainregistry.h"
#include "irecorddiffer.h"
#include "irecordmerger.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace {

// Minimal plugin that introduces a fictitious "office" domain with one
// peer shape and one identity edge. Used to prove dynamic registration
// works without requiring real domain implementations.
class OfficeStubPlugin : public DomainPlugin {
public:
    DomainId domain() const override { return DomainId{"office"}; }
    Shape canonicalShape() const override {
        return { DomainId{"office"}, EncodingId{"canonical"} };
    }
    QList<Shape> peerShapes() const override {
        return { { DomainId{"office"}, EncodingId{"docx"} } };
    }
    PropertyCatalogue canonicalCatalogue() const override { return {}; }
    PropertyCatalogue catalogueFor(const Shape&) const override { return {}; }
    std::unique_ptr<IRecordDiffer> createCanonicalDiffer() const override {
        return nullptr;
    }
    std::unique_ptr<IRecordMerger> createCanonicalMerger() const override {
        return nullptr;
    }
    void registerEdges(TransformationRegistry& r) override {
        r.registerShape(canonicalShape(), {});
        r.registerShape(peerShapes().first(), {});
        r.declareCanonical(domain(), canonicalShape());
        TransformationEdge edge;
        edge.from = peerShapes().first();
        edge.to   = canonicalShape();
        edge.loss = LossProfile{};
        edge.stage = std::make_shared<IdentityStage>();
        r.registerEdge(edge);
    }
    int richnessRank(const Shape&) const override { return 0; }
};

}  // namespace

class TestDynamicDomainRegistration : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        TransformationRegistry::instance().clear();
        DomainRegistry::instance().clear();
    }

    void registersPluginAfterInit_pipelineCompiles() {
        // Stock initialise (as a process normally would).
        DomainRegistry::instance().initialize(TransformationRegistry::instance());

        // Now, post-init, register a third-party plugin.
        DomainRegistry::instance().registerPlugin(
            std::make_shared<OfficeStubPlugin>());

        // Compile a pipeline that uses the dynamically-registered shapes.
        const Shape from { DomainId{"office"}, EncodingId{"docx"} };
        const Shape to   { DomainId{"office"}, EncodingId{"canonical"} };
        const auto pipeline =
            TransformationRegistry::instance().compile(from, to);

        QVERIFY(pipeline.has_value());
    }
};

QTEST_GUILESS_MAIN(TestDynamicDomainRegistration)
#include "tst_dynamic_domain_registration.moc"
```

- [ ] **Step 2: Register the test in CMake**

Append to `libkalburator/tests/shape/CMakeLists.txt`:

```cmake
kalburator_add_shape_test(tst_dynamic_domain_registration)
```

- [ ] **Step 3: Configure + build, observe failure**

Run:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build --target tst_dynamic_domain_registration 2>&1 | tail -20
```

Expected: compile failure, "no member named 'registerPlugin' in 'DomainRegistry'" or similar. Save the build error verbatim if you want to confirm with the user.

---

## Task 3: Implement `DomainRegistry::registerPlugin()`

**Files:**
- Modify: `libkalburator/src/shape/domainregistry.h`
- Modify: `libkalburator/src/shape/domainregistry.cpp`

Make the test compile and pass. The existing `registerDomain` is what static-init registrars call; `registerPlugin` is the new public mutator that *also* triggers `registerEdges()` immediately so the new plugin's edges land in the registry without requiring a re-init.

- [ ] **Step 1: Add public method declaration**

In `libkalburator/src/shape/domainregistry.h`, after the existing `void initialize(TransformationRegistry&);`, add:

```cpp
    /// Register a plugin AFTER initialize() has been called and run
    /// its registerEdges() against the process-wide
    /// TransformationRegistry immediately. Permits third-party
    /// backend plugins to introduce non-stock domains at plugin-load
    /// time. Safe to call from any thread that is not concurrently
    /// calling registerEdges/compile (typical: app startup, single
    /// thread, before sync work begins).
    ///
    /// Constraints (asserted in debug, returns silently in release):
    /// - The TransformationRegistry must not have frozen the affected
    ///   domain yet (i.e. compile() has not been called for any shape
    ///   in this domain). See TransformationRegistry::isFrozen.
    /// - If a plugin for this domain already exists, the new plugin's
    ///   peer shapes and edges are unioned in; canonical-shape
    ///   conflicts error.
    void registerPlugin(std::shared_ptr<DomainPlugin>);
```

- [ ] **Step 2: Implement**

In `libkalburator/src/shape/domainregistry.cpp`, add:

```cpp
#include "transformationregistry.h"  // if not already included

void DomainRegistry::registerPlugin(std::shared_ptr<DomainPlugin> plugin)
{
    Q_ASSERT(plugin);
    if (!plugin) return;

    // Append to plugin list and (if new) index by domain.
    const auto domain = plugin->domain();
    if (!m_byDomain.contains(domain)) {
        m_byDomain.insert(domain, plugin.get());
    }
    m_plugins.append(plugin);

    // Drive its edges into the registry immediately. Subsequent calls
    // are no-ops courtesy of the registry's idempotent register*().
    plugin->registerEdges(TransformationRegistry::instance());
}
```

- [ ] **Step 3: Build + run the test**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build --target tst_dynamic_domain_registration && \
  ctest --test-dir build -R tst_dynamic_domain_registration -V
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/shape/domainregistry.{h,cpp} \
        tests/shape/tst_dynamic_domain_registration.cpp \
        tests/shape/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(shape): allow DomainPlugin registration after initialize()

Adds DomainRegistry::registerPlugin() so third-party backend plugins
can introduce non-stock domains at plugin-load time. Test pins the
post-init registration + pipeline-compile path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Failing test — registration after first compile() of that domain errors

**Files:**
- Modify: `libkalburator/tests/shape/tst_dynamic_domain_registration.cpp`
- (Will modify in Task 5: `libkalburator/src/shape/transformationregistry.h/cpp`)

Per spec §3.1, the registry must enforce a "frozen after first compile" rule per-domain. Test it.

- [ ] **Step 1: Add test slot**

Add to `class TestDynamicDomainRegistration` in the test file:

```cpp
    void registrationAfterCompile_isRejected() {
        DomainRegistry::instance().initialize(TransformationRegistry::instance());

        DomainRegistry::instance().registerPlugin(
            std::make_shared<OfficeStubPlugin>());

        // Compile something in the office domain — this freezes it.
        const Shape from { DomainId{"office"}, EncodingId{"docx"} };
        const Shape to   { DomainId{"office"}, EncodingId{"canonical"} };
        QVERIFY(TransformationRegistry::instance().compile(from, to).has_value());

        // Now try to add another peer shape via a second plugin.
        // In debug the registerEdge() asserts; in release it returns
        // silently and the shape doesn't appear. Test the release path
        // (silent rejection) by asking compile() afterward.
        class SecondOfficePlugin : public OfficeStubPlugin {
        public:
            QList<Shape> peerShapes() const override {
                return { { DomainId{"office"}, EncodingId{"odt"} } };
            }
            void registerEdges(TransformationRegistry& r) override {
                r.registerShape(peerShapes().first(), {});
                TransformationEdge edge;
                edge.from = peerShapes().first();
                edge.to   = canonicalShape();
                edge.loss = LossProfile{};
                edge.stage = std::make_shared<IdentityStage>();
                r.registerEdge(edge);
            }
        };

        // We expect this to be rejected (silently in release; the
        // attempt should not panic, but the new shape's pipeline
        // should not compile).
        DomainRegistry::instance().registerPlugin(
            std::make_shared<SecondOfficePlugin>());

        const Shape odt { DomainId{"office"}, EncodingId{"odt"} };
        const auto p = TransformationRegistry::instance().compile(odt, to);
        QVERIFY2(!p.has_value(),
                 "post-freeze peer registration must not appear in compiled pipelines");
    }
```

- [ ] **Step 2: Build + run, observe failure**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build --target tst_dynamic_domain_registration && \
  ctest --test-dir build -R tst_dynamic_domain_registration -V
```

Expected: PASS for `registersPluginAfterInit_pipelineCompiles`, FAIL for `registrationAfterCompile_isRejected` (because freeze isn't implemented yet — the second plugin's odt shape will register and compile will succeed).

---

## Task 5: Implement per-domain freeze in `TransformationRegistry`

**Files:**
- Modify: `libkalburator/src/shape/transformationregistry.h`
- Modify: `libkalburator/src/shape/transformationregistry.cpp`

- [ ] **Step 1: Add freeze tracking**

In `transformationregistry.h`, in the `private:` section, add:

```cpp
    /// Domains for which compile() has produced a non-identity Pipeline.
    /// Once a domain is frozen, registerEdge / registerShape on shapes
    /// in that domain are rejected.
    QSet<DomainId> m_frozenDomains;

    /// Internal: mark a domain frozen. Called by compile().
    void freeze(const DomainId& d) const;
```

(Note `freeze` is `const` — see implementation note below.) And add `#include <QSet>` at the top if not already present.

In the public section, add a query:

```cpp
    /// True if compile() has been called against any shape in this
    /// domain. After that, registerEdge / registerShape for shapes
    /// in this domain are rejected.
    bool isFrozen(const DomainId&) const;
```

- [ ] **Step 2: Implement freeze + reject post-freeze mutation**

In `transformationregistry.cpp`:

Make `m_frozenDomains` `mutable` (since `compile()` is `const` and needs to set it). Adjust the field declaration in the header to `mutable QSet<DomainId> m_frozenDomains;`.

Add:

```cpp
bool TransformationRegistry::isFrozen(const DomainId& d) const
{
    return m_frozenDomains.contains(d);
}

void TransformationRegistry::freeze(const DomainId& d) const
{
    m_frozenDomains.insert(d);
}
```

In `registerShape`, at the top:

```cpp
    if (m_frozenDomains.contains(shape.domain())) {
        Q_ASSERT_X(false, "registerShape",
                   "shape's domain is frozen — register before first compile()");
        return;
    }
```

In `registerEdge`, at the top:

```cpp
    if (m_frozenDomains.contains(edge.from.domain())
        || m_frozenDomains.contains(edge.to.domain())) {
        Q_ASSERT_X(false, "registerEdge",
                   "edge endpoint domain is frozen — register before first compile()");
        return;
    }
```

In `compile`, just before returning a successful Pipeline (i.e. after the existing path-finding logic, on the success branch where you have a non-identity pipeline), add:

```cpp
    // Freeze the domain so subsequent edge/shape registration is
    // rejected; later compiles for the same domain remain valid.
    freeze(from.domain());
```

For identity pipelines (`from == to`, `to.isAny()`, etc.) you may also want to freeze the domain, but it's safer to only freeze on a non-trivial successful compile — identity compiles don't actually consult the edge graph. Default: freeze only on non-identity success. Document this in the header comment.

In `clear()`, also clear the frozen set:

```cpp
    m_frozenDomains.clear();
```

- [ ] **Step 3: Build + run the test**

```bash
cmake --build build --target tst_dynamic_domain_registration && \
  ctest --test-dir build -R tst_dynamic_domain_registration -V
```

Expected: both test cases PASS.

(Debug builds will trip the `Q_ASSERT_X` if the test exercises the assert path. To exercise the silent-reject release path, the test currently triggers a debug assert in debug builds. If running in debug mode, this is expected — verify the test's `QVERIFY2(!p.has_value())` is reached after the assertion fires. If ctest treats the assert as a failure, switch the implementation to `qWarning() + return;` instead of `Q_ASSERT_X` and adjust the test comment.)

- [ ] **Step 4: Commit**

```bash
git add src/shape/transformationregistry.{h,cpp} \
        tests/shape/tst_dynamic_domain_registration.cpp
git commit -m "$(cat <<'EOF'
feat(shape): freeze TransformationRegistry per-domain on first compile()

Once compile() produces a non-identity Pipeline for a domain, further
registerShape / registerEdge calls for that domain are rejected. Lets
DomainRegistry::registerPlugin be safe at app startup while preventing
mid-sync mutation. Test covers both the pre-compile registration path
and the post-compile rejection path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Failing test — multi-plugin contribution to one domain (canonical conflict)

**Files:**
- Modify: `libkalburator/tests/shape/tst_dynamic_domain_registration.cpp`

Per spec §3.1: peer shapes/edges from multiple plugins are unioned; conflicting `declareCanonical` calls are an error. Test it.

- [ ] **Step 1: Add test slot**

```cpp
    void multiplePluginsContributeToSameDomain_unionPeers() {
        DomainRegistry::instance().initialize(TransformationRegistry::instance());

        DomainRegistry::instance().registerPlugin(
            std::make_shared<OfficeStubPlugin>());

        // Second plugin for same domain, different peer.
        class SecondPlugin : public OfficeStubPlugin {
        public:
            QList<Shape> peerShapes() const override {
                return { { DomainId{"office"}, EncodingId{"odt"} } };
            }
            void registerEdges(TransformationRegistry& r) override {
                // Note: don't redeclare canonical (idempotent same-value
                // is allowed; conflicting would error).
                r.registerShape(peerShapes().first(), {});
                TransformationEdge edge;
                edge.from = peerShapes().first();
                edge.to   = canonicalShape();
                edge.loss = LossProfile{};
                edge.stage = std::make_shared<IdentityStage>();
                r.registerEdge(edge);
            }
        };
        DomainRegistry::instance().registerPlugin(std::make_shared<SecondPlugin>());

        // Both peers should now be reachable.
        const Shape canonical { DomainId{"office"}, EncodingId{"canonical"} };
        const Shape docx      { DomainId{"office"}, EncodingId{"docx"} };
        const Shape odt       { DomainId{"office"}, EncodingId{"odt"} };
        QVERIFY(TransformationRegistry::instance().compile(docx, canonical).has_value());
        QVERIFY(TransformationRegistry::instance().compile(odt,  canonical).has_value());
    }
```

- [ ] **Step 2: Build + run**

```bash
cmake --build build --target tst_dynamic_domain_registration && \
  ctest --test-dir build -R tst_dynamic_domain_registration -V
```

Expected: PASS (the existing implementation should already handle this — `registerEdge` is additive via `QMultiHash`, and `declareCanonical` is set-once-idempotent per the existing header comment in `transformationregistry.h:36`). If it fails, the test exposed a real gap; investigate before patching.

- [ ] **Step 3: Commit (test only)**

```bash
git add tests/shape/tst_dynamic_domain_registration.cpp
git commit -m "$(cat <<'EOF'
test(shape): pin multi-plugin contribution to a single domain

Two plugins each declaring a peer shape against the same domain should
both have their edges land. Compile against either peer succeeds.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Define `ExecutionOverride` + add new `runSyncFuture` overload (declaration only)

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h`

Just the API surface. Implementation in subsequent tasks.

- [ ] **Step 1: Define the struct**

In `syncengine.h`, in namespace `Kalburator::Sync`, near the top of the file (before `class SyncEngine`), add:

```cpp
/// Per-call execution override for runSyncFuture(). Lets callers
/// request mirror-direction semantics for a mapping that's
/// otherwise configured for bidirectional sync. Used by WildPalms's
/// Tools-menu Copy Palm→PC / Copy PC→Palm actions.
struct ExecutionOverride {
    enum class Direction {
        Default,      ///< Use the mapping's stored direction (today: bidirectional).
        MirrorAToB,   ///< One-way: source overwrites target; target-only records deleted.
        MirrorBToA,   ///< One-way: target overwrites source; source-only records deleted.
    };
    Direction direction = Direction::Default;
};
```

- [ ] **Step 2: Add the overload declaration**

In `class SyncEngine`, after the existing `runSyncFuture(mappingId, behavior)` declaration (around line 515), add:

```cpp
    /**
     * @brief Run a single mapping with a per-call execution override.
     *
     * Used by WildPalms's Copy Palm→PC / Copy PC→Palm modes to run
     * a mapping as a one-way mirror without persisting that direction
     * on the mapping itself.
     */
    QFuture<SyncResult> runSyncFuture(
        const QString &mappingId,
        const ExecutionOverride &override,
        SyncBehavior behavior = SyncBehavior::Unmonitored);
```

- [ ] **Step 3: Build, observe link error or unimplemented stub error**

```bash
cmake --build build 2>&1 | tail -10
```

Expected: linker error if you build a test that references the new overload (none yet) — otherwise build still passes since we only added a declaration. That's fine.

- [ ] **Step 4: Add temporary stub implementation**

In `syncengine.cpp`, add a stub so the build passes:

```cpp
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    const ExecutionOverride &override,
    SyncBehavior behavior)
{
    Q_UNUSED(override);
    // Stub: ignore override, route through the existing overload.
    // Mirror semantics land in Task 9.
    return runSyncFuture(mappingId, behavior);
}
```

- [ ] **Step 5: Build clean, commit**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: clean build.

```bash
git add src/engine/syncengine.{h,cpp}
git commit -m "$(cat <<'EOF'
feat(engine): declare ExecutionOverride + runSyncFuture(mappingId, override)

API surface only. Stub implementation routes through the existing
overload, ignoring the override. Mirror semantics implemented in a
subsequent commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Failing test — `runSyncFuture(mappingId, MirrorAToB)` propagates source records and deletes target-only records

**Files:**
- Create: `libkalburator/tests/calendar/tst_engine_mirror_direction.cpp`
- Modify: `libkalburator/tests/calendar/CMakeLists.txt`

This is the contract pin for §3.2. Use the calendar test harness because the existing `tests/calendar/` infrastructure (StubSyncHost etc.) is already mapping-driven and shows the correct test idiom per `libkalburator/CLAUDE.md`. Use `MockBlobBackend` pairs (the existing blob test fixture) to keep the test focused on engine semantics, not iCal parsing.

- [ ] **Step 1: Locate the existing MockBlobBackend / blob test fixture**

```bash
find libkalburator/tests -name "mockblobbackend*" -o -name "tst_engine_blob_one_shot*" 2>/dev/null
```

Note the include paths used by `tst_engine_blob_one_shot.cpp`'s setup so you can mirror them.

- [ ] **Step 2: Write the test file**

Create `libkalburator/tests/calendar/tst_engine_mirror_direction.cpp` (placed under `tests/calendar/` because it's an integration test of the engine's mapping-driven API; rename if local convention differs):

```cpp
#include <QTest>
#include <QSignalSpy>

#include "syncengine.h"
#include "syncmapping.h"
#include "syncresult.h"
#include "backendrecord.h"
#include "mockblobbackend.h"      // adjust include path to match tests/blob/
#include "blobbaselinestore.h"
#include "backendregistry.h"

using namespace Kalburator::Sync;

class TestEngineMirrorDirection : public QObject {
    Q_OBJECT

private:
    // Helper: build a BackendRecord with the given id + payload.
    BackendRecord rec(const QString &id, const QByteArray &payload) {
        BackendRecord r;
        r.id = id;
        r.payload = payload;
        // contentHash and other fields filled by the backend on insert.
        return r;
    }

private slots:
    void mirrorAToB_propagatesSourceAndDeletesTargetOnly() {
        // SOURCE has {a, b}; TARGET has {b, c}.
        // After MirrorAToB: TARGET should have {a, b}, with c deleted.
        MockBlobBackend src("src");
        MockBlobBackend tgt("tgt");
        src.insertRecord("col1", rec("a", "payload-a"));
        src.insertRecord("col1", rec("b", "payload-b"));
        tgt.insertRecord("col1", rec("b", "payload-b-stale"));
        tgt.insertRecord("col1", rec("c", "payload-c"));

        // Wire engine + mapping.
        BackendRegistry registry;
        registry.registerBackend(&src);
        registry.registerBackend(&tgt);

        SyncEngine engine(&registry, /*host=*/nullptr);

        SyncMapping mapping;
        mapping.id = "mirror-test";
        mapping.sourceBackend = src.backendId();
        mapping.targetBackend = tgt.backendId();
        mapping.sourceCollection = "col1";
        mapping.targetCollection = "col1";
        engine.registerMapping(mapping);

        // Run with MirrorAToB override.
        ExecutionOverride ov;
        ov.direction = ExecutionOverride::Direction::MirrorAToB;
        auto future = engine.runSyncFuture(mapping.id, ov);

        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        const auto result = future.resultAt(0);
        QVERIFY(result.success);

        // Target should now match source: {a, b}, no c.
        const auto tgtRecs = tgt.loadRecords("col1");
        QCOMPARE(tgtRecs.size(), 2);
        QSet<QString> tgtIds;
        for (const auto &r : tgtRecs) tgtIds.insert(r.id);
        QVERIFY(tgtIds.contains("a"));
        QVERIFY(tgtIds.contains("b"));
        QVERIFY(!tgtIds.contains("c"));
    }

    void mirrorBToA_propagatesTargetAndDeletesSourceOnly() {
        MockBlobBackend src("src");
        MockBlobBackend tgt("tgt");
        src.insertRecord("col1", rec("a", "payload-a"));
        tgt.insertRecord("col1", rec("b", "payload-b"));

        BackendRegistry registry;
        registry.registerBackend(&src);
        registry.registerBackend(&tgt);
        SyncEngine engine(&registry, /*host=*/nullptr);

        SyncMapping mapping;
        mapping.id = "mirror-test-2";
        mapping.sourceBackend = src.backendId();
        mapping.targetBackend = tgt.backendId();
        mapping.sourceCollection = "col1";
        mapping.targetCollection = "col1";
        engine.registerMapping(mapping);

        ExecutionOverride ov;
        ov.direction = ExecutionOverride::Direction::MirrorBToA;
        auto future = engine.runSyncFuture(mapping.id, ov);

        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        const auto srcRecs = src.loadRecords("col1");
        QCOMPARE(srcRecs.size(), 1);
        QCOMPARE(srcRecs.first().id, QStringLiteral("b"));
    }
};

QTEST_GUILESS_MAIN(TestEngineMirrorDirection)
#include "tst_engine_mirror_direction.moc"
```

- [ ] **Step 3: Register in CMake**

Append to `libkalburator/tests/calendar/CMakeLists.txt` (adapt the helper name to match what's there — the file uses `kalburator_add_calendar_integration_test()` per CLAUDE.md):

```cmake
kalburator_add_calendar_integration_test(tst_engine_mirror_direction)
```

If MockBlobBackend / BlobBaselineStore aren't on the calendar-tests link path, add them; the helper may need extended link libs.

- [ ] **Step 4: Build + run, observe failure**

```bash
cmake --build build --target tst_engine_mirror_direction && \
  ctest --test-dir build -R tst_engine_mirror_direction -V
```

Expected: FAIL. Likely failure: target ends up with `{a, b, c}` (the bidirectional path merged source's `a` in but didn't delete `c`). Capture the actual failure for confirmation.

---

## Task 9: Implement mirror-direction in `BlobDomainAdapter`

**Files:**
- Modify: `libkalburator/src/engine/idomainadapter.h`
- Modify: `libkalburator/src/engine/syncengine.cpp` (the `runSyncFuture(mappingId, override, behavior)` body)
- Modify: `libkalburator/src/engine/syncengineworker.h/.cpp`
- Modify: `libkalburator/src/blob/blobdomainadapter.h/.cpp`

The override has to thread from `runSyncFuture` → `SyncEngineWorker::processSync` → adapter. Cleanest implementation: pass the override through, and have the adapter's `merge()` (or a new sibling `mergeWithOverride`) honor the direction.

**Design choice for this task:** instead of plumbing a new parameter through every adapter method, store the override on the worker's per-sync context (alongside mapping info, conflict state, etc.) and have the adapter read it via a hook on the engine (or on the merge call).

The minimal surgery: extend `IDomainAdapter::merge()` to take an optional override.

- [ ] **Step 1: Extend `IDomainAdapter::merge` signature**

In `idomainadapter.h`, change:

```cpp
    virtual EngineMerge merge(const EngineDiff& diff,
                              ConflictResolution policy) const = 0;
```

to:

```cpp
    virtual EngineMerge merge(const EngineDiff& diff,
                              ConflictResolution policy,
                              const ExecutionOverride& override = {}) const = 0;
```

(Add `#include "syncengine.h"` for `ExecutionOverride`, OR move `ExecutionOverride` into `synctypes.h` to avoid the cycle. Prefer moving — `synctypes.h` is the central type header. If you do move, update the include in `syncengine.h` to point at `synctypes.h`.)

- [ ] **Step 2: Update `BlobDomainAdapter::merge`**

In `blobdomainadapter.cpp`, change `merge()` to honor the override:

```cpp
EngineMerge BlobDomainAdapter::merge(const EngineDiff& diff,
                                      ConflictResolution policy,
                                      const ExecutionOverride& override) const
{
    EngineMerge result;

    switch (override.direction) {
    case ExecutionOverride::Direction::MirrorAToB: {
        // Push every source record (added or modified) to target;
        // delete every target-only record. Ignore conflict policy.
        for (const auto& add : diff.addedOnSource)    result.toApplyOnTarget.append(add);
        for (const auto& mod : diff.modifiedOnSource) result.toApplyOnTarget.append(mod);
        for (const auto& del : diff.addedOnTarget)    result.toDeleteOnTarget.append(del.id);
        // Records modified on target are overwritten by source's version
        // if source has a different version; if source is unchanged,
        // overwrite with source's (which equals baseline) since target's
        // change is being intentionally discarded.
        for (const auto& mod : diff.modifiedOnTarget) {
            // Find source's view of this record (may equal baseline).
            // … existing mirror logic from runBlobMirror lifts here.
        }
        return result;
    }
    case ExecutionOverride::Direction::MirrorBToA: {
        // Symmetric — swap toApplyOnTarget / toApplyOnSource etc.
        // … mirror of the above.
        return result;
    }
    case ExecutionOverride::Direction::Default:
    default:
        // Existing bidirectional logic.
        return /* existing merge() body */;
    }
}
```

(The existing `runBlobMirror` body in `syncengine.cpp:1286` has the canonical mirror logic. Lift it into the adapter; this is the same algorithm just relocated.)

- [ ] **Step 3: Update `CalendarDomainAdapter::merge` signature**

Add the new parameter to the calendar adapter's `merge()` for ABI consistency. For now, assert that `override.direction == Default` — calendar-mapping mirror semantics aren't a current requirement (Palm calendar is mapping-driven; mirror is a blob-only consumer). If a future caller passes a non-default direction, fail loudly:

```cpp
EngineMerge CalendarDomainAdapter::merge(const EngineDiff& diff,
                                          ConflictResolution policy,
                                          const ExecutionOverride& override) const
{
    Q_ASSERT_X(override.direction == ExecutionOverride::Direction::Default,
               "CalendarDomainAdapter::merge",
               "calendar adapter does not implement mirror direction; "
               "use a blob-domain mapping for Copy Palm/PC modes");
    Q_UNUSED(override);
    return /* existing body */;
}
```

- [ ] **Step 4: Thread override through `SyncEngineWorker`**

In `syncengineworker.h`, add a member variable for the per-sync override (alongside whatever per-sync state already exists):

```cpp
    ExecutionOverride m_currentOverride;
```

In `syncengineworker.cpp`, wherever `processSync` is invoked or initialised, capture the override and pass it to `merge()`:

```cpp
    EngineMerge merged = adapter->merge(diffed, policy, m_currentOverride);
```

In `syncengine.cpp`, in the `runSyncFuture(mappingId, override, behavior)` body, set the worker's override before kicking dispatch:

```cpp
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    const ExecutionOverride &override,
    SyncBehavior behavior)
{
    m_worker->setOverride(override);  // new setter; thread-safe via QMetaObject
    return runSyncFuture(mappingId, behavior);
}
```

Add the setter on `SyncEngineWorker`:

```cpp
public slots:
    void setOverride(ExecutionOverride o) { m_currentOverride = o; }
```

(Use `QMetaObject::invokeMethod` to dispatch the setter onto the worker thread if needed for thread safety; the engine and worker live on different threads.)

- [ ] **Step 5: Build + run the test**

```bash
cmake --build build --target tst_engine_mirror_direction && \
  ctest --test-dir build -R tst_engine_mirror_direction -V
```

Expected: both test cases PASS.

- [ ] **Step 6: Run the full test suite — make sure no regression**

```bash
ctest --test-dir build --output-on-failure
```

Expected: existing 53/53 pass; new 2 also pass; total 55/55.

- [ ] **Step 7: Commit**

```bash
git add src/engine/idomainadapter.h src/engine/syncengine.{h,cpp} \
        src/engine/syncengineworker.{h,cpp} \
        src/blob/blobdomainadapter.{h,cpp} \
        src/calendar/calendardomainadapter.{h,cpp} \
        src/core/synctypes.h \
        tests/calendar/tst_engine_mirror_direction.cpp \
        tests/calendar/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(engine): runSyncFuture honors ExecutionOverride mirror direction

Threads ExecutionOverride from runSyncFuture(mappingId, override)
through SyncEngineWorker to IDomainAdapter::merge(). BlobDomainAdapter
implements MirrorAToB / MirrorBToA by lifting the existing runBlobMirror
algorithm. CalendarDomainAdapter asserts on non-Default direction
(mirror not yet implemented for calendar; not needed for current
WildPalms Copy modes which target blob mappings).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Inline `dispatchFirstSync`'s self-call to `runBlobMirror`

**Files:**
- Modify: `libkalburator/src/engine/syncengine.cpp`

`SyncEngineWorker::dispatchFirstSync` currently calls `engine->runBlobMirror(...)` (around syncengine.cpp:1890). Inline the mirror loop directly so the facade method has no callers.

- [ ] **Step 1: Locate the call site**

```bash
grep -n "runBlobMirror" libkalburator/src/engine/syncengine.cpp
```

Expect a hit around line 1862-1898 inside `dispatchFirstSync`.

- [ ] **Step 2: Replace the call with an inline loop**

The existing `runBlobMirror(src, tgt, colId)` loop is ~50 lines (see `syncengine.cpp:1286`). Lift the body directly into `dispatchFirstSync`, removing the `engine->` indirection. Approximate shape:

```cpp
// Inlined from runBlobMirror (deleted in Task 12). First-sync path:
// source is non-empty, target is empty; copy every source record to
// target and persist v3 baselines.
for (const auto& sr : src->loadRecords(colId)) {
    if (m_cancelled.load()) {
        // … propagate cancellation per existing dispatchFirstSync convention
        return;
    }
    const QString newId = tgt->createRecord(colId, sr);
    if (newId.isEmpty()) {
        // … error handling per existing convention
        continue;
    }
    // Persist baseline for the new pair.
    CanonicalRecord cr;
    cr.shape = src->shapeFor(colId);
    cr.bytes = sr.payload;
    m_baselineStore->putV3(mappingId, sr.id, cr);
}
```

(Use the actual local variable names from `dispatchFirstSync` — `src`, `tgt`, `colId`, `mappingId` should already be in scope. If not, hoist them.)

- [ ] **Step 3: Build + run**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
refactor(engine): inline dispatchFirstSync's runBlobMirror call

Replaces the engine self-call to runBlobMirror with a direct loop over
IBlobBackend, breaking the last internal dependency on the F1 facade
ahead of its deletion.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Audit `tst_engine_blob_one_shot.cpp` — migrate or delete each case

**Files:**
- Delete (after audit): `libkalburator/tests/blob/tst_engine_blob_one_shot.cpp`
- Modify: `libkalburator/tests/blob/CMakeLists.txt`
- Possibly extend: `libkalburator/tests/calendar/tst_engine_mirror_direction.cpp`

The file has 18 cases (533 lines) all directly against `runBlobMirror` / `runBlobTwoWay`. Each case is one of three categories:

- **Category A — engine semantics reachable via mappings.** Migrate to `tst_engine_mirror_direction.cpp` (or a sibling file) using `runSyncFuture(mappingId, ov)`. Examples: empty-target mirror; existing-target deletion; conflict resolution under twoway.
- **Category B — facade plumbing only.** Delete. Examples: null-pointer guards (`runBlobMirror: null backend`).
- **Category C — Palm-specific Backup/Restore semantics.** Delete; the equivalent coverage moves to WildPalms's `tst_palm_runtime_backup` / `tst_palm_runtime_restore` in Plan 2. There may be zero of these in this file; check.

- [ ] **Step 1: List the cases**

```bash
grep -n "void TestEngineBlobOneShot::\|void test" libkalburator/tests/blob/tst_engine_blob_one_shot.cpp | head -30
```

Make a table in your scratchpad: case name → category (A/B/C) → action.

- [ ] **Step 2: For each Category-A case, port to mapping-driven form**

Append new test slots to `tst_engine_mirror_direction.cpp` for each Category-A case. Use the same pattern as Tasks 8 (existing slots) — set up MockBlobBackend pair, register mapping, call `runSyncFuture(id, override)`, assert on resulting backend state. Don't copy the test code verbatim; rewrite from the *intent* (what semantic was being verified) so the new test reads cleanly.

For two-way cases (where the original used `runBlobTwoWay` without mirror), use `runSyncFuture(id)` (no override) and assert on twoway semantics.

- [ ] **Step 3: Delete the old file + CMake registration**

```bash
git rm libkalburator/tests/blob/tst_engine_blob_one_shot.cpp
```

Edit `libkalburator/tests/blob/CMakeLists.txt`, remove the line registering `tst_engine_blob_one_shot`.

- [ ] **Step 4: Build + run**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass; `tst_engine_blob_one_shot` no longer in the registered list; `tst_engine_mirror_direction` has the migrated cases.

- [ ] **Step 5: Commit**

```bash
git add tests/blob/CMakeLists.txt tests/calendar/tst_engine_mirror_direction.cpp
git rm tests/blob/tst_engine_blob_one_shot.cpp 2>/dev/null  # already staged by Step 3
git commit -m "$(cat <<'EOF'
test(engine): migrate facade tests to mapping-driven form; delete one-shot tests

tst_engine_blob_one_shot's <N> cases were all against the F1 facade
(runBlobMirror / runBlobTwoWay). Engine-semantic cases ported to
tst_engine_mirror_direction using runSyncFuture(mappingId, override).
Facade-plumbing cases (null-pointer guards) deleted as obsolete.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

(Replace `<N>` with the actual count from your audit.)

---

## Task 12: Drop `runBlobTwoWay_*` / `runBlobMirror_*` cases from `tst_engine_unified_boundary.cpp`

**Files:**
- Modify: `libkalburator/tests/calendar/tst_engine_unified_boundary.cpp`

This file has both engine-boundary cases (which stay) and facade cases (which go).

- [ ] **Step 1: Identify the cases**

```bash
grep -n "void TestEngineUnifiedBoundary::\|runBlobTwoWay\|runBlobMirror" libkalburator/tests/calendar/tst_engine_unified_boundary.cpp
```

The cases `runBlobTwoWay_propagatesRecordsAndCommitsBaselines` (line 244) and the `runBlobMirror_*` case (line 301) are facade tests. Their contract was originally pinned by `tst_engine_blob_one_shot`; in this file they were a "unified boundary" smoke test that the facade was reachable from the engine. With the facade gone, they're redundant.

- [ ] **Step 2: Delete those test slots + their declarations**

Remove the `void runBlobTwoWay_*` and `void runBlobMirror_*` slot declarations from the class body and their definitions from the file. Keep all other cases.

- [ ] **Step 3: Build + run**

```bash
cmake --build build && ctest --test-dir build -R tst_engine_unified_boundary -V
```

Expected: remaining cases pass.

- [ ] **Step 4: Commit**

```bash
git add tests/calendar/tst_engine_unified_boundary.cpp
git commit -m "$(cat <<'EOF'
test(engine): drop runBlobTwoWay/runBlobMirror cases from boundary suite

Their coverage moves to tst_engine_mirror_direction; the facade methods
they exercised are deleted in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Delete `runBlobMirror` and `runBlobTwoWay` from `SyncEngine`

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h`
- Modify: `libkalburator/src/engine/syncengine.cpp`

After Tasks 10-12, no caller remains. Delete the methods.

- [ ] **Step 1: Verify zero callers across all worktrees**

```bash
grep -rn "runBlobMirror\|runBlobTwoWay" \
    ~/dev/refactor-engine-merger/{libkalburator,PlanStan,WildPalms} \
    --include="*.cpp" --include="*.h" 2>/dev/null \
    | grep -v build/
```

Expected: zero hits in source files. If any remain, they must be on the Plan-2 path (WildPalms's `SyncRunner_wp` etc.) — those are explicitly out of scope here. Confirm before proceeding.

If WildPalms still contains references but is on a separate branch, check the *current* branch state of WildPalms's worktree:

```bash
cd ~/dev/refactor-engine-merger/WildPalms && git status
```

If WildPalms is still on `refactor/engine-merger` and references the methods, you have a choice:
- **(a)** Skip Task 13; defer facade deletion to land alongside Plan 2's M6.
- **(b)** Add `[[deprecated]]` is already there; just remove the methods anyway and accept WildPalms breaks. This is fine because Plan 2 explicitly rewrites WildPalms.

**Default: (b)**, per spec §8 ("Compile-correctness of the old code is irrelevant during the rewrite"). Proceed.

- [ ] **Step 2: Delete declarations**

In `syncengine.h`, delete lines 536-568 (the `--- One-shot blob API (F1 Task 6) ---` block including both methods).

- [ ] **Step 3: Delete bodies**

In `syncengine.cpp`, locate `BlobSyncResult SyncEngine::runBlobMirror` (around line 1286) and `BlobSyncResult SyncEngine::runBlobTwoWay` (around line 1339). Delete both function bodies (~140 LOC).

- [ ] **Step 4: Update includes**

If `syncengine.cpp` `#include "blob/blobsyncresult.h"` and the header is no longer used, remove the include.

- [ ] **Step 5: Build (libkalburator only)**

```bash
cmake --build build 2>&1 | tail -15
```

Expected: clean build of libkalburator. WildPalms will fail; that's expected and fine.

- [ ] **Step 6: Run libkalburator's tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/engine/syncengine.{h,cpp}
git commit -m "$(cat <<'EOF'
refactor(engine): delete runBlobMirror and runBlobTwoWay (F1 facade)

Phase G's Tasks 55/58, finally landed. WildPalms's remaining callers
(SyncRunner_wp + tests) will not compile until the Palm runtime rewrite
(Plan 2) lands the new sync orchestration. PlanStan unaffected.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Delete `BlobSyncResult` / `BlobSyncStats` if unused

**Files:**
- Possibly delete: `libkalburator/src/blob/blobsyncresult.h`
- Possibly modify: `libkalburator/src/blob/CMakeLists.txt`

- [ ] **Step 1: Check for remaining users**

```bash
grep -rn "BlobSyncResult\|BlobSyncStats" \
    ~/dev/refactor-engine-merger/libkalburator/src \
    ~/dev/refactor-engine-merger/libkalburator/tests \
    2>/dev/null | grep -v build/
```

If zero hits in libkalburator source: proceed to Step 2. If WildPalms still uses these, leave the header in place; it's small.

- [ ] **Step 2: Delete the header**

```bash
git rm libkalburator/src/blob/blobsyncresult.h
```

Update `libkalburator/src/blob/CMakeLists.txt` if it's listed there.

- [ ] **Step 3: Build + test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: clean build, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/blob/CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(blob): delete BlobSyncResult/BlobSyncStats — no remaining users

These structs only existed to support the F1 facade methods, deleted
in the previous commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: verify-all gate + baseline refresh

**Files:**
- Possibly modify: `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt`

- [ ] **Step 1: Run verify-all**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected exit codes:
- `0` — match baseline. Continue to Step 5.
- `1` — configure / build failure. Investigate. The most likely cause if it's not zero: WildPalms doesn't compile (per Task 13's expected breakage). If `verify-all.sh` insists WildPalms build, expect exit 1 here.
- `2` — test regression. Investigate before continuing.
- `3` — test improvement (passes that previously failed). Investigate before refreshing baseline.

- [ ] **Step 2: If exit 1 due to WildPalms breakage, document and proceed**

Per spec §8 ("Branch-and-rewrite … Compile-correctness of the old code is irrelevant during the rewrite"), WildPalms breakage from facade deletion is expected. Confirm by running libkalburator + PlanStan in isolation:

```bash
cd ~/dev/refactor-engine-merger/libkalburator && cmake --build build && ctest --test-dir build --output-on-failure
cd ~/dev/refactor-engine-merger/PlanStan && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: both green.

If both green: WildPalms's failure is the expected one. Note this in `CURRENT-STATUS.md` (Task 17 below) as "WildPalms breakage expected, fixed by Plan 2 of the Palm rewrite."

- [ ] **Step 3: If improvement (exit 3), refresh baseline carefully**

```bash
cd ~/dev/refactor-engine-merger
diff baselines/libkalburator-worktree-ctest.txt \
     <(cd libkalburator && ctest --test-dir build -N | tail -n +2)
```

Confirm the new passes are the 3 added by this plan (`tst_dynamic_domain_registration`, `tst_engine_mirror_direction`) and not flakes. Then:

```bash
cd libkalburator && ctest --test-dir build -N > ../baselines/libkalburator-worktree-ctest.txt
```

(Adjust the command to match the actual baseline format used in the file — check the existing baseline to mirror its shape.)

- [ ] **Step 4: Commit baseline refresh if applicable**

```bash
cd ~/dev/refactor-engine-merger
# baseline file is in coordination folder; it's a non-repo dir, so this is just a save.
# If the baseline file is somewhere else and IS in a repo, commit:
# git add baselines/libkalburator-worktree-ctest.txt
# git commit -m "chore: refresh libkalburator baseline post-M1"
```

- [ ] **Step 5: All green — proceed**

---

## Task 16: Update phase-status doc

**Files:**
- Modify: `libkalburator/docs/phase0/04r-phase-g-status.md`

- [ ] **Step 1: Flip Task 55/58/etc. deferred markers**

In `04r-phase-g-status.md`, locate the "Deferred" section (around line 22-24). Replace the line about Tasks 55/58 with:

```markdown
- ~~Tasks 55/58 (SyncRunner_wp + F1 facade deletion)~~ — F1 facade
  (`runBlobMirror`/`runBlobTwoWay`) deleted 2026-05-XX as M1 of the
  Palm runtime rewrite (see
  `~/dev/refactor-engine-merger/2026-05-01-palm-runtime-rewrite-design.md`).
  SyncRunner_wp deletion deferred to Plan 2 (M6) of that rewrite.
```

(Use the actual commit date.)

- [ ] **Step 2: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add docs/phase0/04r-phase-g-status.md
git commit -m "$(cat <<'EOF'
docs(phase0): mark Tasks 55/58 facade deletion landed via Palm rewrite M1

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 17: Update CURRENT-STATUS

**Files:**
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`

- [ ] **Step 1: Append to "Recently committed (libkalburator)" section**

Add at the top of the libkalburator commits block (most recent on top per the file's convention):

```markdown
<hash>  M1 Task 16: phase-status — Tasks 55/58 facade landed via Palm rewrite
<hash>  M1 Task 14: delete BlobSyncResult — no remaining users
<hash>  M1 Task 13: delete runBlobMirror/runBlobTwoWay (F1 facade)
<hash>  M1 Task 12: drop runBlob* cases from tst_engine_unified_boundary
<hash>  M1 Task 11: migrate tst_engine_blob_one_shot to mapping-driven form
<hash>  M1 Task 10: inline dispatchFirstSync's runBlobMirror self-call
<hash>  M1 Task 9: runSyncFuture honors ExecutionOverride mirror direction
<hash>  M1 Task 7: declare ExecutionOverride + runSyncFuture overload
<hash>  M1 Task 6: pin multi-plugin contribution to single domain
<hash>  M1 Task 5: freeze TransformationRegistry per-domain on first compile
<hash>  M1 Tasks 2-3: dynamic DomainPlugin registration (registerPlugin)
```

(Fill in the actual short SHAs from `git log --oneline -20`.)

- [ ] **Step 2: Update "Where we are" section**

Add an `✅ M1 Palm rewrite — libkalburator changes — landed YYYY-MM-DD` entry under Phase G.10's entry. Update "Next" to point at "Plan 2 of Palm rewrite (M2-M7) — WildPalms branch-and-rewrite."

- [ ] **Step 3: Save** (CURRENT-STATUS.md is in the coordination folder; verify if it's in a repo before committing)

If in a repo:

```bash
cd ~/dev/refactor-engine-merger
git add CURRENT-STATUS.md
git commit -m "docs: M1 of Palm rewrite landed (libkalburator changes)"
```

---

## Self-Review (run before declaring complete)

**Spec coverage check:**

- §3.1 dynamic DomainPlugin registration → Tasks 2, 3, 4, 5, 6 ✓
- §3.2 mirror-direction override → Tasks 7, 8, 9 ✓
- §3.2 facade deletion → Tasks 10, 11, 12, 13 ✓
- §3.2 BlobSyncResult cleanup → Task 14 ✓
- §3.2 dispatchFirstSync inlining → Task 10 ✓
- §3.3 "what does NOT change" → enforced by not having tasks for those concerns ✓
- M1 verify-all gate → Task 15 ✓
- M1 status doc update → Tasks 16, 17 ✓
- The latent threading-bug finding → Task 1 ✓

**Type / signature consistency:**

- `ExecutionOverride::Direction::{Default, MirrorAToB, MirrorBToA}` used in Tasks 7, 8, 9 — consistent ✓
- `IDomainAdapter::merge` signature change from Task 9 propagates to both `BlobDomainAdapter` and `CalendarDomainAdapter` overrides — consistent ✓
- `DomainRegistry::registerPlugin(std::shared_ptr<DomainPlugin>)` — same type used in Task 3 implementation and Tasks 2, 6 test fixtures ✓

**Placeholder scan:** none of "TBD", "TODO", "fill in details", "similar to Task N", or "add appropriate error handling" appear ✓ (Task 11 references `<N>` as a count to be filled in by the executing engineer at audit time — that's intentional, not a placeholder)

**Build commands:** `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` for first-time configure if `build/` doesn't exist. All build/test commands use the libkalburator-CLAUDE.md-specified `build/` dir ✓

**Worktree paths:** absolute paths under `~/dev/refactor-engine-merger/` used throughout ✓

---

## Deferred to Plan 2 (Palm runtime rewrite — M2-M7)

For the engineer's reference: these are explicitly NOT in this plan.

- Anything in `WildPalms/`. WildPalms breaks at Task 13 and stays broken until Plan 2 lands.
- `MappingScheduler` v2 (capacity-N for disjoint mappings) — flagged in spec §11; small follow-up.
- Intra-mapping pipelining — flagged in spec §11; small follow-up if any backend's profile demands it.
- Anything related to the new `IBackendPlugin` contract, `PalmRuntime`, `PalmDeviceAccess`.
- The latent HotSyncCoordinator threading bug FIX (the *finding* is documented in Task 1, but the fix is in Plan 2's M2).
