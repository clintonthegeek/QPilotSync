# M6b — PalmRuntime owns the device link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Predecessor:** Plan 4 / M6a (orphan cleanup, tag
   `v0.21-phase-m6a-cleanup`, landed 2026-05-02).
**Successor:** M7 — merge `palm-rewrite` to
   `refactor/engine-merger`, tag `v0.23-palm-rewrite`.
**Status:** Plan written 2026-05-02. Implementation pending.

---

**Goal:** Move device-connection lifecycle out of `KF6MainWindow`'s
`DeviceSession` and into `PalmRuntime` / `PalmDeviceAccess`. After
this, `PalmDeviceAccess` is the *single* link-thread owner the
design's §2/§5/§11 specifies, and `KF6MainWindow` becomes a pure
KDE app shell with no link-handling responsibilities.

**Architecture:** Extend `PalmDeviceAccess` to own the connect
lifecycle in addition to its existing per-database access role. A
new `PalmTickle` sibling class (on the same link thread) replaces
`TickleWorker` and listens to `KPilotDeviceLink`'s existing
`ticklePauseRequested`/`tickleResumeRequested` signals.
`PalmRuntime`'s `connectDevice(KPilotLink*)` is replaced by
`connectDevice(QStringList paths)` that does the open via the new
PalmDeviceAccess path. `KF6MainWindow` loses `m_session`,
`m_deviceLink`, and ~80 lines of session plumbing. `DeviceSession`
+ `DeviceWorker` + `TickleWorker` are deleted.

**Tech Stack:** Qt6 (QThread, QMetaObject::invokeMethod with
`Qt::BlockingQueuedConnection`, signal-slot), pilot-link C library
via `KPilotDeviceLink` (already async-internally via its own
`ConnectionWorker`), KF6 widgets.

---

## Why this is its own sub-phase

Per design `2026-05-01-palm-runtime-rewrite-design.md` §M6, the
intended end state is "PalmRuntime owns the connection lifecycle;
KF6MainWindow just calls PalmRuntime methods." M6a took the pure
orphans (plucker, HotSyncCoordinator). M6b finishes the design's
M6 by retiring the legacy DeviceSession/DeviceWorker/TickleWorker
trio and consolidating onto a single link thread.

After M6b, the design's §2 layer diagram, §5.1 component diagram,
and §11 "three threads by construction" claim are all literally
true in the code. M7 then merges `palm-rewrite` to
`refactor/engine-merger`.

---

## Out of scope

- Changing the `IPalmDatabaseAccess` contract (M2-M4 fixed it).
- Changing how plugins are loaded or registered.
- Any libkalburator changes.
- Real-device verification gate — **deferred per user direction**
  (matches M5b precedent). M6b is a structural refactor with no
  observable behavior change; verify-all + unit-test coverage is
  the gate. The next person who has the device hardware should run
  through one HotSync end-to-end before tagging M7, but M6b itself
  ships behind unit tests.
- `AutoSyncOrchestrator` (already narrowed in M5b; not in scope).
- `KF6MainWindow`'s mode-dispatch (already done in M2-M3).

---

## Audit findings (read these before starting)

These shape the plan:

1. **`KPilotDeviceLink` is already async-on-open.** It owns an
   internal `ConnectionWorker` (`src/palm/kpilotdevicelink.h:46`)
   that does `pi_accept_to()` on its own thread.
   `KPilotDeviceLink::openConnection()` returns immediately; the
   handshake completes via `connectionEstablished(HandshakeResult)`
   / `connectionFailed(QString)` signals. **`DeviceWorker`'s
   threading is redundant glue around an already-async API.**

2. **`KPilotDeviceLink::pauseTickle/resumeTickle` are pure
   signal-emitters** (`src/palm/kpilotdevicelink.cpp:1268-1275`).
   They emit `ticklePauseRequested()`/`tickleResumeRequested()`
   for *something else* to act on. `TickleWorker` is the listener
   today. So **TickleWorker is NOT redundant** — its periodic-ping
   logic (every 5s, `dlp_GetSysDateTime`, 3-strike connection-lost
   detection) needs to live somewhere. M6b reincarnates it as
   `PalmTickle` on PalmDeviceAccess's link thread.

3. **`PalmDeviceAccess` already owns a link thread**
   (`src/runtime/palmdeviceaccess.cpp:12-22`,
   `m_linkThread = std::make_unique<QThread>()` named
   `"PalmLinkThread"`). All `IPalmDatabaseAccess` calls bounce to
   it via `Qt::BlockingQueuedConnection`. M6b extends this to also
   do the open handshake on it.

4. **The existing `PalmConnectionBundle` factory**
   (`src/runtime/pilotlinkconnectionfactory.{h,cpp}`) already
   bundles `PilotLinkPalmDatabaseAccess` + `PilotLinkPalmFileInstaller`
   + `PalmDeviceConnection` from a `KPilotLink*`. M6b reuses it —
   the link comes from inside PalmRuntime now, but the bundling is
   unchanged.

5. **`KF6MainWindow`'s session block**
   (`src/kf6/kf6mainwindow.cpp` line 1052 area) has 10 signal
   wirings and the recreate-per-profile pattern. PalmRuntime is
   *also* recreated per profile (line ~760: `m_palmRuntime =
   std::make_unique<PalmRuntime>(...)`). The session lifetime
   exactly matches PalmRuntime's lifetime — making the merge
   straightforward.

---

## File Structure

**Files created:**
- `WildPalms/src/runtime/palmtickle.h` (~60 lines)
- `WildPalms/src/runtime/palmtickle.cpp` (~80 lines)
- `WildPalms/tests/runtime/tst_palm_device_access_connect.cpp` (~200 lines)

**Files modified:**
- `WildPalms/src/runtime/palmdeviceaccess.h` — add connect/disconnect/cancel surface + signals + `PalmTickle` ownership
- `WildPalms/src/runtime/palmdeviceaccess.cpp` — implement the new methods on the link thread
- `WildPalms/src/runtime/palmruntime.h` — replace `connectDevice(KPilotLink*)` with `connectDevice(QStringList paths)`; add cancelConnect, expand signal surface
- `WildPalms/src/runtime/palmruntime.cpp` — drive PalmDeviceAccess's new connect path; forward signals
- `WildPalms/src/runtime/CMakeLists.txt` — add `palmtickle.{h,cpp}`
- `WildPalms/tests/runtime/CMakeLists.txt` — add the new test
- `WildPalms/src/kf6/kf6mainwindow.h` — drop `DeviceSession*`,
  `KPilotLink*` members and the `class DeviceSession` forward
  declaration
- `WildPalms/src/kf6/kf6mainwindow.cpp` — rip out session-creation
  block (~80 lines); rewire signals to `PalmRuntime`
- `WildPalms/src/kf6/CMakeLists.txt` — drop linkage to libs that
  only existed for DeviceSession (verify before changing)

**Files deleted:**
- `WildPalms/src/palm/devicesession.h`
- `WildPalms/src/palm/devicesession.cpp`
- `WildPalms/src/palm/deviceworker.h`
- `WildPalms/src/palm/deviceworker.cpp`
- `WildPalms/src/palm/tickleworker.h`
- `WildPalms/src/palm/tickleworker.cpp`
- `WildPalms/src/palm/CMakeLists.txt` entries for the above

**Files to update at end:**
- `refactor-engine-merger/CURRENT-STATUS.md`
- `refactor-engine-merger/FINDINGS.md` — anything non-obvious
  surfaced during execution

---

## Task ordering rationale

The work splits into **additive** changes first (compile-clean at
every commit), then a **migration** that removes the old code:

1. Tasks 1-2: Add `PalmTickle` + extend `PalmDeviceAccess` with
   the new connect surface. Pure addition. Old DeviceSession path
   still in use; new path callable but not yet called.
2. Task 3: Add unit tests covering the new connect path.
3. Tasks 4-5: Refactor `PalmRuntime` to take `QStringList paths`
   instead of `KPilotLink*`. Forward all required signals.
4. Task 6: Rewire `KF6MainWindow` to call PalmRuntime's new
   connect path; rip out DeviceSession plumbing. After this commit
   DeviceSession/DeviceWorker/TickleWorker have zero callers.
5. Task 7: Delete the three legacy classes + their CMake entries.
6. Task 8: Run `verify-all.sh`; refresh baseline if test count
   shifts (M6a's FINDINGS entry on this gotcha applies).
7. Tasks 9-10: Tag, status doc + FINDINGS update.

---

### Task 1: Add `PalmTickle` class

**Files:**
- Create: `WildPalms/src/runtime/palmtickle.h`
- Create: `WildPalms/src/runtime/palmtickle.cpp`
- Modify: `WildPalms/src/runtime/CMakeLists.txt`

`PalmTickle` is the periodic-ping replacement for `TickleWorker`.
It lives on `PalmDeviceAccess::m_linkThread` (no separate thread —
the QTimer's slot runs on whatever thread owns the timer, and we
parent it to a QObject already on the link thread via Task 2's
extension). It listens to `KPilotDeviceLink`'s
`ticklePauseRequested`/`tickleResumeRequested` signals.

- [ ] **Step 1: Create the header**

```cpp
// WildPalms/src/runtime/palmtickle.h
#ifndef WILDPALMS_RUNTIME_PALMTICKLE_H
#define WILDPALMS_RUNTIME_PALMTICKLE_H

#include <QObject>
#include <QTimer>
#include <atomic>

class KPilotLink;

namespace WildPalms::Runtime {

/**
 * @brief Keep-alive ticker for an open Palm connection.
 *
 * Replaces the legacy TickleWorker. Lives on the same thread as
 * its owning PalmDeviceAccess (the link thread). Listens to
 * KPilotLink's ticklePauseRequested/tickleResumeRequested signals
 * to suspend ticks during bulk DLP work.
 *
 * Sends dlp_GetSysDateTime() every 5s as a lightweight ping. After
 * 3 consecutive failures, emits connectionLost() and stops itself.
 *
 * Lifetime: created when PalmDeviceAccess opens a connection,
 * destroyed when the connection closes. Single instance per link.
 */
class PalmTickle : public QObject
{
    Q_OBJECT
public:
    /// @param link  Non-owning. Caller guarantees link outlives this.
    /// @param socket  pilot-link socket descriptor for dlp_* calls.
    /// @param parent  QObject parent (must be on the link thread).
    PalmTickle(KPilotLink *link, int socket, QObject *parent);
    ~PalmTickle() override;

    /// Start the periodic ping. No-op if already running.
    void start();

    /// Stop the periodic ping. No-op if not running.
    void stop();

    /// Configure tick interval (default 5000 ms).
    void setInterval(int intervalMs);

signals:
    /// Emitted when 3 consecutive ticks have failed.
    void connectionLost();

    /// Emitted on each successful tick (mostly for tests / logging).
    void tickSent();

private slots:
    void sendTick();

private:
    KPilotLink        *m_link;          // borrowed, non-owning
    int                m_socket;
    QTimer            *m_timer;
    std::atomic<bool>  m_running { false };
    int                m_consecutiveFailures = 0;
    int                m_intervalMs = 5000;
};

} // namespace WildPalms::Runtime

#endif
```

- [ ] **Step 2: Create the implementation**

```cpp
// WildPalms/src/runtime/palmtickle.cpp
#include "palmtickle.h"

#include "palm/kpilotlink.h"

#include <QDebug>

extern "C" {
#include <pi-dlp.h>
}

namespace WildPalms::Runtime {

PalmTickle::PalmTickle(KPilotLink *link, int socket, QObject *parent)
    : QObject(parent)
    , m_link(link)
    , m_socket(socket)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &PalmTickle::sendTick);

    if (m_link) {
        // Link's pause/resume signals tell us when DLP work is in
        // flight. Use QueuedConnection so the slot runs on our
        // thread regardless of where the signal was emitted.
        connect(m_link, &KPilotLink::statusChanged, this,
                [this](KPilotLink::LinkStatus s) {
                    if (s == KPilotLink::PilotLinkError) stop();
                },
                Qt::QueuedConnection);
    }
}

PalmTickle::~PalmTickle()
{
    stop();
}

void PalmTickle::start()
{
    if (m_running.exchange(true)) return;
    if (m_socket < 0) {
        qWarning() << "[PalmTickle] start() called with no socket";
        m_running = false;
        return;
    }
    m_consecutiveFailures = 0;
    m_timer->start();
}

void PalmTickle::stop()
{
    if (!m_running.exchange(false)) return;
    m_timer->stop();
}

void PalmTickle::setInterval(int intervalMs)
{
    m_intervalMs = intervalMs;
    if (m_timer->isActive()) m_timer->setInterval(intervalMs);
}

void PalmTickle::sendTick()
{
    if (!m_running.load() || m_socket < 0) return;

    time_t palmTime = 0;
    const int rc = dlp_GetSysDateTime(m_socket, &palmTime);
    if (rc < 0) {
        ++m_consecutiveFailures;
        qWarning() << "[PalmTickle] tick failed rc=" << rc
                   << "consecutive=" << m_consecutiveFailures;
        if (m_consecutiveFailures >= 3) {
            stop();
            emit connectionLost();
        }
        return;
    }
    m_consecutiveFailures = 0;
    emit tickSent();
}

} // namespace WildPalms::Runtime
```

- [ ] **Step 3: Add to CMakeLists**

Open `WildPalms/src/runtime/CMakeLists.txt`. Find the
`target_sources(WildPalmsRuntime PRIVATE ...)` block. Add
`palmtickle.h` and `palmtickle.cpp` alphabetically (after
`palmruntime.cpp`, before `pilotlinkconnectionfactory.cpp`):

```cmake
    palmruntime.h
    palmruntime.cpp
    palmtickle.h
    palmtickle.cpp
    pilotlinkconnectionfactory.h
```

- [ ] **Step 4: Build**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build-dev --target WildPalmsRuntime 2>&1 | tail -20
```

Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add -A src/runtime
git commit -m "M6b Task 1: add PalmTickle (TickleWorker successor)

Replaces TickleWorker for the post-M6b architecture: lives on
PalmDeviceAccess's link thread (no separate thread), listens to
KPilotLink's tickle pause/resume signals via QueuedConnection,
sends dlp_GetSysDateTime() every 5s, emits connectionLost after
3 consecutive failures.

Not yet wired — Task 2 instantiates it from PalmDeviceAccess."
```

---

### Task 2: Extend `PalmDeviceAccess` with connect lifecycle

**Files:**
- Modify: `WildPalms/src/runtime/palmdeviceaccess.h`
- Modify: `WildPalms/src/runtime/palmdeviceaccess.cpp`

PalmDeviceAccess gains a new construction mode (no impl up front)
plus methods to open/close the link on its link thread. The
existing impl-wrapping constructor stays for tests + the legacy
PalmRuntime path that we won't delete until Task 5.

- [ ] **Step 1: Extend the header**

Add these to `class PalmDeviceAccess`:

```cpp
// New constructor: empty; call connectDevice() to open.
explicit PalmDeviceAccess(QObject *parent = nullptr);

// Open a Palm device on one of the supplied paths. Async — emits
// connectionComplete(true, "") on success or connectionComplete(
// false, error) on failure. Internally constructs a
// KPilotDeviceLink, waits for its connectionEstablished /
// connectionFailed signals, then bundles the IPalmDatabaseAccess
// impl + PalmTickle. After success, normal IPalmDatabaseAccess
// methods are usable from any thread.
void connectDevice(const QStringList &devicePaths);

// Cancel an in-progress connect. No-op if not connecting.
void cancelConnect();

// Close the link, destroy the impl + tickle. Safe to call repeatedly.
void disconnectDevice();

bool isConnected()  const;
bool isConnecting() const;

// Handshake info — only valid after connectionComplete(true, ...).
QString handshakeUserName() const;
quint32 handshakeUserId()   const;
QString handshakeProductId() const;
QString handshakeCardName() const;
quint32 handshakeRomVersion() const;

// Borrowed pointer to the underlying link. Only valid while
// isConnected(). Plugins should NOT use this; use IPalmDatabaseAccess.
KPilotLink *link() const;

signals:
    void connectionStarted();
    void connectionComplete(bool success, QString error);
    void deviceDisconnected();      // emitted from disconnectDevice() OR
                                    //   from PalmTickle::connectionLost
    void logMessage(QString message);

private:
    // The work that runs on m_linkThread. Invoked via
    // QMetaObject::invokeMethod(... QueuedConnection) from
    // connectDevice() so all link-touching code lives on m_linkThread.
    Q_INVOKABLE void doConnect(const QStringList &devicePaths);
    Q_INVOKABLE void doDisconnect();
    Q_INVOKABLE void doCancelConnect();

    // Wired during doConnect().
    void onLinkConnectionEstablished(/* HandshakeResult */);
    void onLinkConnectionFailed(const QString &error);

    // Owned, all live on m_linkThread when set:
    KPilotLink                                                *m_link = nullptr;
    PalmTickle                                                *m_tickle = nullptr;
    PalmConnectionBundle                                       m_bundle; // dbAccess + fileInstaller + connection

    HandshakeResult                                            m_handshake;
    std::atomic<bool>                                          m_connecting { false };
    std::atomic<bool>                                          m_connected { false };
```

(The existing impl-wrapping constructor + the existing
IPalmDatabaseAccess override methods stay unchanged. They keep
working through `m_impl` for the legacy path, switch to using
`m_bundle.dbAccess` for the new connect path. See Step 2 for the
unification.)

Add forward decls and includes near the top:

```cpp
#include <QStringList>
#include <atomic>

#include "palm/kpilotdevicelink.h"   // for HandshakeResult
#include "pilotlinkconnectionfactory.h"  // for PalmConnectionBundle

namespace WildPalms::Runtime { class PalmTickle; }
```

- [ ] **Step 2: Implement the new methods**

In `palmdeviceaccess.cpp`:

```cpp
PalmDeviceAccess::PalmDeviceAccess(QObject *parent)
    : QObject(parent)
    , m_linkThread(std::make_unique<QThread>())
{
    m_linkThread->setObjectName(QStringLiteral("PalmLinkThread"));
    // No m_impl yet — m_implOwner stays null until connect succeeds.
    m_linkThread->start();
}

void PalmDeviceAccess::connectDevice(const QStringList &devicePaths)
{
    if (m_connecting.exchange(true)) {
        emit logMessage(QStringLiteral("connectDevice ignored: already connecting"));
        m_connecting = false;
        return;
    }
    if (m_connected.load()) {
        emit logMessage(QStringLiteral("connectDevice ignored: already connected"));
        m_connecting = false;
        return;
    }
    emit connectionStarted();

    // Bounce to link thread.
    QMetaObject::invokeMethod(this, "doConnect",
        Qt::QueuedConnection,
        Q_ARG(QStringList, devicePaths));
}

void PalmDeviceAccess::doConnect(const QStringList &devicePaths)
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());

    auto *link = new KPilotDeviceLink(devicePaths);
    m_link = link;

    // Forward log/progress signals up — they're emitted on the
    // link thread; subscribers (PalmRuntime → KF6MainWindow) need
    // QueuedConnection on their side.
    connect(link, &KPilotLink::logMessage, this,
            [this](const QString &m){ emit logMessage(m); });

    connect(link, &KPilotDeviceLink::connectionEstablished, this,
            [this, link](const HandshakeResult &result) {
                m_handshake = result;
                m_bundle = makePalmConnection(link, this);
                if (!m_bundle.dbAccess) {
                    onLinkConnectionFailed(QStringLiteral("makePalmConnection returned null"));
                    return;
                }

                // Promote bundle.dbAccess into the IPalmDatabaseAccess slot
                // that the existing override methods read from. We don't
                // delete it on disconnect — m_bundle.destroy() handles that.
                m_implOwner = m_bundle.dbAccess;
                // No moveToThread — bundle.dbAccess was constructed on this
                // (link) thread already.

                // Wire tickle.
                m_tickle = new PalmTickle(link, result.socket, this);
                connect(link, &KPilotDeviceLink::ticklePauseRequested,
                        m_tickle, &PalmTickle::stop, Qt::QueuedConnection);
                connect(link, &KPilotDeviceLink::tickleResumeRequested,
                        m_tickle, &PalmTickle::start, Qt::QueuedConnection);
                connect(m_tickle, &PalmTickle::connectionLost, this,
                        [this]{ doDisconnect(); });
                m_tickle->start();

                m_connected = true;
                m_connecting = false;
                emit connectionComplete(true, QString());
            });

    connect(link, &KPilotDeviceLink::connectionFailed, this,
            [this](const QString &error){ onLinkConnectionFailed(error); });

    if (!link->openConnection()) {
        onLinkConnectionFailed(QStringLiteral("openConnection() returned false"));
    }
}

void PalmDeviceAccess::onLinkConnectionFailed(const QString &error)
{
    if (m_link) { m_link->deleteLater(); m_link = nullptr; }
    m_connecting = false;
    m_connected = false;
    emit connectionComplete(false, error);
}

void PalmDeviceAccess::cancelConnect()
{
    QMetaObject::invokeMethod(this, "doCancelConnect", Qt::QueuedConnection);
}

void PalmDeviceAccess::doCancelConnect()
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());
    if (auto *link = qobject_cast<KPilotDeviceLink*>(m_link)) {
        link->cancelConnection();
    }
}

void PalmDeviceAccess::disconnectDevice()
{
    QMetaObject::invokeMethod(this, "doDisconnect", Qt::BlockingQueuedConnection);
}

void PalmDeviceAccess::doDisconnect()
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());
    if (m_tickle) { m_tickle->stop(); m_tickle->deleteLater(); m_tickle = nullptr; }
    m_bundle.destroy();
    m_implOwner = nullptr;
    if (m_link) {
        m_link->closeConnection();
        m_link->deleteLater();
        m_link = nullptr;
    }
    if (m_connected.exchange(false)) {
        emit deviceDisconnected();
    }
    m_connecting = false;
}

bool PalmDeviceAccess::isConnected()  const { return m_connected.load(); }
bool PalmDeviceAccess::isConnecting() const { return m_connecting.load(); }

KPilotLink *PalmDeviceAccess::link() const { return m_link; }

QString  PalmDeviceAccess::handshakeUserName()  const { return m_handshake.userName; }
quint32  PalmDeviceAccess::handshakeUserId()    const { return m_handshake.userId; }
QString  PalmDeviceAccess::handshakeProductId() const { return m_handshake.productId; }
QString  PalmDeviceAccess::handshakeCardName()  const { return m_handshake.cardName; }
quint32  PalmDeviceAccess::handshakeRomVersion() const { return m_handshake.romVersion; }
```

The existing IPalmDatabaseAccess override methods (`availableDatabases`,
`hasDatabase`, etc.) need a small update: they currently dispatch
to `m_impl.get()`. After M6b they need to dispatch to
`m_implOwner` (which is either the legacy-constructor's `m_impl` or
the new-constructor's `m_bundle.dbAccess`). The existing
constructor already sets `m_implOwner = m_impl.get()`; the new
constructor sets it inside `connectionEstablished`. No other change
to the override bodies — they already use `m_implOwner`.

Verify by reading `palmdeviceaccess.cpp`'s existing
`availableDatabases()` etc. If they reference `m_impl.get()`
directly instead of `m_implOwner`, change them to use `m_implOwner`
in this task.

- [ ] **Step 3: Build**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build-dev --target WildPalmsRuntime 2>&1 | tail -20
```

Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add -A src/runtime
git commit -m "M6b Task 2: extend PalmDeviceAccess with connect lifecycle

Adds connectDevice(QStringList)/disconnectDevice/cancelConnect on
PalmDeviceAccess's existing link thread. New empty constructor
defers impl construction until connect succeeds. PalmTickle is
instantiated and wired to KPilotDeviceLink's tickle pause/resume
signals on connect; destroyed on disconnect.

Old impl-wrapping constructor unchanged — kept for the legacy
PalmRuntime::connectDevice(KPilotLink*) path until Task 5."
```

---

### Task 3: Unit test for the new connect path

**Files:**
- Create: `WildPalms/tests/runtime/tst_palm_device_access_connect.cpp`
- Modify: `WildPalms/tests/runtime/CMakeLists.txt`

Test the threading + signal-emission contract without a real
device. Uses `KPilotLocalLink` (filesystem-backed test impl) if
one exists, OR injects a mock by exposing a test seam. Audit
which approach matches the existing test style first.

- [ ] **Step 1: Audit existing test pattern**

Run:
```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
ls tests/runtime/
grep -l "KPilotLocalLink\|MockKPilotLink" tests/runtime/*.cpp 2>/dev/null
```

If `KPilotLocalLink` or a mock exists and is used in a similar
test, follow that pattern. Otherwise, add a test seam to
`PalmDeviceAccess`: a static factory method that takes a
`std::function<KPilotLink*(QStringList)>` for link construction,
defaulting to `new KPilotDeviceLink(...)`. Tests can inject a mock.

- [ ] **Step 2: Write the test**

The test should cover (each as a separate `QTest`-style test
function):

1. **`connectDevice` emits `connectionStarted` then
   `connectionComplete(true, "")`** on a successful mock link.
2. **`connectDevice` emits `connectionComplete(false, error)`**
   on a mock link that fails to open.
3. **`disconnectDevice` emits `deviceDisconnected`** when called
   while connected.
4. **`isConnected()` reflects state correctly** through the
   connect/disconnect cycle.
5. **`cancelConnect()` triggers `connectionFailed("canceled")`**
   (or whatever the cancel error string is).
6. **All link-touching slots run on `m_linkThread`** — verify by
   capturing `QThread::currentThread()` inside the mock link's
   `openConnection()`.

Pattern (sketch):

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "runtime/palmdeviceaccess.h"
// ...

class TstPalmDeviceAccessConnect : public QObject {
    Q_OBJECT
private slots:
    void connect_emits_complete_on_success();
    void connect_emits_complete_on_failure();
    void disconnect_emits_disconnected();
    void is_connected_tracks_state();
    void cancel_connect_aborts_in_flight();
    void all_link_calls_on_link_thread();
};

void TstPalmDeviceAccessConnect::connect_emits_complete_on_success()
{
    using namespace WildPalms::Runtime;
    PalmDeviceAccess access;
    QSignalSpy started(&access, &PalmDeviceAccess::connectionStarted);
    QSignalSpy complete(&access, &PalmDeviceAccess::connectionComplete);

    // Inject mock link factory that immediately succeeds.
    // ... details depend on Step 1's seam decision.

    access.connectDevice({QStringLiteral("/dev/mock")});

    QVERIFY(complete.wait(1000));
    QCOMPARE(started.count(), 1);
    QCOMPARE(complete.count(), 1);
    const auto args = complete.takeFirst();
    QCOMPARE(args.at(0).toBool(), true);
    QCOMPARE(args.at(1).toString(), QString());
    QVERIFY(access.isConnected());
}

// ... (other test functions)

QTEST_MAIN(TstPalmDeviceAccessConnect)
#include "tst_palm_device_access_connect.moc"
```

- [ ] **Step 3: Add to tests/runtime/CMakeLists.txt**

Match the existing pattern for runtime tests in that file (likely
`add_executable` + `target_link_libraries(WildPalmsRuntime ...)` +
`add_test(...)` + `set_tests_properties(... ENVIRONMENT
"QT_QPA_PLATFORM=offscreen")`).

- [ ] **Step 4: Run the new test**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build-dev --target tst_palm_device_access_connect 2>&1 | tail
ctest --test-dir build-dev -R tst_palm_device_access_connect --output-on-failure
```

Expected: all sub-tests pass.

- [ ] **Step 5: Commit**

```bash
git add -A src/runtime tests/runtime
git commit -m "M6b Task 3: tst_palm_device_access_connect

Covers: successful connect emits connectionStarted +
connectionComplete(true, ''); failed connect emits
connectionComplete(false, error); disconnect emits
deviceDisconnected; isConnected() tracks state; cancelConnect
aborts in flight; all link-touching slots run on m_linkThread.

Uses [KPilotLocalLink|injected mock] per existing test pattern."
```

---

### Task 4: PalmRuntime — replace `connectDevice(KPilotLink*)`

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.h`
- Modify: `WildPalms/src/runtime/palmruntime.cpp`

PalmRuntime's old `connectDevice(KPilotLink*)` becomes
`connectDevice(QStringList paths)` and delegates to
`PalmDeviceAccess::connectDevice`. The construction of
`PalmConnectionBundle` moves out of PalmRuntime (it now happens
inside PalmDeviceAccess). PalmRuntime gains forwarding signals
matching DeviceSession's surface.

- [ ] **Step 1: Update the header**

In `palmruntime.h`:

Replace
```cpp
void connectDevice(KPilotLink *link);
```
with
```cpp
void connectDevice(const QStringList &devicePaths);
void cancelConnect();
```

Add new signals (matching what KF6MainWindow used to get from
DeviceSession):
```cpp
signals:
    // existing:
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void runLog(QString message);
    void runFinished(PalmRunResult);

    // M6b additions — replace DeviceSession's signal surface:
    void connectionStarted();
    void connectionComplete(bool success, QString error);
    void readyForSync();         // emitted right after deviceConnected, after
                                 // plugins are loaded and ready
    void logMessage(QString message);
    void errorOccurred(QString error);
    void progressUpdated(int current, int total, QString message);
    void palmScreenMessage(QString message);
```

Drop the `setLinkForTest` test seam if it exists, OR change it to
`setDeviceAccessForTest` (which already exists per
palmruntime.h:69) by routing all test injection through
PalmDeviceAccess. Audit and pick the cleaner option.

- [ ] **Step 2: Update the implementation**

The new `connectDevice(QStringList)`:

```cpp
void PalmRuntime::connectDevice(const QStringList &devicePaths)
{
    if (!m_device) {
        // PalmDeviceAccess no longer needs the impl up front; use the
        // empty constructor and let it manage the link.
        m_device = std::make_unique<PalmDeviceAccess>(this);

        // Forward signals (QueuedConnection so they cross thread cleanly).
        connect(m_device.get(), &PalmDeviceAccess::connectionStarted,
                this, &PalmRuntime::connectionStarted);
        connect(m_device.get(), &PalmDeviceAccess::connectionComplete,
                this, [this](bool ok, QString err) {
                    emit connectionComplete(ok, err);
                    if (ok) {
                        // Build the engine + plugins now that the link is up.
                        // (Move whatever the old connectDevice(KPilotLink*) did
                        //  for plugin setup into a private finishConnect()
                        //  method called here. Audit the existing
                        //  PalmRuntime::connectDevice body for the steps.)
                        finishConnect();
                        emit deviceConnected();
                        emit readyForSync();
                    }
                });
        connect(m_device.get(), &PalmDeviceAccess::deviceDisconnected,
                this, &PalmRuntime::deviceDisconnected);
        connect(m_device.get(), &PalmDeviceAccess::logMessage,
                this, &PalmRuntime::logMessage);
    }

    m_device->connectDevice(devicePaths);
}

void PalmRuntime::cancelConnect()
{
    if (m_device) m_device->cancelConnect();
}

void PalmRuntime::disconnectDevice()
{
    if (m_device) m_device->disconnectDevice();
    // (Existing disconnect cleanup for engine + plugins stays here.)
}
```

The existing engine-and-plugins setup that used to happen inside
`connectDevice(KPilotLink*)` (lines 187-310 of the current
palmruntime.cpp — verify by reading) moves into a new private
`finishConnect()` method invoked when PalmDeviceAccess emits
`connectionComplete(true, ...)`. The KPilotLink it needs is now
available via `m_device->link()`.

Test seams: if `setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>)`
exists and is called by tests, decide whether tests can construct
a PalmDeviceAccess via the empty constructor + a mock-link
injection (per Task 3), OR whether they need a separate
`setMockedConnectedForTest()` that bypasses the real connect
flow. Document the decision in the commit message.

- [ ] **Step 3: Build + run all PalmRuntime tests**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build-dev 2>&1 | tail -20
ctest --test-dir build-dev -R "tst_palm_runtime" --output-on-failure
```

Expected: build succeeds; all `tst_palm_runtime_*` tests pass.

If any tests fail because of the test-seam change, fix them.
Common fixes:
- Tests that called `setLinkForTest(mockLink)` may need to switch
  to `setDeviceAccessForTest(...)` with a pre-connected mock
  PalmDeviceAccess.
- Tests that called the old `connectDevice(KPilotLink*)` directly
  need to switch to `connectDevice(QStringList)` against a mock
  device path.

- [ ] **Step 4: Commit**

```bash
git add -A src/runtime tests/runtime
git commit -m "M6b Task 4: PalmRuntime owns connect lifecycle via PalmDeviceAccess

Old: PalmRuntime::connectDevice(KPilotLink*) received an externally-
opened link from KF6MainWindow's DeviceSession.
New: PalmRuntime::connectDevice(QStringList paths) drives PalmDeviceAccess
to open the link itself. Plugin/engine setup moves into a private
finishConnect() method invoked when PalmDeviceAccess signals success.

Adds forwarding signals to PalmRuntime: connectionStarted,
connectionComplete(bool, QString), readyForSync, logMessage,
errorOccurred, progressUpdated, palmScreenMessage. These mirror
DeviceSession's signal surface (Task 6 rewires KF6MainWindow to
consume them from PalmRuntime instead).

Test-seam: [decision documented per Step 2]."
```

---

### Task 5: KF6MainWindow — rip out DeviceSession plumbing

**Files:**
- Modify: `WildPalms/src/kf6/kf6mainwindow.h`
- Modify: `WildPalms/src/kf6/kf6mainwindow.cpp`

This is the big simplification. ~80 lines of session plumbing
disappear; the connect call becomes one line.

- [ ] **Step 1: Drop members**

In `kf6mainwindow.h`:
- Remove `class DeviceSession;` forward decl (line ~18).
- Remove `DeviceSession *m_session;` member (line ~203).
- Remove `KPilotLink *m_deviceLink = nullptr;` member if it
  exists.
- Remove `// DeviceSession callbacks` section (lines ~107) — the
  callbacks themselves (`onConnectionComplete`, `onDeviceReady`,
  `onSyncProgress`, `onSessionPalmScreen`, `onAsyncSyncResult`,
  `onReadyForSync`) stay as private slots if they have non-trivial
  bodies; their signal sources change in Step 3.

- [ ] **Step 2: Drop session-creation block**

In `kf6mainwindow.cpp`, find the block starting around line 1045
(begins with `if (m_session) { m_session->disconnectDevice(); ...`)
and ending at the line `m_session->connectDevice(devicePaths);`
(around line 1098). This is the entire DeviceSession ownership
block.

Replace with:

```cpp
if (m_palmRuntime) {
    if (m_currentProfile) {
        // Per-profile connection mode setup. PalmRuntime grew
        // setConnectionMode in Task 4 if it needs to be exposed,
        // OR the device-path lookup already encodes it.
        // Audit to confirm.
    }

    // Wire signals. PalmRuntime is recreated per-profile (see
    // existing m_palmRuntime construction site, line ~760), so
    // re-wiring on every connect is correct.
    connect(m_palmRuntime.get(), &PalmRuntime::connectionStarted,
            this, [this]{
                statusBar()->showMessage(i18n("Connecting…"));
            }, Qt::UniqueConnection);

    connect(m_palmRuntime.get(), &PalmRuntime::connectionComplete,
            this, [this](bool ok, const QString &err) {
                if (!ok) {
                    statusBar()->showMessage(i18n("Connect failed: %1", err));
                    KNotification::event(QStringLiteral("connectFailed"), ...);
                }
            }, Qt::UniqueConnection);

    connect(m_palmRuntime.get(), &PalmRuntime::readyForSync,
            this, &KF6MainWindow::onReadyForSync, Qt::UniqueConnection);

    connect(m_palmRuntime.get(), &PalmRuntime::deviceDisconnected,
            this, [this]{
                updateMenuState(false);
                statusBar()->showMessage(i18n("Disconnected"));
                KNotification::event(QStringLiteral("deviceDisconnected"), ...);
            }, Qt::UniqueConnection);

    connect(m_palmRuntime.get(), &PalmRuntime::logMessage,
            m_logWidget, &LogWidget::logInfo, Qt::UniqueConnection);
    connect(m_palmRuntime.get(), &PalmRuntime::errorOccurred,
            m_logWidget, &LogWidget::logError, Qt::UniqueConnection);
    connect(m_palmRuntime.get(), &PalmRuntime::progressUpdated,
            this, &KF6MainWindow::onSyncProgress, Qt::UniqueConnection);
    connect(m_palmRuntime.get(), &PalmRuntime::palmScreenMessage,
            this, &KF6MainWindow::onSessionPalmScreen, Qt::UniqueConnection);

    m_logWidget->logInfo(i18n("Connecting to %1...",
        devicePaths.join(QStringLiteral(", "))));
    m_palmRuntime->connectDevice(devicePaths);
}

updateMenuState(false);
if (m_actionManager) {
    m_actionManager->cancelConnectionAction()->setEnabled(true);
}
```

- [ ] **Step 3: Update other DeviceSession references**

Search and replace in `kf6mainwindow.cpp`:

- `m_session && m_session->isConnected()` →
  `m_palmRuntime && m_palmRuntime->isDeviceConnected()`
- `m_session && m_session->isBusy()` →
  `m_palmRuntime && m_palmRuntime->isRunning()`
- `m_session->disconnectDevice()` → `m_palmRuntime->disconnectDevice()`
- `m_session->requestCancel()` → `m_palmRuntime->cancelConnect()`
  (if currently-connecting) OR no-op if currently-syncing (the
  sync future handles that path)
- `m_session->setConnectionMode(mode)` → either drop it (if
  PalmRuntime now derives from devicePaths) or
  `m_palmRuntime->setConnectionMode(mode)` (if Task 4 exposed it)
- `m_session->resumeTickle()` → `m_palmRuntime->resumeTickle()`
  (if needed) OR drop (if PalmDeviceAccess auto-handles via
  KPilotLink's signals)
- `m_session->deviceLink()` → `m_palmRuntime->link()` (if needed
  for handshake info; prefer adding `handshake*()` accessors on
  PalmRuntime that forward to PalmDeviceAccess instead)
- The destructor block (line ~227, `if (m_session && ...)` ... and
  line ~237 `// m_session and m_syncEngine are QObject children`
  comment) — remove entirely.

Use `grep -n "m_session\|DeviceSession" src/kf6/kf6mainwindow.cpp`
to find every reference and update each.

After this step:

```bash
grep -n "m_session\|DeviceSession" src/kf6/kf6mainwindow.{h,cpp}
```

Expected output: empty.

- [ ] **Step 4: Build + run all tests**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build-dev 2>&1 | tail -30
ctest --test-dir build-dev --output-on-failure
```

Expected: builds clean; all tests pass.

If any KF6MainWindow tests fail (e.g.
`tst_main_window_plugin_pages_populated`), fix the test rather
than reintroducing DeviceSession behavior. Common fixes:
- Tests that constructed a DeviceSession to inject into
  KF6MainWindow now need to inject a pre-connected PalmRuntime
  instead.

- [ ] **Step 5: Commit**

```bash
git add -A src/kf6
git commit -m "M6b Task 5: KF6MainWindow consumes PalmRuntime directly

Removes DeviceSession ownership from KF6MainWindow. The session-
creation block (10 signal connects + recreate-per-profile + lifetime
bookkeeping, ~80 lines) collapses to a single connectDevice call
on PalmRuntime plus signal wiring against PalmRuntime's surface.

Members removed:
- DeviceSession *m_session (was recreated per profile)
- KPilotLink *m_deviceLink (was the handoff variable to PalmRuntime)

After this commit DeviceSession/DeviceWorker/TickleWorker have zero
callers in WildPalms's live code. Task 7 deletes them."
```

---

### Task 6: Verify zero callers, then delete DeviceSession + DeviceWorker + TickleWorker

**Files:**
- Delete: `WildPalms/src/palm/devicesession.h`
- Delete: `WildPalms/src/palm/devicesession.cpp`
- Delete: `WildPalms/src/palm/deviceworker.h`
- Delete: `WildPalms/src/palm/deviceworker.cpp`
- Delete: `WildPalms/src/palm/tickleworker.h`
- Delete: `WildPalms/src/palm/tickleworker.cpp`
- Modify: `WildPalms/src/palm/CMakeLists.txt`

- [ ] **Step 1: Confirm zero callers**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
grep -rn -E "DeviceSession|DeviceWorker|TickleWorker" --include="*.h" --include="*.cpp" --include="CMakeLists.txt" 2>/dev/null | grep -v build-dev/ | grep -v "src/palm/devicesession\|src/palm/deviceworker\|src/palm/tickleworker"
```

Expected: empty output.

If any matches remain, **stop**: Task 5 was incomplete. Fix the
references in `kf6mainwindow.cpp` first; do not delete files with
live callers.

- [ ] **Step 2: Delete the source files**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
rm src/palm/devicesession.h src/palm/devicesession.cpp
rm src/palm/deviceworker.h   src/palm/deviceworker.cpp
rm src/palm/tickleworker.h   src/palm/tickleworker.cpp
```

- [ ] **Step 3: Update src/palm/CMakeLists.txt**

Open `WildPalms/src/palm/CMakeLists.txt`. Find the entries for
`devicesession`, `deviceworker`, `tickleworker` (and any
`MOC`-related lines for them). Delete those entries. The block
typically looks like:

```cmake
target_sources(WildPalmsPalm PRIVATE
    ...
    devicesession.h
    devicesession.cpp
    deviceworker.h
    deviceworker.cpp
    tickleworker.h
    tickleworker.cpp
    ...
)
```

Delete the six lines (or whatever the actual layout is).

- [ ] **Step 4: Build everything**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
cmake --build build-dev 2>&1 | tail -20
```

Expected: builds clean.

If linker complains about missing `DeviceSession::*` symbols,
something still references them. Re-run Step 1's grep with
broader scope (include `*.txt`, `*.cmake`).

- [ ] **Step 5: Run all tests**

```bash
ctest --test-dir build-dev --output-on-failure 2>&1 | tail -30
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add -A src/palm
git commit -m "M6b Task 6: delete DeviceSession + DeviceWorker + TickleWorker

The legacy connect-time runtime classes (1054 lines) had zero
callers after Task 5. Their responsibilities are now:

- DeviceSession        → PalmRuntime + PalmDeviceAccess
- DeviceWorker         → KPilotDeviceLink's internal ConnectionWorker
                         (already async; DeviceWorker was just glue)
- TickleWorker         → PalmTickle (Task 1), on PalmDeviceAccess's
                         link thread, listening to KPilotDeviceLink's
                         existing pause/resume signals

The design's §11 'three threads by construction' (GUI / engine /
link) is now literally true — no second link thread, no glue layer."
```

---

### Task 7: Run verify-all.sh

**Files:** none modified.

- [ ] **Step 1: Run verify-all from the coordination folder**

```bash
cd /home/clinton/dev/refactor-engine-merger
./scripts/verify-all.sh 2>&1 | tail -40
```

Per CLAUDE.md exit codes:
- `0` → match baseline. Proceed to Task 8.
- `1` → configure or build failure. Stop, fix, re-run.
- `2` → test regression (pass→fail OR test deleted). Per the
  FINDINGS entry from M6a: if the only flips are `LOST: <name>`
  entries for tests that legitimately disappeared (e.g. tests
  inside DeviceSession's test suite that we deleted), this is
  expected — refresh the baseline.
- `3` → test improvement (fail→pass). Investigate before
  refreshing.

- [ ] **Step 2: If exit was 2 due to LOST tests only, refresh baseline**

The legacy DeviceSession/DeviceWorker/TickleWorker had no tests
of their own (verified in M6a's audit), so this case shouldn't
arise. But the new `tst_palm_device_access_connect` *is* a new
test — it should appear as `GAINED:`, not `LOST:`. The script
treats GAINED tests as IMPROVEMENT (exit 3). If exit 3:

```bash
cp baselines/wildpalms-worktree-ctest.txt.last \
   baselines/wildpalms-worktree-ctest.txt
./scripts/verify-all.sh 2>&1 | tail -10
```

Expected: exit 0.

If the second run still doesn't exit 0, investigate before
proceeding.

- [ ] **Step 3: Confirm test counts**

Read the final summary line from verify-all output. Expected:

- libkalburator: **54/54** pass (unchanged)
- PlanStan: **90/114** pass (unchanged)
- WildPalms: **75/75** pass (74 from post-M6a + 1 new
  `tst_palm_device_access_connect`)

Or possibly higher if `tst_palm_device_access_connect` exposes
multiple sub-tests as separate ctest entries (`-DTEST_FUNCTION=`
style). Note the actual count for Task 9.

---

### Task 8: Tag v0.22-phase-m6b-runtime-owns-link

- [ ] **Step 1: Confirm branch + commits**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git branch --show-current   # expect: palm-rewrite
git log --oneline -7
```

Expected: HEAD is the M6b Task 6 commit; previous commits are
M6b Tasks 1-5.

- [ ] **Step 2: User authorization for the tag**

Per refactor-engine-merger/CLAUDE.md, `git tag` is destructive.
**The user has pre-authorized M6b execution including the tag**
(per the conversation that produced this plan). Proceed.

- [ ] **Step 3: Create the annotated tag**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git tag -a v0.22-phase-m6b-runtime-owns-link -m "M6b — PalmRuntime owns the device link

Moves device-connection lifecycle out of KF6MainWindow's
DeviceSession and into PalmRuntime/PalmDeviceAccess. After this
tag, PalmDeviceAccess is the single link-thread owner the design's
§5/§11 specifies: connect, sync, and tickle all run on its
m_linkThread. KF6MainWindow becomes a pure KDE app shell.

Net deltas:
- Added: PalmTickle class (~140 LOC), PalmDeviceAccess connect
  surface (~200 LOC), tst_palm_device_access_connect (~200 LOC)
- Deleted: DeviceSession, DeviceWorker, TickleWorker (~1054 LOC)
- KF6MainWindow: -80 LOC of session plumbing, 2 fewer members

Real-device verification deferred per user direction (matches M5b
precedent). M7 should run a real-device HotSync before tagging
v0.23-palm-rewrite if the hardware is available."
```

---

### Task 9: Update CURRENT-STATUS.md

**Files:**
- Modify: `refactor-engine-merger/CURRENT-STATUS.md`

- [ ] **Step 1: Read the current file**

- [ ] **Step 2: Bump the date line**

```
**Last updated:** 2026-05-02 (M6b complete — PalmRuntime owns the link; DeviceSession trio deleted; <test-count> tests green)
```

(Substitute the actual WildPalms test count from Task 7 Step 3.)

- [ ] **Step 3: Insert M6b entry after M6a, before "## Next"**

```markdown
✅ **Palm runtime rewrite — Plan 5 / M6b** — PalmRuntime owns
   the device link. PalmDeviceAccess extended to do the connect
   handshake on its existing link thread; new `PalmTickle` sibling
   replaces `TickleWorker` (no second thread). PalmRuntime's
   `connectDevice(KPilotLink*)` becomes `connectDevice(QStringList
   paths)`. KF6MainWindow loses `m_session` + `m_deviceLink` + ~80
   lines of session plumbing. `DeviceSession` + `DeviceWorker` +
   `TickleWorker` deleted (1054 LOC). Design's §11 "three threads
   by construction" (GUI / engine / link) is now literally true.
   Real-device verification deferred per user direction. Tag
   `v0.22-phase-m6b-runtime-owns-link`. WildPalms: 74→<count> tests
   (added `tst_palm_device_access_connect`).
```

- [ ] **Step 4: Update "Next" — only M7 remains**

```markdown
## Next

⬜ **M7** — merge `palm-rewrite` (WildPalms branch) to
   `refactor/engine-merger`; cross-repo verify-all clean; tag
   `v0.23-palm-rewrite` on WildPalms HEAD. Real-device HotSync
   recommended before tagging if hardware available.
```

- [ ] **Step 5: Update "Recently committed (WildPalms)"**

Prepend Tasks 1-6's SHAs (most recent first). Get them via:
```bash
cd WildPalms && git log --oneline -6 --format="%h  %s"
```

- [ ] **Step 6: Bump test posture section**

Update header to `## Test posture (2026-05-02, post-M6b)` and
WildPalms line to the actual count from Task 7.

- [ ] **Step 7: Sanity-check ≤ 150 lines**

If over, prune the oldest "Recently committed" entries (keep
M5/M6a/M6b — load-bearing).

---

### Task 10: Append FINDINGS if anything non-obvious surfaced

**Files (conditional):**
- Modify: `refactor-engine-merger/FINDINGS.md`

Likely candidates for an entry:
- Anything specific about how `KPilotDeviceLink::ConnectionWorker`
  interacts with PalmDeviceAccess's link thread that wasn't
  obvious from the audit (e.g. unexpected blocking, signal-edge
  cases).
- The test-seam decision for PalmDeviceAccess's mock-link
  injection if it ended up unusual.
- Anything that future M6b-style "move responsibility into
  PalmRuntime" work would benefit from knowing.

Skip this task if no non-obvious lesson surfaced.

---

### Task 11: M7 — Merge palm-rewrite to refactor/engine-merger

**Files (conditional):**
- Modify (conceptually): `refactor-engine-merger/CURRENT-STATUS.md`
- Modify (conceptually): `refactor-engine-merger/baselines/wildpalms-worktree-ctest.txt`

- [ ] **Step 1: Confirm WildPalms HEAD is the M6b tag**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git log --oneline -1
git describe --exact-match HEAD 2>/dev/null
```

Expected: tag `v0.22-phase-m6b-runtime-owns-link` is on HEAD.

- [ ] **Step 2: Switch WildPalms worktree to refactor/engine-merger**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git status                         # must be clean
git checkout refactor/engine-merger
git log --oneline -3               # confirm where refactor/engine-merger is
```

The refactor/engine-merger branch was last touched at... read the
log and verify.

- [ ] **Step 3: Merge palm-rewrite**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git merge --no-ff palm-rewrite -m "Merge branch 'palm-rewrite' — Palm runtime rewrite (M1-M6b)

Lands the full Palm runtime rewrite per design
2026-05-01-palm-runtime-rewrite-design.md. Sub-phases:

- M1: libkalburator dynamic DomainPlugin registration +
      ExecutionOverride; F1 facade deleted
- M2: PalmRuntime + PalmDeviceAccess + IBackendPluginV2
- M3: HotSync/FullSync/Copy*/Backup/Restore via PalmRuntime
- M4: Memo/Contacts/Todo/Webcal migrated to V2
- M5a: ConflictHandler + ConflictDialog wired
- M5b: SettingsDialog Sync page + MappingEditorDialog
- M5c: Per-plugin view smoke + _v2 tests rewritten
- M6a: Plucker tree + HotSyncCoordinator deleted
- M6b: KF6MainWindow off DeviceSession; PalmRuntime owns the link

Net result: design's §2 layer diagram + §5 component diagram +
§11 three-threads-by-construction are literally true in the code."
```

If merge conflicts arise, **stop and surface to user**. The
branches diverged for ~1 month so conflicts are possible (most
likely in CMakeLists.txt files if any cross-cutting changes
landed on refactor/engine-merger during that time).

- [ ] **Step 4: Run cross-repo verify-all**

```bash
cd /home/clinton/dev/refactor-engine-merger
./scripts/verify-all.sh 2>&1 | tail -30
```

Expected: exit 0. Same baseline-refresh logic as Task 7 if needed.

- [ ] **Step 5: Tag v0.23-palm-rewrite**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git tag -a v0.23-palm-rewrite -m "Palm runtime rewrite — merged to refactor/engine-merger

Endpoint of the M1-M6b rewrite. WildPalms's runtime, shell, and
plugin contract are all on the post-design architecture. Ready
for the next phase of refactor/engine-merger work."
```

- [ ] **Step 6: Update CURRENT-STATUS.md and FINDINGS for M7**

Mark M7 complete. Move M6b + M7 entries from "Where we are" /
"Next" to a "Done" structure if appropriate, OR leave M6b under
"Where we are" and add M7 alongside it. Match the file's
existing pattern.

Update Next section — likely empty after M7 unless follow-up
work has been identified during execution.

---

## Self-review checklist

Before declaring M6b complete:

1. **Spec coverage:** Plan covered (a) PalmTickle, (b) extended
   PalmDeviceAccess, (c) tests, (d) PalmRuntime refactor,
   (e) KF6MainWindow simplification, (f) deletion of legacy trio,
   (g) verify-all, (h) tag, (i) status doc, (j) M7 merge + tag.

2. **Behavior preservation:** All previously-passing tests still
   pass. New tests (Task 3) pass. No exit-code surprises in
   verify-all that weren't anticipated.

3. **Three-threads claim:** After Task 6, search the codebase for
   `new QThread\|moveToThread` calls outside of
   `palmdeviceaccess.cpp`'s `m_linkThread` setup,
   `kpilotdevicelink.cpp`'s `ConnectionWorker`, and
   libkalburator's `SyncEngine`. Any other QThread suggests an
   unexpected fourth thread snuck in.

4. **Reversibility:** Each commit is independently revertable.
   Reverting Task 6 (deletion) restores DeviceSession to a
   compilable but unused state; reverting Tasks 5+6 restores the
   pre-M6b architecture.

---

## Estimated effort

Per the original design: "~2 days." Per actual M6a-style
execution: ~6-10 hours of dispatched subagent work + coordination
overhead. The biggest unknown is Task 4 (PalmRuntime refactor) —
if `finishConnect()` extraction is messier than expected (e.g.
existing `connectDevice(KPilotLink*)` body has order-dependent
side effects that don't transplant cleanly), it could add an
hour or two.

If execution exceeds 16 hours, pause and re-read the audit
findings before continuing.
