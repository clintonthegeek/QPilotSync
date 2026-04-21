# Phase E.5 — PalmConflictHandler + PalmBackendConfig Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land `PalmConflictHandler : Kalburator::Sync::QSyncCore::ConflictHandler`
plus the `PalmBackendConfig` struct carrying the Palm-specific fields
that were stripped from upstream `ConflictPolicy` (Phase B). The handler
applies standard `ConflictPolicy` resolution and then overlays three
Palm-specific rules — **archive-bit safety**, **secret-flag protection**,
and **category tie-break** — before returning a decision. Registers
under backend id `"palm"` in a `ConflictHandlerRegistry`.

**Architecture:** New static lib `WildPalmsPalmConflict` at
`src/palm/conflict/`. Links `WildPalmsPalmSync` (for `IPalmDatabaseAccess`
and `PalmBackend::decodeRecordId`) and transitively `Kalburator::Sync`
(for `ConflictHandler`, `ConflictRecord`, `ConflictPolicy`). Sibling to
`src/palm/sync/` (E.3) and `src/palm/device/` (E.4). The handler holds:

1. A **borrowed** `IPalmDatabaseAccess *` — reads live records during
   resolution to inspect `attributes` (archive/secret bits) and
   `category` slots that `RecordSnapshot.content` alone can't expose.
2. A **borrowed** `PalmBackendConfig *` — drives keep-alive behaviour
   and will later carry HotSync tickle interval + user name.

**Why handler queries the device:** upstream `BackendRecord` has no
metadata field; `RecordSnapshot.metadata` is a `QVariantMap` but the
engine populates it from `BackendRecord` which doesn't carry Palm
attributes. Rather than patch upstream or widen `BackendRecord`, the
handler reads the live record by decoding `conflict.source.id` /
`conflict.target.id` through `PalmBackend::decodeRecordId()`. This
matches the spec's "Holds a `PalmDeviceConnection*`" language and keeps
the Palm degradation inside the Palm-side module (per
`memory/feedback_library_vs_backend_responsibility.md`).

**Delegation model:** `PalmConflictHandler` composes
`AutomaticConflictHandler`'s policy logic — duplicating it would drift.
The handler calls `policy.shouldAutoResolve` / `policy.getAutoDecision`
(the same free functions on `ConflictPolicy`) for the base decision,
then runs Palm-specific overlays that can **override** the base (e.g.
force `UseSource` when the base would delete an archived target).

**Tech Stack:** C++20, Qt6 (Core, Test), `Kalburator::Sync` +
`WildPalmsPalmSync`. No new runtime dependency.

**Repo:** All work in `~/dev/WildPalms/`. No upstream changes.

**Scope not in E.5:**

- **HotSync tickle thread.** `PalmBackendConfig::hotSyncTickleInterval`
  is a field on the config but no tickle worker is started. That's a
  runtime concern and lands with app-layer wiring (E.16).
- **UI prompting.** `canPrompt()` returns `false`. Interactive resolution
  is the existing `InteractiveConflictHandler` in `src/app/`; unifying
  it with `PalmConflictHandler` is an E.9+ concern when the plugin ABI
  lands.
- **Category-ID remap store.** The spec mentions a `CategoryMappingStore`
  used by the handler. That store ships with E.6 (`PalmCalendarBackend`)
  when virtual sub-calendars per category slot are introduced. E.5's
  category tie-break is a simple "prefer non-zero over zero" rule that
  doesn't need a mapping store.
- **Bit-preservation on write-back.** Preserving archive/secret bits
  through `PalmBackend::updateRecord()` requires threading the attribute
  byte through `BackendRecord ↔ PalmRecord` conversion — that touches
  `palm/sync/palmbackend.cpp` and is better done alongside the typed
  adapters in E.7. E.5's tests verify the *decision* reflects bit
  semantics; *applying* the decision with full bit fidelity is later.
- **Application-layer wiring.** Nothing constructs a
  `PalmConflictHandler` at app startup yet; E.16 handles runtime
  plumbing.

**Spec reference:**
`docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
§"WP-side class layout" (`PalmConflictHandler` + `PalmBackendConfig`
rows) and the sub-phases table (E.5 row).

---

## File Structure

| Path | Role | Created / Modified |
|---|---|---|
| `src/palm/conflict/palmbackendconfig.h` | `PalmBackendConfig` struct + `ConnectionBehavior` enum | Create |
| `src/palm/conflict/palmbackendconfig.cpp` | Enum serialization helpers | Create |
| `src/palm/conflict/palmconflicthandler.h` | Handler header | Create |
| `src/palm/conflict/palmconflicthandler.cpp` | Handler impl (base policy + overlays) | Create |
| `src/palm/conflict/CMakeLists.txt` | New static lib `WildPalmsPalmConflict` | Create |
| `src/CMakeLists.txt` | `add_subdirectory(palm/conflict)` | Modify |
| `tests/palmconflict/CMakeLists.txt` | Test target wiring | Create |
| `tests/palmconflict/tst_palmbackendconfig.cpp` | Config struct + enum tests | Create |
| `tests/palmconflict/tst_palmconflicthandler.cpp` | Handler behaviour tests | Create |
| `tests/palmconflict/tst_palmconflicthandler_registration.cpp` | `ConflictHandlerRegistry` integration | Create |
| `tests/CMakeLists.txt` | `add_subdirectory(palmconflict)` | Modify |
| `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` | Mark E.5 ✅ | Modify |

---

## Task 1: PalmBackendConfig struct + ConnectionBehavior enum

**Files:**
- Create: `src/palm/conflict/palmbackendconfig.h`
- Create: `src/palm/conflict/palmbackendconfig.cpp`
- Create: `src/palm/conflict/CMakeLists.txt`
- Create: `tests/palmconflict/CMakeLists.txt`
- Create: `tests/palmconflict/tst_palmbackendconfig.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the config header.**

File `src/palm/conflict/palmbackendconfig.h`:

```cpp
#ifndef WILDPALMS_CONFLICT_PALMBACKENDCONFIG_H
#define WILDPALMS_CONFLICT_PALMBACKENDCONFIG_H

#include <QString>

namespace WildPalms::PalmConflict {

/**
 * @brief HotSync connection-persistence policy during conflict resolution.
 *
 * Palm DLP sessions keep the device in a "listening" mode; leaving the
 * session open while a user ponders a conflict prompt ties up the
 * cradle. These three modes cover the realistic choices:
 */
enum class ConnectionBehavior {
    KeepAlive,             ///< Session stays open through the prompt; caller
                           ///< issues periodic DLP tickles.
    DisconnectAndDefer,    ///< Session closes immediately; conflict is
                           ///< persisted for later resolution.
    TimeoutThenDefer,      ///< Session stays open for `connectionTimeoutSeconds`,
                           ///< then closes and defers.
};

QString connectionBehaviorToString(ConnectionBehavior b);
ConnectionBehavior connectionBehaviorFromString(const QString &s);

/**
 * @brief Palm-specific config consulted by `PalmConflictHandler`.
 *
 * Every field has a sane default so callers can default-construct and
 * mutate only what they care about. This struct is a plain-old-data
 * holder — no Qt MOC, no signals, no ownership semantics.
 *
 * Stored on the PalmBackend instance (later — E.16 wires it). The
 * handler reads it via a borrowed pointer.
 */
struct PalmBackendConfig {
    ConnectionBehavior connectionBehavior  = ConnectionBehavior::KeepAlive;
    int                connectionTimeoutSeconds = 60;
    int                hotSyncTickleIntervalSeconds = 5;
    QString            userName;  ///< Set from dlp_ReadUserInfo at session
                                  ///< start. Empty in test/default contexts.

    bool operator==(const PalmBackendConfig &other) const = default;
};

} // namespace WildPalms::PalmConflict

#endif // WILDPALMS_CONFLICT_PALMBACKENDCONFIG_H
```

- [ ] **Step 2: Write the config impl (enum helpers only).**

File `src/palm/conflict/palmbackendconfig.cpp`:

```cpp
#include "palmbackendconfig.h"

namespace WildPalms::PalmConflict {

QString connectionBehaviorToString(ConnectionBehavior b)
{
    switch (b) {
        case ConnectionBehavior::KeepAlive:          return QStringLiteral("KeepAlive");
        case ConnectionBehavior::DisconnectAndDefer: return QStringLiteral("DisconnectAndDefer");
        case ConnectionBehavior::TimeoutThenDefer:   return QStringLiteral("TimeoutThenDefer");
    }
    return QStringLiteral("KeepAlive");
}

ConnectionBehavior connectionBehaviorFromString(const QString &s)
{
    if (s == QStringLiteral("DisconnectAndDefer")) return ConnectionBehavior::DisconnectAndDefer;
    if (s == QStringLiteral("TimeoutThenDefer"))   return ConnectionBehavior::TimeoutThenDefer;
    return ConnectionBehavior::KeepAlive;
}

} // namespace WildPalms::PalmConflict
```

- [ ] **Step 3: Write the CMake target.**

File `src/palm/conflict/CMakeLists.txt`:

```cmake
# WildPalmsPalmConflict — Palm-aware ConflictHandler + BackendConfig.
#
# Phase E.5 of the libkalburator integration. Implements
# Kalburator::Sync::QSyncCore::ConflictHandler with Palm-specific
# overlays (archive-bit safety, secret-flag protection, category
# tie-break) and registers under backend id "palm".
#
# Links WildPalmsPalmSync so the handler can decode Palm record IDs
# and query IPalmDatabaseAccess during resolution. Transitively pulls
# Kalburator::Sync for the ConflictHandler base interface.

add_library(WildPalmsPalmConflict STATIC
    palmbackendconfig.h
    palmbackendconfig.cpp
    palmconflicthandler.h
    palmconflicthandler.cpp
)

target_include_directories(WildPalmsPalmConflict
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmConflict
    PUBLIC
        Qt::Core
        WildPalmsPalmSync
)

set_target_properties(WildPalmsPalmConflict PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

`palmconflicthandler.{h,cpp}` are referenced here but filled in by
Task 2. Land placeholder files now so the CMake target builds:

```bash
cd ~/dev/WildPalms
cat > src/palm/conflict/palmconflicthandler.h <<'EOF'
// Placeholder — filled in by Phase E.5 task 2.
#ifndef WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H
#define WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H
#endif
EOF
cat > src/palm/conflict/palmconflicthandler.cpp <<'EOF'
// Placeholder — filled in by Phase E.5 task 2.
#include "palmconflicthandler.h"
namespace { [[maybe_unused]] int wp_palmconflicthandler_placeholder() { return 0; } }
EOF
```

- [ ] **Step 4: Hook into the src tree.**

Edit `src/CMakeLists.txt`. After the existing `add_subdirectory(palm/device)`
line (landed in E.4 task 1), add:

```cmake
# Palm-aware ConflictHandler + BackendConfig (Phase E.5 of libkalburator
# integration). Registers under backend id "palm" in
# Kalburator::Sync::QSyncCore::ConflictHandlerRegistry.
add_subdirectory(palm/conflict)
```

- [ ] **Step 5: Write the config unit tests.**

File `tests/palmconflict/tst_palmbackendconfig.cpp`:

```cpp
#include <QtTest/QtTest>

#include "palmbackendconfig.h"

using WildPalms::PalmConflict::ConnectionBehavior;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::connectionBehaviorFromString;
using WildPalms::PalmConflict::connectionBehaviorToString;

class TestPalmBackendConfig : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreSensible();
    void equalityIsStructural();
    void connectionBehaviorStringRoundTrip();
    void unknownBehaviorStringFallsBackToKeepAlive();
};

void TestPalmBackendConfig::defaultsAreSensible()
{
    PalmBackendConfig cfg;
    QCOMPARE(cfg.connectionBehavior, ConnectionBehavior::KeepAlive);
    QCOMPARE(cfg.connectionTimeoutSeconds, 60);
    QCOMPARE(cfg.hotSyncTickleIntervalSeconds, 5);
    QVERIFY(cfg.userName.isEmpty());
}

void TestPalmBackendConfig::equalityIsStructural()
{
    PalmBackendConfig a;
    PalmBackendConfig b;
    QCOMPARE(a, b);

    b.connectionTimeoutSeconds = 120;
    QVERIFY(!(a == b));
}

void TestPalmBackendConfig::connectionBehaviorStringRoundTrip()
{
    for (auto b : { ConnectionBehavior::KeepAlive,
                    ConnectionBehavior::DisconnectAndDefer,
                    ConnectionBehavior::TimeoutThenDefer }) {
        const auto s = connectionBehaviorToString(b);
        QCOMPARE(connectionBehaviorFromString(s), b);
    }
}

void TestPalmBackendConfig::unknownBehaviorStringFallsBackToKeepAlive()
{
    QCOMPARE(connectionBehaviorFromString(QStringLiteral("nonsense")),
             ConnectionBehavior::KeepAlive);
}

QTEST_MAIN(TestPalmBackendConfig)
#include "tst_palmbackendconfig.moc"
```

- [ ] **Step 6: Write the tests CMakeLists.**

File `tests/palmconflict/CMakeLists.txt`:

```cmake
# Phase E.5 — PalmConflictHandler + PalmBackendConfig tests.
# Each test links WildPalmsPalmConflict (transitively WildPalmsPalmSync
# + Kalburator::Sync). Deliberately does NOT link WildPalmsCore or pisock.

function(add_palm_conflict_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsPalmConflict
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_conflict_test(tst_palmbackendconfig tst_palmbackendconfig.cpp)
```

- [ ] **Step 7: Hook into the tests tree.**

Edit `tests/CMakeLists.txt`. After the existing `add_subdirectory(palmdevice)`
line (landed in E.4 task 1), add:

```cmake
# ============================================================
# Phase E.5 — Palm-aware ConflictHandler + BackendConfig
# ============================================================

add_subdirectory(palmconflict)
```

- [ ] **Step 8: Configure + build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_palmbackendconfig
```

Expected: 4 tests PASS.

- [ ] **Step 9: Full WP ctest.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure
```

Expected: 24/24 pass (23 pre-E.5 + new config test).

- [ ] **Step 10: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/conflict/ src/CMakeLists.txt \
        tests/palmconflict/ tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-conflict): scaffold WildPalmsPalmConflict + PalmBackendConfig

Phase E.5 task 1: new static library WildPalmsPalmConflict houses
the Palm-aware ConflictHandler (landed in task 2) and the
PalmBackendConfig struct carrying the Palm-specific fields that were
stripped from upstream ConflictPolicy in Phase B.

PalmBackendConfig is a plain struct (ConnectionBehavior +
connectionTimeoutSeconds + hotSyncTickleIntervalSeconds + userName).
ConnectionBehavior enum covers KeepAlive / DisconnectAndDefer /
TimeoutThenDefer — the three realistic session-persistence policies
during conflict resolution.

Four unit tests: default values, structural equality, enum
round-trip, graceful fallback on unknown enum strings. Full WP ctest
at 24/24.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: PalmConflictHandler scaffold + base-policy delegation

**Files:**
- Replace: `src/palm/conflict/palmconflicthandler.h`
- Replace: `src/palm/conflict/palmconflicthandler.cpp`
- Create: `tests/palmconflict/tst_palmconflicthandler.cpp`
- Modify: `tests/palmconflict/CMakeLists.txt`

- [ ] **Step 1: Write the handler header.**

File `src/palm/conflict/palmconflicthandler.h`:

```cpp
#ifndef WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H
#define WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H

#include "conflictpolicy.h"

#include "palmbackendconfig.h"

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PalmConflict {

/**
 * @brief Palm-aware implementation of
 *        `Kalburator::Sync::QSyncCore::ConflictHandler`.
 *
 * Resolution algorithm:
 *
 *   1. Delegate to `ConflictPolicy` for the base decision (mirrors
 *      `AutomaticConflictHandler`): if `shouldAutoResolve` is true, take
 *      `getAutoDecision`; otherwise honour `fallback` (Defer / Skip /
 *      UseDefault / Abort).
 *   2. Apply Palm overlays, which may **override** the base decision:
 *        a. **Archive safety** — never delete an archived record on the
 *           Palm side; flip `DeleteBoth` / delete-directional decisions
 *           to preserve the archived side.
 *        b. **Secret protection** — in `BothModified` where exactly one
 *           side is secret and the base decision is `UseBoth`
 *           (`DuplicateAll`), prefer the secret side to avoid leaking
 *           the record into a non-secret duplicate.
 *        c. **Category tie-break** — in `BothModified` with a
 *           `NewerWins` / `OlderWins` base that collapses on equal
 *           timestamps, prefer the side with a non-zero category.
 *
 * Overlays only fire when the ID decodes as a Palm record (via
 * `PalmBackend::decodeRecordId`) and `IPalmDatabaseAccess` returns a
 * record with the matching attributes. Non-Palm conflicts fall through
 * unchanged — the handler is safe to register as the registry's default
 * even in mixed-backend scenarios.
 *
 * Lifetime: does NOT own the `IPalmDatabaseAccess` or `PalmBackendConfig`.
 * Caller retains ownership; both must outlive the handler.
 */
class PalmConflictHandler
    : public Kalburator::Sync::QSyncCore::ConflictHandler
{
public:
    PalmConflictHandler(WildPalms::PalmSync::IPalmDatabaseAccess *device,
                        const PalmBackendConfig *config);
    ~PalmConflictHandler() override = default;

    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &policy) override;

    bool canPrompt() const override { return false; }
    bool shouldKeepConnectionAlive() const override;
    QList<Kalburator::Sync::QSyncCore::ConflictRecord> pendingConflicts()
        const override { return m_pending; }

    void onSyncStart() override;
    void onSyncEnd(bool hadConflicts, bool allResolved) override;

    /// Test hook: accessor for last overlay applied (empty if base decision
    /// stood). Values: "", "archive", "secret", "category".
    const QString &lastOverlay() const { return m_lastOverlay; }

private:
    // Helper: returns nullopt if the id doesn't decode or the device
    // has no matching record. Used by overlays.
    std::optional<WildPalms::PalmSync::PalmRecord>
        lookupPalmRecord(const QString &encodedId) const;

    // Overlay application — each returns the (possibly adjusted) decision.
    Kalburator::Sync::QSyncCore::ConflictDecision applyOverlays(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        Kalburator::Sync::QSyncCore::ConflictDecision baseDecision);

    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    const PalmBackendConfig *m_config = nullptr;
    QList<Kalburator::Sync::QSyncCore::ConflictRecord> m_pending;
    QString m_lastOverlay;
};

} // namespace WildPalms::PalmConflict

#endif // WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H
```

- [ ] **Step 2: Write the handler impl (base delegation only; overlays
      stubbed).**

File `src/palm/conflict/palmconflicthandler.cpp`:

```cpp
#include "palmconflicthandler.h"

#include <QDateTime>

#include "ipalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmrecord.h"

namespace WildPalms::PalmConflict {

using Kalburator::Sync::QSyncCore::ConflictDecision;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictRecord;
using Kalburator::Sync::QSyncCore::FallbackBehavior;
using WildPalms::PalmSync::IPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

PalmConflictHandler::PalmConflictHandler(IPalmDatabaseAccess *device,
                                         const PalmBackendConfig *config)
    : m_device(device)
    , m_config(config)
{
}

ConflictDecision PalmConflictHandler::handleConflict(
    ConflictRecord &conflict, const ConflictPolicy &policy)
{
    m_lastOverlay.clear();

    // Stage 1: compute base decision, mirroring AutomaticConflictHandler.
    ConflictDecision baseDecision = ConflictDecision::Pending;
    QString resolvedBy;

    if (policy.shouldAutoResolve(conflict)) {
        baseDecision = policy.getAutoDecision(conflict);
        if (baseDecision != ConflictDecision::Pending) {
            resolvedBy = QStringLiteral("policy:palm");
        }
    }

    if (baseDecision == ConflictDecision::Pending) {
        switch (policy.fallback) {
            case FallbackBehavior::Defer:
                conflict.decision = ConflictDecision::Pending;
                m_pending.append(conflict);
                return ConflictDecision::Pending;
            case FallbackBehavior::Skip:
                baseDecision = ConflictDecision::Skip;
                resolvedBy = QStringLiteral("fallback:skip");
                break;
            case FallbackBehavior::UseDefault:
                baseDecision = policy.getAutoDecision(conflict);
                if (baseDecision == ConflictDecision::Pending) {
                    baseDecision = ConflictDecision::Skip;
                }
                resolvedBy = QStringLiteral("fallback:default");
                break;
            case FallbackBehavior::Abort:
                baseDecision = ConflictDecision::Skip;
                resolvedBy = QStringLiteral("fallback:abort");
                break;
        }
    }

    // Stage 2: Palm overlays may override.
    const auto finalDecision = applyOverlays(conflict, baseDecision);

    conflict.decision = finalDecision;
    conflict.resolvedAt = QDateTime::currentDateTime();
    conflict.resolvedBy = m_lastOverlay.isEmpty()
        ? resolvedBy
        : QStringLiteral("palm-overlay:%1").arg(m_lastOverlay);
    return finalDecision;
}

bool PalmConflictHandler::shouldKeepConnectionAlive() const
{
    if (!m_config) return true;
    return m_config->connectionBehavior == ConnectionBehavior::KeepAlive
        || m_config->connectionBehavior == ConnectionBehavior::TimeoutThenDefer;
    // TimeoutThenDefer is "alive until timeout"; a per-session timer
    // lives in the runtime (E.16). For the handler's static answer we
    // treat it as "yes, keep alive" — the runtime clips it when the
    // timer fires.
}

void PalmConflictHandler::onSyncStart()
{
    m_pending.clear();
    m_lastOverlay.clear();
}

void PalmConflictHandler::onSyncEnd(bool, bool) {}

std::optional<PalmRecord> PalmConflictHandler::lookupPalmRecord(
    const QString &encodedId) const
{
    if (!m_device) return std::nullopt;
    QString dbName;
    std::uint32_t numericId = 0;
    if (!PalmBackend::decodeRecordId(encodedId, &dbName, &numericId)) {
        return std::nullopt;
    }
    return m_device->readRecord(dbName, numericId);
}

ConflictDecision PalmConflictHandler::applyOverlays(
    ConflictRecord &, ConflictDecision baseDecision)
{
    // Filled in by Tasks 3/4/5.
    return baseDecision;
}

} // namespace WildPalms::PalmConflict
```

- [ ] **Step 3: Write the handler base-policy tests.**

File `tests/palmconflict/tst_palmconflicthandler.cpp`:

```cpp
#include <QtTest/QtTest>

#include "conflictpolicy.h"
#include "conflictrecord.h"

#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmbackendconfig.h"
#include "palmconflicthandler.h"

using Kalburator::Sync::QSyncCore::AutoResolveStrategy;
using Kalburator::Sync::QSyncCore::ConflictDecision;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictRecord;
using Kalburator::Sync::QSyncCore::ConflictType;
using Kalburator::Sync::QSyncCore::FallbackBehavior;
using Kalburator::Sync::QSyncCore::PromptStrategy;
using Kalburator::Sync::QSyncCore::RecordSnapshot;
using WildPalms::PalmConflict::ConnectionBehavior;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::PalmConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

namespace {

ConflictRecord makeBothModifiedConflict(
    const QString &sourceId, const QString &targetId,
    const QDateTime &sourceTime, const QDateTime &targetTime)
{
    ConflictRecord cr;
    cr.type = ConflictType::BothModified;
    cr.source.id = sourceId;
    cr.source.content = QByteArrayLiteral("src");
    cr.source.lastModified = sourceTime;
    cr.target.id = targetId;
    cr.target.content = QByteArrayLiteral("tgt");
    cr.target.lastModified = targetTime;
    return cr;
}

} // namespace

class TestPalmConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void sourceAlwaysWinsPolicyYieldsUseSource();
    void targetAlwaysWinsPolicyYieldsUseTarget();
    void newerWinsRespectsTimestamps();
    void deferFallbackAccumulatesPending();
    void skipFallbackReturnsSkip();
    void nonPalmIdsFallThroughUnchanged();
};

void TestPalmConflictHandler::sourceAlwaysWinsPolicyYieldsUseSource()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(cr.decision, ConflictDecision::UseSource);
}

void TestPalmConflictHandler::targetAlwaysWinsPolicyYieldsUseTarget()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
}

void TestPalmConflictHandler::newerWinsRespectsTimestamps()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    const auto now = QDateTime::currentDateTimeUtc();
    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        /*sourceTime=*/now,
        /*targetTime=*/now.addSecs(-60));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
}

void TestPalmConflictHandler::deferFallbackAccumulatesPending()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);
    handler.onSyncStart();

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::deferAll();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::Pending);
    QCOMPARE(handler.pendingConflicts().size(), 1);
}

void TestPalmConflictHandler::skipFallbackReturnsSkip()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::Skip;
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::Skip);
}

void TestPalmConflictHandler::nonPalmIdsFallThroughUnchanged()
{
    // Non-Palm ids: no prefix "palm:", so decodeRecordId fails and
    // overlays should be no-ops. Base policy decision stands.
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        QStringLiteral("local:memo:1"),
        QStringLiteral("local:memo:1"),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QVERIFY(handler.lastOverlay().isEmpty());
}

QTEST_MAIN(TestPalmConflictHandler)
#include "tst_palmconflicthandler.moc"
```

- [ ] **Step 4: Register the test.**

Append to `tests/palmconflict/CMakeLists.txt`:

```cmake
add_palm_conflict_test(tst_palmconflicthandler tst_palmconflicthandler.cpp)
```

- [ ] **Step 5: Build + run.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_palmconflicthandler
ctest --test-dir build --output-on-failure -R tst_palmconflicthandler
```

Expected: 6 tests PASS.

- [ ] **Step 6: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/conflict/palmconflicthandler.h \
        src/palm/conflict/palmconflicthandler.cpp \
        tests/palmconflict/tst_palmconflicthandler.cpp \
        tests/palmconflict/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm-conflict): PalmConflictHandler scaffold + base-policy delegation

Phase E.5 task 2: PalmConflictHandler implements
Kalburator::Sync::QSyncCore::ConflictHandler. Delegates to the
upstream ConflictPolicy for auto-resolve / fallback decisions,
mirroring AutomaticConflictHandler's algorithm, and exposes a hook
for Palm-specific overlays (filled in by tasks 3-5).

Holds borrowed pointers to IPalmDatabaseAccess (for live-record
queries during overlay resolution) and PalmBackendConfig (for
keep-alive behaviour). canPrompt() returns false —
InteractiveConflictHandler unification is an E.9+ concern.

Six unit tests cover base-policy delegation (SourceAlwaysWins,
TargetAlwaysWins, NewerWins), defer/skip fallbacks, and graceful
pass-through on non-Palm record IDs.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Archive-bit safety overlay

**Files:**
- Modify: `src/palm/conflict/palmconflicthandler.cpp`
- Modify: `tests/palmconflict/tst_palmconflicthandler.cpp`

**Rule:** Archived records must survive. If the base decision would
destroy an archived record that the device still holds:

| Conflict type | Live archived side (per device) | Base decision | Overlay adjustment |
|---|---|---|---|
| `ModifiedVsDeleted` (source live, target deleted) | source | `DeleteBoth` (e.g. `autoTargetWins` on ModifiedVsDeleted returns DeleteBoth) | Flip to `UseSource` |
| `DeletedVsModified` (source deleted, target live) | target | `DeleteBoth` | Flip to `UseTarget` |

In all other cases (`BothModified`, non-destructive base decisions,
non-archived records, records the device doesn't hold), the overlay
passes through unchanged. The device is the source of truth for the
archive bit — if `IPalmDatabaseAccess::readRecord` returns `nullopt`
(record gone), we honour the base decision regardless of what the
snapshot claims.

- [ ] **Step 1: Replace the overlay stub with the archive rule.**

In `src/palm/conflict/palmconflicthandler.cpp`, replace the `applyOverlays`
implementation with:

```cpp
ConflictDecision PalmConflictHandler::applyOverlays(
    ConflictRecord &conflict, ConflictDecision baseDecision)
{
    using Kalburator::Sync::QSyncCore::ConflictType;

    // Archive-bit safety: never destroy an archived record on the Palm
    // side. Only fires on destructive decisions (DeleteBoth, UseTarget
    // for a DeletedVsModified, UseSource for a ModifiedVsDeleted).
    const auto sourcePalm = lookupPalmRecord(conflict.source.id);
    const auto targetPalm = lookupPalmRecord(conflict.target.id);
    const bool sourceArchived = sourcePalm && sourcePalm->isArchived();
    const bool targetArchived = targetPalm && targetPalm->isArchived();

    if (sourceArchived || targetArchived) {
        // Destructive directional decisions: a decision that removes
        // the archived side must flip to preserve it.
        if (baseDecision == ConflictDecision::DeleteBoth) {
            m_lastOverlay = QStringLiteral("archive");
            if (sourceArchived) return ConflictDecision::UseSource;
            return ConflictDecision::UseTarget;
        }
        if (baseDecision == ConflictDecision::UseTarget
            && conflict.type == ConflictType::ModifiedVsDeleted
            && sourceArchived) {
            m_lastOverlay = QStringLiteral("archive");
            return ConflictDecision::UseSource;
        }
        if (baseDecision == ConflictDecision::UseSource
            && conflict.type == ConflictType::DeletedVsModified
            && targetArchived) {
            m_lastOverlay = QStringLiteral("archive");
            return ConflictDecision::UseTarget;
        }
    }

    return baseDecision;
}
```

- [ ] **Step 2: Append archive-overlay tests.**

Append three new `private slots:` entries to the `TestPalmConflictHandler`
class in `tests/palmconflict/tst_palmconflicthandler.cpp`:

```cpp
    void archivedSourceSurvivesModifiedVsDeleted();
    void archivedTargetSurvivesDeletedVsModified();
    void nonArchivedRecordGetsDeleted();
```

Add these slot implementations before the `QTEST_MAIN` line:

```cpp
void TestPalmConflictHandler::archivedSourceSurvivesModifiedVsDeleted()
{
    // Palm source record is live and archived; other side deleted it.
    // Base TargetAlwaysWins on ModifiedVsDeleted → DeleteBoth. Overlay
    // must preserve the archived source via UseSource.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord archived;
    archived.recordId = 7;
    archived.attributes = PalmRecord::AttrArchived;
    archived.data = QByteArrayLiteral("archived-body");
    archived.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), archived);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr;
    cr.type = Kalburator::Sync::QSyncCore::ConflictType::ModifiedVsDeleted;
    cr.source.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 7);
    cr.source.content = QByteArrayLiteral("archived-body");
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.id = QStringLiteral("local:memo:7");
    cr.target.content.clear();
    cr.target.lastModified = QDateTime::currentDateTimeUtc();

    auto policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("archive"));
}

void TestPalmConflictHandler::archivedTargetSurvivesDeletedVsModified()
{
    // Palm target record is live and archived; source (some other
    // backend) deleted its copy. Base SourceAlwaysWins on
    // DeletedVsModified → DeleteBoth. Overlay flips to UseTarget.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord archived;
    archived.recordId = 11;
    archived.attributes = PalmRecord::AttrArchived;
    archived.data = QByteArrayLiteral("archived-body");
    archived.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), archived);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr;
    cr.type = Kalburator::Sync::QSyncCore::ConflictType::DeletedVsModified;
    cr.source.id = QStringLiteral("local:memo:11");
    cr.source.content.clear();
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 11);
    cr.target.content = QByteArrayLiteral("archived-body");
    cr.target.lastModified = QDateTime::currentDateTimeUtc();

    auto policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("archive"));
}

void TestPalmConflictHandler::nonArchivedRecordGetsDeleted()
{
    // Control: Palm record live but NOT archived. Overlay must not
    // fire — base decision stands.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord plain;
    plain.recordId = 13;
    plain.attributes = 0;
    plain.data = QByteArrayLiteral("plain");
    plain.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), plain);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr;
    cr.type = Kalburator::Sync::QSyncCore::ConflictType::ModifiedVsDeleted;
    cr.source.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 13);
    cr.source.content = QByteArrayLiteral("plain");
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.id = QStringLiteral("local:memo:13");
    cr.target.content.clear();
    cr.target.lastModified = QDateTime::currentDateTimeUtc();

    auto policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::DeleteBoth);
    QVERIFY(handler.lastOverlay().isEmpty());
}
```

- [ ] **Step 3: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_palmconflicthandler
ctest --test-dir build --output-on-failure -R tst_palmconflicthandler
```

Expected: 9 tests PASS.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/conflict/palmconflicthandler.cpp \
        tests/palmconflict/tst_palmconflicthandler.cpp
git commit -m "$(cat <<'EOF'
feat(palm-conflict): archive-bit safety overlay

Phase E.5 task 3: first Palm-specific overlay in PalmConflictHandler.
When resolution would destroy an archived record (DeleteBoth or a
directional decision that removes the archived side of a
Modified/Deleted conflict), the overlay flips the decision to
preserve the archived record.

Implements the invariant the stock HotSync conduits provided:
"archived records are never silently removed by a sync." Handler
queries the live device via its borrowed IPalmDatabaseAccess to
inspect the AttrArchived bit, which is not carried on
BackendRecord's payload.

Three new overlay tests (SourceAlwaysWins on DeletedVsModified with
archived source, TargetAlwaysWins on ModifiedVsDeleted with archived
target, archived source surviving target-side deletion) bring the
handler suite to 9 passing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Secret-flag protection overlay

**Files:**
- Modify: `src/palm/conflict/palmconflicthandler.cpp`
- Modify: `tests/palmconflict/tst_palmconflicthandler.cpp`

**Rule:** Secret records should not be duplicated into non-secret
visibility. In `BothModified` conflicts where exactly one side has the
`AttrSecret` bit and the base decision is `UseBoth` (set by
`AutoResolveStrategy::DuplicateAll`), flip the decision to the secret
side — `UseSource` if source is secret, `UseTarget` if target is.

Rationale: `UseBoth` creates a new record on the non-secret side (the
target typically), copying the payload without the secret flag. For
records the user marked private, that's a privacy leak. The overlay
degrades `UseBoth` to a single-side resolution, preserving the secret
bit.

Both sides secret or neither secret: overlay does nothing.

- [ ] **Step 1: Extend the overlay with the secret rule.**

In `src/palm/conflict/palmconflicthandler.cpp`, extend `applyOverlays`
after the existing archive block (so archive safety takes precedence
over secret protection):

```cpp
    // Secret-flag protection: UseBoth on a BothModified where exactly
    // one side is secret would duplicate the record without the secret
    // bit; flip to keep only the secret side.
    const bool sourceSecret = sourcePalm && sourcePalm->isSecret();
    const bool targetSecret = targetPalm && targetPalm->isSecret();
    if (baseDecision == ConflictDecision::UseBoth
        && conflict.type == ConflictType::BothModified
        && (sourceSecret != targetSecret)) {
        m_lastOverlay = QStringLiteral("secret");
        return sourceSecret ? ConflictDecision::UseSource
                            : ConflictDecision::UseTarget;
    }
```

Place this block **after** the archive safety block so archive
preservation runs first (more protective).

- [ ] **Step 2: Append secret-overlay tests.**

Append to `TestPalmConflictHandler`:

```cpp
    void secretSourceOverridesDuplicateAll();
    void secretTargetOverridesDuplicateAll();
    void neitherSecretLeavesDuplicateAll();
```

And slot implementations:

```cpp
void TestPalmConflictHandler::secretSourceOverridesDuplicateAll()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord secret;
    secret.recordId = 21;
    secret.attributes = PalmRecord::AttrSecret;
    secret.data = QByteArrayLiteral("secret-src");
    secret.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), secret);

    PalmRecord visible;
    visible.recordId = 22;
    visible.attributes = 0;
    visible.data = QByteArrayLiteral("plain-tgt");
    visible.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), visible);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 21),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 22),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::DuplicateAll;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("secret"));
}

void TestPalmConflictHandler::secretTargetOverridesDuplicateAll()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord visible;
    visible.recordId = 31;
    visible.attributes = 0;
    visible.data = QByteArrayLiteral("plain-src");
    visible.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), visible);

    PalmRecord secret;
    secret.recordId = 32;
    secret.attributes = PalmRecord::AttrSecret;
    secret.data = QByteArrayLiteral("secret-tgt");
    secret.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), secret);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 31),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 32),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::DuplicateAll;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("secret"));
}

void TestPalmConflictHandler::neitherSecretLeavesDuplicateAll()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord a;
    a.recordId = 41;
    a.data = QByteArrayLiteral("a");
    a.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), a);

    PalmRecord b;
    b.recordId = 42;
    b.data = QByteArrayLiteral("b");
    b.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), b);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 41),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 42),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::DuplicateAll;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseBoth);
    QVERIFY(handler.lastOverlay().isEmpty());
}
```

- [ ] **Step 3: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_palmconflicthandler
ctest --test-dir build --output-on-failure -R tst_palmconflicthandler
```

Expected: 12 tests PASS.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/conflict/palmconflicthandler.cpp \
        tests/palmconflict/tst_palmconflicthandler.cpp
git commit -m "$(cat <<'EOF'
feat(palm-conflict): secret-flag protection overlay

Phase E.5 task 4: second overlay in PalmConflictHandler.
DuplicateAll on a BothModified where exactly one side is
AttrSecret would clone the record without the secret bit, leaking
it into non-secret visibility. Overlay degrades the decision to
the secret side only.

Archive safety runs first (more protective), then secret
protection. Both-sides-secret and neither-side-secret fall through
to the base UseBoth.

Three new tests (source-secret, target-secret, neither-secret
unchanged) bring the handler suite to 12 passing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Category tie-break overlay

**Files:**
- Modify: `src/palm/conflict/palmconflicthandler.cpp`
- Modify: `tests/palmconflict/tst_palmconflicthandler.cpp`

**Rule:** When both sides of a `BothModified` carry identical
timestamps and the base decision was produced by `NewerWins` /
`OlderWins` (which collapse to `UseTarget` on ties in the upstream
implementation — see `conflictpolicy.cpp:59-72`), prefer the side with
a non-zero category. Rationale: category 0 is "Unfiled" in Palm
semantics; losing a user-assigned category (1..15) is worse than losing
"Unfiled" on the other side.

If both categories are zero or both non-zero, the overlay does nothing.

**Guard:** This overlay ONLY fires on timestamp-based strategies
(NewerWins / OlderWins). Other strategies retain their base decision.

- [ ] **Step 1: Extend the overlay with the category tie-break.**

In `src/palm/conflict/palmconflicthandler.cpp`, extend `applyOverlays`
after the secret-flag block. The overlay needs access to the policy's
`autoResolve` strategy; update `applyOverlays` to take the policy:

Replace the forward declaration in the header:

```cpp
    Kalburator::Sync::QSyncCore::ConflictDecision applyOverlays(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &policy,
        Kalburator::Sync::QSyncCore::ConflictDecision baseDecision);
```

And update the call site in `handleConflict`:

```cpp
    const auto finalDecision = applyOverlays(conflict, policy, baseDecision);
```

Then implement the extended overlay:

```cpp
ConflictDecision PalmConflictHandler::applyOverlays(
    ConflictRecord &conflict, const ConflictPolicy &policy,
    ConflictDecision baseDecision)
{
    using Kalburator::Sync::QSyncCore::AutoResolveStrategy;
    using Kalburator::Sync::QSyncCore::ConflictType;

    const auto sourcePalm = lookupPalmRecord(conflict.source.id);
    const auto targetPalm = lookupPalmRecord(conflict.target.id);
    const bool sourceArchived = sourcePalm && sourcePalm->isArchived();
    const bool targetArchived = targetPalm && targetPalm->isArchived();

    // Archive safety.
    if (sourceArchived || targetArchived) {
        if (baseDecision == ConflictDecision::DeleteBoth) {
            m_lastOverlay = QStringLiteral("archive");
            return sourceArchived ? ConflictDecision::UseSource
                                  : ConflictDecision::UseTarget;
        }
        if (baseDecision == ConflictDecision::UseTarget
            && conflict.type == ConflictType::ModifiedVsDeleted
            && sourceArchived) {
            m_lastOverlay = QStringLiteral("archive");
            return ConflictDecision::UseSource;
        }
        if (baseDecision == ConflictDecision::UseSource
            && conflict.type == ConflictType::DeletedVsModified
            && targetArchived) {
            m_lastOverlay = QStringLiteral("archive");
            return ConflictDecision::UseTarget;
        }
    }

    // Secret protection.
    const bool sourceSecret = sourcePalm && sourcePalm->isSecret();
    const bool targetSecret = targetPalm && targetPalm->isSecret();
    if (baseDecision == ConflictDecision::UseBoth
        && conflict.type == ConflictType::BothModified
        && (sourceSecret != targetSecret)) {
        m_lastOverlay = QStringLiteral("secret");
        return sourceSecret ? ConflictDecision::UseSource
                            : ConflictDecision::UseTarget;
    }

    // Category tie-break.
    const bool timestampStrategy =
        policy.autoResolve == AutoResolveStrategy::NewerWins
        || policy.autoResolve == AutoResolveStrategy::OlderWins;
    if (timestampStrategy
        && conflict.type == ConflictType::BothModified
        && conflict.source.lastModified == conflict.target.lastModified
        && sourcePalm && targetPalm) {
        const bool sourceUnfiled = sourcePalm->category == 0;
        const bool targetUnfiled = targetPalm->category == 0;
        if (sourceUnfiled != targetUnfiled) {
            m_lastOverlay = QStringLiteral("category");
            return sourceUnfiled ? ConflictDecision::UseTarget
                                 : ConflictDecision::UseSource;
        }
    }

    return baseDecision;
}
```

- [ ] **Step 2: Append category-overlay tests.**

Append to `TestPalmConflictHandler`:

```cpp
    void categoryTieBreakFavoursNonUnfiledSide();
    void categoryTieBreakInactiveWhenTimestampsDiffer();
```

And slot implementations:

```cpp
void TestPalmConflictHandler::categoryTieBreakFavoursNonUnfiledSide()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord unfiled;
    unfiled.recordId = 51;
    unfiled.category = 0;
    unfiled.data = QByteArrayLiteral("u");
    unfiled.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
    dev.createRecord(QStringLiteral("MemoDB"), unfiled);

    PalmRecord categorised;
    categorised.recordId = 52;
    categorised.category = 3;
    categorised.data = QByteArrayLiteral("c");
    categorised.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
    dev.createRecord(QStringLiteral("MemoDB"), categorised);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 51),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 52),
        QDateTime::fromMSecsSinceEpoch(1000),
        QDateTime::fromMSecsSinceEpoch(1000));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    // Base NewerWins on equal timestamps returns UseTarget (upstream
    // ties go to target); overlay should flip to UseTarget (categorised
    // side) anyway — the assertion is that the categorised side wins.
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("category"));
}

void TestPalmConflictHandler::categoryTieBreakInactiveWhenTimestampsDiffer()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord unfiled;
    unfiled.recordId = 61;
    unfiled.category = 0;
    unfiled.data = QByteArrayLiteral("u");
    unfiled.lastModified = QDateTime::fromMSecsSinceEpoch(2000);
    dev.createRecord(QStringLiteral("MemoDB"), unfiled);

    PalmRecord categorised;
    categorised.recordId = 62;
    categorised.category = 3;
    categorised.data = QByteArrayLiteral("c");
    categorised.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
    dev.createRecord(QStringLiteral("MemoDB"), categorised);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 61),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 62),
        QDateTime::fromMSecsSinceEpoch(2000),
        QDateTime::fromMSecsSinceEpoch(1000));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    // Timestamps differ → NewerWins base decision (UseSource on
    // source-newer) stands. Category overlay inactive.
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QVERIFY(handler.lastOverlay().isEmpty());
}
```

- [ ] **Step 3: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_palmconflicthandler
ctest --test-dir build --output-on-failure -R tst_palmconflicthandler
```

Expected: 14 tests PASS.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/WildPalms
git add src/palm/conflict/palmconflicthandler.h \
        src/palm/conflict/palmconflicthandler.cpp \
        tests/palmconflict/tst_palmconflicthandler.cpp
git commit -m "$(cat <<'EOF'
feat(palm-conflict): category tie-break overlay

Phase E.5 task 5: third Palm overlay. When NewerWins/OlderWins
collapse on equal timestamps, the upstream policy returns UseTarget
by default — which can silently drop a user-assigned category slot
onto the "Unfiled" (category 0) side. Overlay checks both sides'
category field via the live device and prefers the non-zero-category
record.

applyOverlays() now takes the policy reference (so it can gate on
autoResolve strategy) — signature change is internal.

Two new tests (category tie-break active on equal timestamps,
inactive when timestamps differ) bring the handler suite to 14
passing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Keep-alive behaviour + sync-lifecycle hooks

**Files:**
- Modify: `tests/palmconflict/tst_palmconflicthandler.cpp`

The handler's `shouldKeepConnectionAlive()` was written in Task 2 but
not tested. This task covers it, plus the lifecycle hooks
(`onSyncStart` clears `m_pending` / `m_lastOverlay`).

- [ ] **Step 1: Append keep-alive and lifecycle tests.**

Append to `TestPalmConflictHandler`:

```cpp
    void keepAliveMatchesConfig();
    void nullConfigKeepsConnectionAlive();
    void onSyncStartClearsPendingAndOverlay();
```

And slot implementations:

```cpp
void TestPalmConflictHandler::keepAliveMatchesConfig()
{
    MockPalmDatabaseAccess dev;

    PalmBackendConfig keep;
    keep.connectionBehavior = ConnectionBehavior::KeepAlive;
    PalmConflictHandler hKeep(&dev, &keep);
    QVERIFY(hKeep.shouldKeepConnectionAlive());

    PalmBackendConfig disconnect;
    disconnect.connectionBehavior = ConnectionBehavior::DisconnectAndDefer;
    PalmConflictHandler hDisconnect(&dev, &disconnect);
    QVERIFY(!hDisconnect.shouldKeepConnectionAlive());

    PalmBackendConfig timeout;
    timeout.connectionBehavior = ConnectionBehavior::TimeoutThenDefer;
    PalmConflictHandler hTimeout(&dev, &timeout);
    QVERIFY(hTimeout.shouldKeepConnectionAlive());
}

void TestPalmConflictHandler::nullConfigKeepsConnectionAlive()
{
    MockPalmDatabaseAccess dev;
    PalmConflictHandler h(&dev, nullptr);
    QVERIFY(h.shouldKeepConnectionAlive());
}

void TestPalmConflictHandler::onSyncStartClearsPendingAndOverlay()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    // Accumulate a pending conflict via deferAll.
    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());
    handler.handleConflict(cr, ConflictPolicy::deferAll());
    QCOMPARE(handler.pendingConflicts().size(), 1);

    handler.onSyncStart();
    QVERIFY(handler.pendingConflicts().isEmpty());
    QVERIFY(handler.lastOverlay().isEmpty());
}
```

- [ ] **Step 2: Build + run.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" --target tst_palmconflicthandler
ctest --test-dir build --output-on-failure -R tst_palmconflicthandler
```

Expected: 17 tests PASS.

- [ ] **Step 3: Commit.**

```bash
cd ~/dev/WildPalms
git add tests/palmconflict/tst_palmconflicthandler.cpp
git commit -m "$(cat <<'EOF'
test(palm-conflict): keep-alive + sync-lifecycle coverage

Phase E.5 task 6: unit tests for PalmConflictHandler's
shouldKeepConnectionAlive() across the three ConnectionBehavior
values (KeepAlive → true, DisconnectAndDefer → false,
TimeoutThenDefer → true — runtime timer clips it), null-config
safety default (true), and the onSyncStart() hook clearing
pending conflicts plus the lastOverlay test accessor.

Handler suite now at 17 passing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: ConflictHandlerRegistry integration test

**Files:**
- Create: `tests/palmconflict/tst_palmconflicthandler_registration.cpp`
- Modify: `tests/palmconflict/CMakeLists.txt`

Proves the handler is registry-compatible: registering it under backend
id `"palm"` and looking it back up via `ConflictHandlerRegistry::handlerFor`
returns the same instance, and the registry's default fallback path
works when no per-backend handler is registered.

- [ ] **Step 1: Write the registration test.**

File `tests/palmconflict/tst_palmconflicthandler_registration.cpp`:

```cpp
#include <QtTest/QtTest>

#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"

#include "mockpalmdatabaseaccess.h"
#include "palmbackendconfig.h"
#include "palmconflicthandler.h"

using Kalburator::Sync::QSyncCore::ConflictHandler;
using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::PalmConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestPalmConflictHandlerRegistration : public QObject
{
    Q_OBJECT
private slots:
    void registersUnderPalmBackendId();
    void unregistrationClearsLookup();
    void defaultHandlerServesMissingBackendIds();
};

void TestPalmConflictHandlerRegistration::registersUnderPalmBackendId()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictHandlerRegistry registry;
    registry.registerHandler(QStringLiteral("palm"), &handler);

    QVERIFY(registry.hasHandler(QStringLiteral("palm")));
    QCOMPARE(registry.handlerFor(QStringLiteral("palm")),
             static_cast<ConflictHandler *>(&handler));
}

void TestPalmConflictHandlerRegistration::unregistrationClearsLookup()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictHandlerRegistry registry;
    registry.registerHandler(QStringLiteral("palm"), &handler);
    registry.unregisterHandler(QStringLiteral("palm"));

    QVERIFY(!registry.hasHandler(QStringLiteral("palm")));
    QCOMPARE(registry.handlerFor(QStringLiteral("palm")),
             static_cast<ConflictHandler *>(nullptr));
}

void TestPalmConflictHandlerRegistration::defaultHandlerServesMissingBackendIds()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler defaultHandler(&dev, &cfg);

    ConflictHandlerRegistry registry;
    registry.setDefaultHandler(&defaultHandler);

    // No "palm" registered; default takes over.
    QCOMPARE(registry.handlerFor(QStringLiteral("palm")),
             static_cast<ConflictHandler *>(&defaultHandler));
    QCOMPARE(registry.handlerFor(QStringLiteral("anything")),
             static_cast<ConflictHandler *>(&defaultHandler));
}

QTEST_MAIN(TestPalmConflictHandlerRegistration)
#include "tst_palmconflicthandler_registration.moc"
```

- [ ] **Step 2: Register the test.**

Append to `tests/palmconflict/CMakeLists.txt`:

```cmake
add_palm_conflict_test(tst_palmconflicthandler_registration
    tst_palmconflicthandler_registration.cpp)
```

- [ ] **Step 3: Build + test.**

```bash
cd ~/dev/WildPalms
cmake -S . -B build
cmake --build build -j"$(nproc)" --target tst_palmconflicthandler_registration
ctest --test-dir build --output-on-failure -R tst_palmconflicthandler_registration
```

Expected: 3 tests PASS.

- [ ] **Step 4: Full WP ctest.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. 26 total (23 pre-E.5 + tst_palmbackendconfig
+ tst_palmconflicthandler + tst_palmconflicthandler_registration).

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/WildPalms
git add tests/palmconflict/tst_palmconflicthandler_registration.cpp \
        tests/palmconflict/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(palm-conflict): ConflictHandlerRegistry integration

Phase E.5 task 7: PalmConflictHandler plays cleanly with
Kalburator::Sync::QSyncCore::ConflictHandlerRegistry — register
under "palm", hasHandler/handlerFor round-trip, unregistration
clears the lookup, and a PalmConflictHandler can also serve as the
registry's default handler for mixed-backend scenarios.

Full WP ctest at 27/27.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Mark E.5 done in the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`

- [ ] **Step 1: Update the sub-phases table.**

In the sub-phases table, change the E.5 row from:

```markdown
| **E.5** | `PalmConflictHandler` + `PalmBackendConfig` + `ConnectionBehavior`. Registers with `ConflictHandlerRegistry`. Unit tests with synthesized conflicts exercising archive/secret/category semantics. | WP | E.4 | WP ctest passes; handler can resolve each conflict shape per Palm semantics. |
```

To:

```markdown
| ✅ **E.5** | `PalmConflictHandler` + `PalmBackendConfig` + `ConnectionBehavior` landed at `src/palm/conflict/` in new static lib `WildPalmsPalmConflict`. Handler delegates to upstream `ConflictPolicy` for base resolution and applies three Palm overlays: archive-bit safety, secret-flag protection, category tie-break. Live device queries via borrowed `IPalmDatabaseAccess*`. Registers with `ConflictHandlerRegistry` under backend id `"palm"`. `CategoryMappingStore` defer to E.6; bit-preservation on apply-path defer to E.7. Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e5-palm-conflict-handler.md`. | WP | E.4 | WP ctest passes; 17 handler tests + 3 registration tests cover each overlay and registry integration. |
```

- [ ] **Step 2: Commit.**

```bash
cd ~/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "$(cat <<'EOF'
docs(phase-e): mark E.5 landed in spec sub-phases table

PalmConflictHandler + PalmBackendConfig landed 2026-04-21 in new
static lib WildPalmsPalmConflict at src/palm/conflict/. Three Palm
overlays (archive / secret / category) layer on top of upstream
ConflictPolicy. CategoryMappingStore and bit-preservation on
apply-path deferred to E.6 / E.7.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

**Spec coverage (Phase-E spec E.5 row):**
- "`PalmConflictHandler` + `PalmBackendConfig` + `ConnectionBehavior`" —
  Tasks 1 + 2. ✓
- "Registers with `ConflictHandlerRegistry`" — Task 7. ✓
- "Unit tests with synthesized conflicts exercising archive/secret/category
  semantics" — Tasks 3/4/5. ✓ (three overlays, 8 overlay-specific tests)
- "WP ctest passes" — Task 7 exit gate. ✓
- "handler can resolve each conflict shape per Palm semantics" — Tasks
  3/4/5 exhaustively drive each overlay. ✓

**Deferred explicitly:**
- `CategoryMappingStore` (E.6 when virtual sub-calendars land).
- Bit-preservation on `updateRecord()` write-back path (E.7 when typed
  adapters land and `palmbackend.cpp`'s `backendToPalm` gets extended).
- HotSync tickle thread (E.16 runtime wiring).
- UI prompting (E.9+ plugin ABI).
- App-layer construction / `SyncCoordinator` registration (E.16).

**Placeholder scan:** no TBD / TODO / FIXME in any task body. Each code
block is complete; only Task 1 Step 3's placeholder-file creation via
`cat > file <<EOF` is intentional and explicit.

**Type consistency:**
- `PalmBackendConfig` (namespace `WildPalms::PalmConflict`) and
  `ConnectionBehavior` enum: field names consistent across Tasks 1 + 2
  + 6 (`connectionBehavior`, `connectionTimeoutSeconds`,
  `hotSyncTickleIntervalSeconds`, `userName`).
- `PalmConflictHandler` method signatures: `handleConflict`,
  `canPrompt`, `shouldKeepConnectionAlive`, `pendingConflicts`,
  `onSyncStart`, `onSyncEnd`, `lastOverlay` — all match `ConflictHandler`
  base per libkalburator headers; `applyOverlays` signature evolves in
  Task 5 from `(ConflictRecord&, ConflictDecision)` to
  `(ConflictRecord&, const ConflictPolicy&, ConflictDecision)`, with
  the header/impl/call-site all updated in Task 5 Step 1.
- `PalmRecord::AttrArchived` / `AttrSecret` constants referenced in
  tests match `src/palm/sync/palmrecord.h:33-37`.
- `PalmBackend::encodeRecordId` / `decodeRecordId` signatures match
  `src/palm/sync/palmbackend.h:63-70` (verified).
- `Kalburator::Sync::QSyncCore::ConflictHandler` method overrides match
  upstream `libkalburator/src/conflict/conflictpolicy.h:147-203`
  (`handleConflict` signature, `canPrompt`, `shouldKeepConnectionAlive`,
  `pendingConflicts`, `onSyncStart`, `onSyncEnd`).
- `Kalburator::Sync::QSyncCore::ConflictHandlerRegistry` methods match
  `libkalburator/src/conflict/conflicthandlerregistry.h` (`registerHandler`,
  `unregisterHandler`, `hasHandler`, `handlerFor`, `setDefaultHandler`).

No gaps detected.

---

## Follow-up plans

After this plan lands:

- **E.6** — `PalmCalendarBackend : SyncBackend`. Virtual sub-calendars
  per Palm category slot. `CategoryMappingStore` lands here.
- **E.7** — Typed adapters for contacts / memos / todos. Extends
  `PalmBackend`'s `backendToPalm` / `palmToBackend` to preserve the
  `attributes` byte through the round-trip — at which point `E.5`'s
  conflict-handler decisions translate to real bit preservation on
  apply.
- **E.16** — App-layer wiring: app startup constructs a
  `PalmConflictHandler` with the Palm-side device + config and
  registers it on the coordinator's handler registry.
