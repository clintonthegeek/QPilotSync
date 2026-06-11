# Sync Patchbay — Part 1 Implementation Plan (Phases 0–1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the static Sync Patchbay — a three-tier (Palm | Hub | Remotes) Graffodil-based graph with full mapping-edit parity — plus the Graffodil features it needs (edge labels, dash offset).

**Architecture:** A pure-data `PatchbayModel` (no QGraphicsView dependency) turns Profile rows + route statuses + provider state into node/port/wire/strand descriptions; `SyncPatchbayView` renders them via Graffodil (`GraphScene`, one generic `PatchNodeItem`, a `SignalPathWire` edge); `PatchbayPage` glues model ↔ Profile/PalmRuntime/AccountController and hosts the inspector. Spec: `docs/superpowers/specs/2026-06-11-sync-patchbay-design.md`.

**Tech Stack:** Qt6 Widgets / QGraphicsView, Graffodil::Core (sibling repo `~/dev/Graffodil`, extended directly), libkalburator types (`CollectionInfo`, `SyncMapping` JSON rows), QtTest.

**Scope note:** This plan covers spec Phases 0 and 1 only. Phase 2 (live animation, read-only guard) and Phase 3 (retire `src/app/mapping/`, dashboard strip) are a follow-up plan after this lands. Nothing in this plan deletes existing UI; the F.3 Settings page keeps working in parallel until Part 2.

**Repos:** Tasks 1–4 run in `/home/clinton/dev/Graffodil` (commit there). Tasks 5–16 run in `/home/clinton/dev/WildPalms`. Build commands:
- Graffodil: `cmake -S . -B build && cmake --build build -j8 && ctest --test-dir build -j8`
- WildPalms: `cmake --build build -j8 && ctest --test-dir build -j8` (configure once in Task 5)

---

## Phase 0 — Graffodil (repo: ~/dev/Graffodil)

### Task 1: CMake consumer guards

Graffodil's root CMakeLists unconditionally builds `demo/` and `tests/`. As an embedded
dependency (add_subdirectory / FetchContent) those must be skippable.

**Files:**
- Modify: `/home/clinton/dev/Graffodil/CMakeLists.txt`

- [ ] **Step 1: Guard demo and tests behind top-level check**

Replace the tail of the root `CMakeLists.txt` (the `add_subdirectory(demo)` /
`enable_testing()` / `add_subdirectory(tests)` lines) with:

```cmake
option(GRAFFODIL_BUILD_DEMO  "Build the demo app"   ${PROJECT_IS_TOP_LEVEL})
option(GRAFFODIL_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})

if(GRAFFODIL_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if(GRAFFODIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Verify standalone build still configures and tests still run**

Run: `cmake -S . -B build && cmake --build build -j8 && ctest --test-dir build -j8`
Expected: configure succeeds, all existing tests pass (suite was green at `d21b49f`).

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: gate demo and tests on PROJECT_IS_TOP_LEVEL for embedded consumers"
```

---

### Task 2: Edge labels (Phase 6c spec)

Implements `docs/specs/edge-labels.md` exactly as spec'd: `EdgeLabelStyle` value type +
`setLabel`/`label`/`setLabelStyle`/`labelStyle` on `GraphEdgeItem`, label as a lazily
created child item repositioned in `adjust()`.

**Files:**
- Modify: `/home/clinton/dev/Graffodil/src/core/include/graffodil/Types.h` (add `EdgeLabelStyle`)
- Modify: `/home/clinton/dev/Graffodil/src/core/include/graffodil/GraphEdgeItem.h`
- Modify: `/home/clinton/dev/Graffodil/src/core/src/GraphEdgeItem.cpp` (path may be `src/GraphEdgeItem.cpp` relative to `src/core/` — locate with `ls src/core`)
- Create: `/home/clinton/dev/Graffodil/tests/tst_edgelabels.cpp`
- Modify: `/home/clinton/dev/Graffodil/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/tst_edgelabels.cpp` (mirror include/style conventions of `tst_graphedgeitem.cpp`):

```cpp
#include <QTest>
#include <QGraphicsView>

#include <graffodil/GraphScene.h>
#include <graffodil/GraphEdgeItem.h>
#include <graffodil/EdgePathStrategy.h>
#include <graffodil/Types.h>

#include "TestHelpers.h"

using namespace Graffodil;
using Graffodil::Test::TestNode;

class TstEdgeLabels : public QObject {
    Q_OBJECT
private slots:
    void labelLazilyCreated();
    void labelPositionFollowsPath();
    void emptyStringRemovesLabel();
    void styleControlsPositionOnPath();
};

namespace {
GraphEdgeItem *makeEdge(GraphScene &scene, TestNode *&a, TestNode *&b)
{
    a = new TestNode(QStringLiteral("a"), QPointF(0, 0));
    b = new TestNode(QStringLiteral("b"), QPointF(400, 0));
    scene.addNode(a);
    scene.addNode(b);
    auto *edge = new GraphEdgeItem(a, QString(), b, QString(),
                                   std::make_unique<DirectPathStrategy>());
    scene.addEdge(edge);
    edge->adjust();
    return edge;
}
} // namespace

void TstEdgeLabels::labelLazilyCreated()
{
    GraphScene scene;
    TestNode *a, *b;
    auto *edge = makeEdge(scene, a, b);

    const int before = edge->graphicsItem()->childItems().size();
    QCOMPARE(edge->label(), QString());

    edge->setLabel(QStringLiteral("42"));
    QCOMPARE(edge->label(), QStringLiteral("42"));
    QCOMPARE(edge->graphicsItem()->childItems().size(), before + 1);
}

void TstEdgeLabels::labelPositionFollowsPath()
{
    GraphScene scene;
    TestNode *a, *b;
    auto *edge = makeEdge(scene, a, b);
    edge->setLabel(QStringLiteral("mid"));
    edge->adjust();

    auto *labelItem = edge->graphicsItem()->childItems().last();
    const QPointF expected =
        edge->path().pointAtPercent(0.5) + edge->labelStyle().offset;
    // label item's scene position should sit at path midpoint + offset
    // (within a couple px — the item centers its text on that point)
    QVERIFY((labelItem->scenePos() - expected).manhattanLength() < 30.0);
}

void TstEdgeLabels::emptyStringRemovesLabel()
{
    GraphScene scene;
    TestNode *a, *b;
    auto *edge = makeEdge(scene, a, b);
    const int before = edge->graphicsItem()->childItems().size();

    edge->setLabel(QStringLiteral("x"));
    edge->setLabel(QString());
    QCOMPARE(edge->label(), QString());
    QCOMPARE(edge->graphicsItem()->childItems().size(), before);
}

void TstEdgeLabels::styleControlsPositionOnPath()
{
    GraphScene scene;
    TestNode *a, *b;
    auto *edge = makeEdge(scene, a, b);
    edge->setLabel(QStringLiteral("q"));

    EdgeLabelStyle style;
    style.positionOnPath = 0.25;
    style.offset = QPointF(0, 0);
    edge->setLabelStyle(style);
    edge->adjust();

    auto *labelItem = edge->graphicsItem()->childItems().last();
    const QPointF expected = edge->path().pointAtPercent(0.25);
    QVERIFY((labelItem->scenePos() - expected).manhattanLength() < 30.0);
}

QTEST_MAIN(TstEdgeLabels)
#include "tst_edgelabels.moc"
```

Note: if existing tests do not use `QTEST_MAIN` in-source, mirror whatever
`tests/CMakeLists.txt` does for `tst_graphedgeitem` (some projects use a shared main).
Check first; copy the registration pattern exactly.

Register in `tests/CMakeLists.txt` (mirror the `tst_graphedgeitem` block):

```cmake
add_executable(tst_edgelabels tst_edgelabels.cpp)
target_link_libraries(tst_edgelabels PRIVATE Graffodil::Core Qt6::Test)
add_test(NAME tst_edgelabels COMMAND tst_edgelabels)
```

If other tests set `QT_QPA_PLATFORM=offscreen` via `set_tests_properties`, copy that too.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j8` — expected: COMPILE ERROR (`setLabel` does not exist). That is the failure signal for an API-extension test.

- [ ] **Step 3: Implement — Types.h**

Add to `Types.h` (after `LayoutResult`):

```cpp
/// Phase 6c: styling for an edge's midpoint label.
struct EdgeLabelStyle {
    QFont font;
    QColor color = QColor(60, 60, 60);
    QColor background = Qt::transparent;
    qreal backgroundPadding = 2.0;
    qreal positionOnPath = 0.5;      ///< 0..1 along path (source → target)
    QPointF offset = {0.0, -6.0};    ///< px offset from the path point
    bool rotateWithPath = false;     ///< align baseline to path tangent
};
```

(`#include <QFont>` if not already present.)

- [ ] **Step 4: Implement — GraphEdgeItem.h**

Add to the public section:

```cpp
    // --- Edge label (Phase 6c) ---
    void setLabel(const QString &text);   // empty string removes the label
    QString label() const { return m_labelText; }
    void setLabelStyle(const EdgeLabelStyle &style);
    EdgeLabelStyle labelStyle() const { return m_labelStyle; }
```

Add to the private section:

```cpp
    void updateLabelGeometry();   // called from adjust()
    QString m_labelText;
    EdgeLabelStyle m_labelStyle;
    QGraphicsItem *m_labelItem = nullptr;  // child item, lazily created
```

- [ ] **Step 5: Implement — GraphEdgeItem.cpp**

Add a small private child item class at file scope (anonymous namespace) and the methods:

```cpp
namespace {
class EdgeLabelItem : public QGraphicsItem {
public:
    explicit EdgeLabelItem(QGraphicsItem *parent) : QGraphicsItem(parent)
    {
        setFlag(QGraphicsItem::ItemIgnoresParentOpacity, false);
    }

    void setContent(const QString &text, const Graffodil::EdgeLabelStyle &style)
    {
        prepareGeometryChange();
        m_text = text;
        m_style = style;
        const QFontMetricsF fm(m_style.font);
        m_textRect = fm.boundingRect(m_text);
        update();
    }

    QRectF boundingRect() const override
    {
        const qreal pad = m_style.backgroundPadding;
        return m_textRect.adjusted(-pad, -pad, pad, pad)
            .translated(-m_textRect.center());
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const QRectF r = boundingRect();
        if (m_style.background.alpha() > 0) {
            p->setPen(Qt::NoPen);
            p->setBrush(m_style.background);
            p->drawRoundedRect(r, 3.0, 3.0);
        }
        p->setFont(m_style.font);
        p->setPen(m_style.color);
        p->drawText(r, Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    Graffodil::EdgeLabelStyle m_style;
    QRectF m_textRect;
};
} // namespace
```

```cpp
void GraphEdgeItem::setLabel(const QString &text)
{
    m_labelText = text;
    if (text.isEmpty()) {
        delete m_labelItem;
        m_labelItem = nullptr;
        return;
    }
    if (!m_labelItem)
        m_labelItem = new EdgeLabelItem(this);
    static_cast<EdgeLabelItem *>(m_labelItem)->setContent(m_labelText, m_labelStyle);
    updateLabelGeometry();
}

void GraphEdgeItem::setLabelStyle(const EdgeLabelStyle &style)
{
    m_labelStyle = style;
    if (m_labelItem) {
        static_cast<EdgeLabelItem *>(m_labelItem)->setContent(m_labelText, m_labelStyle);
        updateLabelGeometry();
    }
}

void GraphEdgeItem::updateLabelGeometry()
{
    if (!m_labelItem)
        return;
    const QPainterPath p = path();
    if (p.isEmpty()) {
        m_labelItem->setVisible(false);   // hidden until first adjust (spec)
        return;
    }
    m_labelItem->setVisible(true);
    const qreal t = qBound(0.0, m_labelStyle.positionOnPath, 1.0);
    // path() is in item coords; child item position is also item coords.
    m_labelItem->setPos(p.pointAtPercent(t) + m_labelStyle.offset);
    if (m_labelStyle.rotateWithPath) {
        qreal angle = -p.angleAtPercent(t);     // Qt angles are CCW-positive
        if (angle > 90.0 || angle < -90.0)      // flip in upside-down range
            angle += 180.0;
        m_labelItem->setRotation(angle);
    } else {
        m_labelItem->setRotation(0.0);
    }
}
```

At the end of `GraphEdgeItem::adjust()` (after path/caches are updated), add:

```cpp
    updateLabelGeometry();
```

- [ ] **Step 6: Run tests**

Run: `cmake --build build -j8 && ctest --test-dir build -j8`
Expected: `tst_edgelabels` PASSES; all pre-existing tests still pass.

- [ ] **Step 7: Update spec status and commit**

Mark 6c as implemented in `ROADMAP.md` (change its checkbox/status cell to done).

```bash
git add src/core tests CMakeLists.txt ROADMAP.md docs/specs/edge-labels.md
git commit -m "feat(core): edge midpoint labels (Phase 6c) — EdgeLabelStyle + GraphEdgeItem::setLabel"
```

---

### Task 3: Dash-offset hook on GraphEdgeItem

A manually steppable dash phase so consumers can drive marching-ants animation
(WildPalms drives it from a `QVariantAnimation`; tests step it directly).

**Files:**
- Modify: `/home/clinton/dev/Graffodil/src/core/include/graffodil/GraphEdgeItem.h`
- Modify: `GraphEdgeItem.cpp` (same file as Task 2)
- Modify: `/home/clinton/dev/Graffodil/tests/tst_edgelabels.cpp` → add cases here or create `tests/tst_dashoffset.cpp` (prefer the latter, mirroring Task 2 registration)

- [ ] **Step 1: Write the failing test** (`tests/tst_dashoffset.cpp`, same helper pattern as Task 2)

```cpp
#include <QTest>
#include <QImage>
#include <QPainter>

#include <graffodil/GraphScene.h>
#include <graffodil/GraphEdgeItem.h>
#include <graffodil/EdgePathStrategy.h>

#include "TestHelpers.h"

using namespace Graffodil;
using Graffodil::Test::TestNode;

class TstDashOffset : public QObject {
    Q_OBJECT
private slots:
    void roundTripAndRepaint();
};

void TstDashOffset::roundTripAndRepaint()
{
    GraphScene scene;
    auto *a = new TestNode(QStringLiteral("a"), QPointF(0, 0));
    auto *b = new TestNode(QStringLiteral("b"), QPointF(300, 0));
    scene.addNode(a);
    scene.addNode(b);
    auto *edge = new GraphEdgeItem(a, QString(), b, QString(),
                                   std::make_unique<DirectPathStrategy>());
    scene.addEdge(edge);
    edge->adjust();

    QPen pen(QColor(0, 0, 255), 3.0);
    pen.setDashPattern({4.0, 4.0});
    edge->setPen(pen);

    QCOMPARE(edge->dashOffset(), 0.0);
    edge->setDashOffset(6.5);
    QCOMPARE(edge->dashOffset(), 6.5);

    // Render twice with different offsets — images must differ (the dash
    // pattern visibly shifted), proving the offset reaches the painter.
    auto render = [&scene]() {
        QImage img(420, 80, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        scene.render(&p, img.rect(), QRectF(-10, -40, 420, 80));
        return img;
    };
    edge->setDashOffset(0.0);
    const QImage at0 = render();
    edge->setDashOffset(4.0);
    const QImage at4 = render();
    QVERIFY(at0 != at4);
}

QTEST_MAIN(TstDashOffset)
#include "tst_dashoffset.moc"
```

Register in `tests/CMakeLists.txt` exactly like `tst_edgelabels`.

- [ ] **Step 2: Verify it fails to compile** (`dashOffset` undefined)

Run: `cmake --build build -j8` — expected: compile error.

- [ ] **Step 3: Implement**

`GraphEdgeItem.h`, public section:

```cpp
    // --- Dash phase (consumer-driven marching animation) ---
    void setDashOffset(qreal offset);
    qreal dashOffset() const { return m_dashOffset; }
```

private section: `qreal m_dashOffset = 0.0;`

`GraphEdgeItem.cpp`:

```cpp
void GraphEdgeItem::setDashOffset(qreal offset)
{
    if (qFuzzyCompare(m_dashOffset, offset))
        return;
    m_dashOffset = offset;
    update();
}
```

In `paint()`, where the stroke pen is finalized just before drawing the path, apply:

```cpp
    if (!penToUse.dashPattern().isEmpty())
        penToUse.setDashOffset(m_dashOffset);
```

(Adapt the variable name to whatever local `QPen` `paint()` actually builds — it
constructs one to apply highlight/gradient; set the dash offset on that same pen.)

- [ ] **Step 4: Run tests**

Run: `cmake --build build -j8 && ctest --test-dir build -j8`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/core tests
git commit -m "feat(core): GraphEdgeItem::setDashOffset — consumer-steppable dash phase for edge animation"
```

---

### Task 4: Version bump, tag, push

**Files:**
- Modify: `/home/clinton/dev/Graffodil/CMakeLists.txt` (`project(Graffodil VERSION 0.1.0 …)` → `0.2.0`)

- [ ] **Step 1: Bump version**

Change `project(Graffodil VERSION 0.1.0 LANGUAGES CXX)` to `VERSION 0.2.0`.

- [ ] **Step 2: Full suite one last time**

Run: `cmake -S . -B build && cmake --build build -j8 && ctest --test-dir build -j8`
Expected: all pass.

- [ ] **Step 3: Commit, tag, push**

```bash
git add CMakeLists.txt
git commit -m "release: v0.2.0 — edge labels (6c), dash offset, embedded-consumer CMake guards"
git tag v0.2.0
git push origin main --tags
```

The push is required: WildPalms's FetchContent fallback (Task 5) fetches `v0.2.0` from
Codeberg. If `git remote -v` shows no `origin`, stop and ask the user (dev-root CLAUDE.md
lists Graffodil as a Codeberg repo, so a remote should exist).

---

## Phase 1 — WildPalms (repo: ~/dev/WildPalms)

### Task 5: Graffodil consumption + WildPalmsAppPatchbay skeleton

**Files:**
- Modify: `/home/clinton/dev/WildPalms/CMakeLists.txt` (after the libkalburator block, ~line 78)
- Modify: `/home/clinton/dev/WildPalms/src/CMakeLists.txt` (next to `add_subdirectory(app/mapping)`, ~line 309)
- Create: `/home/clinton/dev/WildPalms/src/app/patchbay/CMakeLists.txt`
- Create: `/home/clinton/dev/WildPalms/src/app/patchbay/patchbaytypes.h`

- [ ] **Step 1: Root CMake — Graffodil block** (mirror of the libkalburator block directly below it)

```cmake
# Graffodil — graph rendering library (Sync Patchbay). Same consumption
# pattern as libkalburator: sibling-dir override for development, pinned
# tag via FetchContent otherwise. See docs/superpowers/specs/
# 2026-06-11-sync-patchbay-design.md §9.
set(WILDPALMS_GRAFFODIL_SOURCE_DIR "" CACHE PATH
    "Optional path to a local Graffodil checkout (override FetchContent)")
set(WILDPALMS_GRAFFODIL_GIT_TAG "v0.2.0" CACHE STRING
    "Graffodil tag to fetch when WILDPALMS_GRAFFODIL_SOURCE_DIR is unset")
set(GRAFFODIL_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(GRAFFODIL_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(WILDPALMS_GRAFFODIL_SOURCE_DIR)
    message(STATUS "Graffodil: using local source at ${WILDPALMS_GRAFFODIL_SOURCE_DIR}")
    add_subdirectory(${WILDPALMS_GRAFFODIL_SOURCE_DIR} graffodil EXCLUDE_FROM_ALL)
else()
    message(STATUS "Graffodil: fetching ${WILDPALMS_GRAFFODIL_GIT_TAG} from Codeberg")
    include(FetchContent)
    FetchContent_Declare(graffodil
        GIT_REPOSITORY https://codeberg.org/clintonthegeek/Graffodil.git
        GIT_TAG        ${WILDPALMS_GRAFFODIL_GIT_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(graffodil)
endif()
```

Note: Graffodil requires Qt 6.8 while WP requires 6.2 — both resolve against the system
Qt; if configure fails on the Qt6 version, lower Graffodil's `find_package(Qt6 6.8 …)` to
the actual system version in a Graffodil commit (check `qmake6 -v`), don't fork it WP-side.

- [ ] **Step 2: `src/app/patchbay/CMakeLists.txt`** (mirror of `src/app/mapping/CMakeLists.txt`, same synctypes.h isolation rationale)

```cmake
# WildPalmsAppPatchbay — Sync Patchbay (three-tier graph centerpiece).
# Separate static lib for the same reason as WildPalmsAppMapping: code here
# uses Kalburator::Sync types from libkalburator's synctypes.h, so it needs
# a TU whose include path does NOT have src/core/ on it.
add_library(WildPalmsAppPatchbay STATIC
    patchbaytypes.h
    patchbaymodel.h
    patchbaymodel.cpp
    patchnodeitem.h
    patchnodeitem.cpp
    signalpathwire.h
    signalpathwire.cpp
    syncpatchbayview.h
    syncpatchbayview.cpp
    patchbayinspector.h
    patchbayinspector.cpp
    patchbaypage.h
    patchbaypage.cpp
)

target_include_directories(WildPalmsAppPatchbay
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        # PatchbayModel/Page include runtime/routemapping.h, profile.h,
        # runtime/accountcontroller.h, runtime/palmruntime.h via src/.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(WildPalmsAppPatchbay
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        Graffodil::Core
    PRIVATE
        PalmDeviceAccessLib
)

set_target_properties(WildPalmsAppPatchbay PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

Until later tasks create them, comment out every source file except `patchbaytypes.h`
plus a placeholder `patchbaymodel.h/.cpp` added in Task 6 — or simply add the files to
this list as the tasks create them. Add the subdirectory in `src/CMakeLists.txt`:

```cmake
add_subdirectory(app/patchbay)
```

and add `WildPalmsAppPatchbay` to `WildPalmsCore`'s PRIVATE `target_link_libraries`
list right after `WildPalmsAppMapping`.

- [ ] **Step 3: `patchbaytypes.h`** — the model's value vocabulary (complete file)

```cpp
// src/app/patchbay/patchbaytypes.h
#pragma once

#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <functional>

#include <collectioninfo.h>

namespace WildPalms::AppPatchbay {

/// Route-domain colors (spec §14). Domain hue identity is fixed.
inline QColor domainColor(const QString &domain)
{
    if (domain == QLatin1String("calendar")) return QColor(0x5b, 0x8d, 0xd9);
    if (domain == QLatin1String("contacts")) return QColor(0x5f, 0xb8, 0x78);
    if (domain == QLatin1String("note"))     return QColor(0xd9, 0xa4, 0x5b);
    if (domain == QLatin1String("todo"))     return QColor(0xb0, 0x7f, 0xd4);
    return QColor(0x8a, 0x93, 0xa3);
}

/// Value-copied conduit descriptor facts (decouples the model from
/// PimPlugin headers; tests construct these directly).
struct ConduitFacts {
    QString conduitId;     ///< persisted as sourceBackend ("calendar", "memo", …)
    QString domain;        ///< route domain ("calendar", "note", …) — PimPlugin::domain().toString()
    QString dbName;        ///< primaryDbName ("DatebookDB", …)
    QString displayName;   ///< conduitDisplayName()
    std::function<bool(const Kalburator::Sync::CollectionInfo &)> matchesCollection;
};

enum class PortKind { WholeDomain, Category, PalmSlot, RemoteCollection, AddCategory };

struct PortDesc {
    QString id;            ///< side-less id, see grammar below
    QString label;
    QString domain;
    PortKind kind = PortKind::RemoteCollection;
    bool waiting = false;     ///< category port: RouteStatus::WaitingForDevice
    bool noFreeSlot = false;  ///< category port: RouteStatus::NoFreeSlot
};

/// Port id grammar (anchor ids add "@l"/"@r" suffix at the item layer):
///   hub whole-domain:  "dom:<domain>"
///   hub category:      "cat:<domain>/<categoryName>"
///   hub add-category:  "add:<domain>"
///   palm slot:         "slot:<dbName>/<index>"
///   palm whole-DB:     "db:<dbName>"
///   remote collection: "col:<collectionId>|<domain>"

struct BandDesc {
    QString domain;        ///< empty for remote nodes' single band
    QString title;
    QString footer;        ///< e.g. "first sync pending"; empty allowed
    QList<PortDesc> ports;
};

enum class NodeKind { Palm, Hub, Remote, GhostRemote };

struct NodeDesc {
    QString id;            ///< "palm" | "hub" | "remote:<providerId>" | "ghost:<providerId>"
    NodeKind kind = NodeKind::Remote;
    QString title;
    QString subtitle;      ///< connection state / busy text / last sync
    bool ghosted = false;
    QList<BandDesc> bands;
};

enum class WireState { TwoWay, OneWayUpload, OneWayDownload, Disabled, Broken };

struct WireDesc {
    QString mappingId;
    QString sourcePortId;  ///< hub port ("dom:…" or "cat:…")
    QString targetNodeId;  ///< "remote:<providerId>" or "ghost:<providerId>"
    QString targetPortId;  ///< "col:<collectionId>|<domain>"
    QString domain;
    WireState state = WireState::TwoWay;
    QString beadGlyph;     ///< "✗" for Broken in Part 1; run history arrives in Part 2
};

enum class StrandState { Solid, Ghost };

struct StrandDesc {
    QString id;            ///< "strand:<hubPortId>"
    QString palmPortId;    ///< "slot:<db>/<i>", or "db:<db>" for ghost/whole-domain
    QString hubPortId;
    QString domain;
    StrandState state = StrandState::Solid;
    bool wholeDomain = false;
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 4: Configure + build green**

Run:
```bash
cmake -S . -B build -DWILDPALMS_GRAFFODIL_SOURCE_DIR=$HOME/dev/Graffodil
cmake --build build -j8
```
Expected: configures and builds. (Using the sibling override keeps the loop fast; the
FetchContent path gets exercised implicitly on any clean configure without the override.)

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt src/app/patchbay
git commit -m "build: consume Graffodil v0.2.0 (sibling override + FetchContent); WildPalmsAppPatchbay skeleton"
```

---

### Task 6: PatchbayModel — palm + hub node construction

**Files:**
- Create: `/home/clinton/dev/WildPalms/src/app/patchbay/patchbaymodel.h`
- Create: `/home/clinton/dev/WildPalms/src/app/patchbay/patchbaymodel.cpp`
- Create: `/home/clinton/dev/WildPalms/tests/runtime/tst_patchbay_model.cpp`
- Modify: `/home/clinton/dev/WildPalms/tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Model header** (complete — later tasks fill the .cpp; declared API is final)

```cpp
// src/app/patchbay/patchbaymodel.h
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include "patchbaytypes.h"
#include "runtime/routemapping.h"   // RouteStatus

namespace WildPalms::AppPatchbay {

/// Pure data layer of the Sync Patchbay (spec §4): turns Profile rows +
/// route statuses + provider state into node/port/wire/strand descriptions.
/// No QGraphicsView dependency; fully unit-testable.
class PatchbayModel : public QObject {
    Q_OBJECT
public:
    struct ProviderEntry {
        QString providerId;
        QString displayName;
        QString busyText;   ///< empty → connected (F.3 convention)
        QList<Kalburator::Sync::CollectionInfo> collections;
    };

    struct Inputs {
        QJsonArray mappings;                            ///< Profile::syncMappingsJson()
        QList<ConduitFacts> conduits;
        QHash<QString, QStringList> slotSnapshot;       ///< dbName → 16 names
        QHash<QString, QStringList> desiredCategories;  ///< dbName → names
        QList<ProviderEntry> providers;
        QHash<QString, WildPalms::Runtime::RouteStatus> routeStatuses;
        bool deviceConnected = false;
        QString deviceName;
    };

    explicit PatchbayModel(QObject *parent = nullptr);

    void setInputs(const Inputs &inputs);   ///< rebuilds everything, emits rebuilt()

    QList<NodeDesc> nodes() const { return m_nodes; }
    QList<WireDesc> wires() const { return m_wires; }
    QList<StrandDesc> strands() const { return m_strands; }
    QJsonArray mappings() const { return m_inputs.mappings; }

    // ── edit operations (Task 9) ──────────────────────────────────────
    /// Returns the new mapping id, or empty on validation failure
    /// (unknown domain/conduit, incompatible collection, duplicate).
    QString addMapping(const QString &hubPortId, const QString &providerId,
                       const QString &collectionId);
    bool removeMapping(const QString &mappingId);
    /// Merge `changes` into the row (mode/conflictPolicy/enabled edits).
    bool updateMapping(const QString &mappingId, const QJsonObject &changes);
    bool addCategory(const QString &domain, const QString &name);
    /// Fails if any mapping row still references the category.
    bool removeCategory(const QString &domain, const QString &name);

    // lookup helpers (inspector / view)
    QJsonObject mappingById(const QString &mappingId) const;
    WildPalms::Runtime::RouteStatus statusFor(const QString &mappingId) const;

signals:
    void rebuilt();
    void mappingsChanged(const QJsonArray &mappings);
    /// dbName + full new desired list (write through to Profile).
    void desiredCategoriesChanged(const QString &dbName, const QStringList &names);

private:
    void rebuild();
    const ConduitFacts *conduitForDomain(const QString &domain) const;
    const ConduitFacts *conduitForId(const QString &conduitId) const;
    /// Category names for a hub band: desiredCategories ∪ names parsed from
    /// rows, original order, "Unfiled" excluded, case-insensitive dedup.
    QStringList hubCategoryNames(const ConduitFacts &c) const;

    Inputs m_inputs;
    QList<NodeDesc> m_nodes;
    QList<WireDesc> m_wires;
    QList<StrandDesc> m_strands;
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 2: Failing tests for node construction** (`tests/runtime/tst_patchbay_model.cpp`)

```cpp
// tests/runtime/tst_patchbay_model.cpp
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "../../src/app/patchbay/patchbaymodel.h"
#include "../wildpalms_qtest_main.h"

#include <collectioninfo.h>

using namespace WildPalms::AppPatchbay;
using Kalburator::Sync::CollectionInfo;
using WildPalms::Runtime::RouteStatus;

namespace {

QList<ConduitFacts> stockConduits()
{
    auto typeIs = [](const char *t) {
        return [t](const CollectionInfo &c) { return c.type == QLatin1String(t); };
    };
    return {
        { "calendar", "calendar", "DatebookDB", "Calendar", typeIs("calendar") },
        { "contacts", "contacts", "AddressDB",  "Contacts", typeIs("contacts") },
        { "memo",     "note",     "MemoDB",     "Memos",    typeIs("notes")    },
        { "todo",     "todo",     "ToDoDB",     "Todos",    typeIs("todo")     },
    };
}

QStringList snapshot16(std::initializer_list<const char *> names)
{
    QStringList l;
    for (const char *n : names) l << QString::fromUtf8(n);
    while (l.size() < 16) l << QString();
    return l;
}

QJsonObject row(const char *id, const char *srcBackend, const char *srcCal,
                const char *tgtBackend, const char *tgtCal,
                const char *mode = "TwoWay", bool enabled = true)
{
    QJsonObject r;
    r["id"] = id;
    r["sourceBackend"] = srcBackend;
    r["sourceCalendar"] = srcCal;
    r["targetBackend"] = tgtBackend;
    r["targetCalendar"] = tgtCal;
    r["mode"] = mode;
    r["conflictPolicy"] = "LastWriteWins";
    r["enabled"] = enabled;
    return r;
}

PatchbayModel::Inputs baseInputs()
{
    PatchbayModel::Inputs in;
    in.conduits = stockConduits();
    in.slotSnapshot = {
        { "DatebookDB", snapshot16({"Unfiled", "Work", "Personal"}) },
        { "AddressDB",  snapshot16({"Unfiled"}) },
    };
    in.desiredCategories = { { "DatebookDB", {"Work", "Personal"} } };
    CollectionInfo cal;
    cal.id = "cal1"; cal.displayName = "Team"; cal.type = "calendar";
    PatchbayModel::ProviderEntry nc{ "acc-1", "Nextcloud", QString(), { cal } };
    in.providers = { nc };
    in.deviceConnected = true;
    in.deviceName = "Palm m515";
    return in;
}

const NodeDesc *nodeById(const QList<NodeDesc> &nodes, const QString &id)
{
    for (const auto &n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const BandDesc *bandByDomain(const NodeDesc &n, const QString &domain)
{
    for (const auto &b : n.bands)
        if (b.domain == domain) return &b;
    return nullptr;
}

} // namespace

class TstPatchbayModel : public QObject {
    Q_OBJECT
private slots:
    // Task 6
    void hubHasBandPerConduit();
    void hubBandPortsOrderedWholeThenCategoriesThenAdd();
    void hubCategoriesUnionDesiredAndRows();
    void palmNodeBandsAndSlots();
    void disconnectedDeviceGhostsPalmNode();
    // Task 7 (slots added there)
    // Task 8 (slots added there)
    // Task 9 (slots added there)
};

void TstPatchbayModel::hubHasBandPerConduit()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *hub = nodeById(m.nodes(), "hub");
    QVERIFY(hub);
    QCOMPARE(hub->kind, NodeKind::Hub);
    QCOMPARE(hub->bands.size(), 4);          // descriptor-driven
    QVERIFY(bandByDomain(*hub, "calendar"));
    QVERIFY(bandByDomain(*hub, "note"));     // memo's domain is "note"
}

void TstPatchbayModel::hubBandPortsOrderedWholeThenCategoriesThenAdd()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    QVERIFY(band);
    QVERIFY(band->ports.size() >= 4);        // All + Work + Personal + add
    QCOMPARE(band->ports.first().kind, PortKind::WholeDomain);
    QCOMPARE(band->ports.first().id, QStringLiteral("dom:calendar"));
    QCOMPARE(band->ports[1].id, QStringLiteral("cat:calendar/Work"));
    QCOMPARE(band->ports.last().kind, PortKind::AddCategory);
}

void TstPatchbayModel::hubCategoriesUnionDesiredAndRows()
{
    auto in = baseInputs();
    // a row referencing a category NOT in desiredCategories must still get a port
    in.mappings.append(row("m1", "calendar", "palm:calendar/name:Conferences",
                           "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    bool found = false;
    for (const auto &p : band->ports)
        if (p.id == QStringLiteral("cat:calendar/Conferences")) found = true;
    QVERIFY(found);
}

void TstPatchbayModel::palmNodeBandsAndSlots()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *palm = nodeById(m.nodes(), "palm");
    QVERIFY(palm);
    QCOMPARE(palm->kind, NodeKind::Palm);
    QVERIFY(!palm->ghosted);
    QCOMPARE(palm->title, QStringLiteral("Palm m515"));
    const auto *db = bandByDomain(*palm, "calendar");
    QVERIFY(db);
    // ports: slot 0 Unfiled, slot 1 Work, slot 2 Personal (empty slots skipped)
    QCOMPARE(db->ports.size(), 3);
    QCOMPARE(db->ports[1].id, QStringLiteral("slot:DatebookDB/1"));
    QCOMPARE(db->ports[1].label, QStringLiteral("Work"));
}

void TstPatchbayModel::disconnectedDeviceGhostsPalmNode()
{
    auto in = baseInputs();
    in.deviceConnected = false;
    PatchbayModel m;
    m.setInputs(in);
    const auto *palm = nodeById(m.nodes(), "palm");
    QVERIFY(palm);
    QVERIFY(palm->ghosted);                  // node never vanishes (spec §5.1)
    QVERIFY(!palm->bands.isEmpty());         // snapshot still rendered
}

WILDPALMS_QTEST_MAIN(TstPatchbayModel)
#include "tst_patchbay_model.moc"
```

Note: check how `tst_syncmappingsgraphview.cpp` invokes its main — it includes
`../wildpalms_qtest_main.h`. Open that header and use the exact same macro/pattern
(if it is `QTEST_MAIN` via wrapper, mirror it; adjust the last two lines accordingly).

Register in `tests/runtime/CMakeLists.txt` (mirror the `tst_syncmappingsgraphview`
block, adding the patchbay lib):

```cmake
add_executable(tst_patchbay_model tst_patchbay_model.cpp)
target_link_libraries(tst_patchbay_model
    PRIVATE
        Qt::Core
        Qt::Test
        KF6::CoreAddons
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsAppPatchbay
)
add_test(NAME tst_patchbay_model COMMAND tst_patchbay_model)
set_tests_properties(tst_patchbay_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Run to verify failure** — `cmake --build build -j8` fails to link/compile (PatchbayModel not implemented).

- [ ] **Step 4: Implement node construction** (`patchbaymodel.cpp`, first slice)

```cpp
// src/app/patchbay/patchbaymodel.cpp
#include "patchbaymodel.h"

#include <QJsonObject>
#include <QUuid>

using Kalburator::Sync::CollectionInfo;
using WildPalms::Runtime::RouteStatus;

namespace WildPalms::AppPatchbay {

namespace {
QString categoryFromSourceCalendar(const QString &sourceCalendar,
                                   const QString &domain)
{
    const QString prefix =
        QStringLiteral("palm:%1/name:").arg(domain);
    if (sourceCalendar.startsWith(prefix))
        return sourceCalendar.mid(prefix.size());
    return QString();
}
} // namespace

PatchbayModel::PatchbayModel(QObject *parent) : QObject(parent) {}

void PatchbayModel::setInputs(const Inputs &inputs)
{
    m_inputs = inputs;
    rebuild();
}

const ConduitFacts *PatchbayModel::conduitForDomain(const QString &domain) const
{
    for (const auto &c : m_inputs.conduits)
        if (c.domain == domain) return &c;
    return nullptr;
}

const ConduitFacts *PatchbayModel::conduitForId(const QString &conduitId) const
{
    for (const auto &c : m_inputs.conduits)
        if (c.conduitId == conduitId) return &c;
    return nullptr;
}

QStringList PatchbayModel::hubCategoryNames(const ConduitFacts &c) const
{
    QStringList names = m_inputs.desiredCategories.value(c.dbName);
    auto containsCi = [&names](const QString &n) {
        for (const auto &x : names)
            if (x.compare(n, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("sourceBackend")).toString() != c.conduitId)
            continue;
        const QString cat = categoryFromSourceCalendar(
            r.value(QLatin1String("sourceCalendar")).toString(), c.domain);
        if (!cat.isEmpty() && !containsCi(cat)
            && cat.compare(QLatin1String("Unfiled"), Qt::CaseInsensitive) != 0)
            names << cat;
    }
    names.removeAll(QString());
    return names;
}

void PatchbayModel::rebuild()
{
    m_nodes.clear();
    m_wires.clear();
    m_strands.clear();

    // ── Palm node ────────────────────────────────────────────────────
    NodeDesc palm;
    palm.id = QStringLiteral("palm");
    palm.kind = NodeKind::Palm;
    palm.title = m_inputs.deviceName.isEmpty()
        ? QStringLiteral("Palm device") : m_inputs.deviceName;
    palm.subtitle = m_inputs.deviceConnected
        ? QStringLiteral("connected") : QStringLiteral("disconnected");
    palm.ghosted = !m_inputs.deviceConnected;
    for (const auto &c : m_inputs.conduits) {
        BandDesc band;
        band.domain = c.domain;
        band.title = QStringLiteral("%1 — %2").arg(c.displayName, c.dbName);
        const QStringList snap = m_inputs.slotSnapshot.value(c.dbName);
        for (int i = 0; i < snap.size(); ++i) {
            if (snap[i].isEmpty())
                continue;
            PortDesc p;
            p.id = QStringLiteral("slot:%1/%2").arg(c.dbName).arg(i);
            p.label = snap[i];
            p.domain = c.domain;
            p.kind = PortKind::PalmSlot;
            band.ports << p;
        }
        palm.bands << band;
    }
    m_nodes << palm;

    // ── Hub node ─────────────────────────────────────────────────────
    NodeDesc hub;
    hub.id = QStringLiteral("hub");
    hub.kind = NodeKind::Hub;
    hub.title = QStringLiteral("HUB");
    for (const auto &c : m_inputs.conduits) {
        BandDesc band;
        band.domain = c.domain;
        band.title = c.displayName;
        PortDesc whole;
        whole.id = QStringLiteral("dom:%1").arg(c.domain);
        whole.label = QStringLiteral("All %1").arg(c.displayName.toLower());
        whole.domain = c.domain;
        whole.kind = PortKind::WholeDomain;
        band.ports << whole;
        for (const QString &cat : hubCategoryNames(c)) {
            PortDesc p;
            p.id = QStringLiteral("cat:%1/%2").arg(c.domain, cat);
            p.label = cat;
            p.domain = c.domain;
            p.kind = PortKind::Category;
            band.ports << p;
        }
        PortDesc add;
        add.id = QStringLiteral("add:%1").arg(c.domain);
        add.label = QStringLiteral("+ category…");
        add.domain = c.domain;
        add.kind = PortKind::AddCategory;
        band.ports << add;
        hub.bands << band;
    }
    m_nodes << hub;

    rebuildRemotes();   // Task 7 (no-op stub until then: define it empty)
    rebuildWiresAndStrands();   // Task 8 (empty stub until then)

    emit rebuilt();
}

// Stubs completed in Tasks 7 and 8:
void PatchbayModel::rebuildRemotes() {}
void PatchbayModel::rebuildWiresAndStrands() {}

QJsonObject PatchbayModel::mappingById(const QString &mappingId) const
{
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("id")).toString() == mappingId)
            return r;
    }
    return {};
}

WildPalms::Runtime::RouteStatus
PatchbayModel::statusFor(const QString &mappingId) const
{
    return m_inputs.routeStatuses.value(mappingId, RouteStatus::Active);
}

} // namespace WildPalms::AppPatchbay
```

Add the two private stub declarations to the header's private section:

```cpp
    void rebuildRemotes();
    void rebuildWiresAndStrands();
```

(Edit-op methods `addMapping`/`removeMapping`/`updateMapping`/`addCategory`/
`removeCategory` get bodies in Task 9; until then give them stub bodies returning
`{}`/`false` so the lib links.)

- [ ] **Step 5: Run tests** — `cmake --build build -j8 && ctest --test-dir build -R tst_patchbay_model -j8`
Expected: the five Task-6 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): PatchbayModel — palm + hub node construction (descriptor-driven bands, names-first category union)"
```

---

### Task 7: PatchbayModel — remote nodes + ghost remotes

**Files:**
- Modify: `src/app/patchbay/patchbaymodel.cpp` (fill `rebuildRemotes()`)
- Modify: `tests/runtime/tst_patchbay_model.cpp`

- [ ] **Step 1: Failing tests** (add slots to the test class + implementations)

```cpp
    void remoteNodePerProvider();
    void portPerCollectionDomainPairing();
    void busyProviderShowsSubtitleAndNoPorts();
    void missingAccountSynthesizesGhostNode();
```

```cpp
void TstPatchbayModel::remoteNodePerProvider()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *nc = nodeById(m.nodes(), "remote:acc-1");
    QVERIFY(nc);
    QCOMPARE(nc->kind, NodeKind::Remote);
    QCOMPARE(nc->title, QStringLiteral("Nextcloud"));
    QCOMPARE(nc->bands.size(), 1);
    QCOMPARE(nc->bands.first().ports.size(), 1);
    QCOMPARE(nc->bands.first().ports.first().id,
             QStringLiteral("col:cal1|calendar"));
}

void TstPatchbayModel::portPerCollectionDomainPairing()
{
    auto in = baseInputs();
    // a mixed VEVENT+VTODO collection matches BOTH calendar and todo conduits
    CollectionInfo mixed;
    mixed.id = "cal2"; mixed.displayName = "Mixed"; mixed.type = "calendar";
    in.conduits[0].matchesCollection =
        [](const CollectionInfo &c) { return c.type == "calendar"; };
    in.conduits[3].matchesCollection =
        [](const CollectionInfo &c) { return c.id == "cal2"; };   // VTODO-capable
    in.providers[0].collections << mixed;
    PatchbayModel m;
    m.setInputs(in);
    const auto *nc = nodeById(m.nodes(), "remote:acc-1");
    QStringList ids;
    for (const auto &p : nc->bands.first().ports) ids << p.id;
    QVERIFY(ids.contains("col:cal2|calendar"));
    QVERIFY(ids.contains("col:cal2|todo"));     // spec §5.3
}

void TstPatchbayModel::busyProviderShowsSubtitleAndNoPorts()
{
    auto in = baseInputs();
    in.providers[0].busyText = QStringLiteral("Connecting…");
    PatchbayModel m;
    m.setInputs(in);
    const auto *nc = nodeById(m.nodes(), "remote:acc-1");
    QCOMPARE(nc->subtitle, QStringLiteral("Connecting…"));
    QVERIFY(nc->bands.first().ports.isEmpty());
}

void TstPatchbayModel::missingAccountSynthesizesGhostNode()
{
    auto in = baseInputs();
    in.mappings.append(row("m9", "calendar", "", "gone-uuid:calX", "calX"));
    PatchbayModel m;
    m.setInputs(in);
    const auto *ghost = nodeById(m.nodes(), "ghost:gone-uuid");
    QVERIFY(ghost);                              // never silently drop (spec §10)
    QCOMPARE(ghost->kind, NodeKind::GhostRemote);
    QVERIFY(ghost->ghosted);
    QCOMPARE(ghost->bands.first().ports.first().id,
             QStringLiteral("col:calX|calendar"));
}
```

- [ ] **Step 2: Verify failure** — build + run; the four new tests fail (no remote nodes yet).

- [ ] **Step 3: Implement `rebuildRemotes()`**

```cpp
void PatchbayModel::rebuildRemotes()
{
    // Real providers
    for (const auto &prov : m_inputs.providers) {
        NodeDesc n;
        n.id = QStringLiteral("remote:%1").arg(prov.providerId);
        n.kind = NodeKind::Remote;
        n.title = prov.displayName;
        n.subtitle = prov.busyText;
        BandDesc band;
        if (prov.busyText.isEmpty()) {
            for (const auto &col : prov.collections) {
                for (const auto &c : m_inputs.conduits) {
                    if (!c.matchesCollection || !c.matchesCollection(col))
                        continue;
                    PortDesc p;
                    p.id = QStringLiteral("col:%1|%2").arg(col.id, c.domain);
                    p.label = col.displayName;
                    p.domain = c.domain;
                    p.kind = PortKind::RemoteCollection;
                    band.ports << p;
                }
            }
        }
        n.bands << band;
        m_nodes << n;
    }

    // Ghost remotes: rows whose targetBackend references an unknown provider
    // or a provider that lacks the collection. targetBackend is
    // "<providerId>:<collectionId>"; providerId never contains ':' (it is an
    // account uuid), so split on the FIRST ':' only.
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        const QString target = r.value(QLatin1String("targetBackend")).toString();
        const int colon = target.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString providerId = target.left(colon);
        const QString collectionId = target.mid(colon + 1);
        const ConduitFacts *c =
            conduitForId(r.value(QLatin1String("sourceBackend")).toString());
        if (!c)
            continue;

        bool resolved = false;
        for (const auto &prov : m_inputs.providers) {
            if (prov.providerId != providerId)
                continue;
            if (!prov.busyText.isEmpty()) { resolved = true; break; } // pending, not broken
            for (const auto &col : prov.collections)
                if (col.id == collectionId) { resolved = true; break; }
            break;
        }
        if (resolved)
            continue;

        const QString ghostId = QStringLiteral("ghost:%1").arg(providerId);
        NodeDesc *ghost = nullptr;
        for (auto &n : m_nodes)
            if (n.id == ghostId) ghost = &n;
        if (!ghost) {
            NodeDesc n;
            n.id = ghostId;
            n.kind = NodeKind::GhostRemote;
            n.title = QStringLiteral("Missing account");
            n.subtitle = providerId;
            n.ghosted = true;
            n.bands << BandDesc{};
            m_nodes << n;
            ghost = &m_nodes.last();
        }
        const QString portId = QStringLiteral("col:%1|%2").arg(collectionId, c->domain);
        bool havePort = false;
        for (const auto &p : ghost->bands.first().ports)
            if (p.id == portId) havePort = true;
        if (!havePort) {
            PortDesc p;
            p.id = portId;
            p.label = collectionId;
            p.domain = c->domain;
            p.kind = PortKind::RemoteCollection;
            ghost->bands.first().ports << p;
        }
    }
}
```

- [ ] **Step 4: Run tests** — all Task 6+7 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): remote nodes — (collection,domain) ports, busy states, ghost nodes for unresolvable targets"
```

---

### Task 8: PatchbayModel — wires + strands

**Files:**
- Modify: `src/app/patchbay/patchbaymodel.cpp` (fill `rebuildWiresAndStrands()`)
- Modify: `tests/runtime/tst_patchbay_model.cpp`

- [ ] **Step 1: Failing tests** (add slots + implementations)

```cpp
    void wireFromWholeDomainRow();
    void wireFromCategoryRow();
    void wireStates();
    void strandsSolidGhostAndNoFreeSlot();
```

```cpp
void TstPatchbayModel::wireFromWholeDomainRow()
{
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    QCOMPARE(m.wires().size(), 1);
    const WireDesc w = m.wires().first();
    QCOMPARE(w.mappingId, QStringLiteral("m1"));
    QCOMPARE(w.sourcePortId, QStringLiteral("dom:calendar"));
    QCOMPARE(w.targetNodeId, QStringLiteral("remote:acc-1"));
    QCOMPARE(w.targetPortId, QStringLiteral("col:cal1|calendar"));
    QCOMPARE(w.state, WireState::TwoWay);
}

void TstPatchbayModel::wireFromCategoryRow()
{
    auto in = baseInputs();
    in.mappings.append(row("m2", "calendar", "palm:calendar/name:Work",
                           "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    QCOMPARE(m.wires().first().sourcePortId,
             QStringLiteral("cat:calendar/Work"));
}

void TstPatchbayModel::wireStates()
{
    auto in = baseInputs();
    in.mappings.append(row("up",   "calendar", "", "acc-1:cal1", "cal1", "OneWayUpload"));
    in.mappings.append(row("off",  "calendar", "", "acc-1:cal1", "cal1", "TwoWay", false));
    in.mappings.append(row("bad",  "calendar", "", "acc-1:cal1", "cal1"));
    in.mappings.append(row("gone", "calendar", "", "nope:calX",  "calX"));
    in.routeStatuses.insert("bad", RouteStatus::NotARoute);
    PatchbayModel m;
    m.setInputs(in);
    QHash<QString, WireState> got;
    for (const auto &w : m.wires()) got[w.mappingId] = w.state;
    QCOMPARE(got["up"],   WireState::OneWayUpload);
    QCOMPARE(got["off"],  WireState::Disabled);     // enabled=false wins over NotARoute
    QCOMPARE(got["bad"],  WireState::Broken);
    QCOMPARE(got["gone"], WireState::Broken);       // ghost target
    // broken wires carry the ✗ bead glyph
    for (const auto &w : m.wires())
        if (w.state == WireState::Broken) QCOMPARE(w.beadGlyph, QStringLiteral("✗"));
}

void TstPatchbayModel::strandsSolidGhostAndNoFreeSlot()
{
    auto in = baseInputs();
    // "Work" IS in the DatebookDB snapshot (slot 1) → Solid strand.
    // "Offsite" is desired but NOT in the snapshot → Ghost strand (waiting).
    // "Stuffed" desired, and a row referencing it reports NoFreeSlot → no
    // strand, port flagged.
    in.desiredCategories["DatebookDB"] = {"Work", "Offsite", "Stuffed"};
    in.mappings.append(row("ns", "calendar", "palm:calendar/name:Stuffed",
                           "acc-1:cal1", "cal1"));
    in.routeStatuses.insert("ns", RouteStatus::NoFreeSlot);
    PatchbayModel m;
    m.setInputs(in);

    QHash<QString, StrandDesc> byHubPort;
    for (const auto &s : m.strands()) byHubPort[s.hubPortId] = s;

    // whole-domain strand per conduit band that has a snapshot
    QVERIFY(byHubPort.contains("dom:calendar"));
    QVERIFY(byHubPort["dom:calendar"].wholeDomain);

    QCOMPARE(byHubPort["cat:calendar/Work"].state, StrandState::Solid);
    QCOMPARE(byHubPort["cat:calendar/Work"].palmPortId,
             QStringLiteral("slot:DatebookDB/1"));

    QCOMPARE(byHubPort["cat:calendar/Offsite"].state, StrandState::Ghost);
    QCOMPARE(byHubPort["cat:calendar/Offsite"].palmPortId,
             QStringLiteral("db:DatebookDB"));

    QVERIFY(!byHubPort.contains("cat:calendar/Stuffed"));
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    for (const auto &p : band->ports)
        if (p.id == "cat:calendar/Stuffed") QVERIFY(p.noFreeSlot);
}
```

- [ ] **Step 2: Verify failure** — build + run, new tests fail.

- [ ] **Step 3: Implement `rebuildWiresAndStrands()`**

```cpp
void PatchbayModel::rebuildWiresAndStrands()
{
    // ── Wires: one per persisted row ─────────────────────────────────
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        const QString id = r.value(QLatin1String("id")).toString();
        const ConduitFacts *c =
            conduitForId(r.value(QLatin1String("sourceBackend")).toString());
        if (!c)
            continue;   // unknown conduit: row is invisible only if we can't
                        // even determine a domain — covered by NotARoute UI in
                        // the inspector; ghost nodes need a domain to draw.

        WireDesc w;
        w.mappingId = id;
        w.domain = c->domain;

        const QString srcCal =
            r.value(QLatin1String("sourceCalendar")).toString();
        const QString cat = categoryFromSourceCalendar(srcCal, c->domain);
        w.sourcePortId = cat.isEmpty()
            ? QStringLiteral("dom:%1").arg(c->domain)
            : QStringLiteral("cat:%1/%2").arg(c->domain, cat);

        const QString target = r.value(QLatin1String("targetBackend")).toString();
        const int colon = target.indexOf(QLatin1Char(':'));
        const QString providerId = colon > 0 ? target.left(colon) : QString();
        const QString collectionId = colon > 0 ? target.mid(colon + 1) : QString();
        w.targetPortId = QStringLiteral("col:%1|%2").arg(collectionId, c->domain);

        // resolve target node: real remote if the port exists there, else ghost
        const QString remoteId = QStringLiteral("remote:%1").arg(providerId);
        const QString ghostId = QStringLiteral("ghost:%1").arg(providerId);
        bool onRemote = false;
        for (const auto &n : m_nodes) {
            if (n.id != remoteId) continue;
            for (const auto &p : n.bands.first().ports)
                if (p.id == w.targetPortId) onRemote = true;
        }
        w.targetNodeId = onRemote ? remoteId : ghostId;

        // state precedence: disabled > broken > mode
        const bool enabled = r.value(QLatin1String("enabled")).toBool(true);
        const QString mode = r.value(QLatin1String("mode")).toString();
        const RouteStatus st = m_inputs.routeStatuses.value(id, RouteStatus::Active);
        if (!enabled || mode == QLatin1String("Disabled"))
            w.state = WireState::Disabled;
        else if (st == RouteStatus::NotARoute || !onRemote)
            w.state = WireState::Broken;
        else if (mode == QLatin1String("OneWayUpload"))
            w.state = WireState::OneWayUpload;
        else if (mode == QLatin1String("OneWayDownload"))
            w.state = WireState::OneWayDownload;
        else
            w.state = WireState::TwoWay;

        if (w.state == WireState::Broken)
            w.beadGlyph = QStringLiteral("✗");

        m_wires << w;
    }

    // ── Strands: system-drawn palm↔hub legs ──────────────────────────
    for (const auto &c : m_inputs.conduits) {
        const QStringList snap = m_inputs.slotSnapshot.value(c.dbName);

        StrandDesc whole;
        whole.hubPortId = QStringLiteral("dom:%1").arg(c.domain);
        whole.id = QStringLiteral("strand:%1").arg(whole.hubPortId);
        whole.palmPortId = QStringLiteral("db:%1").arg(c.dbName);
        whole.domain = c.domain;
        whole.state = StrandState::Solid;
        whole.wholeDomain = true;
        m_strands << whole;

        for (const QString &cat : hubCategoryNames(c)) {
            const QString hubPort = QStringLiteral("cat:%1/%2").arg(c.domain, cat);

            // NoFreeSlot: any row on this category reporting it suppresses the
            // strand and flags the port (set below on the hub NodeDesc).
            bool noSlot = false;
            for (const auto &v : m_inputs.mappings) {
                const QJsonObject r = v.toObject();
                if (r.value(QLatin1String("sourceBackend")).toString() != c.conduitId)
                    continue;
                if (categoryFromSourceCalendar(
                        r.value(QLatin1String("sourceCalendar")).toString(),
                        c.domain).compare(cat, Qt::CaseInsensitive) != 0)
                    continue;
                const QString id = r.value(QLatin1String("id")).toString();
                if (m_inputs.routeStatuses.value(id, RouteStatus::Active)
                    == RouteStatus::NoFreeSlot)
                    noSlot = true;
            }

            int slotIdx = -1;
            for (int i = 0; i < snap.size(); ++i)
                if (snap[i].compare(cat, Qt::CaseInsensitive) == 0) slotIdx = i;

            if (noSlot) {
                for (auto &n : m_nodes) {
                    if (n.kind != NodeKind::Hub) continue;
                    for (auto &b : n.bands)
                        for (auto &p : b.ports)
                            if (p.id == hubPort) p.noFreeSlot = true;
                }
                continue;   // no strand
            }

            StrandDesc s;
            s.hubPortId = hubPort;
            s.id = QStringLiteral("strand:%1").arg(hubPort);
            s.domain = c.domain;
            if (slotIdx >= 0) {
                s.state = StrandState::Solid;
                s.palmPortId = QStringLiteral("slot:%1/%2").arg(c.dbName).arg(slotIdx);
            } else {
                s.state = StrandState::Ghost;     // WaitingForDevice
                s.palmPortId = QStringLiteral("db:%1").arg(c.dbName);
                for (auto &n : m_nodes) {
                    if (n.kind != NodeKind::Hub) continue;
                    for (auto &b : n.bands)
                        for (auto &p : b.ports)
                            if (p.id == hubPort) p.waiting = true;
                }
            }
            m_strands << s;
        }
    }
}
```

- [ ] **Step 4: Run tests** — all pass (`ctest --test-dir build -R tst_patchbay_model`).

- [ ] **Step 5: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): wires from rows (state precedence, ghost targets) + system strands (solid/ghost/no-free-slot)"
```

---

### Task 9: PatchbayModel — edit operations

**Files:**
- Modify: `src/app/patchbay/patchbaymodel.cpp`
- Modify: `tests/runtime/tst_patchbay_model.cpp`

- [ ] **Step 1: Failing tests**

```cpp
    void addMappingCreatesRowAndWire();
    void addMappingRejectsDuplicateAndMismatch();
    void removeAndUpdateMapping();
    void addRemoveCategory();
```

```cpp
void TstPatchbayModel::addMappingCreatesRowAndWire()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    QSignalSpy spy(&m, &PatchbayModel::mappingsChanged);

    const QString id = m.addMapping("cat:calendar/Work", "acc-1", "cal1");
    QVERIFY(!id.isEmpty());
    QCOMPARE(spy.count(), 1);
    const QJsonObject r = m.mappingById(id);
    QCOMPARE(r["sourceBackend"].toString(), QStringLiteral("calendar"));
    QCOMPARE(r["sourceCalendar"].toString(),
             QStringLiteral("palm:calendar/name:Work"));
    QCOMPARE(r["targetBackend"].toString(), QStringLiteral("acc-1:cal1"));
    QCOMPARE(r["targetCalendar"].toString(), QStringLiteral("cal1"));
    QCOMPARE(r["mode"].toString(), QStringLiteral("TwoWay"));
    QCOMPARE(r["conflictPolicy"].toString(), QStringLiteral("LastWriteWins"));
    QVERIFY(r["enabled"].toBool());
    QCOMPARE(m.wires().size(), 1);
}

void TstPatchbayModel::addMappingRejectsDuplicateAndMismatch()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    QVERIFY(!m.addMapping("dom:calendar", "acc-1", "cal1").isEmpty());
    // exact duplicate
    QVERIFY(m.addMapping("dom:calendar", "acc-1", "cal1").isEmpty());
    // domain mismatch: contacts conduit does not match a calendar collection
    QVERIFY(m.addMapping("dom:contacts", "acc-1", "cal1").isEmpty());
    // unknown provider/collection
    QVERIFY(m.addMapping("dom:calendar", "nope", "cal1").isEmpty());
    QCOMPARE(m.wires().size(), 1);
}

void TstPatchbayModel::removeAndUpdateMapping()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const QString id = m.addMapping("dom:calendar", "acc-1", "cal1");

    QJsonObject changes;
    changes["mode"] = "OneWayUpload";
    changes["enabled"] = true;
    QVERIFY(m.updateMapping(id, changes));
    QCOMPARE(m.wires().first().state, WireState::OneWayUpload);

    QVERIFY(m.removeMapping(id));
    QVERIFY(m.wires().isEmpty());
    QVERIFY(!m.removeMapping(id));   // already gone
}

void TstPatchbayModel::addRemoveCategory()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    QSignalSpy spy(&m, &PatchbayModel::desiredCategoriesChanged);

    QVERIFY(m.addCategory("calendar", "Offsite"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("DatebookDB"));
    QVERIFY(spy.first().at(1).toStringList().contains("Offsite"));
    QVERIFY(!m.addCategory("calendar", "offsite"));   // case-insensitive dup
    QVERIFY(!m.addCategory("calendar", ""));

    // removeCategory refuses while a row references it
    const QString id = m.addMapping("cat:calendar/Offsite", "acc-1", "cal1");
    QVERIFY(!m.removeCategory("calendar", "Offsite"));
    QVERIFY(m.removeMapping(id));
    QVERIFY(m.removeCategory("calendar", "Offsite"));
}
```

(`#include <QSignalSpy>` at the top of the test file.)

- [ ] **Step 2: Verify failure** — stubs return empty/false, tests fail.

- [ ] **Step 3: Implement the edit ops**

```cpp
QString PatchbayModel::addMapping(const QString &hubPortId,
                                  const QString &providerId,
                                  const QString &collectionId)
{
    // parse hub port → domain + optional category
    QString domain, category;
    if (hubPortId.startsWith(QLatin1String("dom:"))) {
        domain = hubPortId.mid(4);
    } else if (hubPortId.startsWith(QLatin1String("cat:"))) {
        const QString rest = hubPortId.mid(4);
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash <= 0) return {};
        domain = rest.left(slash);
        category = rest.mid(slash + 1);
    } else {
        return {};
    }
    const ConduitFacts *c = conduitForDomain(domain);
    if (!c)
        return {};

    // target must exist and match the conduit's domain rules
    const Kalburator::Sync::CollectionInfo *col = nullptr;
    for (const auto &prov : m_inputs.providers) {
        if (prov.providerId != providerId) continue;
        for (const auto &x : prov.collections)
            if (x.id == collectionId) col = &x;
    }
    if (!col || !c->matchesCollection || !c->matchesCollection(*col))
        return {};

    const QString sourceCalendar = category.isEmpty()
        ? QString()
        : QStringLiteral("palm:%1/name:%2").arg(domain, category);
    const QString targetBackend =
        QStringLiteral("%1:%2").arg(providerId, collectionId);

    // duplicate guard (same source slice → same target)
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("sourceBackend")).toString() == c->conduitId
            && r.value(QLatin1String("sourceCalendar")).toString() == sourceCalendar
            && r.value(QLatin1String("targetBackend")).toString() == targetBackend
            && r.value(QLatin1String("targetCalendar")).toString() == collectionId)
            return {};
    }

    QJsonObject row;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    row[QLatin1String("id")] = id;
    row[QLatin1String("sourceBackend")] = c->conduitId;
    row[QLatin1String("sourceCalendar")] = sourceCalendar;
    row[QLatin1String("targetBackend")] = targetBackend;
    row[QLatin1String("targetCalendar")] = collectionId;
    row[QLatin1String("mode")] = QStringLiteral("TwoWay");
    row[QLatin1String("conflictPolicy")] = QStringLiteral("LastWriteWins");
    row[QLatin1String("enabled")] = true;
    m_inputs.mappings.append(row);

    rebuild();
    emit mappingsChanged(m_inputs.mappings);
    return id;
}

bool PatchbayModel::removeMapping(const QString &mappingId)
{
    for (int i = 0; i < m_inputs.mappings.size(); ++i) {
        if (m_inputs.mappings[i].toObject()
                .value(QLatin1String("id")).toString() != mappingId)
            continue;
        m_inputs.mappings.removeAt(i);
        rebuild();
        emit mappingsChanged(m_inputs.mappings);
        return true;
    }
    return false;
}

bool PatchbayModel::updateMapping(const QString &mappingId,
                                  const QJsonObject &changes)
{
    for (int i = 0; i < m_inputs.mappings.size(); ++i) {
        QJsonObject r = m_inputs.mappings[i].toObject();
        if (r.value(QLatin1String("id")).toString() != mappingId)
            continue;
        for (auto it = changes.begin(); it != changes.end(); ++it)
            r[it.key()] = it.value();
        m_inputs.mappings[i] = r;
        rebuild();
        emit mappingsChanged(m_inputs.mappings);
        return true;
    }
    return false;
}

bool PatchbayModel::addCategory(const QString &domain, const QString &name)
{
    const ConduitFacts *c = conduitForDomain(domain);
    const QString trimmed = name.trimmed();
    if (!c || trimmed.isEmpty()
        || trimmed.compare(QLatin1String("Unfiled"), Qt::CaseInsensitive) == 0)
        return false;
    QStringList names = m_inputs.desiredCategories.value(c->dbName);
    for (const auto &x : names)
        if (x.compare(trimmed, Qt::CaseInsensitive) == 0) return false;
    if (names.size() >= 15)   // 16 slots, Unfiled implicit at 0
        return false;
    names << trimmed;
    m_inputs.desiredCategories[c->dbName] = names;
    rebuild();
    emit desiredCategoriesChanged(c->dbName, names);
    return true;
}

bool PatchbayModel::removeCategory(const QString &domain, const QString &name)
{
    const ConduitFacts *c = conduitForDomain(domain);
    if (!c)
        return false;
    // refuse while any row references the category (spec §7.4)
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("sourceBackend")).toString() != c->conduitId)
            continue;
        if (categoryFromSourceCalendar(
                r.value(QLatin1String("sourceCalendar")).toString(), domain)
                .compare(name, Qt::CaseInsensitive) == 0)
            return false;
    }
    QStringList names = m_inputs.desiredCategories.value(c->dbName);
    bool removed = false;
    for (int i = names.size() - 1; i >= 0; --i) {
        if (names[i].compare(name, Qt::CaseInsensitive) == 0) {
            names.removeAt(i);
            removed = true;
        }
    }
    if (!removed)
        return false;
    m_inputs.desiredCategories[c->dbName] = names;
    rebuild();
    emit desiredCategoriesChanged(c->dbName, names);
    return true;
}
```

- [ ] **Step 4: Run the full model suite** — all pass.

- [ ] **Step 5: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): model edit ops — addMapping (validated), remove/update, category lifecycle"
```

---

### Task 10: PatchNodeItem — one generic node item for all three tiers

One `QGraphicsItem` + `IGraphNode` implementation renders any `NodeDesc` (palm, hub,
remote, ghost): header, bands, port rows. Anchor sides differ by node kind (palm → right
only, hub → both, remote → left only). View tests for items come with Task 12 (they need
a scene); this task is build-green + commit.

**Files:**
- Create: `src/app/patchbay/patchnodeitem.h`
- Create: `src/app/patchbay/patchnodeitem.cpp`
- Modify: `src/app/patchbay/CMakeLists.txt` (uncomment/add the two files)

- [ ] **Step 1: Header**

```cpp
// src/app/patchbay/patchnodeitem.h
#pragma once

#include <QGraphicsObject>

#include <graffodil/IGraphNode.h>
#include <graffodil/Types.h>

#include "patchbaytypes.h"

namespace WildPalms::AppPatchbay {

/// Generic patchbay node: renders a NodeDesc (header + bands + port rows)
/// and exposes Graffodil anchors per port. Anchor id grammar:
/// "<portId>@l" / "<portId>@r" — which sides exist depends on NodeKind:
///   Palm → @r only; Hub → @l and @r; Remote/GhostRemote → @l only.
/// Anchor.metadata is a QVariantMap {"port": portId, "kind": int(PortKind),
/// "domain": domain, "node": nodeId} used by the view for drop validation.
class PatchNodeItem : public QGraphicsObject, public Graffodil::IGraphNode {
    Q_OBJECT
public:
    explicit PatchNodeItem(const NodeDesc &desc);

    void setDesc(const NodeDesc &desc);
    NodeDesc desc() const { return m_desc; }

    // geometry constants (shared with the view's layout math)
    static constexpr qreal kWidth = 200.0;
    static constexpr qreal kHeaderH = 28.0;
    static constexpr qreal kBandHeaderH = 20.0;
    static constexpr qreal kRowH = 22.0;
    static constexpr qreal kBandPad = 6.0;

    qreal contentHeight() const;             ///< full node height for layout
    /// Item-coords center of a port's row; the view uses this for the
    /// inline category editor; -1 y if port unknown.
    QPointF portRowCenter(const QString &portId) const;
    /// Port at an item-coords position (for AddCategory click routing).
    QString portAt(const QPointF &itemPos) const;

    // ── IGraphNode ────────────────────────────────────────────────────
    QString nodeId() const override { return m_desc.id; }
    QList<Graffodil::Anchor> anchors() const override;
    QRectF nodeBoundingRect() const override;
    QGraphicsItem *graphicsItem() override { return this; }

    // ── QGraphicsItem ────────────────────────────────────────────────
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt,
               QWidget *w) override;

signals:
    /// Click landed on an AddCategory ghost row.
    void addCategoryClicked(const QString &domain, const QPointF &scenePos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    struct RowRef { int band; int port; qreal y; };   // y = row top, item coords
    QList<RowRef> rows() const;
    bool leftAnchors() const;
    bool rightAnchors() const;

    NodeDesc m_desc;
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 2: Implementation**

```cpp
// src/app/patchbay/patchnodeitem.cpp
#include "patchnodeitem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QVariantMap>

namespace WildPalms::AppPatchbay {

namespace {
QColor chromeFor(NodeKind k)
{
    switch (k) {
    case NodeKind::Palm:        return QColor(0x2a, 0x3a, 0x50);
    case NodeKind::Hub:         return QColor(0x35, 0x2e, 0x55);
    case NodeKind::Remote:      return QColor(0x2a, 0x4a, 0x2a);
    case NodeKind::GhostRemote: return QColor(0x4a, 0x2a, 0x2a);
    }
    return Qt::darkGray;
}
} // namespace

PatchNodeItem::PatchNodeItem(const NodeDesc &desc) : m_desc(desc)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setOpacity(m_desc.ghosted ? 0.45 : 1.0);
}

void PatchNodeItem::setDesc(const NodeDesc &desc)
{
    prepareGeometryChange();
    m_desc = desc;
    setOpacity(m_desc.ghosted ? 0.45 : 1.0);
    update();
}

bool PatchNodeItem::leftAnchors() const  { return m_desc.kind != NodeKind::Palm; }
bool PatchNodeItem::rightAnchors() const
{
    return m_desc.kind == NodeKind::Palm || m_desc.kind == NodeKind::Hub;
}

QList<PatchNodeItem::RowRef> PatchNodeItem::rows() const
{
    QList<RowRef> out;
    qreal y = kHeaderH;
    for (int b = 0; b < m_desc.bands.size(); ++b) {
        y += kBandHeaderH;
        for (int p = 0; p < m_desc.bands[b].ports.size(); ++p) {
            out.append({b, p, y});
            y += kRowH;
        }
        y += kBandPad;
    }
    return out;
}

qreal PatchNodeItem::contentHeight() const
{
    qreal y = kHeaderH;
    for (const auto &band : m_desc.bands)
        y += kBandHeaderH + band.ports.size() * kRowH + kBandPad;
    return y + 4.0;
}

QPointF PatchNodeItem::portRowCenter(const QString &portId) const
{
    for (const auto &r : rows())
        if (m_desc.bands[r.band].ports[r.port].id == portId)
            return QPointF(kWidth / 2.0, r.y + kRowH / 2.0);
    return QPointF(0, -1);
}

QString PatchNodeItem::portAt(const QPointF &itemPos) const
{
    for (const auto &r : rows())
        if (itemPos.y() >= r.y && itemPos.y() < r.y + kRowH)
            return m_desc.bands[r.band].ports[r.port].id;
    return {};
}

QList<Graffodil::Anchor> PatchNodeItem::anchors() const
{
    QList<Graffodil::Anchor> out;
    for (const auto &r : rows()) {
        const PortDesc &p = m_desc.bands[r.band].ports[r.port];
        if (p.kind == PortKind::AddCategory)
            continue;   // not connectable
        QVariantMap meta{
            {QStringLiteral("port"),   p.id},
            {QStringLiteral("kind"),   int(p.kind)},
            {QStringLiteral("domain"), p.domain},
            {QStringLiteral("node"),   m_desc.id},
        };
        const qreal cy = r.y + kRowH / 2.0;
        if (leftAnchors()) {
            Graffodil::Anchor a;
            a.id = p.id + QStringLiteral("@l");
            a.scenePos = mapToScene(QPointF(0.0, cy));
            a.outwardDirection = QPointF(-1.0, 0.0);
            a.metadata = meta;
            out << a;
        }
        if (rightAnchors()) {
            Graffodil::Anchor a;
            a.id = p.id + QStringLiteral("@r");
            a.scenePos = mapToScene(QPointF(kWidth, cy));
            a.outwardDirection = QPointF(1.0, 0.0);
            a.metadata = meta;
            out << a;
        }
    }
    // whole-DB anchor for ghost strands (palm only, band headers)
    if (m_desc.kind == NodeKind::Palm) {
        qreal y = kHeaderH;
        for (const auto &band : m_desc.bands) {
            // derive dbName from any slot port id "slot:<db>/<i>"; bands with
            // no claimed slots still get the anchor at the band header.
            QString db;
            if (!band.ports.isEmpty()) {
                const QString id = band.ports.first().id;   // slot:<db>/<i>
                db = id.mid(5, id.lastIndexOf(QLatin1Char('/')) - 5);
            } else {
                db = band.title.section(QStringLiteral(" — "), 1, 1);
            }
            Graffodil::Anchor a;
            a.id = QStringLiteral("db:%1@r").arg(db);
            a.scenePos = mapToScene(QPointF(kWidth, y + kBandHeaderH / 2.0));
            a.outwardDirection = QPointF(1.0, 0.0);
            a.metadata = QVariantMap{{QStringLiteral("port"),
                                      QStringLiteral("db:%1").arg(db)}};
            out << a;
            y += kBandHeaderH + band.ports.size() * kRowH + kBandPad;
        }
    }
    return out;
}

QRectF PatchNodeItem::nodeBoundingRect() const
{
    return mapToScene(boundingRect()).boundingRect();
}

QRectF PatchNodeItem::boundingRect() const
{
    return QRectF(0, 0, kWidth, contentHeight());
}

void PatchNodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *,
                          QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect();

    // body + header
    p->setPen(QPen(chromeFor(m_desc.kind).lighter(160), 1.2));
    p->setBrush(QColor(0x20, 0x22, 0x2b));
    p->drawRoundedRect(r, 8, 8);
    p->setBrush(chromeFor(m_desc.kind));
    p->setPen(Qt::NoPen);
    p->drawRoundedRect(QRectF(0, 0, kWidth, kHeaderH), 8, 8);
    p->setPen(QColor(0xd8, 0xdd, 0xe6));
    QFont f = p->font();
    f.setBold(true);
    p->setFont(f);
    p->drawText(QRectF(10, 0, kWidth - 64, kHeaderH),
                Qt::AlignVCenter | Qt::AlignLeft, m_desc.title);
    f.setBold(false);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p->setFont(f);
    p->setPen(QColor(0x9a, 0xa3, 0xb2));
    p->drawText(QRectF(10, 0, kWidth - 16, kHeaderH),
                Qt::AlignVCenter | Qt::AlignRight, m_desc.subtitle);

    // bands + rows
    qreal y = kHeaderH;
    for (const auto &band : m_desc.bands) {
        p->setPen(domainColor(band.domain).lighter(125));
        p->drawText(QRectF(10, y, kWidth - 16, kBandHeaderH),
                    Qt::AlignVCenter | Qt::AlignLeft, band.title.toUpper());
        y += kBandHeaderH;
        for (const auto &port : band.ports) {
            const bool isAdd = port.kind == PortKind::AddCategory;
            p->setPen(isAdd ? QColor(0x6f, 0x7a, 0x8a)
                            : QColor(0xcf, 0xd8, 0xe3));
            p->drawText(QRectF(16, y, kWidth - 40, kRowH),
                        Qt::AlignVCenter | Qt::AlignLeft, port.label);
            // port dots
            const qreal cy = y + kRowH / 2.0;
            if (!isAdd) {
                QColor dot = domainColor(port.domain);
                if (port.waiting)    dot = QColor(0xd9, 0xa4, 0x5b);
                if (port.noFreeSlot) dot = QColor(0xc2, 0x54, 0x50);
                p->setBrush(dot);
                p->setPen(Qt::NoPen);
                if (leftAnchors())
                    p->drawEllipse(QPointF(0.0, cy), 4, 4);
                if (rightAnchors())
                    p->drawEllipse(QPointF(kWidth, cy), 4, 4);
            }
            y += kRowH;
        }
        y += kBandPad;
    }
}

void PatchNodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    const QString port = portAt(event->pos());
    if (port.startsWith(QLatin1String("add:"))) {
        emit addCategoryClicked(port.mid(4), event->scenePos());
        event->accept();
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 3: Build** — `cmake --build build -j8`, expected green.

- [ ] **Step 4: Commit**

```bash
git add src/app/patchbay
git commit -m "feat(patchbay): PatchNodeItem — generic NodeDesc renderer with per-kind anchor sides"
```

---

### Task 11: SignalPathWire — wire + strand edge item

**Files:**
- Create: `src/app/patchbay/signalpathwire.h`
- Create: `src/app/patchbay/signalpathwire.cpp`
- Modify: `src/app/patchbay/CMakeLists.txt`

- [ ] **Step 1: Header**

```cpp
// src/app/patchbay/signalpathwire.h
#pragma once

#include <graffodil/GraphEdgeItem.h>

#include "patchbaytypes.h"

namespace WildPalms::AppPatchbay {

/// Signal-path edge (spec §6): domain-colored stroke; chevrons for one-way
/// direction; dashed/desaturated when disabled; red when broken; glyph bead
/// via Graffodil's edge label. Also renders read-only palm strands.
class SignalPathWire : public Graffodil::GraphEdgeItem {
public:
    enum class Role { Wire, Strand };

    SignalPathWire(Role role,
                   Graffodil::IGraphNode *source, const QString &sourceAnchorId,
                   Graffodil::IGraphNode *target, const QString &targetAnchorId,
                   const QString &domain);

    Role role() const { return m_role; }
    QString domain() const { return m_domain; }

    /// Wire role only.
    void setWireState(WireState state);
    WireState wireState() const { return m_wireState; }
    void setBead(const QString &glyph);

    /// Strand role only.
    void setStrandState(StrandState state, bool wholeDomain);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    void applyPen();

    Role m_role;
    QString m_domain;
    WireState m_wireState = WireState::TwoWay;
    StrandState m_strandState = StrandState::Solid;
    bool m_wholeDomain = false;
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 2: Implementation**

```cpp
// src/app/patchbay/signalpathwire.cpp
#include "signalpathwire.h"

#include <QPainter>

#include <graffodil/EdgePathStrategy.h>

namespace WildPalms::AppPatchbay {

SignalPathWire::SignalPathWire(Role role,
                               Graffodil::IGraphNode *source,
                               const QString &sourceAnchorId,
                               Graffodil::IGraphNode *target,
                               const QString &targetAnchorId,
                               const QString &domain)
    : Graffodil::GraphEdgeItem(source, sourceAnchorId, target, targetAnchorId,
                               std::make_unique<Graffodil::BezierPathStrategy>())
    , m_role(role)
    , m_domain(domain)
{
    if (role == Role::Strand) {
        // not selectable, no bead, sits under wires
        graphicsItem()->setFlag(QGraphicsItem::ItemIsSelectable, false);
        graphicsItem()->setZValue(-2.0);
        setHitWidth(0.0);
    }
    applyPen();
}

void SignalPathWire::setWireState(WireState state)
{
    m_wireState = state;
    applyPen();
    graphicsItem()->update();
}

void SignalPathWire::setBead(const QString &glyph)
{
    Graffodil::EdgeLabelStyle style;
    style.color = m_wireState == WireState::Broken
        ? QColor(0xf0, 0xa8, 0xa5) : QColor(0x9f, 0xd8, 0xa8);
    style.background = QColor(0x22, 0x2a, 0x36);
    style.backgroundPadding = 4.0;
    setLabelStyle(style);
    setLabel(glyph);
}

void SignalPathWire::setStrandState(StrandState state, bool wholeDomain)
{
    m_strandState = state;
    m_wholeDomain = wholeDomain;
    applyPen();
    graphicsItem()->update();
}

void SignalPathWire::applyPen()
{
    QColor c = domainColor(m_domain);
    QPen pen(c, 2.5);

    if (m_role == Role::Strand) {
        pen.setWidthF(m_wholeDomain ? 3.0 : 2.0);
        if (m_strandState == StrandState::Ghost) {
            pen.setStyle(Qt::DashLine);
            c.setAlphaF(0.45);
            pen.setColor(c);
        }
        setPen(pen);
        return;
    }

    switch (m_wireState) {
    case WireState::Disabled:
        pen.setColor(QColor(0x56, 0x60, 0x70));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(2.0);
        break;
    case WireState::Broken:
        pen.setColor(QColor(0xc2, 0x54, 0x50));
        break;
    default:
        break;
    }
    setPen(pen);
}

void SignalPathWire::paint(QPainter *painter,
                           const QStyleOptionGraphicsItem *option,
                           QWidget *widget)
{
    Graffodil::GraphEdgeItem::paint(painter, option, widget);

    // chevrons for one-way wires (spec §6.2)
    if (m_role != Role::Wire)
        return;
    if (m_wireState != WireState::OneWayUpload
        && m_wireState != WireState::OneWayDownload)
        return;

    const QPainterPath p = path();
    if (p.isEmpty())
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(domainColor(m_domain).lighter(135), 2.0);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (qreal t : {0.3, 0.5, 0.7}) {
        const QPointF pt = p.pointAtPercent(t);
        qreal angle = -p.angleAtPercent(t);
        // upload: hub → remote = along the path; download: against it
        if (m_wireState == WireState::OneWayDownload)
            angle += 180.0;
        painter->save();
        painter->translate(pt);
        painter->rotate(angle);
        painter->drawPolyline(QPolygonF{
            QPointF(-4, -5), QPointF(4, 0), QPointF(-4, 5)});
        painter->restore();
    }
}

} // namespace WildPalms::AppPatchbay
```

Direction convention: every wire is constructed source=hub-port-anchor (`@r`),
target=remote-port-anchor (`@l`). `OneWayUpload` (Palm/hub → remote) points along the
path; `OneWayDownload` points back toward the hub.

- [ ] **Step 3: Build green, commit**

```bash
git add src/app/patchbay
git commit -m "feat(patchbay): SignalPathWire — signal-path wire/strand rendering, chevrons, beads"
```

---

### Task 12: SyncPatchbayView — scene build, layout, tools

**Files:**
- Create: `src/app/patchbay/syncpatchbayview.h`
- Create: `src/app/patchbay/syncpatchbayview.cpp`
- Create: `tests/runtime/tst_patchbay_view.cpp`
- Modify: `src/app/patchbay/CMakeLists.txt`, `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Failing view tests** (`tst_patchbay_view.cpp`; same registration pattern as `tst_patchbay_model`, plus `Qt::Widgets` if not already linked transitively; reuse the `baseInputs()` helpers — copy them into this file, they are 60 lines and the two tests must stay independent)

```cpp
#include <QtTest/QtTest>

#include "../../src/app/patchbay/patchbaymodel.h"
#include "../../src/app/patchbay/syncpatchbayview.h"
#include "../wildpalms_qtest_main.h"

// … copy of baseInputs()/row()/snapshot16()/stockConduits() from
//   tst_patchbay_model.cpp …

using namespace WildPalms::AppPatchbay;

class TstPatchbayView : public QObject {
    Q_OBJECT
private slots:
    void rebuildPopulatesScene();
    void columnsAreOrdered();
    void wireCarriesState();
};

void TstPatchbayView::rebuildPopulatesScene()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    model.setInputs(in);

    SyncPatchbayView view;
    view.setModel(&model);

    QCOMPARE(view.nodeCount(), 3);      // palm + hub + remote:acc-1
    QCOMPARE(view.wireCount(), 1);
    QVERIFY(view.strandCount() >= 4);   // 4 whole-domain strands minimum
    QVERIFY(view.wireItem("m1") != nullptr);
}

void TstPatchbayView::columnsAreOrdered()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    const QPointF palm = view.nodePos("palm");
    const QPointF hub = view.nodePos("hub");
    const QPointF remote = view.nodePos("remote:acc-1");
    QVERIFY(palm.x() < hub.x());
    QVERIFY(hub.x() < remote.x());
}

void TstPatchbayView::wireCarriesState()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1",
                           "OneWayUpload"));
    model.setInputs(in);
    SyncPatchbayView view;
    view.setModel(&model);
    QCOMPARE(view.wireItem("m1")->wireState(), WireState::OneWayUpload);
}

WILDPALMS_QTEST_MAIN(TstPatchbayView)
#include "tst_patchbay_view.moc"
```

(Use the same main-macro pattern discovered in Task 6.)

- [ ] **Step 2: Header**

```cpp
// src/app/patchbay/syncpatchbayview.h
#pragma once

#include <QGraphicsView>
#include <QHash>

#include "patchbaytypes.h"

namespace Graffodil {
class GraphScene;
class DefaultGraphTool;
class CreateEdgeTool;
class IGraphNode;
}

namespace WildPalms::AppPatchbay {

class PatchbayModel;
class PatchNodeItem;
class SignalPathWire;

class SyncPatchbayView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SyncPatchbayView(QWidget *parent = nullptr);
    ~SyncPatchbayView() override;

    void setModel(PatchbayModel *model);   ///< borrowed; triggers rebuild
    void rebuild();                        ///< re-sync scene from model

    // test seams (F.3 convention)
    int nodeCount() const;
    int wireCount() const;
    int strandCount() const;
    SignalPathWire *wireItem(const QString &mappingId) const;
    QPointF nodePos(const QString &nodeId) const;

signals:
    void wireSelected(const QString &mappingId);   ///< empty = deselected
    void addCategoryRequested(const QString &domain, const QPointF &scenePos);

private:
    void onEdgeRequested(Graffodil::IGraphNode *source,
                         const QString &sourceAnchorId,
                         Graffodil::IGraphNode *target,
                         const QString &targetAnchorId);
    void onSelectionChanged();
    void applyColumnLayout();

    PatchbayModel *m_model = nullptr;
    Graffodil::GraphScene *m_scene = nullptr;
    Graffodil::DefaultGraphTool *m_tool = nullptr;
    Graffodil::CreateEdgeTool *m_createTool = nullptr;
    QHash<QString, PatchNodeItem *> m_nodeItems;     // nodeId → item
    QHash<QString, SignalPathWire *> m_wireItems;    // mappingId → item
    QList<SignalPathWire *> m_strandItems;
    bool m_didInitialFit = false;
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 3: Implementation**

```cpp
// src/app/patchbay/syncpatchbayview.cpp
#include "syncpatchbayview.h"

#include <QGraphicsItem>

#include <graffodil/GraphScene.h>
#include <graffodil/DefaultGraphTool.h>
#include <graffodil/CreateEdgeTool.h>
#include <graffodil/Types.h>

#include "patchbaymodel.h"
#include "patchnodeitem.h"
#include "signalpathwire.h"

namespace WildPalms::AppPatchbay {

namespace {
constexpr qreal kPalmX = 40.0;
constexpr qreal kHubX = 460.0;
constexpr qreal kRemoteX = 900.0;
constexpr qreal kTopY = 40.0;
constexpr qreal kStackGap = 24.0;
} // namespace

SyncPatchbayView::SyncPatchbayView(QWidget *parent) : QGraphicsView(parent)
{
    m_scene = new Graffodil::GraphScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(QColor(0x16, 0x16, 0x1d));

    m_tool = new Graffodil::DefaultGraphTool(this);
    m_createTool = new Graffodil::CreateEdgeTool(this);
    m_tool->addAnchorRoute(m_createTool, 12.0);
    m_scene->setActiveTool(m_tool);

    connect(m_createTool, &Graffodil::CreateEdgeTool::edgeRequested,
            this, &SyncPatchbayView::onEdgeRequested);
    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &SyncPatchbayView::onSelectionChanged);
}

SyncPatchbayView::~SyncPatchbayView() = default;

void SyncPatchbayView::setModel(PatchbayModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
        connect(m_model, &PatchbayModel::rebuilt,
                this, &SyncPatchbayView::rebuild);
    rebuild();
}

void SyncPatchbayView::rebuild()
{
    // tear down previous items (scene registry does not own them)
    const auto cleared = m_scene->clearGraph();
    for (auto *e : cleared.edges) delete e->graphicsItem();
    for (auto *n : cleared.nodes) delete n->graphicsItem();
    m_nodeItems.clear();
    m_wireItems.clear();
    m_strandItems.clear();
    if (!m_model)
        return;

    // nodes
    for (const auto &desc : m_model->nodes()) {
        auto *item = new PatchNodeItem(desc);
        m_scene->addNode(item);
        m_nodeItems.insert(desc.id, item);
        connect(item, &PatchNodeItem::addCategoryClicked,
                this, &SyncPatchbayView::addCategoryRequested);
    }

    applyColumnLayout();

    // strands (under wires)
    auto *hub = m_nodeItems.value(QStringLiteral("hub"));
    auto *palm = m_nodeItems.value(QStringLiteral("palm"));
    if (hub && palm) {
        for (const auto &s : m_model->strands()) {
            auto *e = new SignalPathWire(SignalPathWire::Role::Strand,
                                         palm, s.palmPortId + QStringLiteral("@r"),
                                         hub, s.hubPortId + QStringLiteral("@l"),
                                         s.domain);
            e->setStrandState(s.state, s.wholeDomain);
            m_scene->addEdge(e);
            e->adjust();
            m_strandItems << e;
        }
    }

    // wires
    for (const auto &w : m_model->wires()) {
        auto *target = m_nodeItems.value(w.targetNodeId);
        if (!hub || !target)
            continue;
        auto *e = new SignalPathWire(SignalPathWire::Role::Wire,
                                     hub, w.sourcePortId + QStringLiteral("@r"),
                                     target, w.targetPortId + QStringLiteral("@l"),
                                     w.domain);
        e->setWireState(w.state);
        if (!w.beadGlyph.isEmpty())
            e->setBead(w.beadGlyph);
        m_scene->addEdge(e);
        e->adjust();
        m_wireItems.insert(w.mappingId, e);
    }

    m_scene->fitSceneRectToContent(120.0);
    if (!m_didInitialFit && !m_nodeItems.isEmpty()) {
        fitInView(m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40),
                  Qt::KeepAspectRatio);
        m_didInitialFit = true;
    }
}

void SyncPatchbayView::applyColumnLayout()
{
    Graffodil::LayoutResult layout;
    qreal palmY = kTopY, hubY = kTopY, remoteY = kTopY;
    // deterministic stacking in model order (spec §6 — manual LayoutResult)
    for (const auto &desc : m_model->nodes()) {
        auto *item = m_nodeItems.value(desc.id);
        if (!item) continue;
        switch (desc.kind) {
        case NodeKind::Palm:
            layout.nodePositions.insert(desc.id, QPointF(kPalmX, palmY));
            palmY += item->contentHeight() + kStackGap;
            break;
        case NodeKind::Hub:
            layout.nodePositions.insert(desc.id, QPointF(kHubX, hubY));
            hubY += item->contentHeight() + kStackGap;
            break;
        case NodeKind::Remote:
        case NodeKind::GhostRemote:
            layout.nodePositions.insert(desc.id, QPointF(kRemoteX, remoteY));
            remoteY += item->contentHeight() + kStackGap;
            break;
        }
    }
    m_scene->applyLayout(layout);
}

void SyncPatchbayView::onEdgeRequested(Graffodil::IGraphNode *source,
                                       const QString &sourceAnchorId,
                                       Graffodil::IGraphNode *target,
                                       const QString &targetAnchorId)
{
    if (!m_model || !source || !target)
        return;
    // normalize: hub end + remote end, either drag direction (spec §7.1)
    auto portOf = [](const QString &anchorId) {
        QString p = anchorId;
        if (p.endsWith(QLatin1String("@l")) || p.endsWith(QLatin1String("@r")))
            p.chop(2);
        return p;
    };
    QString hubPort, remoteNodeId, remotePort;
    auto classify = [&](Graffodil::IGraphNode *n, const QString &anchorId) {
        const QString port = portOf(anchorId);
        if (n->nodeId() == QLatin1String("hub")
            && (port.startsWith(QLatin1String("dom:"))
                || port.startsWith(QLatin1String("cat:"))))
            hubPort = port;
        else if (n->nodeId().startsWith(QLatin1String("remote:"))
                 && port.startsWith(QLatin1String("col:"))) {
            remoteNodeId = n->nodeId();
            remotePort = port;
        }
    };
    classify(source, sourceAnchorId);
    classify(target, targetAnchorId);
    if (hubPort.isEmpty() || remotePort.isEmpty())
        return;   // palm-tier or invalid endpoints: not user-wirable (spec §3)

    // "col:<collectionId>|<domain>" → collectionId; "remote:<id>" → providerId
    const QString providerId = remoteNodeId.mid(7);
    const QString body = remotePort.mid(4);
    const QString collectionId = body.left(body.lastIndexOf(QLatin1Char('|')));
    m_model->addMapping(hubPort, providerId, collectionId);
    // model rebuild redraws the scene; invalid drops are silently ignored
    // (addMapping validates domain compatibility + duplicates)
}

void SyncPatchbayView::onSelectionChanged()
{
    for (auto it = m_wireItems.constBegin(); it != m_wireItems.constEnd(); ++it) {
        if (it.value()->graphicsItem()->isSelected()) {
            emit wireSelected(it.key());
            return;
        }
    }
    emit wireSelected(QString());
}

int SyncPatchbayView::nodeCount() const { return m_nodeItems.size(); }
int SyncPatchbayView::wireCount() const { return m_wireItems.size(); }
int SyncPatchbayView::strandCount() const { return m_strandItems.size(); }

SignalPathWire *SyncPatchbayView::wireItem(const QString &mappingId) const
{
    return m_wireItems.value(mappingId);
}

QPointF SyncPatchbayView::nodePos(const QString &nodeId) const
{
    auto *item = m_nodeItems.value(nodeId);
    return item ? item->pos() : QPointF();
}

} // namespace WildPalms::AppPatchbay
```

Note on wires: `GraphEdgeItem` already sets `ItemIsSelectable` appropriately via its own
flags — verify by checking the Graffodil source; if it does NOT, add
`graphicsItem()->setFlag(QGraphicsItem::ItemIsSelectable, true)` for Role::Wire in the
`SignalPathWire` constructor.

- [ ] **Step 4: Run view tests** — `ctest --test-dir build -R tst_patchbay_view`, expected PASS.

- [ ] **Step 5: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): SyncPatchbayView — Graffodil scene binding, three-column layout, drag-to-connect"
```

---

### Task 13: Interaction tests — drag-connect row creation + delete

The view's `onEdgeRequested` and delete path need direct behavioral coverage (the F.3
suite did this with synthesized mouse events; here we exercise the handler + model
contract directly, which is the part that owns correctness — Graffodil's own tests cover
the gesture mechanics).

**Files:**
- Modify: `tests/runtime/tst_patchbay_view.cpp`
- Modify: `src/app/patchbay/syncpatchbayview.h/.cpp` (test seam + delete wiring)

- [ ] **Step 1: Add a test seam + delete handling to the view**

Header, public section:

```cpp
    /// Test seam: drive the CreateEdgeTool result path directly.
    void requestEdgeForTest(const QString &sourceNodeId,
                            const QString &sourceAnchorId,
                            const QString &targetNodeId,
                            const QString &targetAnchorId);
    /// Delete all selected wires (Delete key path; also used by context menu).
    void deleteSelectedWires();
```

Implementation:

```cpp
void SyncPatchbayView::requestEdgeForTest(const QString &sourceNodeId,
                                          const QString &sourceAnchorId,
                                          const QString &targetNodeId,
                                          const QString &targetAnchorId)
{
    onEdgeRequested(m_nodeItems.value(sourceNodeId),
                    sourceAnchorId,
                    m_nodeItems.value(targetNodeId),
                    targetAnchorId);
}

void SyncPatchbayView::deleteSelectedWires()
{
    if (!m_model)
        return;
    QStringList doomed;
    for (auto it = m_wireItems.constBegin(); it != m_wireItems.constEnd(); ++it)
        if (it.value()->graphicsItem()->isSelected())
            doomed << it.key();
    for (const QString &id : doomed)
        m_model->removeMapping(id);
}
```

Wire the Delete key: connect `SelectMoveTool::deleteRequested` in the constructor
(after `m_tool` is created):

```cpp
    connect(m_tool->selectMoveTool(), &Graffodil::SelectMoveTool::deleteRequested,
            this, [this](const QList<Graffodil::IGraphNode *> &,
                         const QList<Graffodil::IGraphEdge *> &) {
                deleteSelectedWires();
            });
```

(`#include <graffodil/SelectMoveTool.h>` in the .cpp.)

- [ ] **Step 2: Failing tests**

```cpp
    void dragConnectCreatesMapping();
    void dragOnPalmTierIgnored();
    void deleteSelectedRemovesMapping();
```

```cpp
void TstPatchbayView::dragConnectCreatesMapping()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    view.requestEdgeForTest("hub", "cat:calendar/Work@r",
                            "remote:acc-1", "col:cal1|calendar@l");
    QCOMPARE(model.mappings().size(), 1);
    QCOMPARE(view.wireCount(), 1);

    // reverse drag direction also works
    view.requestEdgeForTest("remote:acc-1", "col:cal1|calendar@l",
                            "hub", "dom:calendar@r");
    QCOMPARE(model.mappings().size(), 2);
}

void TstPatchbayView::dragOnPalmTierIgnored()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);
    view.requestEdgeForTest("palm", "slot:DatebookDB/1@r",
                            "hub", "cat:calendar/Work@l");
    QCOMPARE(model.mappings().size(), 0);   // strands are not user-wirable
}

void TstPatchbayView::deleteSelectedRemovesMapping()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    model.setInputs(in);
    SyncPatchbayView view;
    view.setModel(&model);

    view.wireItem("m1")->graphicsItem()->setSelected(true);
    view.deleteSelectedWires();
    QCOMPARE(model.mappings().size(), 0);
    QCOMPARE(view.wireCount(), 0);
}
```

- [ ] **Step 3: Run** — build + `ctest -R tst_patchbay_view`, all pass.

- [ ] **Step 4: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): drag-to-connect creates rows (both directions), delete removes; palm tier read-only"
```

---

### Task 14: PatchbayInspector + PatchbayPage

Inspector: the F.3 `MappingInspectorPanel` surface (mode / conflict policy / enabled)
plus the status story (spec §7.2). Page: glue to Profile / AccountController /
PalmRuntime with write-through persistence.

**Files:**
- Create: `src/app/patchbay/patchbayinspector.h/.cpp`
- Create: `src/app/patchbay/patchbaypage.h/.cpp`
- Modify: `src/app/patchbay/CMakeLists.txt`

- [ ] **Step 1: Inspector header**

```cpp
// src/app/patchbay/patchbayinspector.h
#pragma once

#include <QJsonObject>
#include <QWidget>

#include "runtime/routemapping.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QStackedWidget;

namespace WildPalms::AppPatchbay {

/// Right-side inspector (spec §7.2): mode, conflict policy, enabled,
/// plus a human explanation of the RouteStatus.
class PatchbayInspector : public QWidget {
    Q_OBJECT
public:
    explicit PatchbayInspector(QWidget *parent = nullptr);

    /// Empty mappingId → placeholder page.
    void setSelectedMapping(const QString &mappingId, const QJsonObject &json,
                            WildPalms::Runtime::RouteStatus status,
                            const QString &categoryName);

signals:
    /// Field edits; the page forwards to PatchbayModel::updateMapping.
    void mappingEdited(const QString &mappingId, const QJsonObject &changes);

private:
    void emitChanges();

    QString m_mappingId;
    bool m_loading = false;
    QStackedWidget *m_stack = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_policy = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 2: Inspector implementation**

```cpp
// src/app/patchbay/patchbayinspector.cpp
#include "patchbayinspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

using WildPalms::Runtime::RouteStatus;

namespace WildPalms::AppPatchbay {

namespace {
QString statusText(RouteStatus st, const QString &category)
{
    switch (st) {
    case RouteStatus::Active:
        return QStringLiteral("Active.");
    case RouteStatus::WaitingForDevice:
        return QStringLiteral(
            "Waiting for device: category \"%1\" will be created on the "
            "Palm at the next HotSync.").arg(category);
    case RouteStatus::NoFreeSlot:
        return QStringLiteral(
            "No free category slot on the device — Palm databases hold at "
            "most 16 categories. Remove a category to make room.");
    case RouteStatus::NotARoute:
        return QStringLiteral(
            "This row is disabled, malformed, or references an unknown "
            "conduit; it will not run.");
    }
    return {};
}
} // namespace

PatchbayInspector::PatchbayInspector(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    auto *placeholder = new QLabel(
        QStringLiteral("Select a connection to edit its properties."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    m_stack->addWidget(placeholder);

    auto *form = new QWidget(this);
    auto *layout = new QFormLayout(form);
    m_mode = new QComboBox(form);
    m_mode->addItems({QStringLiteral("TwoWay"),
                      QStringLiteral("OneWayUpload"),
                      QStringLiteral("OneWayDownload")});
    m_policy = new QComboBox(form);
    m_policy->addItems({QStringLiteral("LastWriteWins"),
                        QStringLiteral("SourceWins"),
                        QStringLiteral("TargetWins"),
                        QStringLiteral("Duplicate"),
                        QStringLiteral("Skip"),
                        QStringLiteral("AskUser")});
    m_enabled = new QCheckBox(QStringLiteral("Enabled"), form);
    m_status = new QLabel(form);
    m_status->setWordWrap(true);
    layout->addRow(QStringLiteral("Sync mode"), m_mode);
    layout->addRow(QStringLiteral("Conflicts"), m_policy);
    layout->addRow(QString(), m_enabled);
    layout->addRow(QStringLiteral("Status"), m_status);
    m_stack->addWidget(form);

    auto onEdit = [this] { if (!m_loading) emitChanges(); };
    connect(m_mode, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_policy, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_enabled, &QCheckBox::toggled, this, onEdit);
}

void PatchbayInspector::setSelectedMapping(const QString &mappingId,
                                           const QJsonObject &json,
                                           RouteStatus status,
                                           const QString &categoryName)
{
    m_mappingId = mappingId;
    if (mappingId.isEmpty()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_loading = true;
    m_mode->setCurrentText(json.value(QLatin1String("mode")).toString());
    m_policy->setCurrentText(
        json.value(QLatin1String("conflictPolicy")).toString());
    m_enabled->setChecked(json.value(QLatin1String("enabled")).toBool(true));
    m_status->setText(statusText(status, categoryName));
    m_loading = false;
    m_stack->setCurrentIndex(1);
}

void PatchbayInspector::emitChanges()
{
    QJsonObject changes;
    changes[QLatin1String("mode")] = m_mode->currentText();
    changes[QLatin1String("conflictPolicy")] = m_policy->currentText();
    changes[QLatin1String("enabled")] = m_enabled->isChecked();
    emit mappingEdited(m_mappingId, changes);
}

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 3: Page header**

```cpp
// src/app/patchbay/patchbaypage.h
#pragma once

#include <QWidget>

#include "patchbaymodel.h"

class Profile;
namespace WildPalms::Runtime {
class AccountController;
class PalmRuntime;
}

namespace WildPalms::AppPatchbay {

class SyncPatchbayView;
class PatchbayInspector;

/// Container gluing PatchbayModel ↔ Profile / AccountController /
/// PalmRuntime, with the graph view + inspector side panel. Persistence is
/// write-through (the patchbay is the living editor, spec §1): every model
/// mutation lands in Profile immediately and hot-reloads PalmRuntime when
/// no run is active.
class PatchbayPage : public QWidget {
    Q_OBJECT
public:
    PatchbayPage(Profile *profile,
                 WildPalms::Runtime::AccountController *accounts,
                 WildPalms::Runtime::PalmRuntime *palmRuntime,
                 QWidget *parent = nullptr);
    ~PatchbayPage() override;

    PatchbayModel *model() const { return m_model; }
    SyncPatchbayView *view() const { return m_view; }

private slots:
    void refreshInputs();    ///< re-gather Inputs from the three sources
    void onWireSelected(const QString &mappingId);
    void onAddCategoryRequested(const QString &domain, const QPointF &scenePos);

private:
    PatchbayModel::Inputs gatherInputs() const;

    Profile *m_profile;                                   // borrowed
    WildPalms::Runtime::AccountController *m_accounts;    // borrowed
    WildPalms::Runtime::PalmRuntime *m_runtime;           // borrowed
    PatchbayModel *m_model = nullptr;
    SyncPatchbayView *m_view = nullptr;
    PatchbayInspector *m_inspector = nullptr;
    bool m_deviceConnected = false;   ///< tracked from PalmRuntime signals
};

} // namespace WildPalms::AppPatchbay
```

- [ ] **Step 4: Page implementation**

```cpp
// src/app/patchbay/patchbaypage.cpp
#include "patchbaypage.h"

#include <QHBoxLayout>

#include "patchbayinspector.h"
#include "syncpatchbayview.h"

#include "profile.h"
#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "plugins/pimplugin.h"

namespace WildPalms::AppPatchbay {

PatchbayPage::PatchbayPage(Profile *profile,
                           WildPalms::Runtime::AccountController *accounts,
                           WildPalms::Runtime::PalmRuntime *palmRuntime,
                           QWidget *parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_accounts(accounts)
    , m_runtime(palmRuntime)
{
    m_model = new PatchbayModel(this);
    m_view = new SyncPatchbayView(this);
    m_inspector = new PatchbayInspector(this);
    m_inspector->setFixedWidth(260);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_inspector);

    // model → persistence (write-through, spec §8/§11)
    connect(m_model, &PatchbayModel::mappingsChanged, this,
            [this](const QJsonArray &rows) {
                m_profile->setSyncMappingsJson(rows);
                m_profile->save();
                if (m_runtime && !m_runtime->isRunning())
                    m_runtime->reloadMappings(rows);
            });
    connect(m_model, &PatchbayModel::desiredCategoriesChanged, this,
            [this](const QString &dbName, const QStringList &names) {
                m_profile->setDesiredCategoryNames(dbName, names);
            });

    // sources → model (rebuild triggers, spec §8)
    if (m_accounts) {
        connect(m_accounts,
                &WildPalms::Runtime::AccountController::providersChanged,
                this, &PatchbayPage::refreshInputs);
        connect(m_accounts,
                &WildPalms::Runtime::AccountController::connectStateChanged,
                this, &PatchbayPage::refreshInputs);
        connect(m_accounts,
                &WildPalms::Runtime::AccountController::accountsReady,
                this, &PatchbayPage::refreshInputs);
    }
    if (m_runtime) {
        connect(m_runtime, &WildPalms::Runtime::PalmRuntime::routeStatusesChanged,
                this, &PatchbayPage::refreshInputs);
        connect(m_runtime, &WildPalms::Runtime::PalmRuntime::deviceConnected,
                this, [this] { m_deviceConnected = true; refreshInputs(); });
        connect(m_runtime, &WildPalms::Runtime::PalmRuntime::deviceDisconnected,
                this, [this] { m_deviceConnected = false; refreshInputs(); });
    }

    // view ↔ inspector
    connect(m_view, &SyncPatchbayView::wireSelected,
            this, &PatchbayPage::onWireSelected);
    connect(m_view, &SyncPatchbayView::addCategoryRequested,
            this, &PatchbayPage::onAddCategoryRequested);
    connect(m_inspector, &PatchbayInspector::mappingEdited, this,
            [this](const QString &id, const QJsonObject &changes) {
                m_model->updateMapping(id, changes);
            });

    refreshInputs();
    m_view->setModel(m_model);
}

PatchbayPage::~PatchbayPage()
{
    // Sever source connections explicitly: AccountController teardown has
    // bitten before (see 1be66a3 — provider signals into half-destroyed
    // widgets). Borrowed pointers must not be dereferenced after this.
    if (m_accounts) disconnect(m_accounts, nullptr, this, nullptr);
    if (m_runtime)  disconnect(m_runtime,  nullptr, this, nullptr);
}

PatchbayModel::Inputs PatchbayPage::gatherInputs() const
{
    PatchbayModel::Inputs in;
    in.mappings = m_profile->syncMappingsJson();

    if (m_runtime) {
        for (auto *plugin : m_runtime->conduits()) {
            ConduitFacts f;
            f.conduitId = plugin->conduitId();
            f.domain = plugin->domain().toString();
            f.dbName = plugin->primaryDbName();
            f.displayName = plugin->conduitDisplayName();
            f.matchesCollection =
                [plugin](const Kalburator::Sync::CollectionInfo &c) {
                    return plugin->matchesCollection(c);
                };
            in.conduits << f;
            in.slotSnapshot.insert(f.dbName,
                                   m_profile->categorySlotNames(f.dbName));
            in.desiredCategories.insert(
                f.dbName, m_profile->desiredCategoryNames(f.dbName));
        }
        in.routeStatuses = m_runtime->routeStatuses();
        in.deviceConnected = m_deviceConnected;
        in.deviceName = m_profile->name();
    }

    if (m_accounts) {
        for (auto *provider : m_accounts->providers()) {
            PatchbayModel::ProviderEntry e;
            e.providerId = provider->id();
            e.displayName = provider->displayName();
            const auto state = m_accounts->stateFor(e.providerId);
            if (state == WildPalms::Runtime::AccountController::
                             ConnectionState::Connecting)
                e.busyText = QStringLiteral("Connecting…");
            else if (state == WildPalms::Runtime::AccountController::
                                  ConnectionState::Error)
                e.busyText = QStringLiteral("Error: %1")
                                 .arg(m_accounts->errorFor(e.providerId));
            else
                e.collections = m_accounts->collectionsFor(e.providerId);
            in.providers << e;
        }
    }
    return in;
}

void PatchbayPage::refreshInputs()
{
    m_model->setInputs(gatherInputs());
}

void PatchbayPage::onWireSelected(const QString &mappingId)
{
    if (mappingId.isEmpty()) {
        m_inspector->setSelectedMapping({}, {}, {}, {});
        return;
    }
    const QJsonObject row = m_model->mappingById(mappingId);
    // category name for the status text, if this is a category route
    QString category;
    const QString srcCal =
        row.value(QLatin1String("sourceCalendar")).toString();
    const int nameAt = srcCal.indexOf(QLatin1String("/name:"));
    if (nameAt > 0)
        category = srcCal.mid(nameAt + 6);
    m_inspector->setSelectedMapping(mappingId, row,
                                    m_model->statusFor(mappingId), category);
}

void PatchbayPage::onAddCategoryRequested(const QString &domain,
                                          const QPointF &scenePos)
{
    Q_UNUSED(scenePos);   // Task 15 replaces this with the inline editor
}

} // namespace WildPalms::AppPatchbay
```

Two flagged refinements for the implementer (resolve while coding, both are
two-liners): (1) `IProvider` display-name/id accessor names — copy whatever
`SyncMappingsPage::reloadGraph()` (`src/app/mapping/syncmappingspage.cpp`) calls on
providers, it assembles the identical `ProviderEntry` data; (2) device-connected
state — check `palmruntime.h` for an accessor; if absent, keep a bool member updated
from `deviceConnected`/`deviceDisconnected` signals and use it in `gatherInputs()`.

- [ ] **Step 5: Build green, commit**

```bash
git add src/app/patchbay
git commit -m "feat(patchbay): PatchbayPage + inspector — write-through persistence, rebuild triggers, status story"
```

---

### Task 15: Inline category editor

Clicking the "+ category…" ghost row opens a `QLineEdit` overlaid on the row
(`QGraphicsProxyWidget`); Enter commits via `PatchbayModel::addCategory`, Esc cancels.
A context-menu "Remove category" on category ports calls `removeCategory` (refusal —
wires still attached — surfaces as a tooltip-style message).

**Files:**
- Modify: `src/app/patchbay/syncpatchbayview.h/.cpp`
- Modify: `tests/runtime/tst_patchbay_view.cpp`

- [ ] **Step 1: Failing test**

```cpp
    void inlineCategoryEditorCommits();
```

```cpp
void TstPatchbayView::inlineCategoryEditorCommits()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    view.openCategoryEditorForTest("calendar");
    QVERIFY(view.categoryEditorVisible());
    view.commitCategoryEditorForTest("Offsite");

    bool found = false;
    for (const auto &n : model.nodes()) {
        if (n.id != "hub") continue;
        for (const auto &b : n.bands)
            for (const auto &p : b.ports)
                if (p.id == "cat:calendar/Offsite") found = true;
    }
    QVERIFY(found);
    QVERIFY(!view.categoryEditorVisible());
}
```

- [ ] **Step 2: Implement**

Header additions (public):

```cpp
    void openCategoryEditor(const QString &domain);
    // test seams
    void openCategoryEditorForTest(const QString &domain) { openCategoryEditor(domain); }
    bool categoryEditorVisible() const;
    void commitCategoryEditorForTest(const QString &text);
```

private members:

```cpp
    QGraphicsProxyWidget *m_categoryEditor = nullptr;
    QString m_categoryEditorDomain;
```

Implementation (`#include <QGraphicsProxyWidget>`, `#include <QLineEdit>`):

```cpp
void SyncPatchbayView::openCategoryEditor(const QString &domain)
{
    if (m_categoryEditor) {
        delete m_categoryEditor;
        m_categoryEditor = nullptr;
    }
    auto *hub = m_nodeItems.value(QStringLiteral("hub"));
    if (!hub || !m_model)
        return;
    m_categoryEditorDomain = domain;

    auto *edit = new QLineEdit;
    edit->setPlaceholderText(QStringLiteral("Category name"));
    edit->setFixedWidth(int(PatchNodeItem::kWidth) - 24);
    m_categoryEditor = m_scene->addWidget(edit);
    const QPointF rowCenter =
        hub->portRowCenter(QStringLiteral("add:%1").arg(domain));
    m_categoryEditor->setPos(
        hub->mapToScene(rowCenter - QPointF(edit->width() / 2.0, 11.0)));
    m_categoryEditor->setZValue(20.0);
    edit->setFocus();

    connect(edit, &QLineEdit::returnPressed, this, [this, edit] {
        const QString name = edit->text().trimmed();
        const QString domain = m_categoryEditorDomain;
        delete m_categoryEditor;          // before addCategory: rebuild()
        m_categoryEditor = nullptr;       // destroys the scene anyway
        if (!name.isEmpty())
            m_model->addCategory(domain, name);
    });
    connect(edit, &QLineEdit::editingFinished, this, [this] {
        // focus loss / Esc without commit
        if (m_categoryEditor) {
            delete m_categoryEditor;
            m_categoryEditor = nullptr;
        }
    });
}

bool SyncPatchbayView::categoryEditorVisible() const
{
    return m_categoryEditor != nullptr;
}

void SyncPatchbayView::commitCategoryEditorForTest(const QString &text)
{
    if (!m_categoryEditor)
        return;
    auto *edit = qobject_cast<QLineEdit *>(m_categoryEditor->widget());
    edit->setText(text);
    emit edit->returnPressed();
}
```

In `rebuild()`, drop any open editor first (the scene teardown deletes the proxy):
set `m_categoryEditor = nullptr;` right after the clear-graph block. And in
`PatchbayPage::onAddCategoryRequested`, replace the stub body with:

```cpp
    Q_UNUSED(scenePos);
    m_view->openCategoryEditor(domain);
```

Context-menu removal (view, `contextMenuEvent` override):

```cpp
void SyncPatchbayView::contextMenuEvent(QContextMenuEvent *event)
{
    const QPointF scenePos = mapToScene(event->pos());
    auto *hub = m_nodeItems.value(QStringLiteral("hub"));
    if (hub && m_model) {
        const QString port = hub->portAt(hub->mapFromScene(scenePos));
        if (port.startsWith(QLatin1String("cat:"))) {
            const QString rest = port.mid(4);
            const QString domain = rest.left(rest.indexOf(QLatin1Char('/')));
            const QString name = rest.mid(rest.indexOf(QLatin1Char('/')) + 1);
            QMenu menu(this);
            QAction *remove =
                menu.addAction(QStringLiteral("Remove category \"%1\"").arg(name));
            if (menu.exec(event->globalPos()) == remove) {
                if (!m_model->removeCategory(domain, name))
                    QToolTip::showText(event->globalPos(),
                        QStringLiteral("Remove its wires first."), this);
            }
            return;
        }
    }
    QGraphicsView::contextMenuEvent(event);
}
```

(`#include <QMenu>`, `#include <QToolTip>`, `#include <QContextMenuEvent>`; declare the
override in the header's protected section.)

- [ ] **Step 3: Run** — `ctest -R tst_patchbay_view`, all pass.

- [ ] **Step 4: Commit**

```bash
git add src/app/patchbay tests/runtime
git commit -m "feat(patchbay): inline category editor + context-menu category removal"
```

---

### Task 16: Host in KF6MainWindow + full-suite sweep

**Files:**
- Modify: `src/kf6/kf6mainwindow.h` (member + include)
- Modify: `src/kf6/kf6mainwindow.cpp` (page creation in profile-load path)
- Modify: `CLAUDE.md` (current-status section)

- [ ] **Step 1: Add the page to the central KPageWidget**

In `kf6mainwindow.h`: forward-declare `namespace WildPalms::AppPatchbay { class PatchbayPage; }`,
add members:

```cpp
    WildPalms::AppPatchbay::PatchbayPage *m_patchbayPage = nullptr;
    KPageWidgetItem *m_patchbayPageItem = nullptr;
```

In `kf6mainwindow.cpp`, locate where the profile-scoped objects come up — the point in
the profile-load path where `m_accountController` and `m_palmRuntime` both exist and
plugin pages are (re)built into `m_pageWidget` (follow how the existing plugin pages
and `m_dashboardWidget` are wired in `createCentralLayout()` and `loadProfile`). Add:

```cpp
    // Sync Patchbay (Part 1): the three-tier mapping editor as the first
    // central page. Replaces nothing yet — the F.3 Settings page retires in
    // Part 2 (spec §11).
    if (m_patchbayPageItem) {
        m_pageWidget->removePage(m_patchbayPageItem);   // deletes the page widget
        m_patchbayPageItem = nullptr;
        m_patchbayPage = nullptr;
    }
    m_patchbayPage = new WildPalms::AppPatchbay::PatchbayPage(
        m_currentProfile.get(), m_accountController.get(),
        m_palmRuntime.get(), this);
    m_patchbayPageItem =
        new KPageWidgetItem(m_patchbayPage, i18n("Patchbay"));
    m_patchbayPageItem->setIcon(
        QIcon::fromTheme(QStringLiteral("network-connect")));
    m_pageWidget->addPage(m_patchbayPageItem);
    m_pageWidget->setCurrentPage(m_patchbayPageItem);
```

`addPage` appends; making the patchbay the FIRST page (and the dashboard strip
rearrangement) is Part 2 consolidation work — for Part 1, `setCurrentPage` makes it
the default-visible page, which is what matters. **Important
teardown note:** the patchbay page must be destroyed BEFORE the AccountController it
borrows when profiles switch (the 1be66a3 lesson) — `removePage` before the controller
is replaced, which the code above does if placed at the top of the rebuild block.
`#include "app/patchbay/patchbaypage.h"` and `#include <KPageWidgetItem>` as needed.

- [ ] **Step 2: Build + run the app smoke check**

```bash
cmake --build build -j8 && ctest --test-dir build -j8
```
Expected: **all tests pass** (123 baseline + tst_patchbay_model + tst_patchbay_view).
Then launch the app (`./build/bin/wildpalms` or the existing run path), load the
wizard-created profile, and confirm: Patchbay page appears, hub shows 4 bands, the
7 mappings render as wires, dragging `dom:todo` → a VTODO collection creates a row.

- [ ] **Step 3: Update CLAUDE.md current-status**

Add a short subsection under "Current branch and state": Sync Patchbay Part 1 landed
(spec + plan paths, Graffodil v0.2.0 pin, what works, Part 2 pending: live animation,
read-only guard, F.3 retirement, dashboard strip).

- [ ] **Step 4: Final commit**

```bash
git add src/kf6 CLAUDE.md
git commit -m "feat(patchbay): host PatchbayPage as central Patchbay page (Part 1 complete)"
```

---

## Verification sweep (after all tasks)

- [ ] Graffodil: `ctest --test-dir build -j8` green, `v0.2.0` tag pushed.
- [ ] WildPalms: full `ctest --test-dir build -j8` green (no regressions in the 123).
- [ ] Manual: wizard profile renders 4 hub↔palm domains + account wires; ghost node
      appears if an account is removed from the profile while rows remain.

## Deferred to Part 2 (do NOT do here)

Live run animation (dash ticker on `setDashOffset`), run-result beads from
`mappingSync*` signals (Part 1 beads carry only the ✗ broken glyph), bead hover
expansion to text capsules (spec §6.3), read-only-during-sync guard, retiring
`src/app/mapping/` + SettingsDialog page + `tst_syncmappingsgraphview`, dashboard →
summary strip, hub record counts/baseline footers, the "Add account…" ghost node
(spec §5.3 — accounts are still added via the Accounts page/wizard in Part 1), wire
context-menu delete (Delete key only in Part 1), hover rings / incompatible-target
dimming polish, patchbay-first page ordering in the main window.
