# Phase E.14 — Plucker Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the legacy `PluckerConduit` (currently `IToolConduit`) as the sixth new-ABI `IBackendPlugin`, source-only blob-backend producing per-channel `.pdb` blobs plus conditional viewer-bootstrap PRCs.

**Architecture:** `PluckerBackendPlugin` exposes a `PluckerBlobBackend` with two collections — `plucker:channels` (one record per due channel, blob = bytes of `.pdb` produced by `PluckerFetcher` wrapping PyPlucker `Spider.py`) and `plucker:bootstrap` (SysZLib + viewer PRC bytes, gated on `IPalmDatabaseAccess::hasDatabase("Plucker")`). Settings as JSON via `IPlugin::loadSettings`/`saveSettings`; channel-management UI via `IPlugin::createSettingsWidget`. Runtime cross-plugin install drain deferred to E.15; E.14 e2e uses `MockBlobBackend` as the install-drain target.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Network, Test), KF6::CoreAddons (`KPluginMetaData`, `KPluginFactory`, `kcoreaddons_add_plugin`), `Kalburator::Sync` (`IBlobBackend`, `BackendRecord`, `CollectionInfo`, `BlobSyncEngine`, `MockBlobBackend`), Python 3 + bundled PyPlucker `Spider.py` (subprocess via `QProcess`).

**Spec:** `docs/superpowers/specs/2026-04-26-phase-e14-plucker-plugin-design.md`

---

## File structure

The Plucker plugin is a **git submodule** at `src/plugins/plucker/` with its own commit history (`wildpalms-conduit-plugin-plucker.git`-style). All new source files land inside the submodule; the parent repo bumps the submodule pointer once per task that lands a new file. Tests live in the parent repo at `tests/plugins/plucker/`.

```
src/plugins/plucker/                          (submodule)
├── CMakeLists.txt                            modified — add WILDPALMS_PLUCKER_PLUGIN_V2 toggle
├── plucker-conduit.json                      unchanged (legacy manifest)
├── plucker-backend-plugin.json               NEW (V2 manifest)
├── pluckerchannel.h                          NEW (lifted struct + helpers)
├── pluckerchannelserializer.{h,cpp}          NEW (V2 JSON ↔ struct)
├── pluckerfetcher.{h,cpp}                    NEW (subprocess wrapper)
├── pluckerblobbackend.{h,cpp}                NEW (IBlobBackend)
├── pluckerbackendplugin.{h,cpp}              NEW (IBackendPlugin)
├── pluckersettingswidget.{h,cpp}             NEW (channel-mgmt UI)
├── pluckerchanneleditor.{h,cpp}              NEW (V2 dialog operating on PluckerChannel)
├── pluckerconfig.{h,cpp}                     modified — drop struct, keep INI loader
├── pluckerconduit.{h,cpp}                    modified — include pluckerchannel.h directly
├── pluckerview.{h,cpp}                       unchanged (legacy)
├── pluckerchanneldialog.{h,cpp}              unchanged (legacy)
├── parser/PyPlucker/                         unchanged (Spider.py + bundled libs)
└── viewer/                                   unchanged (SysZLib.prc + viewer_en.prc)

tests/plugins/                                (parent repo)
├── CMakeLists.txt                            modified — add plucker subdirectory
└── plucker/                                  NEW
    ├── CMakeLists.txt                        NEW
    ├── tst_pluckerchannel.cpp                NEW
    ├── tst_pluckerfetcher.cpp                NEW
    ├── tst_pluckerblobbackend.cpp            NEW
    ├── tst_pluckerbackendplugin.cpp          NEW
    ├── tst_plucker_v2_e2e.cpp                NEW
    └── fixtures/                             NEW
        ├── spider_stub.py
        ├── spider_fail.py
        ├── spider_hang.py
        ├── SysZLib_stub.prc
        └── viewer_stub.prc

docs/superpowers/specs/                       (parent repo)
└── 2026-04-21-phase-e-plugin-abi-rewrite-design.md   modified — flip E.14 row to ✅

MEMORY.md + project_phase_e14_plucker.md      (auto-memory)  NEW
```

**Submodule workflow** (same as E.13): each task that adds a file inside `src/plugins/plucker/` lands two commits — one inside the submodule, one in the parent bumping the submodule pointer. Test files live in the parent repo and commit in one shot.

---

## Task 1: Lift `PluckerChannel` struct + helpers into shared header

**Files:**
- Create: `src/plugins/plucker/pluckerchannel.h`
- Modify: `src/plugins/plucker/pluckerconfig.h` (drop struct + statics; keep `PluckerConfig` class)
- Modify: `src/plugins/plucker/pluckerconfig.cpp` (drop static-method bodies that moved)
- Modify: `src/plugins/plucker/pluckerconduit.cpp` (add `#include "pluckerchannel.h"` if it now needs it directly — check after edit)

**Goal:** Extract `struct PluckerChannel` plus the `isDue`, `nextDueTime`, `sanitizeDocFile`, and `buildCLIArgs` helpers out of `PluckerConfig` into a header both V1 and V2 link against. No behavioural change.

- [ ] **Step 1: Create `pluckerchannel.h` with the lifted struct + helpers**

```cpp
#ifndef PLUCKERCHANNEL_H
#define PLUCKERCHANNEL_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace WildPalms::PluckerPlugin {

struct PluckerChannel {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    QString homeUrl;

    // Spidering
    int     maxDepth     = 2;
    bool    stayOnHost   = false;
    bool    depthFirst   = false;
    QString userAgent;
    QString urlPattern;

    // Images
    int  bpp                    = 8;
    int  maxWidth               = 150;
    int  maxHeight              = 250;
    int  altMaxWidth            = 450;
    int  altMaxHeight           = 800;
    bool noImages               = false;
    int  imageCompressionLimit  = 50;

    // Output
    QString compression = QStringLiteral("zlib");
    QString category;

    // Destination
    QString storageMode    = QStringLiteral("ram");
    QString cardDirectory;

    // Scheduling
    bool      updateEnabled   = true;
    int       updateFrequency = 1;
    QString   updatePeriod    = QStringLiteral("days");
    QDateTime lastFetched;
};

/// True when a channel is enabled and its next-due time is in the past.
bool      pluckerIsDue(const PluckerChannel &channel);

/// Returns lastFetched + N period (or invalid QDateTime if never fetched).
QDateTime pluckerNextDueTime(const PluckerChannel &channel);

/// Replaces non-[a-zA-Z0-9_-] with '_'; empty input -> "untitled".
QString   pluckerSanitizeDocFile(const QString &name);

/// PyPlucker Spider.py CLI args derived from the channel + outputDir.
QStringList pluckerBuildCliArgs(const PluckerChannel &channel,
                                 const QString &outputDir);

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERCHANNEL_H
```

- [ ] **Step 2: Create `pluckerchannel.cpp` with helper bodies (verbatim port of statics from `pluckerconfig.cpp`)**

```cpp
#include "pluckerchannel.h"

#include <QRegularExpression>

namespace WildPalms::PluckerPlugin {

bool pluckerIsDue(const PluckerChannel &channel)
{
    if (!channel.updateEnabled) return false;
    if (!channel.lastFetched.isValid()) return true;
    return QDateTime::currentDateTime() >= pluckerNextDueTime(channel);
}

QDateTime pluckerNextDueTime(const PluckerChannel &channel)
{
    if (!channel.lastFetched.isValid()) return QDateTime();

    QDateTime next = channel.lastFetched;
    int freq = qMax(1, channel.updateFrequency);

    if (channel.updatePeriod == "hours")        next = next.addSecs(freq * 3600);
    else if (channel.updatePeriod == "days")    next = next.addDays(freq);
    else if (channel.updatePeriod == "weeks")   next = next.addDays(freq * 7);
    else if (channel.updatePeriod == "months")  next = next.addMonths(freq);
    else                                        next = next.addDays(freq);

    return next;
}

QString pluckerSanitizeDocFile(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                 QStringLiteral("_"));
    if (safe.isEmpty()) safe = QStringLiteral("untitled");
    return safe;
}

QStringList pluckerBuildCliArgs(const PluckerChannel &channel,
                                  const QString &outputDir)
{
    QStringList args;
    args << QStringLiteral("--home-url=%1").arg(channel.homeUrl);
    args << QStringLiteral("--doc-name=%1").arg(channel.name);
    args << QStringLiteral("--doc-file=%1").arg(pluckerSanitizeDocFile(channel.name));
    args << QStringLiteral("--pluckerdir=%1").arg(outputDir);
    args << QStringLiteral("--maxdepth=%1").arg(channel.maxDepth);
    args << QStringLiteral("--bpp=%1").arg(channel.bpp);
    args << QStringLiteral("--maxwidth=%1").arg(channel.maxWidth);
    args << QStringLiteral("--maxheight=%1").arg(channel.maxHeight);
    args << QStringLiteral("--alt-maxwidth=%1").arg(channel.altMaxWidth);
    args << QStringLiteral("--alt-maxheight=%1").arg(channel.altMaxHeight);
    args << QStringLiteral("--compression=%1").arg(channel.compression);

    if (channel.stayOnHost) args << QStringLiteral("--stayonhost");
    if (channel.depthFirst) args << QStringLiteral("--depth-first");
    if (channel.noImages)   args << QStringLiteral("--noimages");
    if (!channel.category.isEmpty())  args << QStringLiteral("--category=%1").arg(channel.category);
    if (!channel.userAgent.isEmpty()) args << QStringLiteral("--user-agent=%1").arg(channel.userAgent);
    if (!channel.urlPattern.isEmpty())args << QStringLiteral("--staybelow=%1").arg(channel.urlPattern);
    args << QStringLiteral("--no-urlinfo");
    return args;
}

} // namespace WildPalms::PluckerPlugin
```

- [ ] **Step 3: Edit `pluckerconfig.h` — drop the struct + static helpers, keep `PluckerConfig` class wrapping a `QList<PluckerChannel>`**

Replace the entire file contents with:

```cpp
#ifndef PLUCKERCONFIG_H
#define PLUCKERCONFIG_H

#include "pluckerchannel.h"

#include <QList>
#include <QString>

class PluckerConfig
{
public:
    PluckerConfig() = default;

    using PluckerChannel = WildPalms::PluckerPlugin::PluckerChannel;

    void addChannel(const PluckerChannel &channel);
    void updateChannel(const PluckerChannel &channel);
    void removeChannel(const QString &id);
    PluckerChannel channel(const QString &id) const;
    QList<PluckerChannel> channels() const;

    void load(const QString &syncPath);
    void save(const QString &syncPath);

    // Compatibility shims so legacy callers keep compiling.
    static bool        isDue(const PluckerChannel &c)          { return WildPalms::PluckerPlugin::pluckerIsDue(c); }
    static QDateTime   nextDueTime(const PluckerChannel &c)    { return WildPalms::PluckerPlugin::pluckerNextDueTime(c); }
    static QString     sanitizeDocFile(const QString &name)    { return WildPalms::PluckerPlugin::pluckerSanitizeDocFile(name); }
    static QStringList buildCLIArgs(const PluckerChannel &c,
                                    const QString &outputDir)  { return WildPalms::PluckerPlugin::pluckerBuildCliArgs(c, outputDir); }

private:
    QList<PluckerChannel> m_channels;
};

#endif // PLUCKERCONFIG_H
```

- [ ] **Step 4: Edit `pluckerconfig.cpp` — drop the four static-method bodies that moved (`isDue`, `nextDueTime`, `sanitizeDocFile`, `buildCLIArgs`)**

Use Edit tool on `src/plugins/plucker/pluckerconfig.cpp` to remove the four static method definitions (`PluckerConfig::isDue`, `PluckerConfig::nextDueTime`, `PluckerConfig::sanitizeDocFile`, `PluckerConfig::buildCLIArgs`) — keep everything else (constructors, addChannel, load, save). The header now provides them inline as forwarders.

- [ ] **Step 5: Update `src/plugins/plucker/CMakeLists.txt` to compile `pluckerchannel.cpp` into the legacy plugin (so the helpers link)**

Add `pluckerchannel.cpp` and `pluckerchannel.h` to the SOURCES list of the legacy `kcoreaddons_add_plugin` block. Do not change anything else yet (that's Task 2).

- [ ] **Step 6: Configure + build legacy plugin to verify no regression**

Run from `/home/clinton/dev/WildPalms`:
```bash
cmake --preset dev
cmake --build build-dev --target wildpalms_plucker
```
Expected: clean build with no errors. The `PluckerChannel` symbol now lives in `WildPalms::PluckerPlugin`; legacy code uses `PluckerConfig::PluckerChannel` typedef.

- [ ] **Step 7: Run existing plucker tests to verify no regression**

Run:
```bash
ctest --preset dev --test-dir build-dev -R pluckerconfig --output-on-failure
```
Expected: `test_pluckerconfig` passes (the legacy test exercises `PluckerConfig` directly, which still uses the same struct + helpers).

- [ ] **Step 8: Commit (submodule + parent pointer)**

Inside the submodule:
```bash
cd src/plugins/plucker
git add pluckerchannel.h pluckerchannel.cpp pluckerconfig.h pluckerconfig.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(plucker): lift PluckerChannel into shared header

Hoist the channel struct + isDue/nextDueTime/sanitizeDocFile/buildCLIArgs
helpers out of PluckerConfig into pluckerchannel.h so the upcoming V2
IBackendPlugin can share them with the legacy IToolConduit. PluckerConfig
keeps inline forwarders for binary-compat with legacy callers.

Phase E.14 Task 1 — prep work for the new plugin ABI.
EOF
)"
```
Then from the parent repo:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker
git commit -m "refactor(plucker): bump submodule — lift PluckerChannel (E.14 Task 1)"
```

---

## Task 2: CMake toggle + V2 manifest

**Files:**
- Modify: `src/plugins/plucker/CMakeLists.txt`
- Create: `src/plugins/plucker/plucker-backend-plugin.json`

**Goal:** Add `WILDPALMS_PLUCKER_PLUGIN_V2=ON` toggle gating V2 sources, mirroring the WebCal pattern. The toggle ON branch lists V2 files (most don't exist yet — they'll land in subsequent tasks; the build will fail until Task 8 adds the last V2 source); the toggle OFF branch keeps the legacy plugin sources. Land the manifest now so it's available for Task 8's plugin shell.

- [ ] **Step 1: Replace `src/plugins/plucker/CMakeLists.txt` with the toggled version**

```cmake
option(WILDPALMS_PLUCKER_PLUGIN_V2
    "Build the new IBackendPlugin-based Plucker plugin" ON)

if (WILDPALMS_PLUCKER_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_plucker_v2
        SOURCES
            pluckerbackendplugin.cpp        pluckerbackendplugin.h
            pluckerblobbackend.cpp          pluckerblobbackend.h
            pluckerfetcher.cpp              pluckerfetcher.h
            pluckerchannel.cpp              pluckerchannel.h
            pluckerchannelserializer.cpp    pluckerchannelserializer.h
            pluckersettingswidget.cpp       pluckersettingswidget.h
            pluckerchanneleditor.cpp        pluckerchanneleditor.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_plucker_v2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    # See calendar plugin's CMakeLists for why this BEFORE include is
    # required (Kalburator::Sync ordering vs WildPalmsCore's legacy
    # ::Sync include).
    target_include_directories(wildpalms_plucker_v2 BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(wildpalms_plucker_v2
        PRIVATE
            WildPalmsCore
            WildPalmsRuntime
            KF6::CoreAddons
            KF6::I18n
            KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
    if(NOT WILDPALMS_INSTALLED)
        target_compile_definitions(wildpalms_plucker_v2 PRIVATE
            PLUCKER_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
else ()
    kcoreaddons_add_plugin(wildpalms_plucker
        SOURCES
            pluckerconduit.cpp
            pluckerconduit.h
            pluckerview.cpp
            pluckerview.h
            pluckerchanneldialog.cpp
            pluckerchanneldialog.h
            pluckerconfig.cpp
            pluckerconfig.h
            pluckerchannel.cpp
            pluckerchannel.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_plucker
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
    )
    if(NOT WILDPALMS_INSTALLED)
        target_compile_definitions(wildpalms_plucker PRIVATE
            PLUCKER_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
endif ()

# Install Plucker data for AppImage / system install (both V1 and V2 use it)
install(DIRECTORY parser/ DESTINATION share/wildpalms/plucker/parser
    FILES_MATCHING PATTERN "*.py")
install(DIRECTORY viewer/ DESTINATION share/wildpalms/plucker/viewer
    FILES_MATCHING PATTERN "*.prc")
```

- [ ] **Step 2: Create the V2 manifest file**

Write `src/plugins/plucker/plucker-backend-plugin.json`:

```json
{
    "KPlugin": {
        "Name": "Plucker",
        "Description": "Fetch web content as Palm Plucker documents",
        "Icon": "text-html",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0.0",
        "Category": "Sync",
        "Id": "plucker"
    },
    "X-WildPalms-PluginType":   "backend",
    "X-WildPalms-PalmDatabases": [],
    "X-WildPalms-DefaultEnabled": false,
    "X-WildPalms-SortOrder":     50
}
```

- [ ] **Step 3: Verify legacy build still works with toggle OFF**

```bash
cmake -S /home/clinton/dev/WildPalms -B /tmp/build-plucker-legacy \
      -DWILDPALMS_PLUCKER_PLUGIN_V2=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/build-plucker-legacy --target wildpalms_plucker
```
Expected: clean build of the legacy plugin.

- [ ] **Step 4: Verify V2 build fails for the right reason (missing source files)**

```bash
cmake --preset dev
cmake --build build-dev --target wildpalms_plucker_v2 2>&1 | head -20
```
Expected: build fails because `pluckerbackendplugin.cpp` etc. don't exist yet. This is intentional — subsequent tasks add them. Move on.

- [ ] **Step 5: Commit (submodule + parent pointer)**

Submodule:
```bash
cd src/plugins/plucker
git add CMakeLists.txt plucker-backend-plugin.json
git commit -m "$(cat <<'EOF'
build(plucker): add WILDPALMS_PLUCKER_PLUGIN_V2 toggle + V2 manifest

Land the CMake gate that swaps between the legacy IToolConduit and the
new IBackendPlugin sources. V2 build will only succeed after Task 8
finishes; legacy build remains green throughout E.14.

Phase E.14 Task 2.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker
git commit -m "build(plucker): bump submodule — V2 toggle + manifest (E.14 Task 2)"
```

---

## Task 3: `PluckerChannel` JSON serializer (TDD)

**Files:**
- Create: `src/plugins/plucker/pluckerchannelserializer.h`
- Create: `src/plugins/plucker/pluckerchannelserializer.cpp`
- Create: `tests/plugins/plucker/CMakeLists.txt`
- Create: `tests/plugins/plucker/tst_pluckerchannel.cpp`
- Modify: `tests/plugins/CMakeLists.txt` (add `add_subdirectory(plucker)`)

**Goal:** Round-trip a `PluckerChannel` through `QJsonObject` with all 25 fields plus `last_fetched` ISO string. snake_case JSON keys per the spec.

- [ ] **Step 1: Create `tests/plugins/plucker/tst_pluckerchannel.cpp` with the failing round-trip test**

```cpp
#include <QJsonObject>
#include <QTest>

#include "pluckerchannel.h"
#include "pluckerchannelserializer.h"

using namespace WildPalms::PluckerPlugin;

class TestPluckerChannel : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip_allFields()
    {
        PluckerChannel original;
        original.id                    = QStringLiteral("abc-123");
        original.name                  = QStringLiteral("BBC News");
        original.homeUrl               = QStringLiteral("https://www.bbc.co.uk/news");
        original.maxDepth              = 4;
        original.stayOnHost            = true;
        original.depthFirst            = true;
        original.userAgent             = QStringLiteral("WildPalms/1.0");
        original.urlPattern            = QStringLiteral("https://www.bbc.co.uk");
        original.bpp                   = 4;
        original.maxWidth              = 160;
        original.maxHeight             = 240;
        original.altMaxWidth           = 600;
        original.altMaxHeight          = 900;
        original.noImages              = true;
        original.imageCompressionLimit = 75;
        original.compression           = QStringLiteral("none");
        original.category              = QStringLiteral("News");
        original.storageMode           = QStringLiteral("vfs");
        original.cardDirectory         = QStringLiteral("/Documents");
        original.updateEnabled         = false;
        original.updateFrequency       = 3;
        original.updatePeriod          = QStringLiteral("weeks");
        original.lastFetched           = QDateTime::fromString(
            QStringLiteral("2026-04-26T10:30:00"), Qt::ISODate);

        const QJsonObject json = pluckerChannelToJson(original);
        const PluckerChannel restored = pluckerChannelFromJson(json);

        QCOMPARE(restored.id,                    original.id);
        QCOMPARE(restored.name,                  original.name);
        QCOMPARE(restored.homeUrl,               original.homeUrl);
        QCOMPARE(restored.maxDepth,              original.maxDepth);
        QCOMPARE(restored.stayOnHost,            original.stayOnHost);
        QCOMPARE(restored.depthFirst,            original.depthFirst);
        QCOMPARE(restored.userAgent,             original.userAgent);
        QCOMPARE(restored.urlPattern,            original.urlPattern);
        QCOMPARE(restored.bpp,                   original.bpp);
        QCOMPARE(restored.maxWidth,              original.maxWidth);
        QCOMPARE(restored.maxHeight,             original.maxHeight);
        QCOMPARE(restored.altMaxWidth,           original.altMaxWidth);
        QCOMPARE(restored.altMaxHeight,          original.altMaxHeight);
        QCOMPARE(restored.noImages,              original.noImages);
        QCOMPARE(restored.imageCompressionLimit, original.imageCompressionLimit);
        QCOMPARE(restored.compression,           original.compression);
        QCOMPARE(restored.category,              original.category);
        QCOMPARE(restored.storageMode,           original.storageMode);
        QCOMPARE(restored.cardDirectory,         original.cardDirectory);
        QCOMPARE(restored.updateEnabled,         original.updateEnabled);
        QCOMPARE(restored.updateFrequency,       original.updateFrequency);
        QCOMPARE(restored.updatePeriod,          original.updatePeriod);
        QCOMPARE(restored.lastFetched,           original.lastFetched);
    }

    void fromJson_emptyObject_yieldsDefaults()
    {
        const PluckerChannel ch = pluckerChannelFromJson(QJsonObject{});
        QCOMPARE(ch.maxDepth, 2);
        QCOMPARE(ch.bpp, 8);
        QCOMPARE(ch.compression, QStringLiteral("zlib"));
        QCOMPARE(ch.storageMode, QStringLiteral("ram"));
        QCOMPARE(ch.updateEnabled, true);
        QCOMPARE(ch.updateFrequency, 1);
        QCOMPARE(ch.updatePeriod, QStringLiteral("days"));
        QVERIFY(!ch.lastFetched.isValid());
    }

    void isDue_neverFetched_isTrue()
    {
        PluckerChannel ch;
        ch.updateEnabled = true;
        QVERIFY(pluckerIsDue(ch));
    }

    void isDue_disabled_isFalse()
    {
        PluckerChannel ch;
        ch.updateEnabled = false;
        QVERIFY(!pluckerIsDue(ch));
    }

    void isDue_recentDailyFetch_isFalse()
    {
        PluckerChannel ch;
        ch.updateEnabled   = true;
        ch.updateFrequency = 1;
        ch.updatePeriod    = QStringLiteral("days");
        ch.lastFetched     = QDateTime::currentDateTime().addSecs(-3600);
        QVERIFY(!pluckerIsDue(ch));
    }

    void sanitizeDocFile_specialChars_becomeUnderscores()
    {
        QCOMPARE(pluckerSanitizeDocFile(QStringLiteral("BBC News!")),
                 QStringLiteral("BBC_News_"));
        QCOMPARE(pluckerSanitizeDocFile(QStringLiteral("")),
                 QStringLiteral("untitled"));
    }
};

QTEST_MAIN(TestPluckerChannel)
#include "tst_pluckerchannel.moc"
```

- [ ] **Step 2: Create `tests/plugins/plucker/CMakeLists.txt`**

```cmake
# Phase E.14 — Plucker plugin tests.
# Tasks 3-7 build test binaries directly against the source files in
# the plucker submodule.

set(PLUCKER_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/plucker)
set(PLUCKER_FIXTURE_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# --- Task 3: PluckerChannel + serializer ---
add_executable(tst_pluckerchannel
    tst_pluckerchannel.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannelserializer.cpp
)
target_include_directories(tst_pluckerchannel
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_pluckerchannel
    PRIVATE
        Qt::Test
        Qt::Core
)
add_test(NAME tst_pluckerchannel COMMAND tst_pluckerchannel)
```

- [ ] **Step 3: Append `add_subdirectory(plucker)` to `tests/plugins/CMakeLists.txt`**

Edit `tests/plugins/CMakeLists.txt` to add at the end:
```cmake

# Phase E.14 — Plucker plugin tests.
add_subdirectory(plucker)
```

- [ ] **Step 4: Run the test to verify it fails for the right reason**

```bash
cmake --preset dev
cmake --build build-dev --target tst_pluckerchannel 2>&1 | head -10
```
Expected: build fails — `pluckerchannelserializer.h` not found. Good.

- [ ] **Step 5: Create `src/plugins/plucker/pluckerchannelserializer.h`**

```cpp
#ifndef PLUCKERCHANNELSERIALIZER_H
#define PLUCKERCHANNELSERIALIZER_H

#include "pluckerchannel.h"

#include <QJsonObject>

namespace WildPalms::PluckerPlugin {

QJsonObject     pluckerChannelToJson(const PluckerChannel &channel);
PluckerChannel  pluckerChannelFromJson(const QJsonObject &json);

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERCHANNELSERIALIZER_H
```

- [ ] **Step 6: Create `src/plugins/plucker/pluckerchannelserializer.cpp`**

```cpp
#include "pluckerchannelserializer.h"

#include <QJsonValue>

namespace WildPalms::PluckerPlugin {

QJsonObject pluckerChannelToJson(const PluckerChannel &c)
{
    QJsonObject o;
    o[QStringLiteral("id")]                      = c.id;
    o[QStringLiteral("name")]                    = c.name;
    o[QStringLiteral("home_url")]                = c.homeUrl;
    o[QStringLiteral("max_depth")]               = c.maxDepth;
    o[QStringLiteral("stay_on_host")]            = c.stayOnHost;
    o[QStringLiteral("depth_first")]             = c.depthFirst;
    o[QStringLiteral("user_agent")]              = c.userAgent;
    o[QStringLiteral("url_pattern")]             = c.urlPattern;
    o[QStringLiteral("bpp")]                     = c.bpp;
    o[QStringLiteral("max_width")]               = c.maxWidth;
    o[QStringLiteral("max_height")]              = c.maxHeight;
    o[QStringLiteral("alt_max_width")]           = c.altMaxWidth;
    o[QStringLiteral("alt_max_height")]          = c.altMaxHeight;
    o[QStringLiteral("no_images")]               = c.noImages;
    o[QStringLiteral("image_compression_limit")] = c.imageCompressionLimit;
    o[QStringLiteral("compression")]             = c.compression;
    o[QStringLiteral("category")]                = c.category;
    o[QStringLiteral("storage_mode")]            = c.storageMode;
    o[QStringLiteral("card_directory")]          = c.cardDirectory;
    o[QStringLiteral("update_enabled")]          = c.updateEnabled;
    o[QStringLiteral("update_frequency")]        = c.updateFrequency;
    o[QStringLiteral("update_period")]           = c.updatePeriod;
    if (c.lastFetched.isValid()) {
        o[QStringLiteral("last_fetched")] = c.lastFetched.toString(Qt::ISODate);
    }
    return o;
}

PluckerChannel pluckerChannelFromJson(const QJsonObject &j)
{
    PluckerChannel c;
    if (j.contains(QStringLiteral("id"))) c.id = j[QStringLiteral("id")].toString();
    c.name                  = j.value(QStringLiteral("name")).toString();
    c.homeUrl               = j.value(QStringLiteral("home_url")).toString();
    c.maxDepth              = j.value(QStringLiteral("max_depth")).toInt(2);
    c.stayOnHost            = j.value(QStringLiteral("stay_on_host")).toBool(false);
    c.depthFirst            = j.value(QStringLiteral("depth_first")).toBool(false);
    c.userAgent             = j.value(QStringLiteral("user_agent")).toString();
    c.urlPattern            = j.value(QStringLiteral("url_pattern")).toString();
    c.bpp                   = j.value(QStringLiteral("bpp")).toInt(8);
    c.maxWidth              = j.value(QStringLiteral("max_width")).toInt(150);
    c.maxHeight             = j.value(QStringLiteral("max_height")).toInt(250);
    c.altMaxWidth           = j.value(QStringLiteral("alt_max_width")).toInt(450);
    c.altMaxHeight          = j.value(QStringLiteral("alt_max_height")).toInt(800);
    c.noImages              = j.value(QStringLiteral("no_images")).toBool(false);
    c.imageCompressionLimit = j.value(QStringLiteral("image_compression_limit")).toInt(50);
    c.compression           = j.value(QStringLiteral("compression"))
                                .toString(QStringLiteral("zlib"));
    c.category              = j.value(QStringLiteral("category")).toString();
    c.storageMode           = j.value(QStringLiteral("storage_mode"))
                                .toString(QStringLiteral("ram"));
    c.cardDirectory         = j.value(QStringLiteral("card_directory")).toString();
    c.updateEnabled         = j.value(QStringLiteral("update_enabled")).toBool(true);
    c.updateFrequency       = j.value(QStringLiteral("update_frequency")).toInt(1);
    c.updatePeriod          = j.value(QStringLiteral("update_period"))
                                .toString(QStringLiteral("days"));
    const QString lf = j.value(QStringLiteral("last_fetched")).toString();
    if (!lf.isEmpty()) {
        c.lastFetched = QDateTime::fromString(lf, Qt::ISODate);
    }
    return c;
}

} // namespace WildPalms::PluckerPlugin
```

- [ ] **Step 7: Build + run the test**

```bash
cmake --build build-dev --target tst_pluckerchannel
ctest --test-dir build-dev -R '^tst_pluckerchannel$' --output-on-failure
```
Expected: PASS (5 test functions).

- [ ] **Step 8: Commit**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerchannelserializer.h pluckerchannelserializer.cpp
git commit -m "$(cat <<'EOF'
feat(plucker): JSON serializer for PluckerChannel

Round-trips all 25 channel fields plus last_fetched as ISO string,
using snake_case JSON keys per the V2 settings shape. Defaults match
the struct's in-class initialisers.

Phase E.14 Task 3.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker tests/plugins/plucker tests/plugins/CMakeLists.txt
git commit -m "test(plucker): tst_pluckerchannel + bump submodule (E.14 Task 3)"
```

---

## Task 4: `PluckerFetcher` (TDD with stub Spider scripts)

**Files:**
- Create: `src/plugins/plucker/pluckerfetcher.h`
- Create: `src/plugins/plucker/pluckerfetcher.cpp`
- Create: `tests/plugins/plucker/fixtures/spider_stub.py`
- Create: `tests/plugins/plucker/fixtures/spider_fail.py`
- Create: `tests/plugins/plucker/fixtures/spider_hang.py`
- Create: `tests/plugins/plucker/tst_pluckerfetcher.cpp`
- Modify: `tests/plugins/plucker/CMakeLists.txt`

**Goal:** Synchronous `QProcess` wrapper that spawns python3 + Spider.py with channel-derived args, awaits completion within a timeout, reads back the produced `.pdb`. Return `Result{success, errorMessage, pdbBytes, docFile}`.

- [ ] **Step 1: Create the three Python stub fixtures**

`tests/plugins/plucker/fixtures/spider_stub.py`:
```python
#!/usr/bin/env python3
"""Stub spider — writes a fixed .pdb and exits 0."""
import argparse
import os
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--home-url")
    parser.add_argument("--doc-name")
    parser.add_argument("--doc-file")
    parser.add_argument("--pluckerdir")
    # Accept everything else
    args, _ = parser.parse_known_args()
    if not args.doc_file or not args.pluckerdir:
        sys.exit(2)
    out = os.path.join(args.pluckerdir, args.doc_file + ".pdb")
    with open(out, "wb") as f:
        f.write(b"PLUCKER_TEST")
    print(f"wrote {out}")
    sys.exit(0)

if __name__ == "__main__":
    main()
```

`tests/plugins/plucker/fixtures/spider_fail.py`:
```python
#!/usr/bin/env python3
"""Stub spider that always fails."""
import sys
print("simulated failure", file=sys.stderr)
sys.exit(1)
```

`tests/plugins/plucker/fixtures/spider_hang.py`:
```python
#!/usr/bin/env python3
"""Stub spider that sleeps forever."""
import time
while True:
    time.sleep(60)
```

- [ ] **Step 2: Create `tests/plugins/plucker/tst_pluckerfetcher.cpp`**

```cpp
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "pluckerchannel.h"
#include "pluckerfetcher.h"

using namespace WildPalms::PluckerPlugin;

class TestPluckerFetcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_fixtureDir = QStringLiteral(PLUCKER_FIXTURE_DIR);
    }

    void fetch_success_returnsPdbBytes()
    {
        PluckerChannel ch;
        ch.id      = QStringLiteral("c1");
        ch.name    = QStringLiteral("Channel One");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QDir(m_fixtureDir).filePath(
            QStringLiteral("spider_stub.py")));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/10000);
        QVERIFY(result.success);
        QVERIFY(result.errorMessage.isEmpty());
        QCOMPARE(result.pdbBytes, QByteArray("PLUCKER_TEST"));
        QCOMPARE(result.docFile, QStringLiteral("Channel_One"));
    }

    void fetch_nonZeroExit_reportsFailure()
    {
        PluckerChannel ch;
        ch.name    = QStringLiteral("Channel Two");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QDir(m_fixtureDir).filePath(
            QStringLiteral("spider_fail.py")));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/10000);
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(result.pdbBytes.isEmpty());
    }

    void fetch_timeout_reportsFailure()
    {
        PluckerChannel ch;
        ch.name    = QStringLiteral("Channel Hang");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QDir(m_fixtureDir).filePath(
            QStringLiteral("spider_hang.py")));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/500);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("timeout"),
                                              Qt::CaseInsensitive));
    }

    void fetch_missingScript_reportsFailure()
    {
        PluckerChannel ch;
        ch.name    = QStringLiteral("X");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QStringLiteral("/no/such/path.py"));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/5000);
        QVERIFY(!result.success);
    }

private:
    QString m_fixtureDir;
};

QTEST_MAIN(TestPluckerFetcher)
#include "tst_pluckerfetcher.moc"
```

- [ ] **Step 3: Append fetcher target to `tests/plugins/plucker/CMakeLists.txt`**

```cmake

# --- Task 4: PluckerFetcher ---
add_executable(tst_pluckerfetcher
    tst_pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
)
target_compile_definitions(tst_pluckerfetcher
    PRIVATE
        PLUCKER_FIXTURE_DIR="${PLUCKER_FIXTURE_DIR}"
)
target_include_directories(tst_pluckerfetcher
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_pluckerfetcher
    PRIVATE
        Qt::Test
        Qt::Core
)
add_test(NAME tst_pluckerfetcher COMMAND tst_pluckerfetcher)
```

- [ ] **Step 4: Run test to confirm failing build**

```bash
cmake --preset dev
cmake --build build-dev --target tst_pluckerfetcher 2>&1 | head -10
```
Expected: build fails — `pluckerfetcher.h` not found.

- [ ] **Step 5: Create `src/plugins/plucker/pluckerfetcher.h`**

```cpp
#ifndef PLUCKERFETCHER_H
#define PLUCKERFETCHER_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include "pluckerchannel.h"

namespace WildPalms::PluckerPlugin {

class PluckerFetcher : public QObject
{
    Q_OBJECT
public:
    struct Result {
        bool       success = false;
        QString    errorMessage;
        QByteArray pdbBytes;
        QString    docFile;
    };

    explicit PluckerFetcher(QObject *parent = nullptr);

    /// Synchronous. Spawns python3 + Spider.py with channel-derived
    /// CLI args, awaits completion (up to timeoutMs), reads back the
    /// produced `<docFile>.pdb` from a fresh QTemporaryDir.
    Result fetch(const PluckerChannel &channel, int timeoutMs = 300000);

    void setSpiderScriptPath(const QString &absPath)        { m_spiderOverride = absPath; }
    void setPythonExecutable(const QString &execName)       { m_pythonOverride = execName; }
    void setOutputDirectoryOverride(const QString &dir)     { m_outputDirOverride = dir; }

Q_SIGNALS:
    void progress(const QString &message);

private:
    QString resolveSpider() const;
    QString resolvePython() const;

    QString m_spiderOverride;
    QString m_pythonOverride;
    QString m_outputDirOverride;
};

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERFETCHER_H
```

- [ ] **Step 6: Create `src/plugins/plucker/pluckerfetcher.cpp`**

```cpp
#include "pluckerfetcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace WildPalms::PluckerPlugin {

PluckerFetcher::PluckerFetcher(QObject *parent)
    : QObject(parent)
{
}

QString PluckerFetcher::resolveSpider() const
{
    if (!m_spiderOverride.isEmpty()) return m_spiderOverride;
#ifdef PLUCKER_DATA_DIR
    const QString dev = QStringLiteral(PLUCKER_DATA_DIR
                                        "/parser/PyPlucker/Spider.py");
    if (QFile::exists(dev)) return QFileInfo(dev).canonicalFilePath();
#endif
    const QString installed = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../share/wildpalms/plucker/parser/PyPlucker/Spider.py");
    if (QFile::exists(installed)) return QFileInfo(installed).canonicalFilePath();
    return QString();
}

QString PluckerFetcher::resolvePython() const
{
    if (!m_pythonOverride.isEmpty()) return m_pythonOverride;
    for (const auto &name : {QStringLiteral("python3"), QStringLiteral("python")}) {
        QProcess probe;
        probe.start(name, {QStringLiteral("--version")});
        if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
            return name;
        }
    }
    return QStringLiteral("python3");
}

PluckerFetcher::Result PluckerFetcher::fetch(const PluckerChannel &channel,
                                               int timeoutMs)
{
    Result result;
    result.docFile = pluckerSanitizeDocFile(channel.name);

    const QString spider = resolveSpider();
    if (spider.isEmpty()) {
        result.errorMessage = QStringLiteral("PyPlucker Spider.py not found");
        return result;
    }
    if (!QFile::exists(spider)) {
        result.errorMessage = QStringLiteral("Spider.py path does not exist: %1").arg(spider);
        return result;
    }

    QTemporaryDir tmpDir;
    QString outputDir = m_outputDirOverride.isEmpty()
                            ? tmpDir.path()
                            : m_outputDirOverride;
    if (!tmpDir.isValid() && m_outputDirOverride.isEmpty()) {
        result.errorMessage = QStringLiteral("Failed to create temp dir");
        return result;
    }
    QDir().mkpath(outputDir);

    QStringList args;
    args << spider;
    args << pluckerBuildCliArgs(channel, outputDir);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"),
               QFileInfo(spider).absolutePath() + QStringLiteral("/.."));
    proc.setProcessEnvironment(env);

    Q_EMIT progress(QStringLiteral("Spidering %1...").arg(channel.name));
    proc.start(resolvePython(), args);

    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        result.errorMessage = QStringLiteral("Plucker timeout for %1").arg(channel.name);
        return result;
    }

    if (proc.exitCode() != 0) {
        const QString output = QString::fromUtf8(proc.readAll());
        result.errorMessage = QStringLiteral("Spider exit %1: %2")
            .arg(proc.exitCode()).arg(output.left(500));
        return result;
    }

    const QString pdbPath = QDir(outputDir).filePath(result.docFile + QStringLiteral(".pdb"));
    QFile pdb(pdbPath);
    if (!pdb.exists()) {
        result.errorMessage = QStringLiteral("Spider produced no .pdb at %1").arg(pdbPath);
        return result;
    }
    if (!pdb.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("Failed to open produced .pdb: %1").arg(pdb.errorString());
        return result;
    }
    result.pdbBytes = pdb.readAll();
    result.success  = true;
    return result;
}

} // namespace WildPalms::PluckerPlugin
```

- [ ] **Step 7: Build + run the test**

```bash
cmake --build build-dev --target tst_pluckerfetcher
ctest --test-dir build-dev -R '^tst_pluckerfetcher$' --output-on-failure
```
Expected: PASS (4 test functions). The hang test relies on `python3` being available in PATH; if absent the test errors clearly.

- [ ] **Step 8: Commit**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerfetcher.h pluckerfetcher.cpp
git commit -m "$(cat <<'EOF'
feat(plucker): PluckerFetcher subprocess wrapper

Synchronous QProcess driver around python3 + PyPlucker Spider.py;
returns produced .pdb bytes plus error metadata. Test seams for the
spider script path / python executable / output directory let unit
tests inject Python stubs without spawning the real spider.

Phase E.14 Task 4.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker tests/plugins/plucker
git commit -m "test(plucker): tst_pluckerfetcher + bump submodule (E.14 Task 4)"
```

---

## Task 5: `PluckerBlobBackend` channels collection (TDD)

**Files:**
- Create: `src/plugins/plucker/pluckerblobbackend.h`
- Create: `src/plugins/plucker/pluckerblobbackend.cpp`
- Create: `tests/plugins/plucker/tst_pluckerblobbackend.cpp`
- Modify: `tests/plugins/plucker/CMakeLists.txt`

**Goal:** First half of `PluckerBlobBackend` — `IBlobBackend` shell, `availableCollections` returning two collections, and `loadRecords("plucker:channels")` driving a mock fetcher per due channel. Bootstrap collection (next task) returns empty for now.

- [ ] **Step 1: Create the test file with channel-collection coverage**

`tests/plugins/plucker/tst_pluckerblobbackend.cpp`:

```cpp
#include <QTest>

#include "pluckerblobbackend.h"
#include "pluckerchannel.h"
#include "pluckerfetcher.h"

using namespace Kalburator::Sync;
using namespace WildPalms::PluckerPlugin;

namespace {

class FakePluckerFetcher : public PluckerFetcher
{
public:
    using PluckerFetcher::PluckerFetcher;

    Result fetch(const PluckerChannel &channel, int /*timeoutMs*/ = 0)
    {
        ++m_calls;
        m_lastChannelId = channel.id;
        Result r;
        r.docFile = pluckerSanitizeDocFile(channel.name);
        if (m_failNext) {
            r.success = false;
            r.errorMessage = QStringLiteral("simulated");
            m_failNext = false;
        } else {
            r.success  = true;
            r.pdbBytes = QStringLiteral("PDB:%1").arg(channel.id).toUtf8();
        }
        return r;
    }

    int      callCount()         const { return m_calls; }
    QString  lastChannelId()     const { return m_lastChannelId; }
    void     setFailNext(bool f)       { m_failNext = f; }

private:
    int     m_calls = 0;
    bool    m_failNext = false;
    QString m_lastChannelId;
};

PluckerChannel makeChannel(const QString &id, const QString &name,
                            bool enabled = true,
                            const QDateTime &lastFetched = {})
{
    PluckerChannel c;
    c.id            = id;
    c.name          = name;
    c.homeUrl       = QStringLiteral("https://example.com/") + id;
    c.updateEnabled = enabled;
    c.lastFetched   = lastFetched;
    return c;
}
} // namespace

class TestPluckerBlobBackend : public QObject
{
    Q_OBJECT

private slots:
    void availableCollections_listsChannelsAndBootstrap()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, /*device=*/nullptr,
                                    {}, {});
        const auto cols = backend.availableCollections();
        QCOMPARE(cols.size(), 2);

        QStringList ids;
        for (const auto &c : cols) ids << c.id;
        std::sort(ids.begin(), ids.end());
        QCOMPARE(ids, (QStringList{
            QStringLiteral("plucker:bootstrap"),
            QStringLiteral("plucker:channels")}));
    }

    void loadRecords_channels_emitsOnePerDueChannel()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha")),
            makeChannel(QStringLiteral("b"), QStringLiteral("Bravo"))
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QCOMPARE(records.size(), 2);
        QCOMPARE(fetcher.callCount(), 2);
        for (const auto &r : records) {
            QCOMPARE(r.collectionId, QStringLiteral("plucker:channels"));
            QVERIFY(r.id.startsWith(QStringLiteral("channel:")));
            QVERIFY(!r.content.isEmpty());
        }
    }

    void loadRecords_channels_skipsDisabled()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha"),
                        /*enabled=*/false),
            makeChannel(QStringLiteral("b"), QStringLiteral("Bravo"))
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QCOMPARE(records.size(), 1);
        QCOMPARE(fetcher.callCount(), 1);
        QCOMPARE(fetcher.lastChannelId(), QStringLiteral("b"));
    }

    void loadRecords_channels_skipsRecentlyFetched()
    {
        const QDateTime recent = QDateTime::currentDateTime().addSecs(-3600);
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha"),
                        /*enabled=*/true, recent)
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QVERIFY(records.isEmpty());
        QCOMPARE(fetcher.callCount(), 0);
    }

    void loadRecords_channels_failedFetchEmitsNothing()
    {
        FakePluckerFetcher fetcher;
        fetcher.setFailNext(true);
        PluckerBlobBackend backend({
            makeChannel(QStringLiteral("a"), QStringLiteral("Alpha"))
        }, &fetcher, nullptr, {}, {});

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:channels"));
        QVERIFY(records.isEmpty());
        QCOMPARE(fetcher.callCount(), 1);
    }

    void writeOps_areReadOnly()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, nullptr, {}, {});
        BackendRecord r;
        r.id            = QStringLiteral("x");
        r.collectionId  = QStringLiteral("plucker:channels");
        QVERIFY(backend.createRecord(QStringLiteral("plucker:channels"), r).isEmpty());
        QVERIFY(!backend.updateRecord(r));
        QVERIFY(!backend.deleteRecord(QStringLiteral("x")));
    }

    void loadRecords_unknownCollection_returnsEmpty()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, nullptr, {}, {});
        QVERIFY(backend.loadRecords(QStringLiteral("nope")).isEmpty());
    }
};

QTEST_MAIN(TestPluckerBlobBackend)
#include "tst_pluckerblobbackend.moc"
```

- [ ] **Step 2: Append blob-backend test target to `tests/plugins/plucker/CMakeLists.txt`**

```cmake

# --- Task 5: PluckerBlobBackend ---
add_executable(tst_pluckerblobbackend
    tst_pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
)
target_include_directories(tst_pluckerblobbackend
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_pluckerblobbackend BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_pluckerblobbackend
    PRIVATE
        WildPalmsCore
        Qt::Test
        Qt::Core
        Kalburator::Sync
)
add_test(NAME tst_pluckerblobbackend COMMAND tst_pluckerblobbackend)
```

- [ ] **Step 3: Run test — expect build failure (header missing)**

```bash
cmake --build build-dev --target tst_pluckerblobbackend 2>&1 | head -10
```
Expected: `pluckerblobbackend.h` not found.

- [ ] **Step 4: Create `src/plugins/plucker/pluckerblobbackend.h`**

```cpp
#ifndef PLUCKERBLOBBACKEND_H
#define PLUCKERBLOBBACKEND_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <iblobbackend.h>

#include "pluckerchannel.h"

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PluckerPlugin {

class PluckerFetcher;

/**
 * Source-only IBlobBackend exposing two collections:
 *   - "plucker:channels"  one record per due channel; blob = .pdb bytes
 *   - "plucker:bootstrap" SysZLib + viewer PRC bytes when device lacks
 *                         the "Plucker" DB
 *
 * Read-only on createRecord / updateRecord / deleteRecord.
 */
class PluckerBlobBackend : public Kalburator::Sync::IBlobBackend
{
    Q_OBJECT
public:
    PluckerBlobBackend(QList<PluckerChannel>                 channels,
                        PluckerFetcher                       *fetcher,
                        WildPalms::PalmSync::IPalmDatabaseAccess *device,
                        QByteArray                            sysZLibBytes,
                        QByteArray                            viewerBytes,
                        QObject                              *parent = nullptr);
    ~PluckerBlobBackend() override;

    // ===== Identity =====
    QString backendId() const override   { return QStringLiteral("plucker"); }
    QString displayName() const override { return QStringLiteral("Plucker"); }
    bool    isAvailable() const override { return true; }

    // ===== Collections =====
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override;

    // ===== Records =====
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                          const Kalburator::Sync::BackendRecord &record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    // ===== Change detection =====
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                              const QDateTime &since) override;

    // ===== Plucker-specific =====
    QList<PluckerChannel> channels() const { return m_channels; }

private:
    QList<Kalburator::Sync::BackendRecord> loadChannelRecords();
    QList<Kalburator::Sync::BackendRecord> loadBootstrapRecords();

    QList<PluckerChannel>                       m_channels;
    PluckerFetcher                             *m_fetcher;     // borrowed
    WildPalms::PalmSync::IPalmDatabaseAccess   *m_device;      // borrowed (may be null)
    QByteArray                                  m_sysZLibBytes;
    QByteArray                                  m_viewerBytes;
};

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERBLOBBACKEND_H
```

- [ ] **Step 5: Create `src/plugins/plucker/pluckerblobbackend.cpp` with channel-collection logic only**

The bootstrap collection method returns empty for now; Task 6 fills it in.

```cpp
#include "pluckerblobbackend.h"

#include <QDateTime>

#include "palm/sync/ipalmdatabaseaccess.h"
#include "pluckerfetcher.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;

namespace WildPalms::PluckerPlugin {

namespace {
constexpr const char *kCollChannels  = "plucker:channels";
constexpr const char *kCollBootstrap = "plucker:bootstrap";
} // namespace

PluckerBlobBackend::PluckerBlobBackend(
        QList<PluckerChannel>                 channels,
        PluckerFetcher                       *fetcher,
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        QByteArray                            sysZLibBytes,
        QByteArray                            viewerBytes,
        QObject                              *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_channels(std::move(channels))
    , m_fetcher(fetcher)
    , m_device(device)
    , m_sysZLibBytes(std::move(sysZLibBytes))
    , m_viewerBytes(std::move(viewerBytes))
{
}

PluckerBlobBackend::~PluckerBlobBackend() = default;

QList<CollectionInfo> PluckerBlobBackend::availableCollections()
{
    CollectionInfo channels;
    channels.id   = QString::fromLatin1(kCollChannels);
    channels.name = QStringLiteral("Plucker Channels");
    channels.type = QStringLiteral("plucker");

    CollectionInfo boot;
    boot.id   = QString::fromLatin1(kCollBootstrap);
    boot.name = QStringLiteral("Plucker Bootstrap PRCs");
    boot.type = QStringLiteral("plucker");

    return {channels, boot};
}

CollectionInfo PluckerBlobBackend::collectionInfo(const QString &collectionId)
{
    for (const auto &c : availableCollections()) {
        if (c.id == collectionId) return c;
    }
    return {};
}

QString PluckerBlobBackend::createCollection(const CollectionInfo &)
{
    return {};   // read-only
}

QList<BackendRecord> PluckerBlobBackend::loadRecords(const QString &collectionId)
{
    if (collectionId == QString::fromLatin1(kCollChannels)) {
        return loadChannelRecords();
    }
    if (collectionId == QString::fromLatin1(kCollBootstrap)) {
        return loadBootstrapRecords();
    }
    return {};
}

QList<BackendRecord> PluckerBlobBackend::loadChannelRecords()
{
    QList<BackendRecord> records;
    if (!m_fetcher) return records;

    for (auto &ch : m_channels) {
        if (!pluckerIsDue(ch)) continue;

        const auto result = m_fetcher->fetch(ch);
        if (!result.success) continue;

        BackendRecord r;
        r.id            = QStringLiteral("channel:%1").arg(ch.id);
        r.collectionId  = QString::fromLatin1(kCollChannels);
        r.content       = result.pdbBytes;
        r.contentType   = QStringLiteral("application/vnd.palm");
        r.modifiedAt    = QDateTime::currentDateTime();
        records.append(r);

        ch.lastFetched = QDateTime::currentDateTime();
    }
    return records;
}

QList<BackendRecord> PluckerBlobBackend::loadBootstrapRecords()
{
    return {};   // implemented in Task 6
}

std::optional<BackendRecord> PluckerBlobBackend::loadRecord(const QString &recordId)
{
    for (const auto &col : availableCollections()) {
        for (const auto &r : loadRecords(col.id)) {
            if (r.id == recordId) return r;
        }
    }
    return std::nullopt;
}

QString PluckerBlobBackend::createRecord(const QString &, const BackendRecord &)
{
    return {};
}

bool PluckerBlobBackend::updateRecord(const BackendRecord &) { return false; }
bool PluckerBlobBackend::deleteRecord(const QString &)        { return false; }

QList<BackendRecord> PluckerBlobBackend::modifiedSince(
    const QString &collectionId, const QDateTime &)
{
    return loadRecords(collectionId);
}

QStringList PluckerBlobBackend::deletedSince(const QString &, const QDateTime &)
{
    return {};
}

} // namespace WildPalms::PluckerPlugin
```

- [ ] **Step 6: Build + run tests**

```bash
cmake --build build-dev --target tst_pluckerblobbackend
ctest --test-dir build-dev -R '^tst_pluckerblobbackend$' --output-on-failure
```
Expected: PASS (7 test functions).

- [ ] **Step 7: Commit**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerblobbackend.h pluckerblobbackend.cpp
git commit -m "$(cat <<'EOF'
feat(plucker): PluckerBlobBackend channel-collection logic

Source-only IBlobBackend that drives the fetcher per due channel and
emits one BackendRecord per produced .pdb. Disabled and recently-fetched
channels are skipped. Failed fetches yield no record (channel will be
retried on next sync). createRecord / updateRecord / deleteRecord are
read-only no-ops. Bootstrap collection returns empty (next task).

Phase E.14 Task 5.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker tests/plugins/plucker
git commit -m "test(plucker): tst_pluckerblobbackend channels (E.14 Task 5)"
```

---

## Task 6: `PluckerBlobBackend` bootstrap collection (TDD)

**Files:**
- Modify: `src/plugins/plucker/pluckerblobbackend.cpp` (fill in `loadBootstrapRecords`)
- Create: `tests/plugins/plucker/fixtures/SysZLib_stub.prc` (4-byte content)
- Create: `tests/plugins/plucker/fixtures/viewer_stub.prc` (4-byte content)
- Modify: `tests/plugins/plucker/tst_pluckerblobbackend.cpp` (add bootstrap tests)

**Goal:** Bootstrap-collection records emitted only when device lacks the `Plucker` DB. Two records, IDs `bootstrap:syszlib` / `bootstrap:viewer`, content = bytes provided to constructor (caller-supplied PRC bytes).

- [ ] **Step 1: Add stub PRC fixture files**

`tests/plugins/plucker/fixtures/SysZLib_stub.prc`:
```
SZLB
```

`tests/plugins/plucker/fixtures/viewer_stub.prc`:
```
VIEW
```

(Each is exactly 4 bytes — content is irrelevant for the bootstrap test, but the bytes must match what the test asserts the backend returns.)

- [ ] **Step 2: Add bootstrap tests to `tst_pluckerblobbackend.cpp`**

Insert before the closing `};` of `TestPluckerBlobBackend`:

```cpp
    void loadRecords_bootstrap_emitsWhenDbMissing()
    {
        WildPalms::PalmSync::MockPalmDatabaseAccess mock;
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, &mock,
                                    QByteArray("SZLB"),
                                    QByteArray("VIEW"));

        const auto records = backend.loadRecords(
            QStringLiteral("plucker:bootstrap"));
        QCOMPARE(records.size(), 2);

        QHash<QString, QByteArray> byId;
        for (const auto &r : records) byId.insert(r.id, r.content);
        QCOMPARE(byId[QStringLiteral("bootstrap:syszlib")], QByteArray("SZLB"));
        QCOMPARE(byId[QStringLiteral("bootstrap:viewer")],  QByteArray("VIEW"));
    }

    void loadRecords_bootstrap_emptyWhenDbPresent()
    {
        WildPalms::PalmSync::MockPalmDatabaseAccess mock;
        mock.createDatabase(QStringLiteral("Plucker"));
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, &mock,
                                    QByteArray("SZLB"),
                                    QByteArray("VIEW"));

        QVERIFY(backend.loadRecords(
            QStringLiteral("plucker:bootstrap")).isEmpty());
    }

    void loadRecords_bootstrap_emptyWhenNoDevice()
    {
        FakePluckerFetcher fetcher;
        PluckerBlobBackend backend({}, &fetcher, nullptr,
                                    QByteArray("SZLB"),
                                    QByteArray("VIEW"));
        QVERIFY(backend.loadRecords(
            QStringLiteral("plucker:bootstrap")).isEmpty());
    }

    void loadRecords_bootstrap_skipsEmptyByteArrays()
    {
        WildPalms::PalmSync::MockPalmDatabaseAccess mock;
        FakePluckerFetcher fetcher;
        // No SysZLib bytes available -> only viewer is emitted.
        PluckerBlobBackend backend({}, &fetcher, &mock,
                                    QByteArray(),
                                    QByteArray("VIEW"));
        const auto records = backend.loadRecords(
            QStringLiteral("plucker:bootstrap"));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].id, QStringLiteral("bootstrap:viewer"));
    }
```

Add at the top of the test file (after existing includes):
```cpp
#include "palm/sync/mockpalmdatabaseaccess.h"
```

- [ ] **Step 3: Update test CMake to link the mock**

In `tests/plugins/plucker/CMakeLists.txt` `tst_pluckerblobbackend` target, the mock lives in `WildPalmsCore` (or an adjacent target — verify by reading `src/palm/sync/CMakeLists.txt`). It should already be reachable via `WildPalmsCore` linkage; if the build fails on `mockpalmdatabaseaccess.cpp` symbols, add `${CMAKE_SOURCE_DIR}/src/palm/sync/mockpalmdatabaseaccess.cpp` to the target's SOURCES list.

Run:
```bash
cmake --build build-dev --target tst_pluckerblobbackend 2>&1 | tail -20
```
If it fails on `MockPalmDatabaseAccess` symbols, add the source file:
```cmake
add_executable(tst_pluckerblobbackend
    tst_pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
    ${CMAKE_SOURCE_DIR}/src/palm/sync/mockpalmdatabaseaccess.cpp
)
```
Expected fix: build proceeds; tests fail because `loadBootstrapRecords` still returns `{}`.

- [ ] **Step 4: Implement `loadBootstrapRecords`**

Edit `pluckerblobbackend.cpp`. Replace the `loadBootstrapRecords` body with:

```cpp
QList<BackendRecord> PluckerBlobBackend::loadBootstrapRecords()
{
    QList<BackendRecord> records;
    if (!m_device) return records;
    if (m_device->hasDatabase(QStringLiteral("Plucker"))) return records;

    const QDateTime now = QDateTime::currentDateTime();

    if (!m_sysZLibBytes.isEmpty()) {
        BackendRecord r;
        r.id            = QStringLiteral("bootstrap:syszlib");
        r.collectionId  = QString::fromLatin1(kCollBootstrap);
        r.content       = m_sysZLibBytes;
        r.contentType   = QStringLiteral("application/vnd.palm");
        r.modifiedAt    = now;
        records.append(r);
    }
    if (!m_viewerBytes.isEmpty()) {
        BackendRecord r;
        r.id            = QStringLiteral("bootstrap:viewer");
        r.collectionId  = QString::fromLatin1(kCollBootstrap);
        r.content       = m_viewerBytes;
        r.contentType   = QStringLiteral("application/vnd.palm");
        r.modifiedAt    = now;
        records.append(r);
    }
    return records;
}
```

- [ ] **Step 5: Build + run tests**

```bash
cmake --build build-dev --target tst_pluckerblobbackend
ctest --test-dir build-dev -R '^tst_pluckerblobbackend$' --output-on-failure
```
Expected: PASS (11 test functions: 7 existing + 4 new bootstrap).

- [ ] **Step 6: Commit**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerblobbackend.cpp
git commit -m "$(cat <<'EOF'
feat(plucker): bootstrap PRC emission gated on device DB state

PluckerBlobBackend's "plucker:bootstrap" collection emits SysZLib +
viewer PRCs as BackendRecords when IPalmDatabaseAccess::hasDatabase
("Plucker") returns false. Empty byte-array inputs are skipped (so
absent bundled PRCs degrade gracefully rather than emitting empty
records).

Phase E.14 Task 6.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker tests/plugins/plucker
git commit -m "test(plucker): bootstrap collection coverage (E.14 Task 6)"
```

---

## Task 7: `PluckerBackendPlugin` shell + KPluginFactory round-trip (TDD)

**Files:**
- Create: `src/plugins/plucker/pluckerbackendplugin.h`
- Create: `src/plugins/plucker/pluckerbackendplugin.cpp`
- Create: `tests/plugins/plucker/tst_pluckerbackendplugin.cpp`
- Modify: `tests/plugins/plucker/CMakeLists.txt`

**Goal:** The plugin shell — IPlugin + IBackendPlugin overrides, JSON settings round-trip via `pluckerChannelFromJson` array, and `K_PLUGIN_FACTORY_WITH_JSON` registration. Also exposes a viewer-PRC-bytes loader (reads the bundled `viewer/SysZLib.prc` + `viewer/viewer_en.prc` via `PLUCKER_DATA_DIR` / AppImage layout) and passes those bytes into the blob backend.

- [ ] **Step 1: Create the test file**

`tests/plugins/plucker/tst_pluckerbackendplugin.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSignalSpy>
#include <QTest>

#include <KPluginFactory>
#include <KPluginMetaData>

#include "pluckerbackendplugin.h"

using namespace WildPalms::PluckerPlugin;

class TestPluckerBackendPlugin : public QObject
{
    Q_OBJECT

private slots:
    void identity_metadata()
    {
        PluckerBackendPlugin plugin;
        QCOMPARE(plugin.pluginId(),    QStringLiteral("plucker"));
        QCOMPARE(plugin.displayName(), QStringLiteral("Plucker"));
        QCOMPARE(plugin.version(),     QStringLiteral("2.0.0"));
        QVERIFY(plugin.claimedDatabases().isEmpty());
        QVERIFY(!plugin.hasMainView());
        QVERIFY(plugin.hasSettings());
    }

    void runAfter_listsAllPriorPlugins()
    {
        PluckerBackendPlugin plugin;
        const QStringList ra = plugin.runAfter();
        QVERIFY(ra.contains(QStringLiteral("memo")));
        QVERIFY(ra.contains(QStringLiteral("calendar")));
        QVERIFY(ra.contains(QStringLiteral("todo")));
        QVERIFY(ra.contains(QStringLiteral("contacts")));
        QVERIFY(ra.contains(QStringLiteral("webcalendar")));
    }

    void settings_roundTripJson()
    {
        QJsonObject in;
        QJsonArray channels;
        QJsonObject ch;
        ch[QStringLiteral("id")]       = QStringLiteral("c1");
        ch[QStringLiteral("name")]     = QStringLiteral("Alpha");
        ch[QStringLiteral("home_url")] = QStringLiteral("https://a.example/");
        ch[QStringLiteral("max_depth")] = 4;
        channels.append(ch);
        in[QStringLiteral("channels")] = channels;

        PluckerBackendPlugin plugin;
        plugin.loadSettings(in);
        const QJsonObject out = plugin.saveSettings();

        QVERIFY(out.contains(QStringLiteral("channels")));
        const QJsonArray outChannels = out[QStringLiteral("channels")].toArray();
        QCOMPARE(outChannels.size(), 1);
        const QJsonObject ch0 = outChannels[0].toObject();
        QCOMPARE(ch0[QStringLiteral("id")].toString(),       QStringLiteral("c1"));
        QCOMPARE(ch0[QStringLiteral("name")].toString(),     QStringLiteral("Alpha"));
        QCOMPARE(ch0[QStringLiteral("max_depth")].toInt(),   4);
    }

    void settings_emptyJsonYieldsNoChannels()
    {
        PluckerBackendPlugin plugin;
        plugin.loadSettings(QJsonObject{});
        QCOMPARE(plugin.channels().size(), 0);
    }

    void factory_loadsViaKPluginFactory()
    {
        // The plugin .so is built as wildpalms_plucker_v2 and installed
        // into wildpalms/plugins. For the test we load the build-tree
        // .so directly.
        const QString pluginSo = QStringLiteral(PLUCKER_PLUGIN_SO_PATH);
        QPluginLoader loader(pluginSo);
        QObject *root = loader.instance();
        QVERIFY2(root, qPrintable(loader.errorString()));

        auto *factory = qobject_cast<KPluginFactory*>(root);
        QVERIFY(factory);

        auto *plugin = factory->create<PluckerBackendPlugin>(this);
        QVERIFY(plugin);
        QCOMPARE(plugin->pluginId(), QStringLiteral("plucker"));
    }
};

QTEST_MAIN(TestPluckerBackendPlugin)
#include "tst_pluckerbackendplugin.moc"
```

- [ ] **Step 2: Append plugin-shell test target to `tests/plugins/plucker/CMakeLists.txt`**

```cmake

# --- Task 7: PluckerBackendPlugin ---
add_executable(tst_pluckerbackendplugin
    tst_pluckerbackendplugin.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerbackendplugin.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannelserializer.cpp
)
target_compile_definitions(tst_pluckerbackendplugin
    PRIVATE
        PLUCKER_PLUGIN_SO_PATH="$<TARGET_FILE:wildpalms_plucker_v2>"
        PLUCKER_DATA_DIR="${PLUCKER_PLUGIN_SRC_DIR}"
)
target_include_directories(tst_pluckerbackendplugin
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_pluckerbackendplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_pluckerbackendplugin
    PRIVATE
        WildPalmsCore
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CoreAddons
        Kalburator::Sync
)
add_dependencies(tst_pluckerbackendplugin wildpalms_plucker_v2)
add_test(NAME tst_pluckerbackendplugin COMMAND tst_pluckerbackendplugin)
```

- [ ] **Step 3: Run test — expect compile + link failure (sources missing)**

```bash
cmake --build build-dev --target tst_pluckerbackendplugin 2>&1 | head -10
```
Expected: `pluckerbackendplugin.h` not found, plus `wildpalms_plucker_v2` target also failing (it depends on the same set of files we're about to add).

- [ ] **Step 4: Create `src/plugins/plucker/pluckerbackendplugin.h`**

```cpp
#ifndef PLUCKERBACKENDPLUGIN_H
#define PLUCKERBACKENDPLUGIN_H

#include <QJsonObject>
#include <QList>
#include <QObject>

#include <core/ibackendplugin.h>

#include "pluckerchannel.h"

namespace WildPalms::PluckerPlugin {

class PluckerBlobBackend;
class PluckerFetcher;

class PluckerBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)

public:
    explicit PluckerBackendPlugin(QObject *parent = nullptr);
    ~PluckerBackendPlugin() override;

    // ===== IPlugin =====
    QString pluginId()    const override { return QStringLiteral("plucker"); }
    QString displayName() const override { return QStringLiteral("Plucker"); }
    QString description() const override
    { return QStringLiteral("Fetch web content as Palm Plucker documents"); }
    QString version()     const override { return QStringLiteral("2.0.0"); }
    QIcon   icon()        const override;

    bool        hasSettings()                    const override { return true; }
    QWidget    *createSettingsWidget(QWidget *parent)        override;
    void        loadSettings(const QJsonObject &settings)    override;
    QJsonObject saveSettings()                   const       override;

    // ===== IBackendPlugin =====
    QStringList claimedDatabases() const override { return {}; }
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                     PalmDeviceConnection         *device) override;

    bool hasMainView() const override { return false; }

    QStringList runBefore() const override { return {}; }
    QStringList runAfter()  const override
    {
        return { QStringLiteral("memo"),
                 QStringLiteral("calendar"),
                 QStringLiteral("todo"),
                 QStringLiteral("contacts"),
                 QStringLiteral("webcalendar") };
    }

    // Test/UI accessors.
    QList<PluckerChannel> channels() const { return m_channels; }
    void setChannels(QList<PluckerChannel> channels);

private:
    QByteArray loadViewerPrc(const QString &filename) const;

    QList<PluckerChannel>  m_channels;
    PluckerFetcher        *m_fetcher = nullptr;   // owned (parented)
    PluckerBlobBackend    *m_backend = nullptr;   // owned by manager
};

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERBACKENDPLUGIN_H
```

- [ ] **Step 5: Create `src/plugins/plucker/pluckerbackendplugin.cpp`**

```cpp
#include "pluckerbackendplugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <KPluginFactory>

#include "palm/palmdeviceconnection.h"
#include "pluckerblobbackend.h"
#include "pluckerchannelserializer.h"
#include "pluckerfetcher.h"

namespace WildPalms::PluckerPlugin {

PluckerBackendPlugin::PluckerBackendPlugin(QObject *parent)
    : QObject(parent)
{
}

PluckerBackendPlugin::~PluckerBackendPlugin() = default;

QIcon PluckerBackendPlugin::icon() const
{
    return QIcon::fromTheme(QStringLiteral("text-html"));
}

QWidget *PluckerBackendPlugin::createSettingsWidget(QWidget * /*parent*/)
{
    // Implemented in Task 9 — returns nullptr until the widget exists.
    return nullptr;
}

void PluckerBackendPlugin::loadSettings(const QJsonObject &settings)
{
    m_channels.clear();
    const QJsonArray arr = settings.value(QStringLiteral("channels")).toArray();
    for (const auto &v : arr) {
        m_channels.append(pluckerChannelFromJson(v.toObject()));
    }
}

QJsonObject PluckerBackendPlugin::saveSettings() const
{
    QJsonArray arr;
    for (const auto &c : m_channels) arr.append(pluckerChannelToJson(c));
    QJsonObject o;
    o[QStringLiteral("channels")] = arr;
    return o;
}

void PluckerBackendPlugin::setChannels(QList<PluckerChannel> channels)
{
    m_channels = std::move(channels);
}

WildPalms::IBackendPlugin::ProvidedBackends
PluckerBackendPlugin::createBackends(Kalburator::Sync::ISyncHost * /*host*/,
                                       PalmDeviceConnection         *device)
{
    if (!m_fetcher) m_fetcher = new PluckerFetcher(this);

    WildPalms::PalmSync::IPalmDatabaseAccess *db =
        device ? device->device() : nullptr;

    m_backend = new PluckerBlobBackend(
        m_channels,
        m_fetcher,
        db,
        loadViewerPrc(QStringLiteral("SysZLib.prc")),
        loadViewerPrc(QStringLiteral("viewer_en.prc")));

    return { m_backend, nullptr };
}

QByteArray PluckerBackendPlugin::loadViewerPrc(const QString &filename) const
{
    auto tryRead = [](const QString &path) -> QByteArray {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    };

#ifdef PLUCKER_DATA_DIR
    const QString dev = QStringLiteral(PLUCKER_DATA_DIR "/viewer/") + filename;
    if (auto bytes = tryRead(dev); !bytes.isEmpty()) return bytes;
#endif
    const QString installed = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../share/wildpalms/plucker/viewer/") + filename;
    return tryRead(installed);
}

} // namespace WildPalms::PluckerPlugin

K_PLUGIN_FACTORY_WITH_JSON(
    PluckerBackendPluginFactory,
    "plucker-backend-plugin.json",
    registerPlugin<WildPalms::PluckerPlugin::PluckerBackendPlugin>();)

#include "pluckerbackendplugin.moc"
```

- [ ] **Step 6: Build `wildpalms_plucker_v2` and the test**

The V2 toggle expects `pluckersettingswidget.cpp` and `pluckerchanneleditor.cpp`. We add empty stubs now so the V2 plugin links; Task 9 fills them with the real UI.

Create `src/plugins/plucker/pluckersettingswidget.h`:
```cpp
#ifndef PLUCKERSETTINGSWIDGET_H
#define PLUCKERSETTINGSWIDGET_H
#include <QWidget>
namespace WildPalms::PluckerPlugin {
class PluckerBackendPlugin;
class PluckerSettingsWidget : public QWidget {
    Q_OBJECT
public:
    PluckerSettingsWidget(PluckerBackendPlugin *plugin, QWidget *parent = nullptr);
};
}
#endif
```

Create `src/plugins/plucker/pluckersettingswidget.cpp`:
```cpp
#include "pluckersettingswidget.h"
namespace WildPalms::PluckerPlugin {
PluckerSettingsWidget::PluckerSettingsWidget(PluckerBackendPlugin *, QWidget *parent)
    : QWidget(parent) {}
} // namespace
```

Create `src/plugins/plucker/pluckerchanneleditor.h`:
```cpp
#ifndef PLUCKERCHANNELEDITOR_H
#define PLUCKERCHANNELEDITOR_H
#include <QDialog>
#include "pluckerchannel.h"
namespace WildPalms::PluckerPlugin {
class PluckerChannelEditor : public QDialog {
    Q_OBJECT
public:
    PluckerChannelEditor(PluckerChannel channel, QWidget *parent = nullptr);
    PluckerChannel channel() const { return m_channel; }
private:
    PluckerChannel m_channel;
};
}
#endif
```

Create `src/plugins/plucker/pluckerchanneleditor.cpp`:
```cpp
#include "pluckerchanneleditor.h"
namespace WildPalms::PluckerPlugin {
PluckerChannelEditor::PluckerChannelEditor(PluckerChannel channel, QWidget *parent)
    : QDialog(parent), m_channel(std::move(channel)) {}
} // namespace
```

```bash
cmake --preset dev
cmake --build build-dev --target wildpalms_plucker_v2 tst_pluckerbackendplugin
```
Expected: clean build.

- [ ] **Step 7: Run the tests**

```bash
ctest --test-dir build-dev -R '^tst_pluckerbackendplugin$' --output-on-failure
```
Expected: PASS (5 test functions). The factory_loadsViaKPluginFactory test loads `$<TARGET_FILE:wildpalms_plucker_v2>` directly from the build tree.

- [ ] **Step 8: Commit**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerbackendplugin.h pluckerbackendplugin.cpp \
        pluckersettingswidget.h pluckersettingswidget.cpp \
        pluckerchanneleditor.h pluckerchanneleditor.cpp
git commit -m "$(cat <<'EOF'
feat(plucker): IBackendPlugin shell + KPluginFactory registration

PluckerBackendPlugin implements IPlugin + IBackendPlugin, round-trips
channels[] settings JSON via pluckerChannelSerializer, loads bundled
SysZLib.prc + viewer_en.prc bytes, and constructs PluckerBlobBackend.
runAfter lists memo/calendar/todo/contacts/webcalendar so the install
drain (E.15) runs after all four record-sync plugins finish. Settings
widget + channel editor stubs land empty; Task 9 fills them in.

Phase E.14 Task 7.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker tests/plugins/plucker
git commit -m "test(plucker): tst_pluckerbackendplugin (E.14 Task 7)"
```

---

## Task 8: `PluckerSettingsWidget` + `PluckerChannelEditor` (UI)

**Files:**
- Modify: `src/plugins/plucker/pluckersettingswidget.{h,cpp}`
- Modify: `src/plugins/plucker/pluckerchanneleditor.{h,cpp}`

**Goal:** Channel-management UI. Re-skin of the legacy `pluckerview.cpp` operating on the V2 plugin's in-memory `QList<PluckerChannel>` directly, with the editor dialog operating on a `PluckerChannel` value.

This task has limited TDD coverage — the UI is glue around `QTreeWidget` + `QDialog`. Verify by:
1. Building the V2 plugin and the existing tests still pass.
2. Manual smoke: launch WildPalms, enable Plucker plugin, add/edit/remove a channel, verify the JSON round-trip via `loadSettings` + `saveSettings`.

- [ ] **Step 1: Replace `pluckerchanneleditor.h` with the real editor**

```cpp
#ifndef PLUCKERCHANNELEDITOR_H
#define PLUCKERCHANNELEDITOR_H

#include <QDialog>

#include "pluckerchannel.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace WildPalms::PluckerPlugin {

class PluckerChannelEditor : public QDialog
{
    Q_OBJECT
public:
    explicit PluckerChannelEditor(PluckerChannel channel,
                                    QWidget        *parent = nullptr);
    PluckerChannel channel() const;

private:
    void buildUi();
    void loadFromChannel();
    void writeToChannel();

    PluckerChannel m_channel;

    QLineEdit  *m_name           = nullptr;
    QLineEdit  *m_homeUrl        = nullptr;
    QSpinBox   *m_maxDepth       = nullptr;
    QCheckBox  *m_stayOnHost     = nullptr;
    QCheckBox  *m_depthFirst     = nullptr;
    QCheckBox  *m_noImages       = nullptr;
    QSpinBox   *m_bpp            = nullptr;
    QSpinBox   *m_maxWidth       = nullptr;
    QSpinBox   *m_maxHeight      = nullptr;
    QComboBox  *m_compression    = nullptr;
    QLineEdit  *m_category       = nullptr;
    QSpinBox   *m_updateFrequency= nullptr;
    QComboBox  *m_updatePeriod   = nullptr;
    QCheckBox  *m_updateEnabled  = nullptr;
};

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERCHANNELEDITOR_H
```

- [ ] **Step 2: Replace `pluckerchanneleditor.cpp` with the implementation**

```cpp
#include "pluckerchanneleditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace WildPalms::PluckerPlugin {

PluckerChannelEditor::PluckerChannelEditor(PluckerChannel channel,
                                             QWidget        *parent)
    : QDialog(parent)
    , m_channel(std::move(channel))
{
    buildUi();
    loadFromChannel();
}

void PluckerChannelEditor::buildUi()
{
    setWindowTitle(tr("Plucker Channel"));
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_name           = new QLineEdit(this);
    m_homeUrl        = new QLineEdit(this);
    m_maxDepth       = new QSpinBox(this);  m_maxDepth->setRange(0, 10);
    m_stayOnHost     = new QCheckBox(tr("Stay on host"), this);
    m_depthFirst     = new QCheckBox(tr("Depth-first"), this);
    m_noImages       = new QCheckBox(tr("No images"), this);
    m_bpp            = new QSpinBox(this);  m_bpp->setRange(1, 32);
    m_maxWidth       = new QSpinBox(this);  m_maxWidth->setRange(0, 9999);
    m_maxHeight      = new QSpinBox(this);  m_maxHeight->setRange(0, 9999);
    m_compression    = new QComboBox(this);
    m_compression->addItems({QStringLiteral("zlib"), QStringLiteral("none")});
    m_category       = new QLineEdit(this);
    m_updateFrequency= new QSpinBox(this);  m_updateFrequency->setRange(1, 999);
    m_updatePeriod   = new QComboBox(this);
    m_updatePeriod->addItems({QStringLiteral("hours"),
                               QStringLiteral("days"),
                               QStringLiteral("weeks"),
                               QStringLiteral("months")});
    m_updateEnabled  = new QCheckBox(tr("Update enabled"), this);

    form->addRow(tr("Name:"),               m_name);
    form->addRow(tr("Home URL:"),           m_homeUrl);
    form->addRow(tr("Max depth:"),          m_maxDepth);
    form->addRow(QString(),                 m_stayOnHost);
    form->addRow(QString(),                 m_depthFirst);
    form->addRow(QString(),                 m_noImages);
    form->addRow(tr("BPP:"),                m_bpp);
    form->addRow(tr("Max width:"),          m_maxWidth);
    form->addRow(tr("Max height:"),         m_maxHeight);
    form->addRow(tr("Compression:"),        m_compression);
    form->addRow(tr("Category:"),           m_category);
    form->addRow(tr("Update frequency:"),   m_updateFrequency);
    form->addRow(tr("Update period:"),      m_updatePeriod);
    form->addRow(QString(),                 m_updateEnabled);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, [this] { writeToChannel(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    root->addLayout(form);
    root->addWidget(buttons);
}

void PluckerChannelEditor::loadFromChannel()
{
    m_name->setText(m_channel.name);
    m_homeUrl->setText(m_channel.homeUrl);
    m_maxDepth->setValue(m_channel.maxDepth);
    m_stayOnHost->setChecked(m_channel.stayOnHost);
    m_depthFirst->setChecked(m_channel.depthFirst);
    m_noImages->setChecked(m_channel.noImages);
    m_bpp->setValue(m_channel.bpp);
    m_maxWidth->setValue(m_channel.maxWidth);
    m_maxHeight->setValue(m_channel.maxHeight);
    const int compIdx = m_compression->findText(m_channel.compression);
    m_compression->setCurrentIndex(compIdx >= 0 ? compIdx : 0);
    m_category->setText(m_channel.category);
    m_updateFrequency->setValue(m_channel.updateFrequency);
    const int periodIdx = m_updatePeriod->findText(m_channel.updatePeriod);
    m_updatePeriod->setCurrentIndex(periodIdx >= 0 ? periodIdx : 1);
    m_updateEnabled->setChecked(m_channel.updateEnabled);
}

void PluckerChannelEditor::writeToChannel()
{
    m_channel.name           = m_name->text();
    m_channel.homeUrl        = m_homeUrl->text();
    m_channel.maxDepth       = m_maxDepth->value();
    m_channel.stayOnHost     = m_stayOnHost->isChecked();
    m_channel.depthFirst     = m_depthFirst->isChecked();
    m_channel.noImages       = m_noImages->isChecked();
    m_channel.bpp            = m_bpp->value();
    m_channel.maxWidth       = m_maxWidth->value();
    m_channel.maxHeight      = m_maxHeight->value();
    m_channel.compression    = m_compression->currentText();
    m_channel.category       = m_category->text();
    m_channel.updateFrequency= m_updateFrequency->value();
    m_channel.updatePeriod   = m_updatePeriod->currentText();
    m_channel.updateEnabled  = m_updateEnabled->isChecked();
}

PluckerChannel PluckerChannelEditor::channel() const
{
    return m_channel;
}

} // namespace WildPalms::PluckerPlugin
```

- [ ] **Step 3: Replace `pluckersettingswidget.h` with the real widget**

```cpp
#ifndef PLUCKERSETTINGSWIDGET_H
#define PLUCKERSETTINGSWIDGET_H

#include <QWidget>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace WildPalms::PluckerPlugin {

class PluckerBackendPlugin;

class PluckerSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    PluckerSettingsWidget(PluckerBackendPlugin *plugin,
                          QWidget               *parent = nullptr);

Q_SIGNALS:
    void settingsChanged();

private:
    void rebuildList();
    void onAdd();
    void onEdit();
    void onRemove();
    void onSelectionChanged();

    PluckerBackendPlugin *m_plugin = nullptr;
    QTreeWidget          *m_list   = nullptr;
    QPushButton          *m_addBtn = nullptr;
    QPushButton          *m_editBtn= nullptr;
    QPushButton          *m_remBtn = nullptr;
};

} // namespace WildPalms::PluckerPlugin

#endif // PLUCKERSETTINGSWIDGET_H
```

- [ ] **Step 4: Replace `pluckersettingswidget.cpp` with the implementation**

```cpp
#include "pluckersettingswidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "pluckerbackendplugin.h"
#include "pluckerchannel.h"
#include "pluckerchanneleditor.h"

namespace WildPalms::PluckerPlugin {

PluckerSettingsWidget::PluckerSettingsWidget(PluckerBackendPlugin *plugin,
                                                 QWidget               *parent)
    : QWidget(parent)
    , m_plugin(plugin)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_list = new QTreeWidget(this);
    m_list->setHeaderLabels({tr("Name"), tr("Home URL"), tr("Last Fetched")});
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(true);
    m_list->header()->setStretchLastSection(true);
    connect(m_list, &QTreeWidget::itemSelectionChanged,
            this, &PluckerSettingsWidget::onSelectionChanged);
    root->addWidget(m_list, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn  = new QPushButton(tr("Add..."), this);
    m_editBtn = new QPushButton(tr("Edit..."), this);
    m_remBtn  = new QPushButton(tr("Remove"), this);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_remBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    connect(m_addBtn, &QPushButton::clicked, this, &PluckerSettingsWidget::onAdd);
    connect(m_editBtn,&QPushButton::clicked, this, &PluckerSettingsWidget::onEdit);
    connect(m_remBtn, &QPushButton::clicked, this, &PluckerSettingsWidget::onRemove);

    rebuildList();
    onSelectionChanged();
}

void PluckerSettingsWidget::rebuildList()
{
    m_list->clear();
    if (!m_plugin) return;
    for (const auto &ch : m_plugin->channels()) {
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, ch.name);
        item->setText(1, ch.homeUrl);
        item->setText(2, ch.lastFetched.isValid()
                            ? ch.lastFetched.toString(Qt::ISODate)
                            : tr("never"));
        item->setData(0, Qt::UserRole, ch.id);
    }
}

void PluckerSettingsWidget::onAdd()
{
    PluckerChannelEditor dlg(PluckerChannel{}, this);
    if (dlg.exec() == QDialog::Accepted) {
        auto channels = m_plugin->channels();
        channels.append(dlg.channel());
        m_plugin->setChannels(channels);
        rebuildList();
        Q_EMIT settingsChanged();
    }
}

void PluckerSettingsWidget::onEdit()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    auto channels = m_plugin->channels();
    for (int i = 0; i < channels.size(); ++i) {
        if (channels[i].id != id) continue;
        PluckerChannelEditor dlg(channels[i], this);
        if (dlg.exec() == QDialog::Accepted) {
            channels[i] = dlg.channel();
            m_plugin->setChannels(channels);
            rebuildList();
            Q_EMIT settingsChanged();
        }
        return;
    }
}

void PluckerSettingsWidget::onRemove()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    auto channels = m_plugin->channels();
    channels.removeIf([&id](const PluckerChannel &c) { return c.id == id; });
    m_plugin->setChannels(channels);
    rebuildList();
    Q_EMIT settingsChanged();
}

void PluckerSettingsWidget::onSelectionChanged()
{
    const bool has = m_list->currentItem() != nullptr;
    m_editBtn->setEnabled(has);
    m_remBtn->setEnabled(has);
}

} // namespace WildPalms::PluckerPlugin
```

- [ ] **Step 5: Wire `createSettingsWidget` to return the new widget**

Edit `src/plugins/plucker/pluckerbackendplugin.cpp` — change:
```cpp
QWidget *PluckerBackendPlugin::createSettingsWidget(QWidget * /*parent*/)
{
    return nullptr;
}
```
to:
```cpp
QWidget *PluckerBackendPlugin::createSettingsWidget(QWidget *parent)
{
    return new PluckerSettingsWidget(this, parent);
}
```

Add include at the top:
```cpp
#include "pluckersettingswidget.h"
```

- [ ] **Step 6: Build the V2 plugin**

```bash
cmake --build build-dev --target wildpalms_plucker_v2
```
Expected: clean build.

- [ ] **Step 7: Re-run all five existing test executables to confirm no regression**

```bash
ctest --test-dir build-dev -R '^tst_plucker' --output-on-failure
```
Expected: all 4 existing tests pass (channel, fetcher, blobbackend, backendplugin).

- [ ] **Step 8: Commit**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerchanneleditor.h pluckerchanneleditor.cpp \
        pluckersettingswidget.h pluckersettingswidget.cpp \
        pluckerbackendplugin.cpp
git commit -m "$(cat <<'EOF'
feat(plucker): channel-management settings widget

PluckerSettingsWidget hosts a QTreeWidget of channels with add/edit/
remove buttons; PluckerChannelEditor is the per-channel dialog.
PluckerBackendPlugin::createSettingsWidget returns a new widget
parented into the host. settingsChanged signal lets the host trigger
saveSettings on any UI mutation.

Phase E.14 Task 8.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker
git commit -m "feat(plucker): bump submodule — settings widget (E.14 Task 8)"
```

---

## Task 9: end-to-end test `tst_plucker_v2_e2e`

**Files:**
- Create: `tests/plugins/plucker/tst_plucker_v2_e2e.cpp`
- Modify: `tests/plugins/plucker/CMakeLists.txt`

**Goal:** Drive `BlobSyncEngine::mirror()` from `PluckerBlobBackend` into `MockBlobBackend`. Two channels (one due, one not). Mocked device that lacks `Plucker` DB on first run, has it on second run. Asserts: due channel produces a record; non-due channel does not; bootstrap PRCs appear in the sink only on the first run; `last_fetched` is updated.

This test does **not** invoke the real PyPlucker subprocess; it uses a `FakePluckerFetcher` test double with the same shape as in Task 5.

- [ ] **Step 1: Create `tests/plugins/plucker/tst_plucker_v2_e2e.cpp`**

```cpp
#include <QDateTime>
#include <QTest>

#include <blobsyncengine.h>
#include <mockblobbackend.h>

#include "palm/sync/mockpalmdatabaseaccess.h"
#include "pluckerbackendplugin.h"
#include "pluckerblobbackend.h"
#include "pluckerchannel.h"
#include "pluckerfetcher.h"

using namespace Kalburator::Sync;
using namespace WildPalms::PluckerPlugin;

namespace {

class FakePluckerFetcher : public PluckerFetcher
{
public:
    using PluckerFetcher::PluckerFetcher;
    Result fetch(const PluckerChannel &channel, int = 0)
    {
        Result r;
        r.success = true;
        r.docFile = pluckerSanitizeDocFile(channel.name);
        r.pdbBytes = QStringLiteral("PDB:%1").arg(channel.id).toUtf8();
        return r;
    }
};

PluckerChannel makeChannel(const QString &id, const QString &name,
                            const QDateTime &lastFetched = {})
{
    PluckerChannel c;
    c.id            = id;
    c.name          = name;
    c.homeUrl       = QStringLiteral("https://example.com/") + id;
    c.updateEnabled = true;
    c.lastFetched   = lastFetched;
    return c;
}
} // namespace

class TestPluckerV2E2E : public QObject
{
    Q_OBJECT

private slots:
    void mirror_dueChannelProducesRecord_nonDueDoesNot()
    {
        FakePluckerFetcher fetcher;
        WildPalms::PalmSync::MockPalmDatabaseAccess device;

        const QDateTime fresh = QDateTime::currentDateTime().addSecs(-3600);
        PluckerBlobBackend src({
            makeChannel(QStringLiteral("due"),    QStringLiteral("Due Channel")),
            makeChannel(QStringLiteral("recent"), QStringLiteral("Recent Channel"), fresh)
        }, &fetcher, &device, QByteArray("SZLB"), QByteArray("VIEW"));

        MockBlobBackend dst;
        CollectionInfo c;
        c.id   = QStringLiteral("plucker:channels");
        c.name = QStringLiteral("Plucker Channels");
        c.type = QStringLiteral("plucker");
        dst.createCollection(c);

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("plucker:channels"));

        const auto records = dst.loadRecords(QStringLiteral("plucker:channels"));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].id, QStringLiteral("channel:due"));
    }

    void mirror_bootstrapEmittedOnFirstRun_absentOnSecond()
    {
        FakePluckerFetcher fetcher;
        WildPalms::PalmSync::MockPalmDatabaseAccess device;
        PluckerBlobBackend src({}, &fetcher, &device,
                                QByteArray("SZLB"), QByteArray("VIEW"));

        MockBlobBackend dst;
        CollectionInfo c;
        c.id   = QStringLiteral("plucker:bootstrap");
        c.name = QStringLiteral("Plucker Bootstrap");
        c.type = QStringLiteral("plucker");
        dst.createCollection(c);

        BlobSyncEngine engine;

        // First run — device lacks Plucker DB.
        engine.mirror(&src, &dst, QStringLiteral("plucker:bootstrap"));
        const auto first = dst.loadRecords(QStringLiteral("plucker:bootstrap"));
        QCOMPARE(first.size(), 2);

        // Simulate Install action having run.
        device.createDatabase(QStringLiteral("Plucker"));

        // Second run — bootstrap collection becomes empty; mirror deletes
        // the now-absent records from dst.
        engine.mirror(&src, &dst, QStringLiteral("plucker:bootstrap"));
        const auto second = dst.loadRecords(QStringLiteral("plucker:bootstrap"));
        QCOMPARE(second.size(), 0);
    }

    void plugin_settingsRoundTripsLastFetched()
    {
        QJsonObject settings;
        QJsonArray channels;
        QJsonObject ch;
        ch[QStringLiteral("id")]            = QStringLiteral("c1");
        ch[QStringLiteral("name")]          = QStringLiteral("Alpha");
        ch[QStringLiteral("home_url")]      = QStringLiteral("https://example.com/");
        ch[QStringLiteral("last_fetched")]  = QStringLiteral("2026-04-26T10:30:00");
        channels.append(ch);
        settings[QStringLiteral("channels")] = channels;

        PluckerBackendPlugin plugin;
        plugin.loadSettings(settings);

        const QJsonObject out = plugin.saveSettings();
        const QJsonArray outChannels = out[QStringLiteral("channels")].toArray();
        QCOMPARE(outChannels.size(), 1);
        QCOMPARE(outChannels[0].toObject()[QStringLiteral("last_fetched")].toString(),
                  QStringLiteral("2026-04-26T10:30:00"));
    }
};

QTEST_MAIN(TestPluckerV2E2E)
#include "tst_plucker_v2_e2e.moc"
```

- [ ] **Step 2: Append e2e test target to `tests/plugins/plucker/CMakeLists.txt`**

```cmake

# --- Task 9: end-to-end ---
add_executable(tst_plucker_v2_e2e
    tst_plucker_v2_e2e.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerbackendplugin.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannelserializer.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckersettingswidget.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchanneleditor.cpp
    ${CMAKE_SOURCE_DIR}/src/palm/sync/mockpalmdatabaseaccess.cpp
)
target_compile_definitions(tst_plucker_v2_e2e
    PRIVATE
        PLUCKER_DATA_DIR="${PLUCKER_PLUGIN_SRC_DIR}"
)
target_include_directories(tst_plucker_v2_e2e
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_plucker_v2_e2e BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_plucker_v2_e2e
    PRIVATE
        WildPalmsCore
        Qt::Test
        Qt::Core
        Qt::Widgets
        KF6::CoreAddons
        Kalburator::Sync
)
add_test(NAME tst_plucker_v2_e2e COMMAND tst_plucker_v2_e2e)
```

- [ ] **Step 3: Build + run the test**

```bash
cmake --build build-dev --target tst_plucker_v2_e2e
ctest --test-dir build-dev -R '^tst_plucker_v2_e2e$' --output-on-failure
```
Expected: PASS (3 test functions).

- [ ] **Step 4: Run the entire WildPalms test suite to verify no regression**

```bash
ctest --test-dir build-dev --output-on-failure
```
Expected: all tests pass — the existing 67 from before E.14 plus the 5 new plucker test executables. If any unrelated tests fail, investigate before proceeding.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add tests/plugins/plucker
git commit -m "$(cat <<'EOF'
test(plucker): tst_plucker_v2_e2e end-to-end

Drives BlobSyncEngine::mirror from PluckerBlobBackend into
MockBlobBackend. Verifies due-channel record emission, non-due skip,
bootstrap-on/off transitions, and settings round-trip including
last_fetched persistence.

Phase E.14 Task 9.
EOF
)"
```

---

## Task 10: Verify legacy build (toggle OFF) and pose64 smoke

**Files:** none modified

**Goal:** Confirm the legacy `PluckerConduit` still builds + tests pass with `WILDPALMS_PLUCKER_PLUGIN_V2=OFF`. Smoke-test the V2 plugin under POSE64 (manual; defers full live-device coverage to E.18).

- [ ] **Step 1: Build legacy variant out-of-tree**

```bash
cmake -S /home/clinton/dev/WildPalms -B /tmp/build-plucker-legacy \
      -DCMAKE_BUILD_TYPE=Debug \
      -DWILDPALMS_PLUCKER_PLUGIN_V2=OFF
cmake --build /tmp/build-plucker-legacy --target wildpalms_plucker
```
Expected: clean build of the legacy `wildpalms_plucker` target. Legacy `pluckerview`, `pluckerchanneldialog`, `pluckerconduit` all compile against the lifted `pluckerchannel.h`.

- [ ] **Step 2: Run legacy plucker tests**

```bash
ctest --test-dir /tmp/build-plucker-legacy -R plucker --output-on-failure
```
Expected: existing `test_pluckerconfig` passes (unchanged).

- [ ] **Step 3: Manual smoke (optional, if a developer is at the keyboard)**

Launch WildPalms with the V2 plugin enabled:
```bash
build-dev/src/wildpalms
```
- Open settings → Plugins → enable Plucker.
- Add a channel pointing at `https://example.com` with maxDepth=1.
- Click "Apply" / "OK" to write settings JSON.
- Verify settings persist by closing and reopening the dialog — the channel reappears.

This is **not** a strict gate — POSE64 live integration lands in E.18.

- [ ] **Step 4: Clean up the temporary build dir**

```bash
rm -rf /tmp/build-plucker-legacy
```

- [ ] **Step 5: No commit needed for this task** — the work is verification.

---

## Task 11: Flip parent-spec row + memory entry

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (flip E.14 row)
- Create: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e14_plucker.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`

**Goal:** Update the parent spec's sub-phases table to mark E.14 done, and record the landing in auto-memory so future sessions know E.14 is in.

- [ ] **Step 1: Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`**

Replace the `| **E.14** | ...` row (currently at line 592) with:

```
| ✅ **E.14** | Plucker rewritten as `IBackendPlugin` (`PluckerBackendPlugin` + `PluckerBlobBackend` + `PluckerFetcher`). Two collections: `plucker:channels` (one record per due channel, blob = .pdb bytes from PyPlucker `Spider.py` subprocess) and `plucker:bootstrap` (SysZLib + viewer PRC bytes, gated on `IPalmDatabaseAccess::hasDatabase("Plucker")`). Source-only — runtime install drain via E.15's `IPluginAction`. Settings as JSON `channels[]` with all 25 fields + `last_fetched` ISO string per channel; per-channel scheduling persisted across runs (Plucker cadences are days/weeks, unlike WebCal). PluckerChannel struct + helpers lifted into shared `pluckerchannel.h` so V1 conduit and V2 plugin share scheduling logic. Settings widget is the channel-management UI (re-skin of legacy `PluckerView`); legacy `PluckerView`/`PluckerChannelDialog` stay with the legacy conduit. CMake toggle `WILDPALMS_PLUCKER_PLUGIN_V2=ON` (default ON); legacy `PluckerConduit` remains buildable. No libkalburator changes — Plucker DB is Palm-only. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e14-plucker-plugin.md`. | WP | E.13 | WP ctest passes; libkalburator ctest unchanged; ~17 tests across channel/fetcher/blob-backend/plugin/e2e. |
```

- [ ] **Step 2: Create memory file `project_phase_e14_plucker.md`**

```markdown
---
name: Phase E.14 — Plucker plugin landed
description: E.14 landed 2026-04-26; Plucker is sixth new-ABI plugin; source-only blob backend, install drain via E.15
type: project
---

E.14 landed 2026-04-26. Plucker is the sixth new-ABI `IBackendPlugin`
(after Memo, Calendar, ToDo, Contacts, WebCalendar). Source-only blob
backend with two collections — `plucker:channels` (one record per due
channel; .pdb bytes via PyPlucker `Spider.py` subprocess wrapped in
`PluckerFetcher`) and `plucker:bootstrap` (SysZLib + viewer PRC bytes
gated on `IPalmDatabaseAccess::hasDatabase("Plucker")`).

**Why:** Same deferral pattern as E.13 (WebCal) — runtime install
pairing waits for E.15 when `Install` becomes `IPluginAction`. E.14
e2e uses `MockBlobBackend` as install-drain target.

**How to apply:** When working on E.15 (Install → `IPluginAction`),
remember Plucker has two collections to drain (channels + bootstrap)
and the bootstrap drain must happen before channel drain (the .pdb
records need the viewer DB present). Also: legacy `PluckerConduit`
still compiles via `WILDPALMS_PLUCKER_PLUGIN_V2=OFF`; full removal is
E.16. PluckerChannel struct now lives in shared `pluckerchannel.h`
under `WildPalms::PluckerPlugin` namespace — both V1 and V2 link
against it.

Settings shape: `{"channels": [{...all 25 fields..., "last_fetched":
"ISO"}]}`. No migration from legacy INI; users reconfigure once.
```

- [ ] **Step 3: Append memory index entry to `MEMORY.md`**

Add this line after the existing `project_phase_e13_webcalendar.md` entry:

```
- [project_phase_e14_plucker.md](project_phase_e14_plucker.md) — E.14 landed 2026-04-26; Plucker is sixth new-ABI plugin; source-only blob backend, install drain via E.15
```

- [ ] **Step 4: Commit the parent-spec row + memory updates**

```bash
cd /home/clinton/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "docs(phase-e): mark E.14 (Plucker plugin) landed 2026-04-26"
```

The memory updates are stored in your `~/.claude` directory and persist across sessions; no commit needed there.

---

## Self-review checklist (run before reporting plan complete)

- [ ] Every task lists exact file paths under "Files".
- [ ] Every code step shows the actual code, not a placeholder.
- [ ] Every test step shows the assertion / `QCOMPARE` calls.
- [ ] Type names used in later tasks match earlier ones (`PluckerChannel`, `PluckerFetcher`, `PluckerBlobBackend`, `PluckerBackendPlugin`).
- [ ] Each task ends in a commit (or, for verification-only tasks, a justification why no commit).
- [ ] Submodule + parent-pointer dual commit pattern is consistent with E.13.
- [ ] Build-time toggle `WILDPALMS_PLUCKER_PLUGIN_V2` is referenced consistently across CMake, docs, and tests.
- [ ] `runAfter()` plugin IDs match real ones: `memo`, `calendar`, `todo` (singular), `contacts`, `webcalendar`.
- [ ] Tests cover spec requirements: channel scheduling, bootstrap gating, JSON round-trip, end-to-end mirror, KPluginFactory load.

---

**Total tasks:** 11
**Expected new test executables:** 5 (`tst_pluckerchannel`, `tst_pluckerfetcher`, `tst_pluckerblobbackend`, `tst_pluckerbackendplugin`, `tst_plucker_v2_e2e`)
**Expected pre-E.14 test count:** 67 → post-E.14: 67 + 5 = 72 (each test executable counts as one ctest entry).
