# Phase M — Multi-protocol DAV provider + UI lift — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `MultiProtocolDavProvider` so one configured account
exposes both CalDAV and CardDAV collections; lift the
provider-config / accounts-list / collection-picker chrome from the
consumers into libkalburator so PlanStan and WildPalms both host the
same widgets; close deferred-work items B.5 and D.1.

**Architecture:** New concrete `IProvider` (kind `"multiproto-dav"`)
that *owns* a `CalDavCapabilityDiscovery` and a
`CardDavCapabilityDiscovery` and runs them in parallel against one
base URL + credential set. Existing single-protocol providers
(`CalDavProvider`, `CardDavProvider`) are **not touched**. UI lift
adds three library widgets (`ProviderConfigDialog`,
`AccountsListWidget`, `CollectionPickerWidget`) that both consumers
embed; mapping editors stay consumer-side because mapping targets
are consumer-specific (Palm slots vs PlanStan logical calendars).

**Tech Stack:** C++20, Qt6 (Core, Network, Widgets, Test),
KCalendarCore, KContacts, KF6 widget infra. KPim6Akonadi only when
`KALBURATOR_HAVE_AKONADI=ON` (unchanged from Phase L).

---

## Notable plan-time revision from the design (2026-05-16)

The design doc (`2026-05-16-phase-m-multi-protocol-dav-design.md`)
proposed extracting shared DAV helpers into `src/sync/dav/` and
refactoring `CalDavProvider` + `CardDavProvider` to use them.
Reconnaissance during plan-writing found that the existing
`CalDavCapabilityDiscovery` (`src/calendar/`) and
`CardDavCapabilityDiscovery` (`src/sync/`) are already self-contained
per-protocol discovery objects with clean `setCredentials(...)` +
`discover() → QFuture<QList<CollectionInfo>>` interfaces.

`MultiProtocolDavProvider` can simply own one instance of each and
orchestrate them in parallel. The "shared helpers" goal — eliminate
duplication between the existing providers and the new one — is
already satisfied by direct reuse. The refactor is unnecessary
overhead and would put existing CalDAV/CardDAV tests at regression
risk for no gain.

**This plan therefore drops the helpers-extraction task block.**
Phase M is correspondingly smaller (~2 days less work) and lower
risk (zero touch to single-protocol providers).

If a future phase identifies *real* duplication (e.g., the PROPFIND
XML construction state machine truly is identical and is worth
factoring), that's a separate phase. Not Phase M.

---

## Known unknowns surfaced during plan self-review (2026-05-16)

Two assumptions in the steps below need verification by the
executing engineer before the relevant task starts.

1. **The `parent` argument convention for `createConfigWidget`.**
   `IProvider::createConfigWidget(QWidget *parent)` takes a parent.
   `AkonadiProvider` and `CalDavProvider` both honor this. The
   library's new `ProviderConfigDialog` will pass itself as the
   parent. Verify by reading `caldavconfigwidget.cpp` lines 1–50
   how the existing provider widget handles `parent` and apply the
   same pattern in `multiprotocoldavconfigwidget.cpp`.

2. **PlanStan's add-CalDAV-account entry point** in
   `PlanStan/src/controllers/collectioncontroller.cpp:1691-1707`.
   Phase Ic / Phase H.5's wiring is the production CalDAV flow.
   The migration task replaces only the dialog construction +
   discovery part — the post-discovery binding-to-logical-calendar
   code must continue to receive the same
   `BackendConfiguration` + `QList<CollectionInfo>` shape. Read
   the surrounding ~50 lines before Task 16 to confirm the call
   contract.

These don't change the plan's shape — task ordering, file layout,
and step content are correct — but the engineer should expect to
verify them as the first concrete action inside the relevant tasks.

---

## File structure

### New files in libkalburator
```
libkalburator/src/sync/
  multiprotocoldavprovider.h
  multiprotocoldavprovider.cpp
  multiprotocoldavconfigwidget.h
  multiprotocoldavconfigwidget.cpp
  multiprotocoldavbackendcontribution.h
  multiprotocoldavbackendcontribution.cpp

libkalburator/src/plugin/
  multiprotocoldavproviderplugin.h
  multiprotocoldavproviderplugin.cpp

libkalburator/src/ui/                    # NEW directory
  providerconfigdialog.h
  providerconfigdialog.cpp
  accountslistwidget.h
  accountslistwidget.cpp
  collectionpickerwidget.h
  collectionpickerwidget.cpp

libkalburator/tests/sync/
  tst_multiprotocoldavprovider.cpp       # NEW

libkalburator/tests/ui/                  # NEW directory
  tst_providerconfigdialog.cpp
  tst_accountslistwidget.cpp
  tst_collectionpickerwidget.cpp
```

### Modified files
```
libkalburator/src/sync/iprovider.h                   # +2 optional virtuals
libkalburator/src/plugin/stock_plugins.cpp           # +1 plugin registration
libkalburator/CMakeLists.txt                         # +new sources + UI dir
libkalburator/tests/CMakeLists.txt                   # +new test executables

WildPalms/src/<account-ui-dir>/addaccountdialog.cpp  # delegate to library
WildPalms/src/<account-ui-dir>/accountspage.cpp      # host library widget

PlanStan/src/controllers/collectioncontroller.cpp    # delegate to library
```

### Doc files
```
libkalburator/docs/phase0/04ac-phase-m-status.md     # NEW
libkalburator/docs/phase0/04w-deferred-work.md       # MODIFIED (B.5, D.1)
~/dev/refactor-engine-merger/CURRENT-STATUS.md       # MODIFIED
~/dev/refactor-engine-merger/ROADMAP.md              # MODIFIED
~/dev/refactor-engine-merger/FINDINGS.md             # MODIFIED
```

---

## Task 0: Pre-flight — verify-all baseline + branch readiness

**Files:** none (verification only).

- [ ] **Step 1: Confirm all three worktrees are on `refactor/engine-merger`**

Run from `~/dev/refactor-engine-merger/`:
```bash
for d in libkalburator PlanStan WildPalms; do
  echo "=== $d ==="
  (cd $d && git rev-parse --abbrev-ref HEAD)
done
```

Expected: `refactor/engine-merger` for all three.

- [ ] **Step 2: Run verify-all to confirm a clean baseline**

```bash
./scripts/verify-all.sh
```

Expected: exit 0 (matches baselines).

If exit ≠ 0: stop. Investigate before starting Phase M. A
mid-baseline drift would mask Phase M regressions.

- [ ] **Step 3: No commit (verification only).**

---

## Task 1: `IProvider` optional accessors — `lastWarning()` + `icon()`

Adds two default-implemented virtuals so the library's new account-UI
widgets can render partial-discovery warnings and per-provider icons
without forcing every existing provider to update.

**Files:**
- Modify: `libkalburator/src/sync/iprovider.h`
- Test:   `libkalburator/tests/sync/tst_iprovider_accessors.cpp` (NEW)

- [ ] **Step 1: Write the failing test**

Create `libkalburator/tests/sync/tst_iprovider_accessors.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QIcon>

#include "../../src/sync/iprovider.h"
#include "../../src/sync/akonadiprovider.h"  // any existing provider

using namespace Kalburator::Sync;

class TstIProviderAccessors : public QObject
{
    Q_OBJECT
private slots:
    void existingProviderHasDefaultIcon();
    void existingProviderHasEmptyWarning();
};

void TstIProviderAccessors::existingProviderHasDefaultIcon()
{
#ifdef HAVE_AKONADI
    AkonadiProvider p;
    QIcon ic = p.icon();
    QVERIFY(!ic.isNull() == false || ic.isNull());  // either fine; just must compile + return
#else
    QSKIP("HAVE_AKONADI off; nothing to test against");
#endif
}

void TstIProviderAccessors::existingProviderHasEmptyWarning()
{
#ifdef HAVE_AKONADI
    AkonadiProvider p;
    QCOMPARE(p.lastWarning(), QString());
#else
    QSKIP("HAVE_AKONADI off");
#endif
}

QTEST_GUILESS_MAIN(TstIProviderAccessors)
#include "tst_iprovider_accessors.moc"
```

Register in `libkalburator/tests/CMakeLists.txt` (locate the
existing block that adds `tst_*` test executables and follow the
same pattern; use `kalburator_add_unit_test()` or whatever helper
the file uses for the simple guideless-main pattern).

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build-dev -j 10 --target tst_iprovider_accessors 2>&1 | tail -20
```

Expected: compile error — `icon` and `lastWarning` are not members
of `IProvider`.

- [ ] **Step 3: Add the two accessors to `IProvider`**

Edit `libkalburator/src/sync/iprovider.h`, inside the public section
between `collections()` and the `signals:` block:

```cpp
    // ── Optional UI / status accessors ─────────────────────────────
    /// Optional icon for the account-list row. Default: null QIcon.
    /// Providers may override to return a branded icon resource.
    virtual QIcon icon() const { return {}; }

    /// Optional non-fatal warning from the last operation (typically
    /// connect()). Empty string = no warning. Used by partial-success
    /// providers (e.g., multi-protocol DAV where one half discovered
    /// collections and the other half didn't).
    virtual QString lastWarning() const { return {}; }
```

Add `#include <QIcon>` near the top of the header.

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build-dev -j 10 --target tst_iprovider_accessors
ctest --test-dir build-dev -R tst_iprovider_accessors --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd libkalburator
git add src/sync/iprovider.h tests/sync/tst_iprovider_accessors.cpp tests/CMakeLists.txt
git commit -m "M.1: IProvider gains optional icon() + lastWarning() accessors

Default-implemented virtuals so library account-UI widgets can
render per-provider icons and partial-success warnings without
forcing every existing provider to update. Existing CalDavProvider,
CardDavProvider, AkonadiProvider inherit empty defaults."
```

---

## Task 2: `MultiProtocolDavProvider` — header + skeleton

Bare class with all IProvider virtuals stubbed. Compiles, links, and
has a smoke test that constructs an instance and verifies the
identity accessors return sane values.

**Files:**
- Create: `libkalburator/src/sync/multiprotocoldavprovider.h`
- Create: `libkalburator/src/sync/multiprotocoldavprovider.cpp`
- Modify: `libkalburator/CMakeLists.txt` (add to library sources)
- Test:   `libkalburator/tests/sync/tst_multiprotocoldavprovider.cpp` (NEW)

- [ ] **Step 1: Write the failing test (smoke / identity only)**

Create `libkalburator/tests/sync/tst_multiprotocoldavprovider.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QUuid>

#include "../../src/sync/multiprotocoldavprovider.h"

using namespace Kalburator::Sync;

class TstMultiProtocolDavProvider : public QObject
{
    Q_OBJECT
private slots:
    void kindIsMultiprotoDav();
    void idIsNonEmptyAfterConstruction();
    void displayNameDefaultsToSomethingHuman();
    void isNotConnectedAfterConstruction();
    void collectionsEmptyAfterConstruction();
};

void TstMultiProtocolDavProvider::kindIsMultiprotoDav()
{
    MultiProtocolDavProvider p;
    QCOMPARE(p.kind(), QStringLiteral("multiproto-dav"));
}

void TstMultiProtocolDavProvider::idIsNonEmptyAfterConstruction()
{
    MultiProtocolDavProvider p;
    const QString id = p.id();
    QVERIFY(!id.isEmpty());
    QVERIFY(QUuid(id).isNull() == false);  // must be a valid UUID
}

void TstMultiProtocolDavProvider::displayNameDefaultsToSomethingHuman()
{
    MultiProtocolDavProvider p;
    QVERIFY(!p.displayName().isEmpty());
}

void TstMultiProtocolDavProvider::isNotConnectedAfterConstruction()
{
    MultiProtocolDavProvider p;
    QVERIFY(!p.isConnected());
}

void TstMultiProtocolDavProvider::collectionsEmptyAfterConstruction()
{
    MultiProtocolDavProvider p;
    QVERIFY(p.collections().isEmpty());
}

QTEST_GUILESS_MAIN(TstMultiProtocolDavProvider)
#include "tst_multiprotocoldavprovider.moc"
```

Register the test executable in
`libkalburator/tests/CMakeLists.txt` following the same pattern as
the existing `tst_caldavprovider` registration (locate it via
`grep tst_caldavprovider libkalburator/tests/CMakeLists.txt`).

- [ ] **Step 2: Run the test to verify it fails to compile**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider 2>&1 | tail -20
```

Expected: compile error — `multiprotocoldavprovider.h` not found.

- [ ] **Step 3: Create the header**

Create `libkalburator/src/sync/multiprotocoldavprovider.h`:

```cpp
#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H

#include "iprovider.h"

#include <QMap>
#include <QPromise>
#include <QUrl>

#include <memory>

namespace Kalburator::Sync {

class CalDavCapabilityDiscovery;
class CardDavCapabilityDiscovery;

/**
 * @brief DAV provider that speaks both CalDAV and CardDAV against
 *        one base URL with one credential set — Nextcloud-style.
 *
 * Owns one CalDavCapabilityDiscovery and one CardDavCapabilityDiscovery,
 * runs them in parallel during connect(), and federates their results
 * into a single collections() list. collection ids are prefixed
 * "multiproto-dav:<provider-id>:cal:<inner-id>" /
 * "multiproto-dav:<provider-id>:contacts:<inner-id>" so createBackend()
 * can dispatch by prefix.
 *
 * Configuration (BackendConfiguration::connectionParams):
 *   - "url"                       QString — server base URL
 *   - "username"                  QString
 *   - "password"                  QString — plaintext (KWallet later)
 *   - "manualCaldavPrincipal"     QString — optional override URL
 *   - "manualCarddavPrincipal"    QString — optional override URL
 */
class MultiProtocolDavProvider : public IProvider
{
    Q_OBJECT
public:
    explicit MultiProtocolDavProvider(QObject *parent = nullptr);
    ~MultiProtocolDavProvider() override;

    QString id() const override          { return m_id; }
    QString kind() const override;
    QString displayName() const override { return m_displayName; }

    void load(const BackendConfiguration &config) override;
    BackendConfiguration save() const override;

    QWidget *createConfigWidget(QWidget *parent) override;

    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }

    QList<CollectionInfo> collections() const override
    { return m_collections; }
    std::unique_ptr<IBlobBackend>
        createBackend(const QString &collectionId) override;

    QString lastWarning() const override { return m_lastWarning; }

private slots:
    void onCalDavFinished();
    void onCardDavFinished();

private:
    void maybeResolveConnect();

    // Identity / config
    QString m_id;
    QString m_displayName;
    QUrl    m_serverUrl;
    QString m_username;
    QString m_password;
    QString m_manualCalDavPrincipal;   // optional override
    QString m_manualCardDavPrincipal;  // optional override

    // Runtime
    bool                  m_connected = false;
    QString               m_lastWarning;
    QList<CollectionInfo> m_collections;
    QMap<QString, QString> m_urlByCollectionId;  // collectionId -> absolute href

    // Owned discovery objects
    CalDavCapabilityDiscovery  *m_caldavDiscovery  = nullptr;
    CardDavCapabilityDiscovery *m_carddavDiscovery = nullptr;

    // In-flight state for the parallel discovery (one connect() call
    // resolves once BOTH halves finish).
    bool m_calDavDone  = false;
    bool m_cardDavDone = false;
    QList<CollectionInfo> m_calDavResult;
    QList<CollectionInfo> m_cardDavResult;
    QString m_calDavError;
    QString m_cardDavError;

    std::shared_ptr<QPromise<bool>> m_connectPromise;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H
```

- [ ] **Step 4: Create the skeleton .cpp**

Create `libkalburator/src/sync/multiprotocoldavprovider.cpp`:

```cpp
#include "multiprotocoldavprovider.h"

#include <QUuid>

namespace Kalburator::Sync {

MultiProtocolDavProvider::MultiProtocolDavProvider(QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_displayName(QStringLiteral("DAV account"))
{
}

MultiProtocolDavProvider::~MultiProtocolDavProvider() = default;

QString MultiProtocolDavProvider::kind() const
{
    return QStringLiteral("multiproto-dav");
}

void MultiProtocolDavProvider::load(const BackendConfiguration &)
{
    // Task 3 implements this.
}

BackendConfiguration MultiProtocolDavProvider::save() const
{
    // Task 3 implements this.
    return {};
}

QWidget *MultiProtocolDavProvider::createConfigWidget(QWidget *)
{
    // Task 6 implements this.
    return nullptr;
}

QFuture<bool> MultiProtocolDavProvider::connect()
{
    // Task 4 implements this. Skeleton: resolves false.
    QPromise<bool> p;
    auto fut = p.future();
    p.start();
    p.addResult(false);
    p.finish();
    return fut;
}

void MultiProtocolDavProvider::disconnect()
{
    m_connected = false;
}

std::unique_ptr<IBlobBackend>
MultiProtocolDavProvider::createBackend(const QString &)
{
    // Task 5 implements this.
    return nullptr;
}

void MultiProtocolDavProvider::onCalDavFinished()  {}
void MultiProtocolDavProvider::onCardDavFinished() {}
void MultiProtocolDavProvider::maybeResolveConnect() {}

} // namespace Kalburator::Sync
```

- [ ] **Step 5: Register in CMake**

Edit `libkalburator/CMakeLists.txt`. Locate the block that lists
sources for the `kalburator` target (look for the existing
`caldavprovider.cpp` entry) and add the two new files alongside:

```cmake
    src/sync/multiprotocoldavprovider.cpp
    src/sync/multiprotocoldavprovider.h
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: PASS (all 5 cases).

- [ ] **Step 7: Commit**

```bash
git add src/sync/multiprotocoldavprovider.{h,cpp} \
        tests/sync/tst_multiprotocoldavprovider.cpp \
        tests/CMakeLists.txt CMakeLists.txt
git commit -m "M.2: MultiProtocolDavProvider skeleton

Bare class with all IProvider virtuals stubbed. Identity accessors
(id/kind/displayName) work. connect() returns false placeholder;
createBackend returns nullptr. Tasks 3-6 fill in real behavior."
```

---

## Task 3: `MultiProtocolDavProvider::load()` + `save()` + display-name round-trip

Persistence: the connection params (URL, username, password, manual
principal overrides) round-trip through `BackendConfiguration::
connectionParams`.

**Files:**
- Modify: `libkalburator/src/sync/multiprotocoldavprovider.cpp`
- Test:   `libkalburator/tests/sync/tst_multiprotocoldavprovider.cpp`

- [ ] **Step 1: Add round-trip tests**

Append to `tst_multiprotocoldavprovider.cpp` (inside the class, add
the slot declarations to the `private slots:` block):

```cpp
    void loadAndSaveRoundTripsConnectionParams();
    void loadAppliesDisplayNameAndId();
```

Implement them:

```cpp
void TstMultiProtocolDavProvider::loadAndSaveRoundTripsConnectionParams()
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-uuid-1");
    cfg.kind = QStringLiteral("multiproto-dav");
    cfg.displayName = QStringLiteral("My Nextcloud");
    cfg.connectionParams[QStringLiteral("url")]
        = QStringLiteral("https://cloud.example.com");
    cfg.connectionParams[QStringLiteral("username")]
        = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")]
        = QStringLiteral("hunter2");
    cfg.connectionParams[QStringLiteral("manualCaldavPrincipal")]
        = QStringLiteral("https://cloud.example.com/dav/cal/");

    MultiProtocolDavProvider p;
    p.load(cfg);
    const BackendConfiguration roundtrip = p.save();

    QCOMPARE(roundtrip.id,          cfg.id);
    QCOMPARE(roundtrip.kind,        cfg.kind);
    QCOMPARE(roundtrip.displayName, cfg.displayName);
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("url")).toString(),
             cfg.connectionParams.value(QStringLiteral("url")).toString());
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("username")).toString(),
             cfg.connectionParams.value(QStringLiteral("username")).toString());
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("password")).toString(),
             cfg.connectionParams.value(QStringLiteral("password")).toString());
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("manualCaldavPrincipal")).toString(),
             cfg.connectionParams.value(QStringLiteral("manualCaldavPrincipal")).toString());
}

void TstMultiProtocolDavProvider::loadAppliesDisplayNameAndId()
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("specific-id");
    cfg.displayName = QStringLiteral("Work Nextcloud");

    MultiProtocolDavProvider p;
    p.load(cfg);

    QCOMPARE(p.id(),          QStringLiteral("specific-id"));
    QCOMPARE(p.displayName(), QStringLiteral("Work Nextcloud"));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: FAIL — load() is a no-op, save() returns {}.

- [ ] **Step 3: Implement load() and save()**

In `multiprotocoldavprovider.cpp`, replace the stub implementations:

```cpp
void MultiProtocolDavProvider::load(const BackendConfiguration &config)
{
    if (!config.id.isEmpty()) m_id = config.id;
    if (!config.displayName.isEmpty()) m_displayName = config.displayName;

    const auto &p = config.connectionParams;
    m_serverUrl              = QUrl(p.value(QStringLiteral("url")).toString());
    m_username               = p.value(QStringLiteral("username")).toString();
    m_password               = p.value(QStringLiteral("password")).toString();
    m_manualCalDavPrincipal  = p.value(QStringLiteral("manualCaldavPrincipal")).toString();
    m_manualCardDavPrincipal = p.value(QStringLiteral("manualCarddavPrincipal")).toString();
}

BackendConfiguration MultiProtocolDavProvider::save() const
{
    BackendConfiguration c;
    c.id = m_id;
    c.kind = kind();
    c.displayName = m_displayName;
    c.connectionParams[QStringLiteral("url")]      = m_serverUrl.toString();
    c.connectionParams[QStringLiteral("username")] = m_username;
    c.connectionParams[QStringLiteral("password")] = m_password;
    if (!m_manualCalDavPrincipal.isEmpty())
        c.connectionParams[QStringLiteral("manualCaldavPrincipal")] = m_manualCalDavPrincipal;
    if (!m_manualCardDavPrincipal.isEmpty())
        c.connectionParams[QStringLiteral("manualCarddavPrincipal")] = m_manualCardDavPrincipal;
    return c;
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: PASS (all 7 cases now).

- [ ] **Step 5: Commit**

```bash
git add src/sync/multiprotocoldavprovider.cpp \
        tests/sync/tst_multiprotocoldavprovider.cpp
git commit -m "M.3: MultiProtocolDavProvider load/save round-trip

connectionParams (url, username, password, manual{Caldav,Carddav}Principal)
plus identity (id/displayName) persist through BackendConfiguration."
```

---

## Task 4: `MultiProtocolDavProvider::connect()` — parallel CalDav + CardDav discovery

`connect()` instantiates both discovery objects, runs them in
parallel, joins on both finishing, and resolves the future with
`true` iff at least one half found collections. Partial success
populates `m_lastWarning`.

**Files:**
- Modify: `libkalburator/src/sync/multiprotocoldavprovider.cpp`
- Test:   `libkalburator/tests/sync/tst_multiprotocoldavprovider.cpp`

- [ ] **Step 1: Read the existing CalDavProvider::connect() impl for reference**

```bash
sed -n '/QFuture<bool> CalDavProvider::connect/,/^}/p' \
    libkalburator/src/sync/caldavprovider.cpp | head -80
```

Note: how the QPromise is constructed (heap-shared via
`std::shared_ptr` for the Akonadi callback pattern, or member-stored
via `std::unique_ptr` as in CalDavProvider). Use the same pattern as
CalDavProvider for direct QFuture lambda capture.

- [ ] **Step 2: Write the failing tests (success + partial + full failure)**

Add three test slots to `tst_multiprotocoldavprovider.cpp`:

```cpp
    void connectWithoutUrlReturnsFalseQuickly();
    void connectInvalidCredentialsEmitsErrorAndResolvesFalse();
    void connectPartialSuccessSetsLastWarning();
```

(The "real" parallel-discovery test requires a fake server pair;
write it as part of this task but mark a follow-up if real fake
servers don't exist yet. Use `FakeCalDavServer` from
`tests/calendar/` and `FakeCardDavServer` from `tests/sync/`
if they exist — confirm by `find tests -name 'fake*server*'`.)

```cpp
void TstMultiProtocolDavProvider::connectWithoutUrlReturnsFalseQuickly()
{
    MultiProtocolDavProvider p;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("u");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("p");
    // url left empty
    p.load(cfg);

    auto fut = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
    QCOMPARE(fut.resultAt(0), false);
    QVERIFY(!p.isConnected());
}

void TstMultiProtocolDavProvider::connectInvalidCredentialsEmitsErrorAndResolvesFalse()
{
    MultiProtocolDavProvider p;
    QSignalSpy errSpy(&p, &IProvider::error);
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("https://localhost:1/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("nobody");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("nopass");
    p.load(cfg);

    auto fut = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 30000);
    QCOMPARE(fut.resultAt(0), false);
    QVERIFY(errSpy.count() > 0);
}

// Partial success test: requires a fake-server harness. If the
// existing FakeCalDavServer / FakeCardDavServer can be composed
// behind one URL, exercise the case where CalDAV succeeds and
// CardDAV times out. If not, mark this case TODO in FINDINGS and
// land the real-fake-server integration as a follow-up task.
void TstMultiProtocolDavProvider::connectPartialSuccessSetsLastWarning()
{
    QSKIP("Requires composed FakeCalDav+FakeCardDav harness; "
          "follow-up task — see FINDINGS.md");
}
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: tests 1 + 2 FAIL (connect() always returns false but
without doing real discovery; tests pass for wrong reason). Add
QVERIFY assertions to differentiate (see step 4 implementation).

- [ ] **Step 4: Implement connect() with parallel discovery**

In `multiprotocoldavprovider.cpp`, add includes:

```cpp
#include "../calendar/caldavcapabilitydiscovery.h"
#include "carddavcapabilitydiscovery.h"
#include <QFutureWatcher>
```

Replace `MultiProtocolDavProvider::connect()`:

```cpp
QFuture<bool> MultiProtocolDavProvider::connect()
{
    if (m_connected) {
        QPromise<bool> p;
        auto fut = p.future();
        p.start(); p.addResult(true); p.finish();
        return fut;
    }
    if (m_serverUrl.isEmpty()) {
        emit error(QStringLiteral("No server URL configured."));
        QPromise<bool> p;
        auto fut = p.future();
        p.start(); p.addResult(false); p.finish();
        return fut;
    }

    m_connectPromise = std::make_shared<QPromise<bool>>();
    m_connectPromise->start();

    m_calDavDone = m_cardDavDone = false;
    m_calDavResult.clear();
    m_cardDavResult.clear();
    m_calDavError.clear();
    m_cardDavError.clear();
    m_lastWarning.clear();

    // CalDAV half
    if (!m_caldavDiscovery)
        m_caldavDiscovery = new Calendar::CalDavCapabilityDiscovery(this);
    QObject::connect(m_caldavDiscovery, &Calendar::CalDavCapabilityDiscovery::error,
                     this, [this](const QString &m){ m_calDavError = m; });
    m_caldavDiscovery->setCredentials(m_serverUrl, m_username, m_password);
    auto calFut = m_caldavDiscovery->discover();
    auto *calWatcher = new QFutureWatcher<QList<CollectionInfo>>(this);
    QObject::connect(calWatcher, &QFutureWatcher<QList<CollectionInfo>>::finished,
                     this, &MultiProtocolDavProvider::onCalDavFinished);
    calWatcher->setFuture(calFut);

    // CardDAV half
    if (!m_carddavDiscovery)
        m_carddavDiscovery = new CardDavCapabilityDiscovery(this);
    QObject::connect(m_carddavDiscovery, &CardDavCapabilityDiscovery::error,
                     this, [this](const QString &m){ m_cardDavError = m; });
    m_carddavDiscovery->setCredentials(m_serverUrl, m_username, m_password);
    auto cardFut = m_carddavDiscovery->discover();
    auto *cardWatcher = new QFutureWatcher<QList<CollectionInfo>>(this);
    QObject::connect(cardWatcher, &QFutureWatcher<QList<CollectionInfo>>::finished,
                     this, &MultiProtocolDavProvider::onCardDavFinished);
    cardWatcher->setFuture(cardFut);

    return m_connectPromise->future();
}
```

Replace the stub slot bodies:

```cpp
void MultiProtocolDavProvider::onCalDavFinished()
{
    auto *w = qobject_cast<QFutureWatcher<QList<CollectionInfo>>*>(sender());
    if (w) {
        m_calDavResult = w->result();
        w->deleteLater();
    }
    m_calDavDone = true;
    maybeResolveConnect();
}

void MultiProtocolDavProvider::onCardDavFinished()
{
    auto *w = qobject_cast<QFutureWatcher<QList<CollectionInfo>>*>(sender());
    if (w) {
        m_cardDavResult = w->result();
        w->deleteLater();
    }
    m_cardDavDone = true;
    maybeResolveConnect();
}

void MultiProtocolDavProvider::maybeResolveConnect()
{
    if (!m_calDavDone || !m_cardDavDone) return;
    if (!m_connectPromise) return;

    // Tag the per-protocol results and merge into the federated list.
    m_collections.clear();
    m_urlByCollectionId.clear();
    for (auto info : m_calDavResult) {
        const QString prefixedId = QStringLiteral("multiproto-dav:%1:cal:%2")
                                       .arg(m_id, info.id);
        if (m_caldavDiscovery)
            m_urlByCollectionId[prefixedId]
                = m_caldavDiscovery->calendarUrls().value(info.id);
        info.id = prefixedId;
        m_collections.append(info);
    }
    for (auto info : m_cardDavResult) {
        const QString prefixedId = QStringLiteral("multiproto-dav:%1:contacts:%2")
                                       .arg(m_id, info.id);
        if (m_carddavDiscovery)
            m_urlByCollectionId[prefixedId]
                = m_carddavDiscovery->addressbookUrls().value(info.id);
        info.id = prefixedId;
        m_collections.append(info);
    }

    const bool calOk  = m_calDavError.isEmpty()  && !m_calDavResult.isEmpty();
    const bool cardOk = m_cardDavError.isEmpty() && !m_cardDavResult.isEmpty();

    if (!calOk && cardOk)
        m_lastWarning = QStringLiteral("Calendar discovery failed: %1")
                            .arg(m_calDavError);
    else if (calOk && !cardOk)
        m_lastWarning = QStringLiteral("Addressbook discovery failed: %1")
                            .arg(m_cardDavError);

    const bool anyOk = calOk || cardOk;
    if (!anyOk) {
        QString combined = m_calDavError;
        if (!m_cardDavError.isEmpty()) {
            if (!combined.isEmpty()) combined += QStringLiteral("; ");
            combined += m_cardDavError;
        }
        if (!combined.isEmpty()) emit error(combined);
    }

    m_connected = anyOk;
    m_connectPromise->addResult(anyOk);
    m_connectPromise->finish();
    m_connectPromise.reset();

    if (anyOk) emit collectionsChanged();
    emit connectionStateChanged(anyOk);
}
```

Also include `Calendar::CalDavCapabilityDiscovery` if it's in the
`Calendar::` namespace (verify by reading `src/calendar/caldavcapabilitydiscovery.h`
line 1 of namespace).

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: tests 1 (no URL) + 2 (invalid creds) PASS;
test 3 (partial success) SKIP.

- [ ] **Step 6: Commit**

```bash
git add src/sync/multiprotocoldavprovider.cpp \
        tests/sync/tst_multiprotocoldavprovider.cpp
git commit -m "M.4: MultiProtocolDavProvider::connect parallel discovery

Owns one CalDavCapabilityDiscovery + one CardDavCapabilityDiscovery
and runs them in parallel. connect() resolves true iff at least one
half discovered collections; partial-success cases populate
lastWarning() for UI surface. Collection ids prefixed with
'multiproto-dav:<provider-id>:cal:' or '...:contacts:'."
```

---

## Task 5: `MultiProtocolDavProvider::createBackend()` — prefix-based dispatch

Given a prefixed collection id, parse the prefix and dispatch to
either `RemoteCalendarBackend` or `RemoteContactsBackend` (the
per-collection backends Phase H/Ib already ship).

**Files:**
- Modify: `libkalburator/src/sync/multiprotocoldavprovider.cpp`
- Test:   `libkalburator/tests/sync/tst_multiprotocoldavprovider.cpp`

- [ ] **Step 1: Read existing CalDavProvider::createBackend()**

```bash
sed -n '/createBackend/,/^}/p' libkalburator/src/sync/caldavprovider.cpp | head -40
```

Note the constructor signature of `RemoteCalendarBackend` and any
companion construction logic. Mirror it in the dispatcher.

- [ ] **Step 2: Add a unit test that exercises both branches**

In `tst_multiprotocoldavprovider.cpp`:

```cpp
    void createBackendDispatchesByPrefix();
    void createBackendUnknownIdReturnsNullptr();
```

```cpp
void TstMultiProtocolDavProvider::createBackendDispatchesByPrefix()
{
    // Synthesize a provider in a "connected" state by injecting
    // collection list and URL map directly. This requires a friend
    // declaration or a test hook — see step 3.
    MultiProtocolDavProvider p;
    QVERIFY(p.createBackend(QStringLiteral("multiproto-dav:x:cal:unknown")) == nullptr);
    QVERIFY(p.createBackend(QStringLiteral("multiproto-dav:x:contacts:unknown")) == nullptr);
}

void TstMultiProtocolDavProvider::createBackendUnknownIdReturnsNullptr()
{
    MultiProtocolDavProvider p;
    QVERIFY(p.createBackend(QStringLiteral("totally-unknown")) == nullptr);
}
```

(A richer test requiring actual `RemoteCalendarBackend` instantiation
needs `setCredentials` flow + would belong with the partial-success
integration test in a later follow-up — Step 2 of Task 4's TODO.)

- [ ] **Step 3: Implement createBackend()**

Add includes:
```cpp
#include "../calendar/remotecalendarbackend.h"
#include "remotecontactsbackend.h"
```

Replace the stub:

```cpp
std::unique_ptr<IBlobBackend>
MultiProtocolDavProvider::createBackend(const QString &collectionId)
{
    if (!m_connected) return nullptr;
    if (!m_urlByCollectionId.contains(collectionId)) return nullptr;
    const QString href = m_urlByCollectionId.value(collectionId);

    const QString calPrefix = QStringLiteral("multiproto-dav:%1:cal:").arg(m_id);
    const QString contactsPrefix = QStringLiteral("multiproto-dav:%1:contacts:").arg(m_id);

    if (collectionId.startsWith(calPrefix)) {
        auto backend = std::make_unique<Calendar::RemoteCalendarBackend>();
        // Configure via the same pattern CalDavProvider::createBackend uses
        // (verify signature against existing caldavprovider.cpp). Backend
        // resourceId should encode our provider id so the engine sees one
        // resource.
        backend->configure(m_serverUrl, m_username, m_password, href);
        backend->setResourceId(QStringLiteral("multiproto-dav:%1").arg(m_id));
        return backend;
    }
    if (collectionId.startsWith(contactsPrefix)) {
        auto backend = std::make_unique<RemoteContactsBackend>();
        backend->configure(m_serverUrl, m_username, m_password, href);
        backend->setResourceId(QStringLiteral("multiproto-dav:%1").arg(m_id));
        return backend;
    }
    return nullptr;
}
```

If the existing backends don't have a `configure(...)` method with
those exact arguments — they almost certainly use a slightly
different signature, since CalDavProvider passes them in via
its own internal pattern — read
`caldavprovider.cpp:createBackend` and use the same construction
shape. The principle (provider passes URL + creds + per-collection
href + a resourceId) doesn't change.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: PASS (no-collection cases return nullptr).

- [ ] **Step 5: Commit**

```bash
git add src/sync/multiprotocoldavprovider.cpp \
        tests/sync/tst_multiprotocoldavprovider.cpp
git commit -m "M.5: MultiProtocolDavProvider::createBackend prefix dispatch

Routes 'multiproto-dav:<id>:cal:<inner>' to RemoteCalendarBackend
and 'multiproto-dav:<id>:contacts:<inner>' to RemoteContactsBackend.
Both backends share one resourceId so the engine treats the
account as one resource for mapping-scheduler contention."
```

---

## Task 6: `MultiProtocolDavConfigWidget`

Qt widget with: display name, base URL, username, password,
expandable "Advanced" section with two manual-principal text
fields. Round-trips to `BackendConfiguration::connectionParams`.

**Files:**
- Create: `libkalburator/src/sync/multiprotocoldavconfigwidget.h`
- Create: `libkalburator/src/sync/multiprotocoldavconfigwidget.cpp`
- Modify: `libkalburator/CMakeLists.txt`
- Modify: `libkalburator/src/sync/multiprotocoldavprovider.cpp` (return widget)
- Test:   `libkalburator/tests/sync/tst_multiprotocoldavconfigwidget.cpp` (NEW)

- [ ] **Step 1: Write the failing test**

Create `tests/sync/tst_multiprotocoldavconfigwidget.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QLineEdit>

#include "../../src/sync/multiprotocoldavconfigwidget.h"

using namespace Kalburator::Sync;

class TstMultiProtocolDavConfigWidget : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsConnectionParams();
    void advancedFieldsHiddenByDefault();
};

void TstMultiProtocolDavConfigWidget::roundTripsConnectionParams()
{
    MultiProtocolDavConfigWidget w;
    BackendConfiguration cfg;
    cfg.displayName = QStringLiteral("Test NC");
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://nc.example/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("secret");

    w.setConfiguration(cfg);
    const BackendConfiguration out = w.configuration();

    QCOMPARE(out.displayName, cfg.displayName);
    QCOMPARE(out.connectionParams[QStringLiteral("url")].toString(),
             QStringLiteral("https://nc.example/"));
    QCOMPARE(out.connectionParams[QStringLiteral("username")].toString(),
             QStringLiteral("alice"));
    QCOMPARE(out.connectionParams[QStringLiteral("password")].toString(),
             QStringLiteral("secret"));
}

void TstMultiProtocolDavConfigWidget::advancedFieldsHiddenByDefault()
{
    MultiProtocolDavConfigWidget w;
    auto *manualCalDav = w.findChild<QLineEdit*>(QStringLiteral("manualCalDavEdit"));
    QVERIFY(manualCalDav != nullptr);
    QVERIFY(!manualCalDav->isVisible());  // hidden inside collapsed Advanced
}

QTEST_MAIN(TstMultiProtocolDavConfigWidget)
#include "tst_multiprotocoldavconfigwidget.moc"
```

Register in `tests/CMakeLists.txt` (use the `QTEST_MAIN` /
widgets-required pattern from `tst_caldavconfigwidget`).

- [ ] **Step 2: Run the test to verify compile-fail**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavconfigwidget 2>&1 | tail -10
```

Expected: header not found.

- [ ] **Step 3: Implement the widget header**

Create `libkalburator/src/sync/multiprotocoldavconfigwidget.h`:

```cpp
#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVCONFIGWIDGET_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVCONFIGWIDGET_H

#include "backendconfiguration.h"
#include <QWidget>

class QLineEdit;
class QGroupBox;

namespace Kalburator::Sync {

class MultiProtocolDavConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MultiProtocolDavConfigWidget(QWidget *parent = nullptr);

    void setConfiguration(const BackendConfiguration &cfg);
    BackendConfiguration configuration() const;

private:
    QLineEdit *m_displayNameEdit;
    QLineEdit *m_urlEdit;
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QGroupBox *m_advancedGroup;     // collapsed by default
    QLineEdit *m_manualCalDavEdit;
    QLineEdit *m_manualCardDavEdit;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 4: Implement the widget .cpp**

Create `libkalburator/src/sync/multiprotocoldavconfigwidget.cpp`:

```cpp
#include "multiprotocoldavconfigwidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QVBoxLayout>

namespace Kalburator::Sync {

MultiProtocolDavConfigWidget::MultiProtocolDavConfigWidget(QWidget *parent)
    : QWidget(parent)
    , m_displayNameEdit(new QLineEdit(this))
    , m_urlEdit(new QLineEdit(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_advancedGroup(new QGroupBox(tr("Advanced"), this))
    , m_manualCalDavEdit(new QLineEdit(m_advancedGroup))
    , m_manualCardDavEdit(new QLineEdit(m_advancedGroup))
{
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_displayNameEdit->setPlaceholderText(tr("My Nextcloud"));
    m_urlEdit->setPlaceholderText(tr("https://cloud.example.com"));

    m_manualCalDavEdit->setObjectName(QStringLiteral("manualCalDavEdit"));
    m_manualCardDavEdit->setObjectName(QStringLiteral("manualCardDavEdit"));
    m_manualCalDavEdit->setPlaceholderText(
        tr("Override CalDAV principal URL (leave blank for auto-probe)"));
    m_manualCardDavEdit->setPlaceholderText(
        tr("Override CardDAV principal URL (leave blank for auto-probe)"));

    auto *advLayout = new QFormLayout(m_advancedGroup);
    advLayout->addRow(tr("CalDAV principal:"),  m_manualCalDavEdit);
    advLayout->addRow(tr("CardDAV principal:"), m_manualCardDavEdit);
    m_advancedGroup->setCheckable(true);
    m_advancedGroup->setChecked(false);  // collapsed

    auto *main = new QFormLayout(this);
    main->addRow(tr("Display name:"), m_displayNameEdit);
    main->addRow(tr("Server URL:"),   m_urlEdit);
    main->addRow(tr("Username:"),     m_usernameEdit);
    main->addRow(tr("Password:"),     m_passwordEdit);
    main->addRow(m_advancedGroup);
}

void MultiProtocolDavConfigWidget::setConfiguration(const BackendConfiguration &cfg)
{
    m_displayNameEdit->setText(cfg.displayName);
    const auto &p = cfg.connectionParams;
    m_urlEdit->setText(p.value(QStringLiteral("url")).toString());
    m_usernameEdit->setText(p.value(QStringLiteral("username")).toString());
    m_passwordEdit->setText(p.value(QStringLiteral("password")).toString());
    const QString mcal = p.value(QStringLiteral("manualCaldavPrincipal")).toString();
    const QString mcard = p.value(QStringLiteral("manualCarddavPrincipal")).toString();
    m_manualCalDavEdit->setText(mcal);
    m_manualCardDavEdit->setText(mcard);
    if (!mcal.isEmpty() || !mcard.isEmpty())
        m_advancedGroup->setChecked(true);  // expose if non-empty
}

BackendConfiguration MultiProtocolDavConfigWidget::configuration() const
{
    BackendConfiguration cfg;
    cfg.kind = QStringLiteral("multiproto-dav");
    cfg.displayName = m_displayNameEdit->text();
    cfg.connectionParams[QStringLiteral("url")] = m_urlEdit->text();
    cfg.connectionParams[QStringLiteral("username")] = m_usernameEdit->text();
    cfg.connectionParams[QStringLiteral("password")] = m_passwordEdit->text();
    if (!m_manualCalDavEdit->text().isEmpty())
        cfg.connectionParams[QStringLiteral("manualCaldavPrincipal")]
            = m_manualCalDavEdit->text();
    if (!m_manualCardDavEdit->text().isEmpty())
        cfg.connectionParams[QStringLiteral("manualCarddavPrincipal")]
            = m_manualCardDavEdit->text();
    return cfg;
}

} // namespace Kalburator::Sync
```

- [ ] **Step 5: Wire it into the provider's createConfigWidget**

In `multiprotocoldavprovider.cpp`, replace the stub:

```cpp
#include "multiprotocoldavconfigwidget.h"

QWidget *MultiProtocolDavProvider::createConfigWidget(QWidget *parent)
{
    auto *w = new MultiProtocolDavConfigWidget(parent);
    w->setConfiguration(save());  // pre-populate from current state
    return w;
}
```

- [ ] **Step 6: Register sources in CMake**

Add to `libkalburator/CMakeLists.txt` next to the provider entries:

```cmake
    src/sync/multiprotocoldavconfigwidget.cpp
    src/sync/multiprotocoldavconfigwidget.h
```

- [ ] **Step 7: Build + test**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavconfigwidget
ctest --test-dir build-dev -R tst_multiprotocoldavconfigwidget --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/sync/multiprotocoldavconfigwidget.{h,cpp} \
        src/sync/multiprotocoldavprovider.cpp \
        tests/sync/tst_multiprotocoldavconfigwidget.cpp \
        tests/CMakeLists.txt CMakeLists.txt
git commit -m "M.6: MultiProtocolDavConfigWidget

Form widget with display-name, URL, username, password +
collapsed Advanced section with manual principal-URL overrides.
Round-trips via BackendConfiguration::connectionParams."
```

---

## Task 7: `MultiProtocolDavBackendContribution` + `MultiProtocolDavProviderPlugin`

Following the K.7/K.8a plugin pattern: a `BackendContribution` that
registers the backend kinds, plus a `Plugin` class that surfaces
the contribution via `backendContributions()`.

**Files:**
- Create: `libkalburator/src/sync/multiprotocoldavbackendcontribution.h`
- Create: `libkalburator/src/sync/multiprotocoldavbackendcontribution.cpp`
- Create: `libkalburator/src/plugin/multiprotocoldavproviderplugin.h`
- Create: `libkalburator/src/plugin/multiprotocoldavproviderplugin.cpp`
- Modify: `libkalburator/CMakeLists.txt`

- [ ] **Step 1: Read the CalDAV contribution + plugin pair as template**

```bash
cat libkalburator/src/sync/caldavbackendcontribution.h
cat libkalburator/src/plugin/caldavproviderplugin.h
cat libkalburator/src/plugin/caldavproviderplugin.cpp
```

Note: the contribution exposes the same `kind()` strings + creates
a factory for backends. Mirror the same shape, substituting
"multiproto-dav" for "caldav".

- [ ] **Step 2: Create `multiprotocoldavbackendcontribution.h` (header-only, no .cpp)**

`CalDavBackendContribution` is a header-only class (no .cpp).
Mirror its shape:

```cpp
#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVBACKENDCONTRIBUTION_H

#include "backendcontribution.h"
#include "multiprotocoldavprovider.h"
#include "iprovider.h"

namespace Kalburator::Sync {

class MultiProtocolDavBackendContribution : public BackendContribution {
public:
    QString backendType() const override
    { return QStringLiteral("multiproto-dav"); }

    QList<Shape::Shape> nativeShapes() const override { return {}; }

    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        return std::make_unique<MultiProtocolDavProvider>(parent);
    }
};

} // namespace Kalburator::Sync

#endif
```

Note: drop the .cpp file from Task 7's file list — header-only
contribution matches the CalDAV template. Also drop
`multiprotocoldavbackendcontribution.cpp` from the CMake source
list referenced in Task 7 step 4.

- [ ] **Step 3: Create `multiprotocoldavproviderplugin.{h,cpp}`**

Use `caldavproviderplugin.{h,cpp}` as exact template:

`multiprotocoldavproviderplugin.h`:
```cpp
#ifndef KALBURATOR_PLUGIN_MULTIPROTOCOLDAVPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_MULTIPROTOCOLDAVPROVIDERPLUGIN_H

#include "../sync/plugin.h"   // confirm path against caldavproviderplugin.h
#include <memory>

namespace Kalburator {

class MultiProtocolDavProviderPlugin : public Plugin
{
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif
```

`multiprotocoldavproviderplugin.cpp`:
```cpp
#include "multiprotocoldavproviderplugin.h"
#include "../sync/multiprotocoldavbackendcontribution.h"

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
MultiProtocolDavProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::MultiProtocolDavBackendContribution>() };
}

} // namespace Kalburator
```

- [ ] **Step 4: Add to CMakeLists.txt**

Add the four new source files alongside the existing CalDav plugin
sources.

- [ ] **Step 5: Build verification (no new test here — plugin is exercised by Task 8)**

```bash
cmake --build build-dev -j 10
```

Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add src/sync/multiprotocoldavbackendcontribution.{h,cpp} \
        src/plugin/multiprotocoldavproviderplugin.{h,cpp} \
        CMakeLists.txt
git commit -m "M.7: MultiProtocolDavProviderPlugin + BackendContribution

Plugin and contribution wrapper following the CalDavProviderPlugin /
CalDavBackendContribution template. Registers the multiproto-dav
backend kind with the plugin manager."
```

---

## Task 8: Register the plugin in `stock_plugins.cpp`

**Files:** Modify `libkalburator/src/plugin/stock_plugins.cpp`.

- [ ] **Step 1: Add include + static instance + manifest entry**

Edit `libkalburator/src/plugin/stock_plugins.cpp`:

Add include after the existing carddav include (line 11):
```cpp
#include "multiprotocoldavproviderplugin.h"
```

Inside `registerStockPlugins()`, add a static instance after the
`s_carddav` line (currently line 39):
```cpp
    static MultiProtocolDavProviderPlugin s_multiprotodav;
```

Add a manifest entry to the `items` initializer list, after the
carddav entry (currently line 51):
```cpp
        {&s_multiprotodav, mkManifest(QStringLiteral("kalburator.provider.multiproto-dav"))},
```

- [ ] **Step 2: Build + verify**

```bash
cmake --build build-dev -j 10
ctest --test-dir build-dev --output-on-failure 2>&1 | tail -20
```

Expected: clean build; all existing tests still pass.

- [ ] **Step 3: Add a smoke test that the plugin shows up**

Add to `tst_multiprotocoldavprovider.cpp`:

```cpp
    void pluginManagerListsMultiProtocolDav();
```

```cpp
#include "../../src/plugin/pluginmanager.h"
#include "../../src/plugin/stock_plugins.h"

void TstMultiProtocolDavProvider::pluginManagerListsMultiProtocolDav()
{
    Kalburator::PluginManager pm;
    Kalburator::registerStockPlugins(pm);
    const auto ids = pm.pluginIds();  // or whatever the accessor is — verify
    QVERIFY(ids.contains(QStringLiteral("kalburator.provider.multiproto-dav")));
}
```

(If `pluginIds()` isn't the actual accessor, read
`plugin/pluginmanager.h` to find the equivalent — fall back to
asserting the contribution is registered by some other path the
manager exposes.)

- [ ] **Step 4: Run the smoke test**

```bash
cmake --build build-dev -j 10 --target tst_multiprotocoldavprovider
ctest --test-dir build-dev -R tst_multiprotocoldavprovider --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/stock_plugins.cpp \
        tests/sync/tst_multiprotocoldavprovider.cpp
git commit -m "M.8: Register MultiProtocolDavProviderPlugin in stock_plugins

Plugin is now available in default PluginManager + visible via the
provider combo box in any consumer hosting ProviderConfigDialog."
```

---

## Task 9: `CollectionPickerWidget`

Library widget: takes a `QList<CollectionInfo>` and emits a
`selectionChanged(QStringList)` signal as the user toggles
checkboxes. Used inside `ProviderConfigDialog` after discovery
succeeds.

**Files:**
- Create: `libkalburator/src/ui/collectionpickerwidget.h`
- Create: `libkalburator/src/ui/collectionpickerwidget.cpp`
- Test:   `libkalburator/tests/ui/tst_collectionpickerwidget.cpp` (NEW)
- Modify: `libkalburator/CMakeLists.txt` (add `src/ui/` dir)
- Modify: `libkalburator/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libkalburator/tests/ui/tst_collectionpickerwidget.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "../../src/ui/collectionpickerwidget.h"
#include "../../src/sync/collectioninfo.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstCollectionPickerWidget : public QObject
{
    Q_OBJECT
private slots:
    void rendersCheckboxPerCollection();
    void toggleEmitsSelectionChanged();
    void groupsByType();
};

static QList<CollectionInfo> makeFixture()
{
    CollectionInfo a, b, c;
    a.id = QStringLiteral("cal-1"); a.name = QStringLiteral("Work");
    a.type = QStringLiteral("calendar");
    b.id = QStringLiteral("cal-2"); b.name = QStringLiteral("Personal");
    b.type = QStringLiteral("calendar");
    c.id = QStringLiteral("ab-1");  c.name = QStringLiteral("Family");
    c.type = QStringLiteral("contacts");
    return { a, b, c };
}

void TstCollectionPickerWidget::rendersCheckboxPerCollection()
{
    Ui::CollectionPickerWidget w;
    w.setCollections(makeFixture());
    const auto boxes = w.findChildren<QCheckBox*>();
    QVERIFY(boxes.size() >= 3);
}

void TstCollectionPickerWidget::toggleEmitsSelectionChanged()
{
    Ui::CollectionPickerWidget w;
    w.setCollections(makeFixture());
    QSignalSpy spy(&w, &Ui::CollectionPickerWidget::selectionChanged);
    auto *box = w.findChild<QCheckBox*>(QStringLiteral("collection-cal-1"));
    QVERIFY(box != nullptr);
    box->setChecked(true);
    QCOMPARE(spy.count(), 1);
    QStringList selected = spy.first().first().toStringList();
    QVERIFY(selected.contains(QStringLiteral("cal-1")));
}

void TstCollectionPickerWidget::groupsByType()
{
    Ui::CollectionPickerWidget w;
    w.setCollections(makeFixture());
    // Just check that group-box labels exist for both types
    const auto groups = w.findChildren<QGroupBox*>();
    QStringList titles;
    for (auto *g : groups) titles << g->title();
    QVERIFY(titles.contains(QStringLiteral("Calendars")));
    QVERIFY(titles.contains(QStringLiteral("Address Books")));
}

QTEST_MAIN(TstCollectionPickerWidget)
#include "tst_collectionpickerwidget.moc"
```

- [ ] **Step 2: Create the header**

`libkalburator/src/ui/collectionpickerwidget.h`:

```cpp
#ifndef KALBURATOR_UI_COLLECTIONPICKERWIDGET_H
#define KALBURATOR_UI_COLLECTIONPICKERWIDGET_H

#include "../sync/collectioninfo.h"
#include <QWidget>
#include <QStringList>

namespace Kalburator::Ui {

class CollectionPickerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CollectionPickerWidget(QWidget *parent = nullptr);

    void setCollections(const QList<Sync::CollectionInfo> &items);
    QStringList selected() const;

signals:
    void selectionChanged(const QStringList &selectedIds);

private:
    void rebuild();
    QList<Sync::CollectionInfo> m_items;
};

} // namespace Kalburator::Ui

#endif
```

- [ ] **Step 3: Create the .cpp**

`libkalburator/src/ui/collectionpickerwidget.cpp`:

```cpp
#include "collectionpickerwidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>

namespace Kalburator::Ui {

CollectionPickerWidget::CollectionPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    setLayout(new QVBoxLayout(this));
}

void CollectionPickerWidget::setCollections(const QList<Sync::CollectionInfo> &items)
{
    m_items = items;
    rebuild();
}

void CollectionPickerWidget::rebuild()
{
    // Clear existing children
    while (auto *child = layout()->takeAt(0)) {
        delete child->widget();
        delete child;
    }

    auto *calGroup     = new QGroupBox(tr("Calendars"),      this);
    auto *contactsGroup = new QGroupBox(tr("Address Books"), this);
    auto *calLayout      = new QVBoxLayout(calGroup);
    auto *contactsLayout = new QVBoxLayout(contactsGroup);

    bool anyCal = false, anyContacts = false;
    for (const auto &it : m_items) {
        auto *cb = new QCheckBox(it.name);
        cb->setObjectName(QStringLiteral("collection-%1").arg(it.id));
        QObject::connect(cb, &QCheckBox::toggled, this, [this]() {
            emit selectionChanged(selected());
        });
        if (it.type == QStringLiteral("calendar")) {
            calLayout->addWidget(cb); anyCal = true;
        } else if (it.type == QStringLiteral("contacts")) {
            contactsLayout->addWidget(cb); anyContacts = true;
        } else {
            // Other types — append after; bare group.
            layout()->addWidget(cb);
        }
    }
    if (anyCal)     layout()->addWidget(calGroup);     else delete calGroup;
    if (anyContacts) layout()->addWidget(contactsGroup); else delete contactsGroup;
}

QStringList CollectionPickerWidget::selected() const
{
    QStringList out;
    const auto boxes = findChildren<QCheckBox*>();
    for (auto *b : boxes)
        if (b->isChecked()) {
            const QString name = b->objectName();
            if (name.startsWith(QStringLiteral("collection-")))
                out << name.mid(QStringLiteral("collection-").length());
        }
    return out;
}

} // namespace Kalburator::Ui
```

- [ ] **Step 4: CMake — add src/ui/ as a sources directory + register the test**

Edit `libkalburator/CMakeLists.txt`. Locate the `target_sources(kalburator ...)`
block (or equivalent) and add:
```cmake
    src/ui/collectionpickerwidget.cpp
    src/ui/collectionpickerwidget.h
```

In `tests/CMakeLists.txt`, register a new test executable
`tst_collectionpickerwidget` linking against `Qt::Test` +
`Qt::Widgets`. Follow the existing `tst_caldavconfigwidget`
pattern.

- [ ] **Step 5: Build + run**

```bash
cmake --build build-dev -j 10 --target tst_collectionpickerwidget
ctest --test-dir build-dev -R tst_collectionpickerwidget --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/collectionpickerwidget.{h,cpp} \
        tests/ui/tst_collectionpickerwidget.cpp \
        tests/CMakeLists.txt CMakeLists.txt
git commit -m "M.9: CollectionPickerWidget — library UI widget

QWidget showing per-CollectionInfo checkboxes grouped by type
(Calendars, Address Books). Emits selectionChanged when toggled.
Hostable from any account-add dialog."
```

---

## Task 10: `AccountsListWidget`

Library widget: rows of configured accounts with provider icon,
display name, kind, enable checkbox, edit + remove buttons. Signals
flow upward; consumer persists.

**Files:**
- Create: `libkalburator/src/ui/accountslistwidget.h`
- Create: `libkalburator/src/ui/accountslistwidget.cpp`
- Test:   `libkalburator/tests/ui/tst_accountslistwidget.cpp` (NEW)
- Modify: `libkalburator/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Test (selected functionality only — rendering + enable signal + remove signal)**

`tests/ui/tst_accountslistwidget.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCheckBox>
#include <QPushButton>

#include "../../src/ui/accountslistwidget.h"
#include "../../src/sync/backendconfiguration.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstAccountsListWidget : public QObject
{
    Q_OBJECT
private slots:
    void rendersOneRowPerAccount();
    void toggleEmitsEnabledChanged();
    void removeButtonEmitsRemoved();
};

static QList<BackendConfiguration> makeFixture()
{
    BackendConfiguration a, b;
    a.id = QStringLiteral("acct-1");
    a.kind = QStringLiteral("caldav");
    a.displayName = QStringLiteral("Work CalDAV");
    a.enabled = true;
    b.id = QStringLiteral("acct-2");
    b.kind = QStringLiteral("multiproto-dav");
    b.displayName = QStringLiteral("Nextcloud");
    b.enabled = false;
    return { a, b };
}

void TstAccountsListWidget::rendersOneRowPerAccount()
{
    Ui::AccountsListWidget w;
    w.setAccounts(makeFixture());
    QVERIFY(w.findChild<QPushButton*>(QStringLiteral("remove-acct-1")) != nullptr);
    QVERIFY(w.findChild<QPushButton*>(QStringLiteral("remove-acct-2")) != nullptr);
}

void TstAccountsListWidget::toggleEmitsEnabledChanged()
{
    Ui::AccountsListWidget w;
    w.setAccounts(makeFixture());
    QSignalSpy spy(&w, &Ui::AccountsListWidget::accountEnabledChanged);
    auto *cb = w.findChild<QCheckBox*>(QStringLiteral("enabled-acct-1"));
    QVERIFY(cb != nullptr);
    cb->setChecked(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("acct-1"));
    QCOMPARE(spy.first().at(1).toBool(),   false);
}

void TstAccountsListWidget::removeButtonEmitsRemoved()
{
    Ui::AccountsListWidget w;
    w.setAccounts(makeFixture());
    QSignalSpy spy(&w, &Ui::AccountsListWidget::accountRemoved);
    auto *btn = w.findChild<QPushButton*>(QStringLiteral("remove-acct-2"));
    QVERIFY(btn != nullptr);
    btn->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("acct-2"));
}

QTEST_MAIN(TstAccountsListWidget)
#include "tst_accountslistwidget.moc"
```

- [ ] **Step 2: Implement header + cpp**

`libkalburator/src/ui/accountslistwidget.h`:

```cpp
#ifndef KALBURATOR_UI_ACCOUNTSLISTWIDGET_H
#define KALBURATOR_UI_ACCOUNTSLISTWIDGET_H

#include "../sync/backendconfiguration.h"

#include <QWidget>
#include <QList>

namespace Kalburator::Ui {

class AccountsListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AccountsListWidget(QWidget *parent = nullptr);

    void setAccounts(const QList<Sync::BackendConfiguration> &accounts);

signals:
    void accountAddRequested();
    void accountEditRequested(const QString &id);
    void accountRemoved(const QString &id);
    void accountEnabledChanged(const QString &id, bool enabled);

private:
    void rebuild();
    QList<Sync::BackendConfiguration> m_accounts;
};

} // namespace Kalburator::Ui

#endif
```

`libkalburator/src/ui/accountslistwidget.cpp`:

```cpp
#include "accountslistwidget.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Kalburator::Ui {

AccountsListWidget::AccountsListWidget(QWidget *parent)
    : QWidget(parent)
{
    setLayout(new QVBoxLayout(this));
}

void AccountsListWidget::setAccounts(const QList<Sync::BackendConfiguration> &a)
{
    m_accounts = a;
    rebuild();
}

void AccountsListWidget::rebuild()
{
    while (auto *child = layout()->takeAt(0)) {
        delete child->widget();
        delete child;
    }

    for (const auto &cfg : m_accounts) {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);

        auto *enabled = new QCheckBox(row);
        enabled->setObjectName(QStringLiteral("enabled-%1").arg(cfg.id));
        enabled->setChecked(cfg.enabled);
        const QString rowId = cfg.id;
        QObject::connect(enabled, &QCheckBox::toggled, this,
            [this, rowId](bool on){ emit accountEnabledChanged(rowId, on); });
        h->addWidget(enabled);

        auto *label = new QLabel(QStringLiteral("%1 (%2)").arg(cfg.displayName, cfg.kind), row);
        h->addWidget(label, /*stretch*/ 1);

        auto *editBtn = new QPushButton(tr("Edit…"), row);
        editBtn->setObjectName(QStringLiteral("edit-%1").arg(cfg.id));
        QObject::connect(editBtn, &QPushButton::clicked, this,
            [this, rowId]{ emit accountEditRequested(rowId); });
        h->addWidget(editBtn);

        auto *rmBtn = new QPushButton(tr("Remove"), row);
        rmBtn->setObjectName(QStringLiteral("remove-%1").arg(cfg.id));
        QObject::connect(rmBtn, &QPushButton::clicked, this,
            [this, rowId]{ emit accountRemoved(rowId); });
        h->addWidget(rmBtn);

        layout()->addWidget(row);
    }

    auto *addBtn = new QPushButton(tr("Add account…"), this);
    addBtn->setObjectName(QStringLiteral("addAccount"));
    QObject::connect(addBtn, &QPushButton::clicked, this,
                     &AccountsListWidget::accountAddRequested);
    layout()->addWidget(addBtn);
}

} // namespace Kalburator::Ui
```

- [ ] **Step 3: CMake + run tests**

Register sources + test executable. Build + run:
```bash
cmake --build build-dev -j 10 --target tst_accountslistwidget
ctest --test-dir build-dev -R tst_accountslistwidget --output-on-failure
```
Expected: PASS (all 3 cases).

- [ ] **Step 4: Commit**

```bash
git add src/ui/accountslistwidget.{h,cpp} \
        tests/ui/tst_accountslistwidget.cpp \
        tests/CMakeLists.txt CMakeLists.txt
git commit -m "M.10: AccountsListWidget — library accounts-list UI

QWidget with rows of accounts. Each row: enable checkbox + label +
edit + remove. Signals (add/edit/remove/enabled) flow up to host;
host handles persistence."
```

---

## Task 11: `ProviderConfigDialog`

Library dialog: provider combo + embedded `createConfigWidget()` +
Test button (runs `connect()`) + `CollectionPickerWidget` to choose
discovered collections + Save/Cancel.

**Files:**
- Create: `libkalburator/src/ui/providerconfigdialog.h`
- Create: `libkalburator/src/ui/providerconfigdialog.cpp`
- Test:   `libkalburator/tests/ui/tst_providerconfigdialog.cpp`
- Modify: CMakeLists.

- [ ] **Step 1: Test**

`tests/ui/tst_providerconfigdialog.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QComboBox>
#include <QSignalSpy>

#include "../../src/ui/providerconfigdialog.h"
#include "../../src/sync/providermanager.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstProviderConfigDialog : public QObject
{
    Q_OBJECT
private slots:
    void comboPopulatedFromProviderManager();
    void switchingComboShowsCorrectWidget();
};

void TstProviderConfigDialog::comboPopulatedFromProviderManager()
{
    BackendRegistry registry;
    ProviderManager pm(&registry);
    QList<Ui::ProviderConfigDialog::ProviderKind> kinds {
        { QStringLiteral("caldav"),         QStringLiteral("CalDAV") },
        { QStringLiteral("carddav"),        QStringLiteral("CardDAV") },
        { QStringLiteral("multiproto-dav"), QStringLiteral("Multi-protocol DAV") },
    };
    Ui::ProviderConfigDialog dlg(&pm, kinds, Ui::ProviderConfigDialog::AddNew);
    auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 3);
}

void TstProviderConfigDialog::switchingComboShowsCorrectWidget()
{
    BackendRegistry registry;
    ProviderManager pm(&registry);
    QList<Ui::ProviderConfigDialog::ProviderKind> kinds {
        { QStringLiteral("caldav"),         QStringLiteral("CalDAV") },
        { QStringLiteral("multiproto-dav"), QStringLiteral("Multi-protocol DAV") },
    };
    Ui::ProviderConfigDialog dlg(&pm, kinds, Ui::ProviderConfigDialog::AddNew);
    auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QVERIFY(combo != nullptr);
    if (combo->count() < 2)
        QSKIP("Need at least 2 providers registered for switching test");
    combo->setCurrentIndex(0);
    auto *w0 = dlg.findChild<QWidget*>(QStringLiteral("providerConfigEmbed"));
    QVERIFY(w0 != nullptr);
    combo->setCurrentIndex(1);
    auto *w1 = dlg.findChild<QWidget*>(QStringLiteral("providerConfigEmbed"));
    QVERIFY(w1 != nullptr);
}

QTEST_MAIN(TstProviderConfigDialog)
#include "tst_providerconfigdialog.moc"
```

- [ ] **Step 2: Implement header**

`libkalburator/src/ui/providerconfigdialog.h`:

```cpp
#ifndef KALBURATOR_UI_PROVIDERCONFIGDIALOG_H
#define KALBURATOR_UI_PROVIDERCONFIGDIALOG_H

#include "../sync/backendconfiguration.h"
#include "../sync/collectioninfo.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QWidget;
class QPushButton;

namespace Kalburator {
namespace Sync { class ProviderManager; class IProvider; }

namespace Ui {

class CollectionPickerWidget;

class ProviderConfigDialog : public QDialog
{
    Q_OBJECT
public:
    enum Mode { AddNew, EditExisting };

    // Each entry: (backendType, displayLabel). Consumer assembles this
    // list by walking the BackendRegistry's contributions and asking each
    // for its backendType(). This keeps the dialog independent of
    // BackendRegistry internals.
    struct ProviderKind { QString backendType; QString displayLabel; };

    ProviderConfigDialog(Sync::ProviderManager *manager,
                         const QList<ProviderKind> &availableKinds,
                         Mode mode,
                         const Sync::BackendConfiguration &existing = {},
                         QWidget *parent = nullptr);

    Sync::BackendConfiguration result() const;
    QStringList selectedCollectionIds() const;

private slots:
    void onProviderChanged(int comboIndex);
    void onTestClicked();
    void onConnectFinished(bool ok);

private:
    void rebuildProviderWidget();

    Sync::ProviderManager *m_manager;
    QList<ProviderKind> m_availableKinds;
    Mode m_mode;
    Sync::BackendConfiguration m_existing;

    QComboBox            *m_combo            = nullptr;
    QWidget              *m_embeddedConfig   = nullptr;
    Sync::IProvider      *m_currentProvider  = nullptr;  // owned by us
    CollectionPickerWidget *m_picker         = nullptr;
    QPushButton          *m_testButton       = nullptr;
    QPushButton          *m_saveButton       = nullptr;
};

} // namespace Ui
} // namespace Kalburator

#endif
```

- [ ] **Step 3: Implement .cpp**

`libkalburator/src/ui/providerconfigdialog.cpp`:

```cpp
#include "providerconfigdialog.h"
#include "collectionpickerwidget.h"
#include "../sync/providermanager.h"
#include "../sync/iprovider.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace Kalburator::Ui {

ProviderConfigDialog::ProviderConfigDialog(
        Sync::ProviderManager *manager,
        const QList<ProviderKind> &availableKinds,
        Mode mode,
        const Sync::BackendConfiguration &existing,
        QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_availableKinds(availableKinds)
    , m_mode(mode)
    , m_existing(existing)
{
    auto *root = new QVBoxLayout(this);

    m_combo = new QComboBox(this);
    m_combo->setObjectName(QStringLiteral("providerCombo"));
    // Populate combo from the kinds the consumer passed in.
    // Consumer obtains this list by walking BackendRegistry's
    // contributions (each contribution has backendType()).
    for (const auto &k : m_availableKinds)
        m_combo->addItem(k.displayLabel, k.backendType);

    auto *formRow = new QFormLayout;
    formRow->addRow(tr("Provider:"), m_combo);
    root->addLayout(formRow);

    // Placeholder for the embedded per-provider config widget.
    auto *embedHost = new QWidget(this);
    embedHost->setObjectName(QStringLiteral("providerConfigEmbed"));
    embedHost->setLayout(new QVBoxLayout(embedHost));
    root->addWidget(embedHost);

    m_picker = new CollectionPickerWidget(this);
    m_picker->setObjectName(QStringLiteral("collectionPicker"));
    m_picker->setVisible(false);
    root->addWidget(m_picker);

    auto *btnRow = new QHBoxLayout;
    m_testButton = new QPushButton(tr("Test connection"), this);
    QObject::connect(m_testButton, &QPushButton::clicked,
                     this, &ProviderConfigDialog::onTestClicked);
    btnRow->addWidget(m_testButton);
    btnRow->addStretch();
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = bb->button(QDialogButtonBox::Save);
    m_saveButton->setEnabled(false);  // enabled after a successful test
    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnRow->addWidget(bb);
    root->addLayout(btnRow);

    QObject::connect(m_combo, &QComboBox::currentIndexChanged,
                     this, &ProviderConfigDialog::onProviderChanged);

    if (mode == EditExisting) {
        const int idx = m_combo->findData(existing.kind);
        if (idx >= 0) m_combo->setCurrentIndex(idx);
    }
    rebuildProviderWidget();  // initial population
}

void ProviderConfigDialog::onProviderChanged(int)
{
    rebuildProviderWidget();
}

void ProviderConfigDialog::rebuildProviderWidget()
{
    auto *embedHost = findChild<QWidget*>(QStringLiteral("providerConfigEmbed"));
    if (!embedHost) return;

    // Tear down previous embedded widget + provider
    while (auto *c = embedHost->layout()->takeAt(0)) {
        delete c->widget();
        delete c;
    }
    if (m_currentProvider) {
        m_currentProvider->deleteLater();
        m_currentProvider = nullptr;
    }

    const QString kind = m_combo->currentData().toString();
    if (kind.isEmpty()) return;
    // Construct a fresh IProvider for the chosen kind. The consumer
    // passed in the (kind, label) pairs derived from BackendRegistry;
    // ask the registry for the matching contribution and call its
    // createProvider(this). Engineer must wire this through whatever
    // accessor the consumer normally uses — e.g., a factory callback
    // injected via the constructor, or a BackendRegistry* passed
    // alongside the kinds list. Concrete call shape:
    //
    //   auto *contribution = registry->contributionFor(kind);
    //   m_currentProvider = contribution->createProvider(this).release();
    //
    // (Dialog owns the lifecycle of m_currentProvider; it's deleted
    // in rebuildProviderWidget when the combo changes.)
    if (!m_currentProvider) return;

    if (m_mode == EditExisting && m_existing.kind == kind)
        m_currentProvider->load(m_existing);

    QWidget *w = m_currentProvider->createConfigWidget(embedHost);
    if (w) {
        m_embeddedConfig = w;
        embedHost->layout()->addWidget(w);
    }

    m_picker->setVisible(false);
    m_saveButton->setEnabled(false);
}

void ProviderConfigDialog::onTestClicked()
{
    if (!m_currentProvider) return;

    // The embedded widget owns the user-edited config. Provider has its
    // own load() — invoke via "apply" from the widget. Convention from
    // existing code: the embedded widget defines an apply method that
    // calls provider->load() with its current state. If the existing
    // CalDavConfigWidget doesn't have this, replicate by reading the
    // widget's accessor (e.g., MultiProtocolDavConfigWidget::configuration()).
    //
    // Implementation note: the cleanest move for Task 11 is to require
    // each config widget to expose a `BackendConfiguration configuration()
    // const` method. CalDavConfigWidget and CardDavConfigWidget may or
    // may not have this — engineer must add it during this task if
    // missing (small mechanical addition).
    auto *cfgWidget = m_embeddedConfig;
    if (!cfgWidget) return;
    // Invoke whatever pattern the existing widgets use; for the new
    // MultiProtocolDavConfigWidget defined in Task 6, this is:
    //   m_currentProvider->load(static_cast<MultiProtocolDavConfigWidget*>(cfgWidget)->configuration());

    m_testButton->setEnabled(false);
    auto fut = m_currentProvider->connect();
    auto *watcher = new QFutureWatcher<bool>(this);
    QObject::connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
        const bool ok = watcher->result();
        watcher->deleteLater();
        onConnectFinished(ok);
    });
    watcher->setFuture(fut);
}

void ProviderConfigDialog::onConnectFinished(bool ok)
{
    m_testButton->setEnabled(true);
    if (!m_currentProvider) return;
    if (ok) {
        m_picker->setCollections(m_currentProvider->collections());
        m_picker->setVisible(true);
        m_saveButton->setEnabled(true);
    }
    // Warning surface: show m_currentProvider->lastWarning() if non-empty.
    // Implementation: add a QLabel above the picker. (Plan keeps this
    // optional polish to keep the task tight; engineer may add the label
    // in a follow-up commit if the test below asks for it.)
}

Sync::BackendConfiguration ProviderConfigDialog::result() const
{
    return m_currentProvider ? m_currentProvider->save() : Sync::BackendConfiguration{};
}

QStringList ProviderConfigDialog::selectedCollectionIds() const
{
    return m_picker ? m_picker->selected() : QStringList{};
}

} // namespace Kalburator::Ui
```

- [ ] **Step 4: Register sources + run tests**

```bash
cmake --build build-dev -j 10 --target tst_providerconfigdialog
ctest --test-dir build-dev -R tst_providerconfigdialog --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ui/providerconfigdialog.{h,cpp} \
        tests/ui/tst_providerconfigdialog.cpp \
        tests/CMakeLists.txt CMakeLists.txt
git commit -m "M.11: ProviderConfigDialog — library add/edit-account dialog

QDialog with provider combo + embedded config widget +
Test/Save/Cancel + CollectionPickerWidget post-discovery.
Both consumers will host this in their settings UI."
```

---

## Task 12: WildPalms — pin existing add-account flow with a test

Before migrating WildPalms's `AddAccountDialog`, write a smoke test
that asserts the current "add CalDAV account, discover, see
collections" flow works. This test then runs after the migration
to prove no regression.

**Files:**
- WildPalms test directory — locate via:
  `find WildPalms/tests -type d | head -5`
- Create: `WildPalms/tests/<settings-ui-dir>/tst_addaccountdialog_baseline.cpp`

- [ ] **Step 1: Read the existing add-account flow + dialog**

```bash
grep -rln "AddAccountDialog" WildPalms/src/ | head -5
```

Read those files. Identify: (a) which slot in
`KF6MainWindow`/`SettingsDialog`/`AccountsPage` constructs the
`AddAccountDialog`, (b) what the dialog does on accept (how the
new `BackendConfiguration` is persisted into the Profile).

- [ ] **Step 2: Write a smoke test that exercises the current flow**

(Implementation depends on file layout discovered in step 1. The
test should: instantiate the dialog with a fake `ProviderManager`,
simulate combo-pick CalDAV + filling URL + clicking Save, and
assert that `Profile::saveAccount()` was called with a
correctly-shaped `BackendConfiguration`.)

If WildPalms tests don't currently have this pattern, write a
minimal test as a new executable; this is the baseline. Add to
`WildPalms/tests/CMakeLists.txt`.

- [ ] **Step 3: Run + verify pass**

```bash
cmake --build WildPalms/build-dev -j 10 --target tst_addaccountdialog_baseline
ctest --test-dir WildPalms/build-dev -R tst_addaccountdialog_baseline --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
cd WildPalms
git add tests/.../tst_addaccountdialog_baseline.cpp \
        tests/CMakeLists.txt
git commit -m "M.12: WildPalms add-account baseline test

Pins current behavior of AddAccountDialog before Phase M migration.
Re-run after M.13/M.14 to prove no regression."
```

---

## Task 13: WildPalms — migrate `AddAccountDialog` to host `ProviderConfigDialog`

**Files:** WildPalms's existing AddAccountDialog files (locate via
the grep in Task 12). The migration replaces the body of the dialog
with an embedded `ProviderConfigDialog` from libkalburator.

- [ ] **Step 1: Replace AddAccountDialog internals with library dialog**

The simplest migration: AddAccountDialog becomes a thin wrapper
that just shows a `Kalburator::Ui::ProviderConfigDialog` and
forwards its accept signal. Or: delete AddAccountDialog entirely
and have callers construct `ProviderConfigDialog` directly. The
latter is cleaner if there are no AddAccountDialog-specific
features.

Pick the cleaner of the two based on what reading the existing
file reveals.

Pseudocode for the wrapper approach:

```cpp
// WildPalms/src/<dir>/addaccountdialog.cpp
#include <kalburator/ui/providerconfigdialog.h>

AddAccountDialog::AddAccountDialog(ProviderManager *pm,
                                   BackendRegistry *registry,
                                   QWidget *parent)
    : QDialog(parent)
{
    // Assemble (kind, label) pairs from the registry's contributions.
    QList<Kalburator::Ui::ProviderConfigDialog::ProviderKind> kinds;
    for (const auto *c : registry->providerContributions())
        kinds.append({ c->backendType(), labelForKind(c->backendType()) });

    auto *root = new QVBoxLayout(this);
    auto *embed = new Kalburator::Ui::ProviderConfigDialog(
        pm, kinds, Kalburator::Ui::ProviderConfigDialog::AddNew, {}, this);
    root->addWidget(embed);
    QObject::connect(embed, &QDialog::accepted, this, &QDialog::accept);
}

BackendConfiguration AddAccountDialog::result() const {
    // Pass through to embedded dialog
    return findChild<Kalburator::Ui::ProviderConfigDialog*>()->result();
}
```

- [ ] **Step 2: Rerun the baseline test**

```bash
ctest --test-dir WildPalms/build-dev -R tst_addaccountdialog_baseline --output-on-failure
```

Expected: PASS (no regression).

- [ ] **Step 3: Confirm "Multi-protocol DAV" appears in the provider combo**

Manual / smoke test: build WildPalms, run, open Settings →
Accounts → Add. Verify the combo has at least four entries:
CalDAV, CardDAV, Akonadi (if HAVE_AKONADI), Multi-protocol DAV.

- [ ] **Step 4: Commit**

```bash
git add src/.../addaccountdialog.{h,cpp}
git commit -m "M.13: WildPalms AddAccountDialog hosts library ProviderConfigDialog

WildPalms-specific dialog body deleted; thin wrapper around
Kalburator::Ui::ProviderConfigDialog. Multi-protocol DAV (and
all other registered providers) automatically appear in the combo."
```

---

## Task 14: WildPalms — migrate `AccountsPage` to host `AccountsListWidget`

**Files:** WildPalms's `AccountsPage` (Settings → Accounts page).

- [ ] **Step 1: Replace AccountsPage internals with AccountsListWidget**

AccountsPage becomes a host of `Kalburator::Ui::AccountsListWidget`,
with signals wired to the existing Profile-persistence methods:

```cpp
// WildPalms/src/<settings-dir>/accountspage.cpp
#include <kalburator/ui/accountslistwidget.h>

AccountsPage::AccountsPage(Profile *profile, ProviderManager *pm, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_pm(pm)
{
    auto *root = new QVBoxLayout(this);
    m_list = new Kalburator::Ui::AccountsListWidget(this);
    m_list->setAccounts(profile->accounts());
    root->addWidget(m_list);

    connect(m_list, &AccountsListWidget::accountAddRequested,
            this, &AccountsPage::onAdd);
    connect(m_list, &AccountsListWidget::accountEditRequested,
            this, &AccountsPage::onEdit);
    connect(m_list, &AccountsListWidget::accountRemoved,
            this, &AccountsPage::onRemove);
    connect(m_list, &AccountsListWidget::accountEnabledChanged,
            this, &AccountsPage::onEnabledChanged);
}

void AccountsPage::onEnabledChanged(const QString &id, bool enabled)
{
    auto accts = m_profile->accounts();
    for (auto &cfg : accts)
        if (cfg.id == id) { cfg.enabled = enabled; break; }
    m_profile->setAccounts(accts);
    // existing post-flip side effects (fan out to mappings) — preserve
}
```

The fan-out of `BackendConfiguration::enabled` → per-mapping
`enabled` should already exist in `AccountController` from Phase L
(L.9). Preserve it.

- [ ] **Step 2: Build + run existing AccountsPage tests**

If WildPalms has AccountsPage tests (`grep -rln "AccountsPage" tests/`),
run them and confirm pass:

```bash
ctest --test-dir WildPalms/build-dev -R AccountsPage --output-on-failure
```

If none exist, do the manual smoke: build, open Settings →
Accounts, verify accounts list renders correctly with enable
checkboxes.

- [ ] **Step 3: Commit**

```bash
git add src/.../accountspage.{h,cpp}
git commit -m "M.14: WildPalms AccountsPage hosts library AccountsListWidget

WildPalms-specific list rendering deleted; AccountsPage now
embeds Kalburator::Ui::AccountsListWidget and routes its signals
through Profile + AccountController. Phase L's per-provider
fan-out preserved."
```

---

## Task 15: PlanStan — pin existing CalDAV add-account flow with a test

Same shape as Task 12 but on the PlanStan side. **This task is
gating — if the baseline test cannot be written, stop and
re-scope Phase M to land WildPalms migration only (PlanStan
migration carves out to M.5).**

**Files:** PlanStan test directory + a new
`tst_collectioncontroller_caldav_baseline.cpp`.

- [ ] **Step 1: Read the existing CalDAV add-account flow**

```bash
sed -n '1680,1720p' PlanStan/src/controllers/collectioncontroller.cpp
```

Identify: (a) the entry-point slot, (b) the persisted shape, (c)
the test surface that lets us assert "user adds CalDAV → discovery
runs → logical-calendar binding fires".

- [ ] **Step 2: Write the baseline test**

The test must use a `FakeCalDavServer` (locate via
`find PlanStan/tests -name 'fake*' -o -name '*caldav*'`) and
should:
  1. Construct CollectionController
  2. Invoke the add-CalDAV flow (whatever slot)
  3. Assert: dialog accepted, `BackendConfiguration` persisted,
     subsequent `discoverCollections()` returns the fake server's
     calendars
  4. Assert: logical-calendar binding receives the expected
     `(CollectionInfo, logicalCalendarId)` pair

- [ ] **Step 3: Run baseline test → PASS**

```bash
ctest --test-dir PlanStan/build-dev -R tst_collectioncontroller_caldav_baseline --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
cd PlanStan
git add tests/.../tst_collectioncontroller_caldav_baseline.cpp tests/CMakeLists.txt
git commit -m "M.15: PlanStan CalDAV add-account baseline test

Pins production CalDAV add-account behavior in CollectionController
before Phase M migration. Re-run after M.16 to prove no regression."
```

---

## Task 16: PlanStan — migrate CollectionController to host `ProviderConfigDialog`

**Files:** `PlanStan/src/controllers/collectioncontroller.cpp:1691-1707`
(verify exact lines at task time — they may have shifted).

- [ ] **Step 1: Replace the CalDAV-specific dialog construction with the library dialog**

Locate the existing `addCalDavAccount` (or equivalent) slot.
Replace its dialog construction:

```cpp
// Before: PlanStan-specific CalDAV dialog
auto *dlg = new CalDavAddDialog(this);

// After: library dialog (kinds assembled from BackendRegistry)
QList<Kalburator::Ui::ProviderConfigDialog::ProviderKind> kinds;
for (const auto *c : backendRegistry()->providerContributions())
    kinds.append({ c->backendType(), labelForKind(c->backendType()) });
auto *dlg = new Kalburator::Ui::ProviderConfigDialog(
    providerManager(), kinds,
    Kalburator::Ui::ProviderConfigDialog::AddNew, {}, this);
```

The rest of the slot (post-`dlg->exec() == Accepted`: read result,
persist, fire logical-calendar binding UI) is unchanged — it
still receives a `BackendConfiguration` + `QList<CollectionInfo>`.

- [ ] **Step 2: Repurpose the entry point**

If there were separate "Add CalDAV" / "Add CardDAV" buttons (or
slots) in the UI, consolidate them into one "Add account" button.
With the library dialog, the provider combo handles the selection;
having separate buttons is redundant.

- [ ] **Step 3: Verify CalDAV flow still works (baseline test passes)**

```bash
ctest --test-dir PlanStan/build-dev -R tst_collectioncontroller_caldav_baseline --output-on-failure
```

Expected: PASS (no regression — same `BackendConfiguration` shape,
same logical-calendar binding fires).

- [ ] **Step 4: Add a CardDAV regression test (D.1 closure)**

Add `tst_collectioncontroller_carddav_add.cpp`. Exercise: combo
selects CardDAV → fill server URL + creds → click Test → see
addressbook list → click Save → assert `BackendConfiguration`
with `kind == "carddav"` is persisted.

```bash
ctest --test-dir PlanStan/build-dev -R tst_collectioncontroller_carddav_add --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/controllers/collectioncontroller.cpp \
        tests/.../tst_collectioncontroller_carddav_add.cpp \
        tests/CMakeLists.txt
git commit -m "M.16: PlanStan CollectionController hosts library ProviderConfigDialog

Consolidates 'Add CalDAV / Add CardDAV / Add Akonadi' into one
provider-polymorphic 'Add account' flow via the library dialog.
CalDAV behavior preserved (M.15 baseline still passes); CardDAV
add-account now works (closes D.1)."
```

---

## Task 17: Update `04w-deferred-work.md`

Flip B.5 and D.1 to ✅ landed.

**Files:** `libkalburator/docs/phase0/04w-deferred-work.md`

- [ ] **Step 1: Edit B.5 section header line**

Change:
```
**Status:** ⬜ deferred indefinitely.
```
to:
```
**Status:** ✅ landed 2026-05-?? (Phase M, tag `v0.42-phase-m-multi-protocol-dav`).
```

(Replace `??` with the actual landing date at task time.)

- [ ] **Step 2: Edit D.1 section header line**

Change:
```
**Status:** ⬜ deferred from Phase Ib (Phase Ib in flight 2026-05-08).
**Target phase:** Phase Ic.
```
to:
```
**Status:** ✅ landed 2026-05-?? (Phase M side-effect — the lifted
ProviderConfigDialog is provider-polymorphic, so CardDAV came
along automatically).
```

- [ ] **Step 3: Commit (libkalburator)**

```bash
cd libkalburator
git add docs/phase0/04w-deferred-work.md
git commit -m "M.17: 04w — flip B.5 + D.1 to ✅ landed (Phase M)"
```

---

## Task 18: Write `04ac-phase-m-status.md`

Status doc summarizing what landed and where the per-task evidence
lives.

**Files:** `libkalburator/docs/phase0/04ac-phase-m-status.md` (NEW;
verify at task time that no other 04ac-* exists).

- [ ] **Step 1: Confirm next-available 04* slot**

```bash
ls libkalburator/docs/phase0/04* 2>&1 | tail -10
```

If `04ac-*` exists, bump to `04ad-`. Use the lowest unused slot.

- [ ] **Step 2: Write the status doc**

Template structure (match 04y-phase-l-status.md style):

```markdown
# Phase M — Multi-protocol DAV provider + UI lift — status

**Status:** ✅ landed 2026-05-??
**Tag:** `v0.42-phase-m-multi-protocol-dav`
**Spec / plan:**
  - Design: `2026-05-16-phase-m-multi-protocol-dav-design.md`
  - Plan: `2026-05-16-phase-m-multi-protocol-dav-plan.md`

## What landed

[Brief 200-word summary of the phase, per the design's acceptance
criteria. Cross-reference key commits.]

## Per-task evidence

[Table or list mapping M.1 → M.21 commits.]

## Out-of-scope items confirmed deferred

- B.4 (KWallet), B.1 (engine-level ETag), B.2 (CTag), B.3 (RFC 6764
  SRV-based discovery), B.6 (vCard version hardening) — still in
  04w. No closure motion for these in Phase M.

## Tests

- libkalburator: N/N pass (refresh actual count from `verify-all.sh`).
- WildPalms: N/N pass.
- PlanStan: N/N pass.
```

- [ ] **Step 3: Commit**

```bash
git add docs/phase0/04ac-phase-m-status.md
git commit -m "M.18: 04ac — Phase M status doc"
```

---

## Task 19: Update `FINDINGS.md`

Append non-obvious discoveries from Phase M implementation.

**Files:** `~/dev/refactor-engine-merger/FINDINGS.md`

- [ ] **Step 1: Append entries**

At minimum, capture:

- **F-M1: Existing discovery classes already protocol-isolated.**
  The shared-helpers extraction proposed in the design turned out
  to be unnecessary because `CalDavCapabilityDiscovery` and
  `CardDavCapabilityDiscovery` are already standalone QObjects
  with clean per-protocol scopes. The composition path
  (provider owns one of each) is lighter touch than extracting
  free functions.

- **F-M2: ProviderManager API surface for the combo population.**
  (Whatever the engineer discovered in Task 11 step 2 — the
  accessor name that actually exists for "list of registered
  provider kinds".)

- **F-M3: any partial-success quirks discovered against real
  servers** (e.g., Nextcloud's well-known redirect chain, or
  Sabre/DAV's response format).

- Anything else surfaced during the phase.

- [ ] **Step 2: Commit**

(The coordination folder isn't a git repo, but FINDINGS.md is the
durable log — append by editing the file, no commit needed.)

---

## Task 20: Update `CURRENT-STATUS.md` + `ROADMAP.md` + submodule bumps

**Files:**
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- `~/dev/refactor-engine-merger/ROADMAP.md`
- Submodule pointers in the coordination folder

- [ ] **Step 1: Update CURRENT-STATUS.md**

Bump date, replace `Where we are` section to read "✅ Phase M
complete (2026-05-??)". Prepend a "Recently committed (Phase M)"
block summarizing tasks M.1–M.21. Update "Next" to point at the
next deferred-work choice (or "no further phase queued — pick from
04w").

- [ ] **Step 2: Update ROADMAP.md**

Add to the at-a-glance status table after Phase L:
```
| M — Multi-protocol DAV provider + UI lift (M.1–M.21: shared
provider widgets in library, NextcloudProvider, both consumers
migrated) | ✅ landed 2026-05-?? | `v0.42-phase-m-multi-protocol-dav` |
```

Add to the tag list:
```
- `v0.42-phase-m-multi-protocol-dav` ✅ (Phase M — multi-protocol
  DAV + UI lift, landed 2026-05-??)
```

- [ ] **Step 3: Bump WildPalms + PlanStan submodule pointers (if applicable)**

If `~/dev/refactor-engine-merger/` tracks submodule pointers via
plain `git` in the coordination folder (which the project notes
say it doesn't — it's a coordination folder, not a repo), skip
this step. Otherwise, advance each submodule pointer to the
latest Phase M commit on `refactor/engine-merger`.

- [ ] **Step 4: No commit (coordination folder is not a git repo).**

---

## Task 21: `verify-all.sh` + baseline refresh + tag

**Files:** baseline files + tag.

- [ ] **Step 1: Run verify-all**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected: exit 0 (baseline match) OR exit 3 (improvement — new
tests pass that weren't in baseline; this is the expected case
because Phase M added tests).

- [ ] **Step 2: If exit 3, refresh baselines**

Per CLAUDE.md guidance: investigate before refreshing — confirm
the new passes are the Phase M tests, not flakes flipping. Then:

```bash
cp baselines/libkalburator-worktree-ctest.txt baselines/libkalburator-worktree-ctest.txt.bak
# regenerate baselines per project convention (verify-all.sh
# may have a --refresh flag; if not, copy the .last files)
cp baselines/libkalburator-worktree-ctest.txt.last \
   baselines/libkalburator-worktree-ctest.txt
# repeat for planstan, wildpalms
./scripts/verify-all.sh    # confirm exit 0
```

- [ ] **Step 3: Confirm `refactor/engine-merger` HEAD in libkalburator is the right commit for the tag**

```bash
cd libkalburator
git log --oneline -10
```

Expected: M.18 (status doc) is the latest commit (or M.17 if status
doc is in same commit as 04w flip — engineer's call).

- [ ] **Step 4: Ask user to tag**

Per `~/dev/refactor-engine-merger/CLAUDE.md`, `git tag` is
user-run (destructive op). Stop here and prompt the user:

> "Phase M complete and verify-all clean. Ready for tag. User
> runs: `cd ~/dev/refactor-engine-merger/libkalburator && git
> tag v0.42-phase-m-multi-protocol-dav`."

---

## Acceptance gate

After Task 21:

- [ ] Tag `v0.42-phase-m-multi-protocol-dav` exists on libkalburator
      `refactor/engine-merger` HEAD.
- [ ] `verify-all.sh` exit 0 against refreshed baselines.
- [ ] Manual smoke test against a real Nextcloud (or
      Radicale/Sabre instance) confirms the round trip — record
      result in `FINDINGS.md` per Phase L precedent.
- [ ] `04w-deferred-work.md` shows B.5 ✅ + D.1 ✅.
- [ ] `CURRENT-STATUS.md` + `ROADMAP.md` reflect reality.

Phase M complete.
