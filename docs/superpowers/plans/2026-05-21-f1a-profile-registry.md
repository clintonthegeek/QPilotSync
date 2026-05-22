# F.1a — Profile persistence + app registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure WildPalms profile persistence into three KConfig files and introduce an app-level `ProfileRegistry`, replacing today's "open by directory" flow.

**Architecture:** Add a new `ProfileRegistry` class under `src/runtime/` that owns `~/.config/wildpalms/wildpalmsrc` and a list of `ProfileEntry` records. Rewrite `Profile::load()` / `Profile::save()` to use three files per profile (`profile.conf` + `accounts.conf` + `mappings.conf`) instead of a single `.wildpalms.conf`. Drop the dead `[conduits]` / `[databaseHandlers]` legacy. Replace `KF6MainWindow`'s `onOpenProfile`/directory-picker startup with registry-driven discovery + a stopgap one-field New Profile dialog (F.1b later supplies the full menu, F.1c the wizard).

**Tech Stack:** Qt6 (QSettings INI, QObject, QSignal/Slot), KF6 (KSharedConfig, KConfigGroup), QtTest, C++20, CMake. Build dir `build-fetchcontent/`. Test main macros `WILDPALMS_QTEST_MAIN` / `WILDPALMS_QTEST_GUILESS_MAIN` (see `tests/wildpalms_qtest_main.h`).

**Reference spec:** `docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md`. Re-read §4 (on-disk layout), §5 (ProfileRegistry API), §6 (Profile changes) when starting any task.

---

## Pre-flight notes for whoever runs this

- **Repo:** `~/dev/WildPalms`. Branch `main`. Don't start work without `git pull`.
- **Build directory:** `build-fetchcontent/`. Configure with `cmake -S . -B build-fetchcontent -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` if the dir doesn't exist. `compile_commands.json` symlink at repo root points there.
- **Test runner:** `cd build-fetchcontent && ctest --output-on-failure`. Individual tests: `./tests/<name>` or `ctest -R <pattern>`.
- **Test baseline at plan start:** 74/74 tests pass (verified at the start of Phase F brainstorming). Re-verify at T1.
- **No subagent isolation needed.** Plan lands directly on `main` task-by-task. Each task ends with a commit; main stays green.
- **No backward compatibility.** Old `.wildpalms.conf` files don't need to keep loading. Don't write migration code.
- **Memory file pointer:** When you remove conduit/databaseHandler tests, search the repo for leftover documentation referring to those methods and update / remove the stale references (see T9).

---

### Task 1: Pre-flight — baseline ctest green

**Files:**
- Read-only: confirm `build-fetchcontent/` is configured and current.

- [ ] **Step 1: Confirm the build directory exists and is current.**

Run:
```bash
cd ~/dev/WildPalms
ls build-fetchcontent/CMakeCache.txt
```
Expected: file exists. If not, configure:
```bash
cmake -S . -B build-fetchcontent -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

- [ ] **Step 2: Build everything.**

Run:
```bash
cmake --build build-fetchcontent --parallel
```
Expected: clean build, no warnings about WildPalms code (warnings inside `_deps/libkalburator-src/` are OK and not our concern).

- [ ] **Step 3: Run the full ctest suite and record the baseline.**

Run:
```bash
cd build-fetchcontent && ctest --output-on-failure 2>&1 | tail -5
```
Expected: line ending in `100% tests passed, 0 tests failed out of 74` (or similar). Note the exact count (e.g. 74) — every subsequent task's exit gate is "ctest count ≥ baseline count and 100% pass". If the baseline isn't green, **stop and investigate** before proceeding; do not write F.1a code on a red baseline.

- [ ] **Step 4: Capture baseline count and stash for later tasks.**

```bash
cd ~/dev/WildPalms
ctest --test-dir build-fetchcontent 2>&1 | tail -1 > /tmp/f1a-baseline.txt
cat /tmp/f1a-baseline.txt
```

No commit. This is a verification step only.

---

### Task 2: ProfileEntry struct + ProfileRegistry header skeleton

**Files:**
- Create: `src/runtime/profileregistry.h`
- Create: `src/runtime/profileregistry.cpp` (empty body, just constructor / destructor)
- Modify: `src/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the header.**

Create `src/runtime/profileregistry.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_PROFILEREGISTRY_H
#define WILDPALMS_RUNTIME_PROFILEREGISTRY_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <KSharedConfig>

namespace WildPalms::Runtime {

/// A single entry in the app-level profile registry.
///
/// `id` is sticky: it matches the on-disk directory basename of the
/// profile and never changes for the life of the profile (even if
/// the user renames the display name). New profiles get the next
/// free `profileN` integer suffix.
///
/// `lastOpened` drives sorting in entries() and last-active recovery.
struct ProfileEntry {
    QString   id;
    QString   name;
    QString   path;
    QDateTime lastOpened;

    bool isValid() const { return !id.isEmpty(); }
};

/// App-level profile registry persisted at
/// ~/.config/wildpalms/wildpalmsrc. One [profile-<id>] group per
/// registered profile plus [General]/lastActiveProfileId.
///
/// One instance per running app; KF6MainWindow owns it. Tests get a
/// second constructor that accepts an explicit KSharedConfig::Ptr +
/// override the default profile root via setDefaultRoot().
class ProfileRegistry : public QObject {
    Q_OBJECT
public:
    explicit ProfileRegistry(QObject *parent = nullptr);

    /// Test seam: use an explicit KSharedConfig (typically pointed at
    /// a QTemporaryDir) instead of the default per-user one. F.1a §11
    /// open implementation point — picked option (a).
    ProfileRegistry(KSharedConfig::Ptr config, QObject *parent = nullptr);

    ~ProfileRegistry() override;

    QList<ProfileEntry> entries() const;
    ProfileEntry        entry(const QString &id) const;
    QString             lastActiveId() const;

    ProfileEntry registerNew(const QString &name,
                             const QString &customPath = QString());
    ProfileEntry registerExisting(const QString &path);
    bool         unregister(const QString &id);
    void         setLastActive(const QString &id);

    QString defaultRoot() const;
    void    setDefaultRoot(const QString &root);

    QString allocateNewId() const;

signals:
    void registryChanged();
    void entryUpdated(QString id);

private:
    QString             m_defaultRoot;
    KSharedConfig::Ptr  m_config;
    QList<ProfileEntry> m_cache;
    QString             m_lastActiveId;

    void load();
    void save() const;

    static bool isValidIdChars(const QString &id);
};

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_PROFILEREGISTRY_H
```

- [ ] **Step 2: Write the stub implementation.**

Create `src/runtime/profileregistry.cpp`:

```cpp
#include "profileregistry.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

namespace WildPalms::Runtime {

ProfileRegistry::ProfileRegistry(QObject *parent)
    : QObject(parent)
    , m_defaultRoot(QDir::homePath() + QStringLiteral("/.wildpalms"))
    , m_config(KSharedConfig::openConfig())
{
    load();
}

ProfileRegistry::ProfileRegistry(KSharedConfig::Ptr config, QObject *parent)
    : QObject(parent)
    , m_defaultRoot(QDir::homePath() + QStringLiteral("/.wildpalms"))
    , m_config(config)
{
    load();
}

ProfileRegistry::~ProfileRegistry() = default;

QList<ProfileEntry> ProfileRegistry::entries() const
{
    return m_cache;
}

ProfileEntry ProfileRegistry::entry(const QString &id) const
{
    for (const auto &e : m_cache)
        if (e.id == id) return e;
    return ProfileEntry{};
}

QString ProfileRegistry::lastActiveId() const
{
    return m_lastActiveId;
}

QString ProfileRegistry::defaultRoot() const
{
    return m_defaultRoot;
}

void ProfileRegistry::setDefaultRoot(const QString &root)
{
    m_defaultRoot = root;
}

QString ProfileRegistry::allocateNewId() const
{
    for (int i = 1; ; ++i) {
        const QString id = QStringLiteral("profile%1").arg(i);
        bool taken = false;
        for (const auto &e : m_cache) {
            if (e.id == id) { taken = true; break; }
        }
        if (!taken) return id;
    }
}

bool ProfileRegistry::isValidIdChars(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return re.match(id).hasMatch();
}

ProfileEntry ProfileRegistry::registerNew(const QString & /*name*/,
                                          const QString & /*customPath*/)
{
    // Implemented in Task 5.
    return ProfileEntry{};
}

ProfileEntry ProfileRegistry::registerExisting(const QString & /*path*/)
{
    // Implemented in Task 6.
    return ProfileEntry{};
}

bool ProfileRegistry::unregister(const QString & /*id*/)
{
    // Implemented in Task 7.
    return false;
}

void ProfileRegistry::setLastActive(const QString & /*id*/)
{
    // Implemented in Task 7.
}

void ProfileRegistry::load()
{
    // Implemented in Task 4.
}

void ProfileRegistry::save() const
{
    // Implemented in Task 4.
}

} // namespace WildPalms::Runtime
```

- [ ] **Step 3: Add the new files to `WildPalmsRuntime`'s sources.**

Edit `src/runtime/CMakeLists.txt`. After the existing `add_library(WildPalmsRuntime STATIC ...)` block (before `target_sources(WildPalmsRuntime PRIVATE ...)`), add `profileregistry.h` and `profileregistry.cpp` to the source list. The block currently ends with `pilotlinkconnectionfactory.h)`; add the two new lines before that closing paren:

```cmake
    # F.1a — App-level profile registry.
    profileregistry.h
    profileregistry.cpp
    # Phase E.16 — pilot-link factory header (forward-decls only;
    # ...
```

- [ ] **Step 4: Add KF6::ConfigCore to WildPalmsRuntime's link line.**

`profileregistry.cpp` uses `KSharedConfig` from `KF6::ConfigCore`. Edit `src/runtime/CMakeLists.txt`'s `target_link_libraries(WildPalmsRuntime ...)` block. The current `PUBLIC` link list has `Qt::Core`, `Qt::Widgets`, `KF6::CoreAddons`, `Kalburator::Sync`. Add `KF6::ConfigCore`:

```cmake
target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        Qt::Widgets
        KF6::CoreAddons
        KF6::ConfigCore   # F.1a — KSharedConfig in profileregistry.cpp
        Kalburator::Sync
    INTERFACE
        WildPalmsCore
)
```

- [ ] **Step 5: Build.**

Run:
```bash
cmake --build build-fetchcontent --parallel
```
Expected: clean build. Both files compile; no test changes yet.

- [ ] **Step 6: Confirm baseline tests still pass.**

Run:
```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: same count as baseline, 100% pass.

- [ ] **Step 7: Commit.**

```bash
cd ~/dev/WildPalms
git add src/runtime/profileregistry.h src/runtime/profileregistry.cpp src/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
F.1a T2: ProfileRegistry skeleton + ProfileEntry struct

New empty-stub class under src/runtime/. Methods are scaffolded but
not implemented yet — registerNew/registerExisting/unregister/
setLastActive/load/save are all bodyless. allocateNewId and
isValidIdChars are real because they're pure (no IO).

Wires KF6::ConfigCore into WildPalmsRuntime's link line so
KSharedConfig is available in subsequent tasks.

Build green, baseline ctest unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: ProfileRegistry pure helpers — `defaultRoot`, `setDefaultRoot`, `allocateNewId`

**Files:**
- Create: `tests/runtime/tst_profileregistry.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Add the test file with the three tests for the pure helpers.**

Create `tests/runtime/tst_profileregistry.cpp`:

```cpp
#include <QtTest/QtTest>

#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileRegistry : public QObject
{
    Q_OBJECT
private slots:
    void defaultRootIsUnderHome();
    void setDefaultRootOverrides();
    void allocateNewIdOnEmptyRegistry();
};

void TstProfileRegistry::defaultRootIsUnderHome()
{
    ProfileRegistry reg;
    QVERIFY(reg.defaultRoot().contains(QStringLiteral(".wildpalms")));
    QVERIFY(!reg.defaultRoot().isEmpty());
}

void TstProfileRegistry::setDefaultRootOverrides()
{
    ProfileRegistry reg;
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    reg.setDefaultRoot(tmp.path());
    QCOMPARE(reg.defaultRoot(), tmp.path());
}

void TstProfileRegistry::allocateNewIdOnEmptyRegistry()
{
    ProfileRegistry reg;
    // Empty registry — first allocation is profile1.
    QCOMPARE(reg.allocateNewId(), QStringLiteral("profile1"));
    QCOMPARE(reg.allocateNewId(), QStringLiteral("profile1"));  // pure; no side effect
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistry)
#include "tst_profileregistry.moc"
```

- [ ] **Step 2: Register the test in `tests/runtime/CMakeLists.txt`.**

Open `tests/runtime/CMakeLists.txt`, find the existing runtime-test helper (the equivalent of `add_wildpalms_test` or whatever the file's helper is named). Add an entry for the new test using the same pattern as `tst_account_controller`:

```cmake
add_wildpalms_runtime_test(tst_profileregistry tst_profileregistry.cpp)
```

(If the helper is named differently, use whatever the existing entries use. Look for the line registering `tst_account_controller` and clone it.)

- [ ] **Step 3: Configure and build.**

```bash
cmake --build build-fetchcontent --parallel
```
Expected: clean build, new test binary `tst_profileregistry` appears in the build dir.

- [ ] **Step 4: Run the new test and verify it passes.**

```bash
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: 1/1 PASS — `defaultRootIsUnderHome`, `setDefaultRootOverrides`, `allocateNewIdOnEmptyRegistry` all green.

- [ ] **Step 5: Run the full suite — total count must be baseline + 1.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 75/75 pass (one new test added).

- [ ] **Step 6: Commit.**

```bash
git add tests/runtime/tst_profileregistry.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
F.1a T3: tests for ProfileRegistry pure helpers

defaultRoot, setDefaultRoot, allocateNewId all work without IO.
allocateNewId returns profile1 on empty registry and is idempotent.
ctest 75/75 green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: ProfileRegistry persistence — `load()` + `save()` + test-seam ctor

**Files:**
- Modify: `src/runtime/profileregistry.cpp`
- Modify: `tests/runtime/tst_profileregistry.cpp`

- [ ] **Step 1: Add failing tests for load/save and the entry accessors.**

Append to `tests/runtime/tst_profileregistry.cpp` (before the `WILDPALMS_QTEST_GUILESS_MAIN` line, inside the class body — add the slot declarations + bodies):

In the `private slots:` block, add:
```cpp
    void emptyRegistry();
    void persistenceRoundTrip();
```

Then add the implementations before `WILDPALMS_QTEST_GUILESS_MAIN`:

```cpp
void TstProfileRegistry::emptyRegistry()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/test-wprc"));
    ProfileRegistry reg(cfg);

    QVERIFY(reg.entries().isEmpty());
    QCOMPARE(reg.lastActiveId(), QString());
    QVERIFY(!reg.entry(QStringLiteral("profile1")).isValid());
}

void TstProfileRegistry::persistenceRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString cfgPath = tmp.path() + QStringLiteral("/test-wprc");

    // Write directly into the KConfig file as if a prior session had
    // registered two profiles.
    {
        auto cfg = KSharedConfig::openConfig(cfgPath);
        KConfigGroup g1(cfg, QStringLiteral("profile-profile1"));
        g1.writeEntry("name", "Alpha");
        g1.writeEntry("path", "/tmp/alpha");
        g1.writeEntry("lastOpened",
                      QDateTime::fromString(QStringLiteral("2026-05-21T10:00:00Z"),
                                            Qt::ISODate));
        KConfigGroup g2(cfg, QStringLiteral("profile-profile2"));
        g2.writeEntry("name", "Beta");
        g2.writeEntry("path", "/tmp/beta");
        g2.writeEntry("lastOpened",
                      QDateTime::fromString(QStringLiteral("2026-05-21T12:00:00Z"),
                                            Qt::ISODate));
        KConfigGroup gen(cfg, QStringLiteral("General"));
        gen.writeEntry("lastActiveProfileId", "profile2");
        cfg->sync();
    }

    // Load into a fresh registry.
    auto cfg = KSharedConfig::openConfig(cfgPath);
    ProfileRegistry reg(cfg);

    const auto entries = reg.entries();
    QCOMPARE(entries.size(), 2);
    // Sorted by lastOpened desc: profile2 (12:00) before profile1 (10:00).
    QCOMPARE(entries.at(0).id, QStringLiteral("profile2"));
    QCOMPARE(entries.at(0).name, QStringLiteral("Beta"));
    QCOMPARE(entries.at(0).path, QStringLiteral("/tmp/beta"));
    QCOMPARE(entries.at(1).id, QStringLiteral("profile1"));
    QCOMPARE(reg.lastActiveId(), QStringLiteral("profile2"));
    QVERIFY(reg.entry(QStringLiteral("profile1")).isValid());
    QVERIFY(!reg.entry(QStringLiteral("nope")).isValid());
}
```

At the top of the test file, after the existing includes, add:
```cpp
#include <KConfigGroup>
#include <QDateTime>
```

- [ ] **Step 2: Build and verify these tests fail.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: `emptyRegistry` PASS (empty cache by default). `persistenceRoundTrip` FAIL: entries is empty because `load()` is a no-op.

- [ ] **Step 3: Implement `load()` and `save()`.**

Edit `src/runtime/profileregistry.cpp`. Add `#include <KConfigGroup>` to the includes block. Replace the stub `load()` and `save()` bodies with:

```cpp
void ProfileRegistry::load()
{
    m_cache.clear();
    m_lastActiveId.clear();

    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("profile-"))) continue;
        const QString id = g.mid(QStringLiteral("profile-").size());
        if (id.isEmpty()) continue;

        KConfigGroup cg(m_config, g);
        ProfileEntry e;
        e.id         = id;
        e.name       = cg.readEntry("name", QString());
        e.path       = cg.readEntry("path", QString());
        e.lastOpened = cg.readEntry("lastOpened", QDateTime());
        m_cache.append(e);
    }

    KConfigGroup gen(m_config, QStringLiteral("General"));
    m_lastActiveId = gen.readEntry("lastActiveProfileId", QString());

    // Sort by lastOpened descending (most recent first).
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });
}

void ProfileRegistry::save() const
{
    // Clear all profile-* groups first.
    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (g.startsWith(QStringLiteral("profile-")))
            m_config->deleteGroup(g);
    }

    // Re-write each cache entry.
    for (const auto &e : m_cache) {
        KConfigGroup cg(m_config, QStringLiteral("profile-") + e.id);
        cg.writeEntry("name", e.name);
        cg.writeEntry("path", e.path);
        cg.writeEntry("lastOpened", e.lastOpened);
    }

    // [General]/lastActiveProfileId.
    KConfigGroup gen(m_config, QStringLiteral("General"));
    gen.writeEntry("lastActiveProfileId", m_lastActiveId);

    m_config->sync();
}
```

Note: `save()` is `const`, but it mutates the on-disk config. That's idiomatic Qt/KConfig — the persistence is a logical no-state-change from the caller's perspective.

- [ ] **Step 4: Build and run.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: both `emptyRegistry` and `persistenceRoundTrip` PASS.

- [ ] **Step 5: Run full suite.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 75/75 pass (count unchanged; tests added to existing binary).

- [ ] **Step 6: Commit.**

```bash
git add src/runtime/profileregistry.cpp tests/runtime/tst_profileregistry.cpp
git commit -m "$(cat <<'EOF'
F.1a T4: ProfileRegistry load/save + entry/lastActiveId accessors

load() walks [profile-*] KConfig groups, sorts by lastOpened desc.
save() rewrites all profile-* groups + [General]/lastActiveProfileId.
Test-seam ctor lets tests point KSharedConfig at a QTemporaryDir.

ctest 75/75 green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: ProfileRegistry — `registerNew` (both forms) + stub profile.conf

**Files:**
- Modify: `src/runtime/profileregistry.cpp`
- Modify: `tests/runtime/tst_profileregistry.cpp`

- [ ] **Step 1: Add failing tests.**

In `tests/runtime/tst_profileregistry.cpp`, add slot declarations:
```cpp
    void registerNewNoPath();
    void registerNewWritesStubProfileConf();
    void registerNewCustomPath();
    void registerNewCustomPathInvalidId();
    void registerNewSecondAllocation();
```

Add the implementations:

```cpp
void TstProfileRegistry::registerNewNoPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto e = reg.registerNew(QStringLiteral("Test Profile"));
    QVERIFY(e.isValid());
    QCOMPARE(e.id, QStringLiteral("profile1"));
    QCOMPARE(e.name, QStringLiteral("Test Profile"));
    QCOMPARE(e.path, tmp.path() + QStringLiteral("/wp-root/profile1"));
    QVERIFY(QDir(e.path).exists());
}

void TstProfileRegistry::registerNewWritesStubProfileConf()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto e = reg.registerNew(QStringLiteral("Stub Test"));
    QVERIFY(e.isValid());

    const QString confPath = e.path + QStringLiteral("/profile.conf");
    QVERIFY2(QFile::exists(confPath),
             qPrintable(QStringLiteral("Expected stub profile.conf at: ") + confPath));

    QSettings s(confPath, QSettings::IniFormat);
    QCOMPARE(s.value("meta/schemaVersion").toInt(), 1);
    QCOMPARE(s.value("profile/id").toString(), e.id);
    QCOMPARE(s.value("profile/name").toString(), QStringLiteral("Stub Test"));
}

void TstProfileRegistry::registerNewCustomPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const QString customPath = tmp.path() + QStringLiteral("/my-special-profile");
    const auto e = reg.registerNew(QStringLiteral("Special"), customPath);
    QVERIFY(e.isValid());
    QCOMPARE(e.id, QStringLiteral("my-special-profile"));  // basename
    QCOMPARE(e.path, customPath);
}

void TstProfileRegistry::registerNewCustomPathInvalidId()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    // Basename contains a dot; not in [A-Za-z0-9_-]+.
    const QString bad = tmp.path() + QStringLiteral("/bad.name");
    const auto e = reg.registerNew(QStringLiteral("X"), bad);
    QVERIFY(!e.isValid());
}

void TstProfileRegistry::registerNewSecondAllocation()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto a = reg.registerNew(QStringLiteral("A"));
    const auto b = reg.registerNew(QStringLiteral("B"));
    QVERIFY(a.isValid());
    QVERIFY(b.isValid());
    QCOMPARE(a.id, QStringLiteral("profile1"));
    QCOMPARE(b.id, QStringLiteral("profile2"));
    QCOMPARE(reg.entries().size(), 2);
}
```

Add `#include <QFile>` and `#include <QSettings>` to the includes if not already present.

- [ ] **Step 2: Build and verify they fail.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: the five new tests FAIL (registerNew returns invalid entry; nothing written to disk).

- [ ] **Step 3: Implement `registerNew`.**

In `src/runtime/profileregistry.cpp`, add `#include <QFile>`, `#include <QFileInfo>`, `#include <QSettings>`. Replace the stub `registerNew` with:

```cpp
ProfileEntry ProfileRegistry::registerNew(const QString &name,
                                          const QString &customPath)
{
    ProfileEntry e;
    e.name       = name;
    e.lastOpened = QDateTime::currentDateTimeUtc();

    if (customPath.isEmpty()) {
        e.id   = allocateNewId();
        e.path = m_defaultRoot + QLatin1Char('/') + e.id;
    } else {
        const QString basename = QFileInfo(customPath).fileName();
        if (!isValidIdChars(basename))
            return ProfileEntry{};
        // Reject if id is already registered.
        for (const auto &existing : m_cache) {
            if (existing.id == basename)
                return ProfileEntry{};
        }
        e.id   = basename;
        e.path = customPath;
    }

    // Create the directory.
    QDir dir(e.path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return ProfileEntry{};

    // Write stub profile.conf so the profile is immediately loadable
    // by Profile::load(). Spec §11 open implementation point — picked
    // option (a).
    {
        const QString confPath = e.path + QStringLiteral("/profile.conf");
        QSettings s(confPath, QSettings::IniFormat);
        s.setValue(QStringLiteral("meta/schemaVersion"), 1);
        s.setValue(QStringLiteral("profile/id"), e.id);
        s.setValue(QStringLiteral("profile/name"), e.name);
        s.sync();
        if (s.status() != QSettings::NoError)
            return ProfileEntry{};
    }

    m_cache.append(e);
    // Re-sort so newest is first.
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });

    save();
    emit registryChanged();
    return e;
}
```

- [ ] **Step 4: Build and verify the new tests pass.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: all current tests in `tst_profileregistry` PASS, including the five new ones.

- [ ] **Step 5: Run full suite.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 75/75 pass.

- [ ] **Step 6: Commit.**

```bash
git add src/runtime/profileregistry.cpp tests/runtime/tst_profileregistry.cpp
git commit -m "$(cat <<'EOF'
F.1a T5: ProfileRegistry::registerNew (both forms) + stub profile.conf

registerNew(name) allocates profileN under defaultRoot, creates the
directory, writes a stub profile.conf with schemaVersion + id +
name so the profile is immediately Profile::load()-able.
registerNew(name, customPath) uses the path's basename as the id,
rejecting non-[A-Za-z0-9_-]+ names. Both forms emit registryChanged.

ctest 75/75 green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: ProfileRegistry — `registerExisting`

**Files:**
- Modify: `src/runtime/profileregistry.cpp`
- Modify: `tests/runtime/tst_profileregistry.cpp`

- [ ] **Step 1: Add failing tests.**

In `tst_profileregistry.cpp`, add slot declarations:
```cpp
    void registerExistingValid();
    void registerExistingMissingConf();
    void registerExistingIdMismatch();
    void registerExistingIdConflict();
```

Implementations:

```cpp
void TstProfileRegistry::registerExistingValid()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Pre-create a profile dir with a valid profile.conf.
    const QString dir = tmp.path() + QStringLiteral("/imported");
    QVERIFY(QDir().mkpath(dir));
    {
        QSettings s(dir + QStringLiteral("/profile.conf"), QSettings::IniFormat);
        s.setValue("meta/schemaVersion", 1);
        s.setValue("profile/id", "imported");
        s.setValue("profile/name", "Imported");
        s.sync();
    }

    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const auto e = reg.registerExisting(dir);
    QVERIFY(e.isValid());
    QCOMPARE(e.id, QStringLiteral("imported"));
    QCOMPARE(e.name, QStringLiteral("Imported"));
    QCOMPARE(e.path, dir);
}

void TstProfileRegistry::registerExistingMissingConf()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/empty");
    QVERIFY(QDir().mkpath(dir));

    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const auto e = reg.registerExisting(dir);
    QVERIFY(!e.isValid());
}

void TstProfileRegistry::registerExistingIdMismatch()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/foo");
    QVERIFY(QDir().mkpath(dir));
    {
        QSettings s(dir + QStringLiteral("/profile.conf"), QSettings::IniFormat);
        s.setValue("meta/schemaVersion", 1);
        s.setValue("profile/id", "bar");  // id != basename
        s.setValue("profile/name", "Mismatch");
        s.sync();
    }

    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const auto e = reg.registerExisting(dir);
    QVERIFY(!e.isValid());
}

void TstProfileRegistry::registerExistingIdConflict()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    // Register profile1 via the normal path.
    const auto first = reg.registerNew(QStringLiteral("A"));
    QVERIFY(first.isValid());

    // Pre-create a dir at a different location but with id=profile1.
    const QString dupDir = tmp.path() + QStringLiteral("/profile1");
    QVERIFY(QDir().mkpath(dupDir));
    {
        QSettings s(dupDir + QStringLiteral("/profile.conf"), QSettings::IniFormat);
        s.setValue("meta/schemaVersion", 1);
        s.setValue("profile/id", "profile1");
        s.setValue("profile/name", "Duplicate");
        s.sync();
    }

    const auto e = reg.registerExisting(dupDir);
    QVERIFY(!e.isValid());
}
```

- [ ] **Step 2: Build + verify they fail.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: the four new tests FAIL.

- [ ] **Step 3: Implement `registerExisting`.**

Replace the stub `registerExisting` in `src/runtime/profileregistry.cpp` with:

```cpp
ProfileEntry ProfileRegistry::registerExisting(const QString &path)
{
    const QString confPath = path + QStringLiteral("/profile.conf");
    if (!QFile::exists(confPath))
        return ProfileEntry{};

    QSettings s(confPath, QSettings::IniFormat);
    const QString id   = s.value(QStringLiteral("profile/id")).toString();
    const QString name = s.value(QStringLiteral("profile/name")).toString();
    if (id.isEmpty())
        return ProfileEntry{};

    // id must match directory basename.
    const QString basename = QFileInfo(path).fileName();
    if (id != basename)
        return ProfileEntry{};

    // Reject if id already in cache (possibly at a different path).
    for (const auto &existing : m_cache) {
        if (existing.id == id)
            return ProfileEntry{};
    }

    ProfileEntry e;
    e.id         = id;
    e.name       = name;
    e.path       = path;
    e.lastOpened = QDateTime::currentDateTimeUtc();

    m_cache.append(e);
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });

    save();
    emit registryChanged();
    return e;
}
```

- [ ] **Step 4: Build + verify all tests pass.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 75/75 total pass.

- [ ] **Step 5: Commit.**

```bash
git add src/runtime/profileregistry.cpp tests/runtime/tst_profileregistry.cpp
git commit -m "$(cat <<'EOF'
F.1a T6: ProfileRegistry::registerExisting

Reads profile.conf at the given path, validates that profile/id
matches the directory basename, refuses if the id is already
registered. Used by F.1b's Import flow.

ctest 75/75 green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: ProfileRegistry — `unregister`, `setLastActive`, signals, sort

**Files:**
- Modify: `src/runtime/profileregistry.cpp`
- Modify: `tests/runtime/tst_profileregistry.cpp`

- [ ] **Step 1: Add failing tests.**

In `tst_profileregistry.cpp`, add slot declarations:
```cpp
    void unregisterDoesNotDelete();
    void unregisterUnknown();
    void setLastActiveRoundTrip();
    void entriesSortedByLastOpenedDesc();
    void registryChangedSignalFires();
    void allocateNewIdAfterUnregister();
```

Implementations:

```cpp
void TstProfileRegistry::unregisterDoesNotDelete()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto e = reg.registerNew(QStringLiteral("ToRemove"));
    QVERIFY(e.isValid());
    QVERIFY(QDir(e.path).exists());

    QVERIFY(reg.unregister(e.id));
    QVERIFY(reg.entries().isEmpty());
    QVERIFY(QDir(e.path).exists());  // dir survives
}

void TstProfileRegistry::unregisterUnknown()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    QVERIFY(!reg.unregister(QStringLiteral("never-registered")));
}

void TstProfileRegistry::setLastActiveRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto a = reg.registerNew(QStringLiteral("A"));
    const auto b = reg.registerNew(QStringLiteral("B"));
    QVERIFY(a.isValid() && b.isValid());

    const QDateTime beforeBump = reg.entry(a.id).lastOpened;
    reg.setLastActive(a.id);
    QCOMPARE(reg.lastActiveId(), a.id);
    QVERIFY(reg.entry(a.id).lastOpened >= beforeBump);

    // Round-trip via a fresh registry instance.
    ProfileRegistry reg2(cfg);
    QCOMPARE(reg2.lastActiveId(), a.id);
}

void TstProfileRegistry::entriesSortedByLastOpenedDesc()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto a = reg.registerNew(QStringLiteral("A"));   // older
    QTest::qSleep(20);
    const auto b = reg.registerNew(QStringLiteral("B"));   // newer

    // After registration, entries() is sorted by lastOpened desc.
    auto entries = reg.entries();
    QCOMPARE(entries.at(0).id, b.id);
    QCOMPARE(entries.at(1).id, a.id);

    // setLastActive bumps to head.
    QTest::qSleep(20);
    reg.setLastActive(a.id);
    entries = reg.entries();
    QCOMPARE(entries.at(0).id, a.id);
}

void TstProfileRegistry::registryChangedSignalFires()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    QSignalSpy spy(&reg, &ProfileRegistry::registryChanged);
    const auto e = reg.registerNew(QStringLiteral("X"));
    QVERIFY(e.isValid());
    QCOMPARE(spy.count(), 1);

    reg.unregister(e.id);
    QCOMPARE(spy.count(), 2);
}

void TstProfileRegistry::allocateNewIdAfterUnregister()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto a = reg.registerNew(QStringLiteral("A"));  // profile1
    const auto b = reg.registerNew(QStringLiteral("B"));  // profile2
    QCOMPARE(a.id, QStringLiteral("profile1"));
    QCOMPARE(b.id, QStringLiteral("profile2"));

    reg.unregister(a.id);  // drop profile1; gap remains
    // Next allocation must NOT recycle profile1 — should be profile3.
    QCOMPARE(reg.allocateNewId(), QStringLiteral("profile3"));
}
```

Add `#include <QSignalSpy>` and `#include <QTest>` if not already present.

- [ ] **Step 2: Build + verify they fail.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
```
Expected: the six new tests FAIL.

- [ ] **Step 3: Implement `unregister`, `setLastActive`, and fix `allocateNewId` semantics.**

Replace the stub `unregister` + `setLastActive` and update `allocateNewId` in `src/runtime/profileregistry.cpp`:

```cpp
bool ProfileRegistry::unregister(const QString &id)
{
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->id == id) {
            m_cache.erase(it);
            if (m_lastActiveId == id)
                m_lastActiveId.clear();
            save();
            emit registryChanged();
            return true;
        }
    }
    return false;
}

void ProfileRegistry::setLastActive(const QString &id)
{
    bool found = false;
    for (auto &e : m_cache) {
        if (e.id == id) {
            e.lastOpened = QDateTime::currentDateTimeUtc();
            found = true;
            break;
        }
    }
    if (!found) return;

    m_lastActiveId = id;
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });
    save();
    emit entryUpdated(id);
}
```

Update `allocateNewId` to never recycle by tracking a high-water mark. Replace it with:

```cpp
QString ProfileRegistry::allocateNewId() const
{
    // Find the highest profileN integer suffix currently or previously
    // recorded; allocate (max + 1). Ids are never recycled.
    int high = 0;
    static const QRegularExpression re(QStringLiteral("^profile(\\d+)$"));
    for (const auto &e : m_cache) {
        const auto m = re.match(e.id);
        if (m.hasMatch())
            high = std::max(high, m.captured(1).toInt());
    }
    // Also scan deleted-but-saved gaps via the highest recorded across
    // all KConfig profile-* groups (so allocate-after-unregister doesn't
    // recycle). We cache the highest seen even after unregister by
    // consulting m_config directly.
    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("profile-"))) continue;
        const auto m = re.match(g.mid(QStringLiteral("profile-").size()));
        if (m.hasMatch())
            high = std::max(high, m.captured(1).toInt());
    }
    return QStringLiteral("profile%1").arg(high + 1);
}
```

This consults both the in-memory cache and the persisted KConfig file. After `unregister` removes the entry from both, the high-water-mark check ensures the next allocation skips the gap — but only if the unregister also leaves a tombstone. Add tombstoning to `unregister`:

```cpp
bool ProfileRegistry::unregister(const QString &id)
{
    bool found = false;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->id == id) {
            m_cache.erase(it);
            found = true;
            break;
        }
    }
    if (!found) return false;

    if (m_lastActiveId == id)
        m_lastActiveId.clear();

    // Write a tombstone group so allocateNewId() still sees the id was used.
    KConfigGroup tomb(m_config, QStringLiteral("tombstone-") + id);
    tomb.writeEntry("retired", true);

    save();
    emit registryChanged();
    return true;
}
```

Update `allocateNewId` to also scan tombstones:

```cpp
QString ProfileRegistry::allocateNewId() const
{
    int high = 0;
    static const QRegularExpression re(QStringLiteral("^profile(\\d+)$"));
    auto scan = [&](const QString &candidate) {
        const auto m = re.match(candidate);
        if (m.hasMatch())
            high = std::max(high, m.captured(1).toInt());
    };

    for (const auto &e : m_cache) scan(e.id);

    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (g.startsWith(QStringLiteral("profile-")))
            scan(g.mid(QStringLiteral("profile-").size()));
        else if (g.startsWith(QStringLiteral("tombstone-")))
            scan(g.mid(QStringLiteral("tombstone-").size()));
    }
    return QStringLiteral("profile%1").arg(high + 1);
}
```

- [ ] **Step 4: Build + run tests.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profileregistry --output-on-failure
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 75/75 total pass; all `tst_profileregistry` tests green.

- [ ] **Step 5: Commit.**

```bash
git add src/runtime/profileregistry.cpp tests/runtime/tst_profileregistry.cpp
git commit -m "$(cat <<'EOF'
F.1a T7: ProfileRegistry — unregister, setLastActive, signals,
non-recycling allocation

unregister removes from cache, leaves a tombstone-* group so the
id is never recycled by future allocateNewId() calls. setLastActive
bumps lastOpened and re-sorts entries. registryChanged signal fires
on register/unregister. entryUpdated signal fires on setLastActive.

ctest 75/75 green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Profile — add `id()`, `defaultPathForId()`, `schemaVersion()` accessors

**Files:**
- Modify: `src/profile.h`
- Modify: `src/profile.cpp`
- Modify: `tests/test_profile.cpp`

- [ ] **Step 1: Add the accessor declarations to `Profile`.**

Edit `src/profile.h`. After the existing `name()` / `setName()` block in the public section, add:

```cpp
    /// Sticky profile id — matches the on-disk directory basename.
    /// Populated by load() from profile.conf:[profile]/id; falls
    /// back to the directory basename if the key is missing or load
    /// hasn't been called.
    QString id() const;

    /// Default path for a fresh profile with the given id under
    /// ~/.wildpalms/<id>. Static helper used by ProfileRegistry
    /// and tests.
    static QString defaultPathForId(const QString &id);

    /// Schema version of the loaded profile.conf. 1 for F.1a.
    int schemaVersion() const;
```

In the private member section, add:
```cpp
    QString m_id;
    int     m_schemaVersion = 1;
```

- [ ] **Step 2: Add failing tests.**

Open `tests/test_profile.cpp`. In the `private slots:` declarations, add:
```cpp
    void testDefaultPathForId();
    void testIdFromBasename();
```

Add the implementations near the existing tests (any order):

```cpp
void TestProfile::testDefaultPathForId()
{
    const QString p = Profile::defaultPathForId(QStringLiteral("profile5"));
    QVERIFY(p.endsWith(QStringLiteral("/.wildpalms/profile5")));
}

void TestProfile::testIdFromBasename()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/myprofile");
    QVERIFY(QDir().mkpath(dir));

    Profile p;
    p.setSyncFolderPath(dir);
    // Before load(), id is empty.
    QCOMPARE(p.id(), QString());
}
```

- [ ] **Step 3: Build + verify the new tests fail to link.**

```bash
cmake --build build-fetchcontent --parallel 2>&1 | grep -E "(error:|undefined reference)" | head -5
```
Expected: linker errors about `Profile::id`, `Profile::defaultPathForId`, `Profile::schemaVersion`.

- [ ] **Step 4: Implement the new accessors.**

Edit `src/profile.cpp`. Add the implementations anywhere reasonable (group with `name()` / `setName()` for clarity):

```cpp
QString Profile::id() const
{
    return m_id;
}

int Profile::schemaVersion() const
{
    return m_schemaVersion;
}

QString Profile::defaultPathForId(const QString &id)
{
    return QDir::homePath() + QStringLiteral("/.wildpalms/") + id;
}
```

- [ ] **Step 5: Build + run tests.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R test_profile --output-on-failure
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: all `test_profile` tests pass; total 75/75.

- [ ] **Step 6: Commit.**

```bash
git add src/profile.h src/profile.cpp tests/test_profile.cpp
git commit -m "$(cat <<'EOF'
F.1a T8: Profile — add id(), defaultPathForId(), schemaVersion()

Pure additions. id() and schemaVersion() return new member
defaults; defaultPathForId is a static helper.
Subsequent tasks (T10/T11) populate m_id and m_schemaVersion
from profile.conf in the new load() path.

ctest 75/75 green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Profile — drop legacy `[conduits]` and `[databaseHandlers]` API

**Files:**
- Modify: `src/profile.h`
- Modify: `src/profile.cpp`
- Modify: `tests/test_profile.cpp`

- [ ] **Step 1: Delete the legacy declarations from `src/profile.h`.**

Remove these public method declarations (find each by name in the header):
```cpp
bool        conduitEnabled(const QString &conduitId) const;
void        setConduitEnabled(const QString &conduitId, bool enabled);
QStringList enabledConduits() const;
QJsonObject conduitSettings(const QString &conduitId) const;
void        setConduitSettings(const QString &id, const QJsonObject &s);

QString     activeDatabaseHandler(const QString &dbName) const;
void        setActiveDatabaseHandler(const QString &dbName,
                                     const QString &conduitId);
QMap<QString, QString> databaseHandlers() const;
```

Remove these private members:
```cpp
QMap<QString, bool>        m_conduitEnabled;
QMap<QString, QJsonObject> m_conduitSettings;
QMap<QString, QString>     m_databaseHandlers;
```

- [ ] **Step 2: Delete the corresponding implementations from `src/profile.cpp`.**

Find and delete the bodies of:
- `Profile::conduitEnabled`
- `Profile::setConduitEnabled`
- `Profile::enabledConduits`
- `Profile::conduitSettings`
- `Profile::setConduitSettings`
- `Profile::activeDatabaseHandler`
- `Profile::setActiveDatabaseHandler`
- `Profile::databaseHandlers`

Also remove the `m_conduitEnabled` / `m_conduitSettings` / `m_databaseHandlers` references inside the existing `Profile::load()` and `Profile::save()` (the `settings.beginGroup("conduits")` and `settings.beginGroup("databaseHandlers")` blocks).

- [ ] **Step 3: Delete the conduit and database-handler tests from `tests/test_profile.cpp`.**

Find every test slot whose name contains `Conduit`, `conduit`, `DatabaseHandler`, or `databaseHandler` (search the file). Common names: `testConduitEnabledDefault`, `testConduitEnabledToggle`, `testConduitSettings`, `testActiveDatabaseHandler`. Delete them entirely — both declarations in `private slots:` and the function bodies. Run:

```bash
cd ~/dev/WildPalms
grep -nE "conduit|databaseHandler|DatabaseHandler" tests/test_profile.cpp
```
Expected after deletion: no matches.

- [ ] **Step 4: Search the broader source tree for any remaining references.**

```bash
grep -rn "conduitEnabled\|setConduitEnabled\|enabledConduits\|conduitSettings\|setConduitSettings\|activeDatabaseHandler\|setActiveDatabaseHandler\|databaseHandlers" src/ tests/ 2>&1 | grep -v "docs/archived"
```
Expected: no matches (or only matches inside `docs/archived/`, which is fine — historical).

- [ ] **Step 5: Build + run.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 75 - (number of deleted tests) / same total pass. The deleted tests don't show up in the count anymore. Should still be 100% pass.

- [ ] **Step 6: Commit.**

```bash
git add src/profile.h src/profile.cpp tests/test_profile.cpp
git commit -m "$(cat <<'EOF'
F.1a T9: Profile — drop legacy [conduits] + [databaseHandlers]

Removes the post-Phase-E dead API:
- conduitEnabled / setConduitEnabled / enabledConduits
- conduitSettings / setConduitSettings
- activeDatabaseHandler / setActiveDatabaseHandler / databaseHandlers
- corresponding m_conduitEnabled, m_conduitSettings, m_databaseHandlers
  members and their load/save handling.

Removes the matching tests from tests/test_profile.cpp.

No production caller of these methods exists post-Phase E (verified
by grep across src/).

ctest pass count drops by the number of removed Conduit/DatabaseHandler
tests; 100% pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Profile — rewrite `save()` to use three files

**Files:**
- Modify: `src/profile.cpp`

- [ ] **Step 1: Replace `Profile::save()`'s body with the orchestrator.**

In `src/profile.cpp`, find `bool Profile::save()` and replace its body:

```cpp
bool Profile::save()
{
    if (m_syncFolderPath.isEmpty())
        return false;

    QDir dir(m_syncFolderPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    if (!saveProfileConf())  return false;
    if (!saveAccountsConf()) return false;
    if (!saveMappingsConf()) return false;
    return true;
}
```

- [ ] **Step 2: Add helper declarations to `src/profile.h`.**

In the `private:` section of `Profile`, add:

```cpp
    bool saveProfileConf() const;
    bool saveAccountsConf() const;
    bool saveMappingsConf() const;

    bool loadProfileConf();
    bool loadAccountsConf();
    bool loadMappingsConf();

    QString sanitizeKConfigGroupId(const QString &id) const;
```

- [ ] **Step 3: Implement `saveProfileConf`.**

In `src/profile.cpp`:

```cpp
bool Profile::saveProfileConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/profile.conf");
    QSettings s(path, QSettings::IniFormat);

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    s.setValue(QStringLiteral("profile/id"),
               m_id.isEmpty() ? QFileInfo(m_syncFolderPath).fileName() : m_id);
    s.setValue(QStringLiteral("profile/name"), m_name);

    s.setValue(QStringLiteral("device/path"),    m_devicePath);
    s.setValue(QStringLiteral("device/baudRate"), m_baudRate);
    s.setValue(QStringLiteral("device/connectionMode"),
               m_connectionMode == ConnectionMode::DisconnectAfterSync
                   ? QStringLiteral("disconnect")
                   : QStringLiteral("keepalive"));
    s.setValue(QStringLiteral("device/autoSyncOnConnect"), m_autoSyncOnConnect);
    s.setValue(QStringLiteral("device/defaultSyncType"),    m_defaultSyncType);
    s.setValue(QStringLiteral("device/userId"),           m_deviceFingerprint.userId);
    s.setValue(QStringLiteral("device/userName"),         m_deviceFingerprint.userName);
    s.setValue(QStringLiteral("device/usbSerialNumber"),  m_deviceFingerprint.usbSerialNumber);
    s.setValue(QStringLiteral("device/modelName"),        m_deviceFingerprint.modelName);
    s.setValue(QStringLiteral("device/manufacturer"),     m_deviceFingerprint.manufacturer);
    s.setValue(QStringLiteral("device/romVersion"),       m_deviceFingerprint.romVersion);
    s.setValue(QStringLiteral("device/productId"),        m_deviceFingerprint.productId);
    s.setValue(QStringLiteral("device/romSize"),
               QString::number(m_deviceFingerprint.romSize));
    s.setValue(QStringLiteral("device/ramSize"),
               QString::number(m_deviceFingerprint.ramSize));
    s.setValue(QStringLiteral("device/ramFree"),
               QString::number(m_deviceFingerprint.ramFree));

    if (m_lastSyncTime.isValid())
        s.setValue(QStringLiteral("sync/lastSyncTime"),
                   m_lastSyncTime.toUTC().toString(Qt::ISODate));
    s.setValue(QStringLiteral("sync/conflictPolicy"),          m_conflictPolicy);
    s.setValue(QStringLiteral("sync/conflictAutoResolve"),     m_conflictAutoResolve);
    s.setValue(QStringLiteral("sync/conflictFallback"),        m_conflictFallback);
    s.setValue(QStringLiteral("sync/conflictPromptStrategy"),  m_conflictPromptStrategy);
    s.setValue(QStringLiteral("sync/conflictConnectionBehavior"),
               m_conflictConnectionBehavior);
    s.setValue(QStringLiteral("sync/conflictTimeoutSeconds"),  m_conflictTimeoutSeconds);

    s.sync();
    return s.status() == QSettings::NoError;
}
```

- [ ] **Step 4: Implement `saveAccountsConf`.**

```cpp
bool Profile::saveAccountsConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/accounts.conf");
    QSettings s(path, QSettings::IniFormat);
    s.clear();   // F.1a: full rewrite, no merge.

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    for (const auto &a : m_accounts) {
        const QString group = QStringLiteral("account-") + sanitizeKConfigGroupId(a.id);
        s.setValue(group + QStringLiteral("/type"),        a.type);
        s.setValue(group + QStringLiteral("/displayName"), a.displayName);
        s.setValue(group + QStringLiteral("/enabled"),     a.enabled);

        const QString paramsGroup = group + QStringLiteral("/params");
        for (auto it = a.connectionParams.constBegin();
             it != a.connectionParams.constEnd(); ++it) {
            s.setValue(paramsGroup + QStringLiteral("/") + it.key(), it.value());
        }
    }

    s.sync();
    return s.status() == QSettings::NoError;
}
```

- [ ] **Step 5: Implement `saveMappingsConf`.**

```cpp
bool Profile::saveMappingsConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/mappings.conf");
    QSettings s(path, QSettings::IniFormat);
    s.clear();

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    QSet<QString> usedGroups;
    for (const auto &v : m_syncMappingsJson) {
        if (!v.isObject()) continue;
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;

        QString group = QStringLiteral("mapping-") + sanitizeKConfigGroupId(id);
        int n = 2;
        while (usedGroups.contains(group)) {
            group = QStringLiteral("mapping-") + sanitizeKConfigGroupId(id)
                  + QStringLiteral("-") + QString::number(n++);
        }
        usedGroups.insert(group);

        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            const QJsonValue val = it.value();
            if (val.isString())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toString());
            else if (val.isBool())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toBool());
            else if (val.isDouble())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toDouble());
            else
                s.setValue(group + QStringLiteral("/") + it.key(),
                           QJsonDocument(val.isObject() ? QJsonDocument(val.toObject())
                                                         : QJsonDocument(val.toArray()))
                               .toJson(QJsonDocument::Compact));
        }
    }

    s.sync();
    return s.status() == QSettings::NoError;
}
```

- [ ] **Step 6: Implement `sanitizeKConfigGroupId`.**

```cpp
QString Profile::sanitizeKConfigGroupId(const QString &id) const
{
    QString out = id;
    out.replace(QLatin1Char('/'), QLatin1Char('_'));
    out.replace(QLatin1Char('['), QLatin1Char('_'));
    out.replace(QLatin1Char(']'), QLatin1Char('_'));
    return out;
}
```

- [ ] **Step 7: Build.**

```bash
cmake --build build-fetchcontent --parallel
```
Expected: clean build. `Profile::load` still uses the OLD single-file path (we replace that in T11), so the existing `test_profile` tests should still pass via the old loader; the new saver writes to three files, but the old loader doesn't read them yet. So a save → load round-trip in tests will currently fail. Don't run those tests yet — let T11 fix the round-trip.

- [ ] **Step 8: Run a partial ctest, skipping `test_profile`.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure -E "test_profile" 2>&1 | tail -3
```
Expected: every other test passes. `test_profile` is excluded because its round-trip tests will fail until T11 lands.

- [ ] **Step 9: Commit.**

```bash
git add src/profile.h src/profile.cpp
git commit -m "$(cat <<'EOF'
F.1a T10: Profile::save() rewritten — three files

saveProfileConf writes identity + device + sync settings to
profile.conf. saveAccountsConf writes one [account-<id>] group
per account with a [account-<id>/params] subgroup (no more JSON
string in INI). saveMappingsConf writes one [mapping-<id>] group
per mapping, with type-aware QSettings serialisation of each
JSON field.

Profile::load() still uses the old single-file path in this
commit; T11 rewrites it. test_profile is temporarily skipped from
the ctest run because round-trip tests will be red until T11.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Profile — rewrite `load()` to use three files

**Files:**
- Modify: `src/profile.cpp`
- Modify: `tests/test_profile.cpp`

- [ ] **Step 1: Replace `Profile::load()`'s body with the orchestrator.**

In `src/profile.cpp`, find `bool Profile::load()` and replace its entire body:

```cpp
bool Profile::load()
{
    if (m_syncFolderPath.isEmpty())
        return false;
    QDir dir(m_syncFolderPath);
    if (!dir.exists())
        return false;

    if (!loadProfileConf())  return false;
    if (!loadAccountsConf()) return false;
    if (!loadMappingsConf()) return false;
    return true;
}
```

- [ ] **Step 2: Implement `loadProfileConf`.**

```cpp
bool Profile::loadProfileConf()
{
    const QString path = m_syncFolderPath + QStringLiteral("/profile.conf");
    if (!QFile::exists(path))
        return false;

    QSettings s(path, QSettings::IniFormat);

    m_schemaVersion = s.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    if (m_schemaVersion != 1) {
        qWarning() << "[Profile] unknown schemaVersion" << m_schemaVersion
                   << "in" << path;
        return false;
    }

    m_id = s.value(QStringLiteral("profile/id")).toString();
    if (m_id.isEmpty())
        m_id = QFileInfo(m_syncFolderPath).fileName();
    m_name = s.value(QStringLiteral("profile/name")).toString();

    m_devicePath = s.value(QStringLiteral("device/path"),
                            DEFAULT_DEVICE_PATH).toString();
    m_baudRate = s.value(QStringLiteral("device/baudRate"),
                          DEFAULT_BAUD_RATE).toString();

    const QString modeStr =
        s.value(QStringLiteral("device/connectionMode"),
                QStringLiteral("keepalive")).toString();
    m_connectionMode = (modeStr == QStringLiteral("disconnect"))
        ? ConnectionMode::DisconnectAfterSync
        : ConnectionMode::KeepAlive;

    m_autoSyncOnConnect = s.value(QStringLiteral("device/autoSyncOnConnect"),
                                   false).toBool();
    m_defaultSyncType   = s.value(QStringLiteral("device/defaultSyncType"),
                                   QStringLiteral("hotsync")).toString();

    m_deviceFingerprint.userId = s.value(
        QStringLiteral("device/userId"), 0).toUInt();
    m_deviceFingerprint.userName = s.value(
        QStringLiteral("device/userName")).toString();
    m_deviceFingerprint.usbSerialNumber = s.value(
        QStringLiteral("device/usbSerialNumber")).toString();
    m_deviceFingerprint.modelName = s.value(
        QStringLiteral("device/modelName")).toString();
    m_deviceFingerprint.manufacturer = s.value(
        QStringLiteral("device/manufacturer")).toString();
    m_deviceFingerprint.romVersion = s.value(
        QStringLiteral("device/romVersion"), 0).toUInt();
    m_deviceFingerprint.productId = s.value(
        QStringLiteral("device/productId")).toString();
    m_deviceFingerprint.romSize = s.value(
        QStringLiteral("device/romSize"), 0).toULongLong();
    m_deviceFingerprint.ramSize = s.value(
        QStringLiteral("device/ramSize"), 0).toULongLong();
    m_deviceFingerprint.ramFree = s.value(
        QStringLiteral("device/ramFree"), 0).toULongLong();

    const QString lastSyncStr = s.value(
        QStringLiteral("sync/lastSyncTime")).toString();
    m_lastSyncTime = lastSyncStr.isEmpty()
        ? QDateTime()
        : QDateTime::fromString(lastSyncStr, Qt::ISODate);

    m_conflictPolicy = s.value(QStringLiteral("sync/conflictPolicy"),
                                DEFAULT_CONFLICT_POLICY).toString();
    m_conflictAutoResolve = s.value(
        QStringLiteral("sync/conflictAutoResolve"),
        QStringLiteral("none")).toString();
    m_conflictFallback = s.value(
        QStringLiteral("sync/conflictFallback"),
        QStringLiteral("defer")).toString();
    m_conflictPromptStrategy = s.value(
        QStringLiteral("sync/conflictPromptStrategy"),
        QStringLiteral("always_ask")).toString();
    m_conflictConnectionBehavior = s.value(
        QStringLiteral("sync/conflictConnectionBehavior"),
        QStringLiteral("keep_alive")).toString();
    m_conflictTimeoutSeconds = s.value(
        QStringLiteral("sync/conflictTimeoutSeconds"), 60).toInt();

    return true;
}
```

- [ ] **Step 3: Implement `loadAccountsConf`.**

```cpp
bool Profile::loadAccountsConf()
{
    m_accounts.clear();
    const QString path = m_syncFolderPath + QStringLiteral("/accounts.conf");
    if (!QFile::exists(path))
        return true;   // empty accounts is valid.

    QSettings s(path, QSettings::IniFormat);
    const int v = s.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    if (v != 1) {
        qWarning() << "[Profile] unknown accounts schemaVersion" << v;
        return false;
    }

    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("account-"))) continue;
        const QString id = g.mid(QStringLiteral("account-").size());
        if (id.isEmpty()) continue;

        Kalburator::Sync::BackendConfiguration bc;
        bc.id          = id;
        bc.type        = s.value(g + QStringLiteral("/type")).toString();
        bc.displayName = s.value(g + QStringLiteral("/displayName")).toString();
        bc.enabled     = s.value(g + QStringLiteral("/enabled"), true).toBool();

        s.beginGroup(g + QStringLiteral("/params"));
        const QStringList paramKeys = s.childKeys();
        for (const QString &k : paramKeys) {
            bc.connectionParams[k] = s.value(k);
        }
        s.endGroup();

        m_accounts.append(bc);
    }
    return true;
}
```

- [ ] **Step 4: Implement `loadMappingsConf`.**

```cpp
bool Profile::loadMappingsConf()
{
    m_syncMappingsJson = QJsonArray{};
    const QString path = m_syncFolderPath + QStringLiteral("/mappings.conf");
    if (!QFile::exists(path))
        return true;   // empty mappings is valid.

    QSettings s(path, QSettings::IniFormat);
    const int v = s.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    if (v != 1) {
        qWarning() << "[Profile] unknown mappings schemaVersion" << v;
        return false;
    }

    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("mapping-"))) continue;

        QJsonObject obj;
        s.beginGroup(g);
        const QStringList keys = s.childKeys();
        for (const QString &k : keys) {
            const QVariant val = s.value(k);
            if (val.typeId() == QMetaType::Bool) {
                obj[k] = val.toBool();
            } else if (val.typeId() == QMetaType::Int
                       || val.typeId() == QMetaType::Double) {
                obj[k] = val.toDouble();
            } else {
                // Strings may have been JSON-serialised objects/arrays
                // for the embedded case; try to parse, fall back to plain
                // string.
                const QByteArray asUtf8 = val.toString().toUtf8();
                const QJsonDocument doc = QJsonDocument::fromJson(asUtf8);
                if (doc.isObject()) obj[k] = doc.object();
                else if (doc.isArray()) obj[k] = doc.array();
                else obj[k] = val.toString();
            }
        }
        s.endGroup();

        m_syncMappingsJson.append(obj);
    }
    return true;
}
```

- [ ] **Step 5: Update `tests/test_profile.cpp` for the new schema.**

The existing tests probably write through the old single-file path. Replace the body of any save/load round-trip test to use the new layout. As a representative example, find a test that saves and re-loads, and update its assertions to inspect the new files:

```cpp
void TestProfile::testSaveCreatesThreeFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Profile p;
    p.setSyncFolderPath(tmp.path() + QStringLiteral("/profile1"));
    p.setName(QStringLiteral("Test"));
    QVERIFY(p.save());

    QVERIFY(QFile::exists(p.syncFolderPath() + QStringLiteral("/profile.conf")));
    QVERIFY(QFile::exists(p.syncFolderPath() + QStringLiteral("/accounts.conf")));
    QVERIFY(QFile::exists(p.syncFolderPath() + QStringLiteral("/mappings.conf")));
}

void TestProfile::testRoundTripBasic()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/profile1");

    Profile p;
    p.setSyncFolderPath(dir);
    p.setName(QStringLiteral("RoundTrip"));
    p.setBaudRate(QStringLiteral("57600"));
    QVERIFY(p.save());

    Profile p2;
    p2.setSyncFolderPath(dir);
    QVERIFY(p2.load());
    QCOMPARE(p2.name(), QStringLiteral("RoundTrip"));
    QCOMPARE(p2.baudRate(), QStringLiteral("57600"));
}
```

Add the slot declarations to the `private slots:` block:
```cpp
    void testSaveCreatesThreeFiles();
    void testRoundTripBasic();
```

Delete or update any existing tests that assert on the OLD single-file format (look for `.wildpalms.conf` string literals in `test_profile.cpp` — those tests are obsolete).

- [ ] **Step 6: Build + run the profile tests.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R test_profile --output-on-failure
```
Expected: all `test_profile` tests now pass against the new three-file layout.

- [ ] **Step 7: Run full suite.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 100% pass. Total count is `(baseline) + 1 + (new test_profile assertions) - (deleted conduit tests)`.

- [ ] **Step 8: Commit.**

```bash
git add src/profile.cpp tests/test_profile.cpp
git commit -m "$(cat <<'EOF'
F.1a T11: Profile::load() rewritten — three files

loadProfileConf reads profile.conf (identity + device + sync
settings). loadAccountsConf reads accounts.conf (per-account INI
groups + nested params subgroup). loadMappingsConf reads
mappings.conf (per-mapping INI groups, type-aware QSettings
deserialisation back to JsonArray).

Missing accounts.conf or mappings.conf is OK (empty state).
Missing profile.conf is an error. Unknown schemaVersion is an
error.

test_profile reshaped for the three-file layout: testSaveCreates-
ThreeFiles + testRoundTripBasic land green. Old single-file
assertions removed.

ctest 100% pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Profile + ProfileRegistry integration test

**Files:**
- Create: `tests/runtime/tst_profile_registry_integration.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the integration test.**

Create `tests/runtime/tst_profile_registry_integration.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::Runtime;

class TstProfileRegistryIntegration : public QObject
{
    Q_OBJECT
private slots:
    void registerNewThenLoadProfile();
    void registerExistingExistingProfile();
};

void TstProfileRegistryIntegration::registerNewThenLoadProfile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto entry = reg.registerNew(QStringLiteral("Integration"));
    QVERIFY(entry.isValid());

    // Stub profile.conf was written; Profile::load should succeed.
    Profile p;
    p.setSyncFolderPath(entry.path);
    QVERIFY(p.load());
    QCOMPARE(p.id(), entry.id);
    QCOMPARE(p.name(), QStringLiteral("Integration"));

    // Save additional state, reload, assert.
    p.setBaudRate(QStringLiteral("38400"));
    QVERIFY(p.save());

    Profile p2;
    p2.setSyncFolderPath(entry.path);
    QVERIFY(p2.load());
    QCOMPARE(p2.id(), entry.id);
    QCOMPARE(p2.name(), QStringLiteral("Integration"));
    QCOMPARE(p2.baudRate(), QStringLiteral("38400"));
}

void TstProfileRegistryIntegration::registerExistingExistingProfile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Create a saved profile directly (no registry yet).
    const QString dir = tmp.path() + QStringLiteral("/preexisting");
    {
        Profile p;
        p.setSyncFolderPath(dir);
        p.setName(QStringLiteral("Pre-existing"));
        QVERIFY(p.save());
    }

    // Manually set [profile]/id since save() will have written it
    // from m_id which is empty by default; fix that by reading-then-
    // overwriting:
    {
        QSettings s(dir + QStringLiteral("/profile.conf"), QSettings::IniFormat);
        s.setValue(QStringLiteral("profile/id"), QStringLiteral("preexisting"));
        s.sync();
    }

    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);

    const auto entry = reg.registerExisting(dir);
    QVERIFY(entry.isValid());
    QCOMPARE(entry.id, QStringLiteral("preexisting"));
    QCOMPARE(entry.name, QStringLiteral("Pre-existing"));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistryIntegration)
#include "tst_profile_registry_integration.moc"
```

- [ ] **Step 2: Register the test.**

In `tests/runtime/CMakeLists.txt`, add:
```cmake
add_wildpalms_runtime_test(tst_profile_registry_integration
    tst_profile_registry_integration.cpp)
```

- [ ] **Step 3: Build + run.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_profile_registry_integration --output-on-failure
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: new test passes; total count + 1.

- [ ] **Step 4: Commit.**

```bash
git add tests/runtime/tst_profile_registry_integration.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
F.1a T12: Profile + ProfileRegistry integration test

End-to-end: registerNew writes a stub profile.conf; Profile::load
succeeds immediately on that stub; subsequent save+load round-trip
preserves state. Separately: pre-existing Profile dir gets picked
up by registerExisting.

ctest 100% pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: Remove `onOpenProfile` from `KF6MainWindow` and `ActionManager`

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`
- Modify: `src/kf6/actionmanager.h`
- Modify: `src/kf6/actionmanager.cpp`

- [ ] **Step 1: Remove the slot declaration from `kf6mainwindow.h`.**

Search the header for `onOpenProfile` and delete:
```cpp
void onOpenProfile();
```

- [ ] **Step 2: Remove the slot body from `kf6mainwindow.cpp`.**

Search the cpp for `void KF6MainWindow::onOpenProfile()` and delete the entire function. Also delete the `connect(m_actionManager, &ActionManager::openProfileRequested, this, &KF6MainWindow::onOpenProfile);` line.

- [ ] **Step 3: Remove the action from `actionmanager.cpp` and `.h`.**

In `src/kf6/actionmanager.h`, delete:
```cpp
void openProfileRequested();
```
and the corresponding action accessor declaration if present (`openProfileAction()` getter, if it exists — search).

In `src/kf6/actionmanager.cpp`, delete the block that creates the QAction (search for `openProfile` — usually `QAction *openProfile = new QAction(...)` plus its `connect(...openProfileRequested)` and its `actionCollection()->addAction(...)` call).

- [ ] **Step 4: Search for any remaining references.**

```bash
grep -rn "openProfileRequested\|onOpenProfile\|openProfileAction" src/ tests/ 2>&1 | head
```
Expected: no matches (or only matches inside `docs/archived/`).

- [ ] **Step 5: Build + run.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: clean build, 100% pass.

- [ ] **Step 6: Commit.**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp \
        src/kf6/actionmanager.h src/kf6/actionmanager.cpp
git commit -m "$(cat <<'EOF'
F.1a T13: remove Open Profile action + slot + signal

KF6MainWindow::onOpenProfile and ActionManager's openProfile
QAction + openProfileRequested signal are removed entirely.
Profile selection now goes through ProfileRegistry (F.1a startup,
T14 next) and will get its real File menu in F.1b.

ctest 100% pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: KF6MainWindow — wire ProfileRegistry into startup + replace `onNewProfile`

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`
- Modify: `src/kf6/CMakeLists.txt` (if needed for ProfileRegistry link)

- [ ] **Step 1: Add member + include to `kf6mainwindow.h`.**

Near the existing includes:
```cpp
#include "runtime/profileregistry.h"
```

In the private members section, add:
```cpp
    std::unique_ptr<WildPalms::Runtime::ProfileRegistry> m_profileRegistry;
```

Add a virtual hook for the stopgap picker (test seam):

In the protected (or `protected virtual`) section, add:
```cpp
    /// Test seam: F.1a stopgap profile-picker UI. Production override
    /// shows a QMessageBox / QInputDialog; tests stub it.
    virtual QString showProfilePickerStopgap();
```

- [ ] **Step 2: Construct the registry in the ctor.**

In `src/kf6/kf6mainwindow.cpp`, find the constructor body. After the existing member initialisations (after `m_actionManager` setup), add:

```cpp
m_profileRegistry =
    std::make_unique<WildPalms::Runtime::ProfileRegistry>(this);
```

- [ ] **Step 3: Replace startup logic.**

Find the existing startup code that calls `QSettings::value("recentProfiles/...")` or similar to auto-load the last profile (search for `recentProfiles` and the existing `loadProfile()` call in the ctor body). Replace it with:

```cpp
const QString lastId = m_profileRegistry->lastActiveId();
if (!lastId.isEmpty()) {
    const auto e = m_profileRegistry->entry(lastId);
    if (e.isValid() && QDir(e.path).exists()) {
        loadProfile(e.path);
        return;   // (or continue, depending on the surrounding flow)
    }
}
// Fall through to stopgap picker.
const QString picked = showProfilePickerStopgap();
if (!picked.isEmpty())
    loadProfile(picked);
```

Adjust the `return` / `continue` to whatever the surrounding control flow needs.

- [ ] **Step 4: Implement `showProfilePickerStopgap` (production).**

In `src/kf6/kf6mainwindow.cpp`:

```cpp
QString KF6MainWindow::showProfilePickerStopgap()
{
    const auto entries = m_profileRegistry->entries();

    if (entries.isEmpty()) {
        QMessageBox::information(this,
            i18n("No Profile"),
            i18n("No WildPalms profile has been created yet.\n"
                 "Let's create one to get started."));

        bool ok = false;
        const QString name = QInputDialog::getText(this,
            i18n("New Profile"),
            i18n("Profile name:"),
            QLineEdit::Normal,
            QString(),
            &ok);
        if (!ok || name.trimmed().isEmpty()) return QString();

        const auto e = m_profileRegistry->registerNew(name.trimmed());
        if (!e.isValid()) {
            QMessageBox::critical(this, i18n("New Profile"),
                i18n("Could not create profile."));
            return QString();
        }
        return e.path;
    }

    // Registry has entries but last-active is stale.
    QStringList names;
    for (const auto &e : entries) names << e.name;
    bool ok = false;
    const QString chosenName = QInputDialog::getItem(this,
        i18n("Select Profile"),
        i18n("Pick a profile:"),
        names, 0, false, &ok);
    if (!ok || chosenName.isEmpty()) return QString();

    for (const auto &e : entries)
        if (e.name == chosenName) return e.path;
    return QString();
}
```

- [ ] **Step 5: Rewrite `onNewProfile()` to use the registry stopgap.**

Find `void KF6MainWindow::onNewProfile()` (currently calls `QFileDialog::getExistingDirectory`). Replace with:

```cpp
void KF6MainWindow::onNewProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        i18n("New Profile"),
        i18n("Profile name:"),
        QLineEdit::Normal,
        QString(),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const auto entry = m_profileRegistry->registerNew(name.trimmed());
    if (!entry.isValid()) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not create profile."));
        return;
    }
    loadProfile(entry.path);
}
```

- [ ] **Step 6: Make `loadProfile()` call `setLastActive()` on success.**

Find `void KF6MainWindow::loadProfile(const QString &path)`. At the end of the function (after `m_currentProfile` is set up + everything works), add:

```cpp
if (m_currentProfile && !m_currentProfile->id().isEmpty())
    m_profileRegistry->setLastActive(m_currentProfile->id());
```

Place this AFTER the existing `m_currentProfile = std::make_unique<Profile>(...); m_currentProfile->load();` sequence — wherever load is verified successful.

- [ ] **Step 7: Add required includes to `kf6mainwindow.cpp`.**

At the top of the file:
```cpp
#include <QInputDialog>
#include <QMessageBox>
```
(Probably already there from other code; add if missing.)

- [ ] **Step 8: Update `kf6/CMakeLists.txt` if needed.**

`KF6MainWindow` already links `WildPalmsRuntime` (which now contains `ProfileRegistry`). No CMakeLists change should be needed; verify by building.

- [ ] **Step 9: Build + run full suite.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: clean build, 100% pass. Some KF6 startup tests may temporarily fail — see T15 for fixes.

- [ ] **Step 10: Commit.**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "$(cat <<'EOF'
F.1a T14: KF6MainWindow — ProfileRegistry-driven startup + stopgap UI

KF6MainWindow owns a ProfileRegistry, constructed in the ctor.
Startup consults lastActiveId() and falls back to the stopgap
picker (showProfilePickerStopgap, virtual for test stubbing).

onNewProfile becomes a one-field QInputDialog::getText prompt
that calls m_profileRegistry->registerNew. F.1c replaces this with
the real wizard.

loadProfile bumps setLastActive() on success.

ctest pass count may shift slightly while existing KF6 startup
tests stabilise; T15 fixes any holdouts.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 15: KF6MainWindow startup regression tests

**Files:**
- Create: `tests/runtime/tst_kf6mainwindow_startup.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Add the two test seams to `KF6MainWindow` (re-edit T14's files).**

T14 already introduced `m_profileRegistry` and the virtual `showProfilePickerStopgap()`. Add two more seams now.

In `src/kf6/kf6mainwindow.h`, add to the public section:

```cpp
    /// Test seam: replace the constructor-time ProfileRegistry with an
    /// explicit one. Caller owns the new registry; KF6MainWindow takes
    /// ownership via move.
    void setProfileRegistryForTest(
        std::unique_ptr<WildPalms::Runtime::ProfileRegistry> reg);

    /// Test seam: invoke the same startup-time registry resolution
    /// the ctor uses. Returns the loaded-profile path, or empty if
    /// the stopgap was invoked and returned no choice.
    QString runStartupForTest();
```

In `src/kf6/kf6mainwindow.cpp`, add the bodies. Hoist the ctor's existing registry-resolution block into a new private helper so the ctor and `runStartupForTest` share one definition:

```cpp
QString KF6MainWindow::runStartupForTest()
{
    return resolveStartupProfile();
}

void KF6MainWindow::setProfileRegistryForTest(
    std::unique_ptr<WildPalms::Runtime::ProfileRegistry> reg)
{
    m_profileRegistry = std::move(reg);
}

QString KF6MainWindow::resolveStartupProfile()
{
    const QString lastId = m_profileRegistry->lastActiveId();
    if (!lastId.isEmpty()) {
        const auto e = m_profileRegistry->entry(lastId);
        if (e.isValid() && QDir(e.path).exists()) {
            loadProfile(e.path);
            return e.path;
        }
    }
    const QString picked = showProfilePickerStopgap();
    if (!picked.isEmpty()) {
        loadProfile(picked);
        return picked;
    }
    return QString();
}
```

Update the ctor to call `resolveStartupProfile()` in place of the block T14 added inline.

Declare the helper in `kf6mainwindow.h` private section:
```cpp
    QString resolveStartupProfile();
```

- [ ] **Step 2: Write the test file.**

Create `tests/runtime/tst_kf6mainwindow_startup.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>

#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

class TestableMainWindow : public KF6MainWindow {
public:
    using KF6MainWindow::KF6MainWindow;

    int     stopgapInvocations = 0;
    QString stopgapReturn;

protected:
    QString showProfilePickerStopgap() override {
        ++stopgapInvocations;
        return stopgapReturn;
    }
};

class TstKf6MainWindowStartup : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegistryInvokesStopgap();
    void validLastActiveAutoLoads();
    void staleLastActiveFallsBack();
};

void TstKf6MainWindowStartup::emptyRegistryInvokesStopgap()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);

    TestableMainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.stopgapReturn = QString();   // user clicks Cancel on stopgap

    const QString picked = w.runStartupForTest();

    QCOMPARE(w.stopgapInvocations, 1);
    QVERIFY(picked.isEmpty());
}

void TstKf6MainWindowStartup::validLastActiveAutoLoads()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto entry = reg->registerNew(QStringLiteral("AutoLoad"));
    QVERIFY(entry.isValid());
    reg->setLastActive(entry.id);
    QVERIFY(QDir(entry.path).exists());

    TestableMainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.stopgapReturn = QString();   // stopgap should never be invoked

    const QString picked = w.runStartupForTest();

    QCOMPARE(w.stopgapInvocations, 0);
    QCOMPARE(picked, entry.path);
}

void TstKf6MainWindowStartup::staleLastActiveFallsBack()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));

    // Pre-write a registry entry whose path does NOT exist.
    {
        KConfigGroup g(cfg, QStringLiteral("profile-profile1"));
        g.writeEntry("name", "Ghost");
        g.writeEntry("path", "/nonexistent-path/profile1");
        g.writeEntry("lastOpened", QDateTime::currentDateTimeUtc());
        KConfigGroup gen(cfg, QStringLiteral("General"));
        gen.writeEntry("lastActiveProfileId", "profile1");
        cfg->sync();
    }

    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    QCOMPARE(reg->lastActiveId(), QStringLiteral("profile1"));

    TestableMainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.stopgapReturn = QString();

    const QString picked = w.runStartupForTest();

    QCOMPARE(w.stopgapInvocations, 1);   // last-active stale, picker fired
    QVERIFY(picked.isEmpty());
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowStartup)
#include "tst_kf6mainwindow_startup.moc"
```

- [ ] **Step 2: Register the test in `tests/runtime/CMakeLists.txt`.**

```cmake
add_wildpalms_runtime_test(tst_kf6mainwindow_startup
    tst_kf6mainwindow_startup.cpp)
```

- [ ] **Step 3: Build + run.**

```bash
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent -R tst_kf6mainwindow_startup --output-on-failure
```
Expected: tests pass (after replacing `QVERIFY(true)` stubs with real assertions per Step 1).

- [ ] **Step 4: Run full suite.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: 100% pass.

- [ ] **Step 5: Commit.**

```bash
git add tests/runtime/tst_kf6mainwindow_startup.cpp \
        tests/runtime/CMakeLists.txt \
        src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "$(cat <<'EOF'
F.1a T15: KF6MainWindow startup regression tests

Three cases: empty registry invokes stopgap; valid last-active
auto-loads; stale last-active falls back to stopgap. Adds
runStartupForTest() and setProfileRegistryForTest() seams on
KF6MainWindow so tests can drive the registry check
deterministically.

ctest 100% pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 16: Update tracking docs

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (cross-reference)
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md` (Phase F status)

- [ ] **Step 1: Add the F.1a landed note to the integration plan.**

Open `docs/plans/2026-04-20-libkalburator-integration.md`. Find the "Phase F" line in the phase overview table (around line 27). Update the Status column from "Not started." to:

```
F.1a ✅ Done <yyyy-mm-dd>. F.1b / F.1c pending.
```

Also update the status-reconciliation bullet that mentions Phase F (around line 45). Change "Phase F (...) is the next active phase. Not started." to:

```
- Phase F is split into three sub-projects per 2026-05-21 brainstorm:
  F.1a (profile persistence + registry) ✅ landed <yyyy-mm-dd>;
  F.1b (new File menu: Switch / Import / Forget) — next;
  F.1c (the multi-page wizard) — after F.1b.
```

Replace `<yyyy-mm-dd>` with the actual date the F.1a final commit lands (run `date -I` to confirm).

- [ ] **Step 2: Cross-reference the F.1a spec from the Phase E spec.**

Open `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. In the "Total" / wrap-up area at the bottom (around line 600), add a short note pointing forward:

```
Phase F begins 2026-05-21. See:
- docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md
- docs/superpowers/plans/2026-05-21-f1a-profile-registry.md (this plan)
```

- [ ] **Step 3: Build (no code change) + run ctest.**

```bash
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -3
```
Expected: still 100% pass (docs-only commit).

- [ ] **Step 4: Commit.**

```bash
git add -f docs/plans/2026-04-20-libkalburator-integration.md \
        docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "$(cat <<'EOF'
F.1a T16: docs — mark F.1a ✅ in integration plan + Phase E spec

Integration plan Phase F row updated: F.1a done, F.1b next, F.1c
after that. Phase E spec gets a forward pointer to the F.1a spec +
plan.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 17: Final verification + push

**Files:** none.

- [ ] **Step 1: Final full ctest run.**

```bash
cd ~/dev/WildPalms
cmake --build build-fetchcontent --parallel
ctest --test-dir build-fetchcontent --output-on-failure 2>&1 | tail -5
```
Expected: 100% pass. Total count should be `baseline + 1 (tst_profileregistry) + 1 (tst_profile_registry_integration) + 1 (tst_kf6mainwindow_startup) - <removed conduit tests>`.

- [ ] **Step 2: Sanity-check the on-disk shape with a manual run.**

```bash
rm -rf /tmp/wp-f1a-sanity
WILDPALMS_HOME=/tmp/wp-f1a-sanity ./build-fetchcontent/src/WildPalms --no-restore 2>&1 | head -20
```
(Adjust the binary path if needed; the executable might be `wildpalms` or `WildPalms` under `build-fetchcontent/src/`.) Expect the app to launch, show the empty-registry stopgap, create `/tmp/wp-f1a-sanity/.wildpalms/profile1/` containing `profile.conf` after the user enters a name. Close the app.

If `WILDPALMS_HOME` isn't an environment variable the app respects, this is a manual visual check — launch the app normally and verify that `~/.config/wildpalms/wildpalmsrc` got a `[profile-profile1]` group and `~/.wildpalms/profile1/profile.conf` exists.

- [ ] **Step 3: Confirm no Open Profile in the menu.**

Launch the app, open the File menu. Expected: no "Open Profile" item. The menu contains New Profile, Close Profile, (existing items).

- [ ] **Step 4: Push.**

```bash
git push origin main
```

- [ ] **Step 5: Capture final summary.**

Output the list of F.1a commits:
```bash
git log --oneline main ^HEAD~17
```

That's the F.1a landing.

---

## Self-review notes (for the engineer running the plan)

If you hit something the plan didn't anticipate:

- **Build error about a deleted method** — likely a consumer of the dropped conduit/databaseHandler API that grep missed in T9 step 4. Find it, decide if it's dead code (delete) or live (talk to the maintainer; this plan assumed only test_profile touched these).
- **KConfig sync race** between `setLastActive` and a concurrent registry read — out of scope. `KSharedConfig` is last-writer-wins; the user is expected to run one WildPalms at a time.
- **`KSharedConfig::openConfig(path)` with an explicit path argument** — KF6's API takes the filename or full path; tests use `tmp.path() + "/wprc"` to point at a per-test config. If the build complains the overload doesn't exist, check the KConfigCore version pinned by FetchContent and switch to `KSharedConfig::Ptr p = KSharedConfig::openConfig(path, KConfig::SimpleConfig);` if needed.
- **Test seam name clash** — `runStartupForTest` / `setProfileRegistryForTest` may need different names if KF6MainWindow already has a similar method. Search before adding.
- **`tests/runtime/CMakeLists.txt` helper name** — the plan uses `add_wildpalms_runtime_test`. If the actual helper is named differently (check the file), use that. Same template, different name.

## What's left after F.1a

- **F.1b** — new File menu (Switch / Import / Forget); removes the stopgap pickers; brainstormed separately.
- **F.1c** — the multi-page wizard; brainstormed separately; reuses the page structure from the superseded F.1 design where possible.
