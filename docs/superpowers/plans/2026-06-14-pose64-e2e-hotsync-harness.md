# POSE64-backed e2e HotSync fidelity harness — Implementation Plan (Phase 1 skeleton)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up an opt-in C++/QtTest target in WildPalms that drives a real HotSync between a headless POSE64-emulated Palm and a headless `PalmRuntime` over POSE64's real PTY/DLP link, and asserts record-level calendar fidelity using an independent pilot-link decoder.

**Architecture:** A new `tests/device-e2e/` tree with a small static lib (`WildPalmsDeviceE2E`) holding three reusable, individually-unit-tested components — `ReControlClient` (QTcpSocket port of POSE64's line protocol), `EmulatorFixture` (launches/controls the `pose64` child), `PilotLinkDecoder` (decodes a DatebookDB `.pdb` via pilot-link's `unpack_Appointment`) — plus a canon-calendar seed builder. The integration test launches the emulator, seeds the real hub, connects `PalmRuntime` to the emulator's pty (single-element device list ⇒ WP's probe is skipped ⇒ `pi_bind` direct, matching POSE64's GATE-4 flow), runs one `hotSync()`, exports `DatebookDB`, decodes it independently, and asserts the seeded event survived. Unit tests run in plain `ctest`; the integration test `QSKIP`s unless `WILDPALMS_POSE64_BIN` + `WILDPALMS_PALM_BASELINE_PSF` are set, and is tagged with CTest label `device-e2e`.

**Tech Stack:** C++17, Qt6 (Core, Network, Test), QtTest, CMake, pilot-link 0.13.0 (`libpisock`, `pi-file.h`/`pi-buffer.h`/`pi-datebook.h`), libkalburator (`GenericSqliteBackend`, `CanonEnvelope`), POSE64 ReControl TCP.

**Spec:** `docs/superpowers/specs/2026-06-14-pose64-e2e-hotsync-harness-design.md`

---

## Key facts the engineer must not re-derive

- **No `src/` change.** WP's `ConnectionWorker::doConnect` skips the multi-port probe when the device-path list has exactly one element (`src/palm/kpilotdevicelink.cpp:189`) and goes straight to `pi_bind`/`pi_listen`/`pi_accept_to`. Pass `["/dev/pts/N"]`.
- **Headless `PalmRuntime` flow:** `PalmRuntime rt(profileDir)` → `rt.connectDevice(QStringList{pty})` (async) → wait for `connectionComplete(bool,QString)`; on success WP auto-runs the private `finishConnect()`, which builds the palm↔hub Star mappings and emits `deviceConnected()` then `readyForSync()`. Then `rt.hotSync()` returns `QFuture<PalmRunResult>` (await with `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), …)`, read `future.resultAt(0)`).
- **Seeding the hub:** the hub is a `Kalburator::Sinks::GenericSqliteBackend` registered under id **`"wp-hub"`**; its calendar collection id is the bare string **`"calendar"`** and stores **canon JSON** (shape `(calendar, canon)`), NOT raw VEVENT. `ensureHubCollections()` already created it in the ctor, so seeding only needs `hub->createRecord("calendar", rec)`. The Star mapping makes the hub the source and the Palm the target (TwoWay), so a seeded hub event propagates outbound to the Palm on HotSync.
- **Canon calendar JSON schema** (from `icalcanonstages.cpp`): `summary`, `description`, `start{dateTime: UTC-ISODate, floating:false}`, `end{…}`, `allDay:bool`, optional `alarms[{type,offset(seconds, negative=before),text}]`, optional `categories`. Mint the envelope with `Kalburator::Shape::CanonEnvelope::stampEnvelope(obj,"calendar",uid)` + `serialize(obj)`.
- **Field survival canon→Palm DatebookDB:** canon `summary` → Palm appointment **description** (the title); canon `description` → Palm **note**; `start`/`end` → begin/end; one Display alarm; `categories[0]` → category slot (named slots need AppInfo reconciliation — **omit categories** so the event lands in **Unfiled = slot 0**, which `pi_file_read_record` reports as `category==0`).
- **pilot-link is 0.13.0 (pi_buffer API):** `unpack_Appointment(struct Appointment*, const pi_buffer_t*, datebookType)` — copy the raw record bytes into a `pi_buffer_t` (`pi_buffer_new`+`pi_buffer_append`) first. Struct/funcs live in `pi-datebook.h` (there is no `pi-appointment.h`). `struct tm` year is +1900, month is 0-based. `a.event != 0` ⇒ all-day. Always `free_Appointment(&a)`. CMake imported target is `pisock` (defined in `lib/CMakeLists.txt`); also `add_dependencies(<tgt> pilot-link-external)`.
- **POSE64 launch (headless):** `$WILDPALMS_POSE64_BIN -psf <baseline.psf> --port <n> -preference PortSerial=serial:pty:HotSync` with env `QT_QPA_PLATFORM=offscreen` (there is **no `--offscreen` flag**; `-preference` must come after `-psf`/`--port`). ReControl server starts only after the Qt event loop is up → poll-connect + `state` until reply starts with `OK`. Only one TCP session at a time (a probe must fully disconnect before the working session connects, else `ERR busy`).
- **ReControl framing:** one command per line (`\n`). Replies start `OK`/`ERR`. Single-line for most commands; **multi-line dot-terminated** for `info`, `apps`, `ui`, `dialog` (data lines, then a line that is just `.`). Decode **latin-1**. The pty appears on `info`'s serial line as `serial=serial:pty:HotSync pty=/dev/pts/N`, and is **absent until the guest opens the port** (poll `info` ~5 s). `button cradle tap` → `OK` (queued). `export <dbname> <path>` → `OK`. `load <path>` → `OK`.
- **Attach-then-tap ordering:** WP must be in `pi_accept_to` (pty open) **before** `button cradle tap`. Call `connectDevice`, `QTest::qWait(1000)` to let the worker open the pty, then send the cradle tap, then await `connectionComplete`. Retry once on failure, dismissing the modal "HotSync Problem" form (form id `12000`, dismiss `tap-id 12004`) if present.
- **CMake gotcha:** the integration exe MUST link `$<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>` or the engine's domain registrars are stripped and the transcode path has no edges. Use `WILDPALMS_QTEST_GUILESS_MAIN` (in `tests/wildpalms_qtest_main.h`), not raw `QTEST_GUILESS_MAIN` (Qt 6.11 exit crash).

---

## File structure

```
tests/device-e2e/
  CMakeLists.txt              # static lib + 4 test exes; device-e2e label; offscreen env
  recontrolclient.h/.cpp      # QTcpSocket line-protocol client (single-line + dot-terminated)
  emulatorfixture.h/.cpp      # launches/controls the pose64 child; ready-poll; pty; load; teardown
  pilotlinkdecoder.h/.cpp     # decodeAppointmentRecord(bytes) + readAppointments(.pdb path)
  canonseed.h/.cpp            # buildCanonCalendarEvent(spec) -> canon JSON bytes
  tst_recontrol_client.cpp    # unit test: in-process fake QTcpServer, framing (always runs)
  tst_pilotlink_decoder.cpp   # unit test: pack_Appointment -> decode round-trip (always runs)
  tst_canon_calendar_seed.cpp # unit test: canon JSON builder shape (always runs)
  tst_device_e2e_hotsync.cpp  # INTEGRATION: real emulator + real PalmRuntime (QSKIP-gated)
tests/CMakeLists.txt          # + add_subdirectory(device-e2e)
docs/device-e2e-harness.md    # how to run it (env vars, ctest -L device-e2e)
```

The static lib lets the three reusable components be unit-tested independently and reused by the matrix/three-tier phases later. `canonseed.cpp` is compiled directly into the two exes that need it (they link `Kalburator::Sync` anyway) so the lib stays free of libkalburator.

---

## Task 1: Scaffold the directory, CMake wiring, and a skip-gated placeholder test

Proves the build/link/gating works (plain `ctest` still green + one skip) before any logic.

**Files:**
- Create: `tests/device-e2e/CMakeLists.txt`
- Create: `tests/device-e2e/tst_device_e2e_hotsync.cpp` (placeholder)
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(device-e2e)`)

- [ ] **Step 1: Create the placeholder integration test**

`tests/device-e2e/tst_device_e2e_hotsync.cpp`:

```cpp
#include <QTest>
#include <QtGlobal>
#include "../wildpalms_qtest_main.h"

namespace {
bool harnessConfigured()
{
    return !qEnvironmentVariableIsEmpty("WILDPALMS_POSE64_BIN")
        && !qEnvironmentVariableIsEmpty("WILDPALMS_PALM_BASELINE_PSF");
}
} // namespace

class TestDeviceE2EHotSync : public QObject
{
    Q_OBJECT
private slots:
    void hubToPalm_calendar_firstHotSync()
    {
        if (!harnessConfigured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        QVERIFY(true); // replaced in Task 6
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestDeviceE2EHotSync)
#include "tst_device_e2e_hotsync.moc"
```

- [ ] **Step 2: Create the CMakeLists for the subtree (lib comes later; placeholder exe now)**

`tests/device-e2e/CMakeLists.txt`:

```cmake
# End-to-end HotSync fidelity harness against a POSE64-emulated Palm.
# The integration test is opt-in (CTest label "device-e2e") and QSKIPs unless
# WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF are set.

add_executable(tst_device_e2e_hotsync tst_device_e2e_hotsync.cpp)
target_link_libraries(tst_device_e2e_hotsync
    PRIVATE
        Qt::Core
        Qt::Test
)
add_test(NAME tst_device_e2e_hotsync COMMAND tst_device_e2e_hotsync)
set_tests_properties(tst_device_e2e_hotsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "device-e2e"
)
```

- [ ] **Step 3: Wire the subdirectory into the test tree**

In `tests/CMakeLists.txt`, add next to the other `add_subdirectory(...)` calls (near `add_subdirectory(runtime)`):

```cmake
add_subdirectory(device-e2e)
```

- [ ] **Step 4: Configure and build**

Run:
```bash
cmake -S . -B build -DWILDPALMS_LIBKALBURATOR_GIT_TAG=v0.77
cmake --build build -j 8 --target tst_device_e2e_hotsync
```
Expected: builds clean.

- [ ] **Step 5: Run the full suite — confirm no regression + the new skip**

Run:
```bash
ctest --test-dir build -j 8 2>&1 | tail -5
ctest --test-dir build -R tst_device_e2e_hotsync -V 2>&1 | grep -E "SKIP|Passed|Failed"
```
Expected: previous total still passes; `tst_device_e2e_hotsync` reports the `QSKIP` message and counts as Passed (a skipped QtTest exits 0).

- [ ] **Step 6: Confirm the label selects it**

Run:
```bash
ctest --test-dir build -L device-e2e -N
```
Expected: lists `tst_device_e2e_hotsync` only.

- [ ] **Step 7: Commit**

```bash
git add tests/device-e2e/CMakeLists.txt tests/device-e2e/tst_device_e2e_hotsync.cpp tests/CMakeLists.txt
git commit -m "test(device-e2e): scaffold opt-in POSE64 HotSync harness target (skip-gated)"
```

---

## Task 2: `ReControlClient` — POSE64 line-protocol client (TDD against an in-process fake server)

**Files:**
- Create: `tests/device-e2e/recontrolclient.h`, `tests/device-e2e/recontrolclient.cpp`
- Create: `tests/device-e2e/tst_recontrol_client.cpp`
- Modify: `tests/device-e2e/CMakeLists.txt`

- [ ] **Step 1: Write the header**

`tests/device-e2e/recontrolclient.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QTcpSocket;

namespace WildPalms {
namespace DeviceE2E {

// One ReControl reply. `ok` is true iff the first line starts with "OK".
// `head` is the remainder of the first line after "OK "/"ERR ".
// `body` holds the indented data lines of a multi-line (dot-terminated) reply.
struct ReControlReply {
    bool ok = false;
    QString head;
    QStringList body;
    QString raw;
};

// Minimal synchronous client for POSE64's ReControl TCP protocol.
// Single-line replies: read until the first '\n'. Multi-line replies
// (info/apps/ui/dialog): read indented lines until a line that is just ".".
// All bytes are decoded latin-1 (Palm OS text is Latin-1).
class ReControlClient
{
public:
    ReControlClient();
    ~ReControlClient();

    bool connectTo(quint16 port, int timeoutMs = 5000, const QString &host = QStringLiteral("localhost"));
    void disconnect();
    bool isConnected() const;

    // Send a single-line command; read a single-line reply.
    ReControlReply command(const QString &cmd, int timeoutMs = 10000);
    // Send a command whose reply is multi-line dot-terminated (info/apps/ui/dialog).
    ReControlReply commandMultiline(const QString &cmd, int timeoutMs = 10000);

private:
    bool readLineLatin1(QString &out, int timeoutMs);
    QTcpSocket *m_sock = nullptr;
};

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 2: Write the failing unit test (fake server speaking the protocol)**

`tests/device-e2e/tst_recontrol_client.cpp`:

```cpp
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "recontrolclient.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::DeviceE2E;

// A tiny in-process ReControl-like server: for each line received, sends a
// canned reply. Single-line for most; dot-terminated multi-line for "info".
class FakeReControlServer : public QObject
{
    Q_OBJECT
public:
    quint16 start()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *c = m_server.nextPendingConnection();
            connect(c, &QTcpSocket::readyRead, this, [this, c] {
                while (c->canReadLine()) {
                    const QString line = QString::fromLatin1(c->readLine()).trimmed();
                    if (line == QLatin1String("button cradle tap"))
                        c->write("OK\n");
                    else if (line == QLatin1String("info"))
                        c->write("OK POSE64 0.9.1\n serial=serial:pty:HotSync pty=/dev/pts/7\n.\n");
                    else if (line == QLatin1String("bogus"))
                        c->write("ERR usage: unknown command\n");
                    else
                        c->write("OK\n");
                }
            });
        });
        return m_server.serverPort();
    }
private:
    QTcpServer m_server;
};

class TestReControlClient : public QObject
{
    Q_OBJECT
private slots:
    void singleLineOk()
    {
        FakeReControlServer srv;
        const quint16 port = srv.start();
        ReControlClient c;
        QVERIFY(c.connectTo(port));
        const ReControlReply r = c.command(QStringLiteral("button cradle tap"));
        QVERIFY(r.ok);
    }

    void singleLineErr()
    {
        FakeReControlServer srv;
        const quint16 port = srv.start();
        ReControlClient c;
        QVERIFY(c.connectTo(port));
        const ReControlReply r = c.command(QStringLiteral("bogus"));
        QVERIFY(!r.ok);
        QVERIFY(r.head.contains(QStringLiteral("unknown command")));
    }

    void multilineInfoCarriesPty()
    {
        FakeReControlServer srv;
        const quint16 port = srv.start();
        ReControlClient c;
        QVERIFY(c.connectTo(port));
        const ReControlReply r = c.commandMultiline(QStringLiteral("info"));
        QVERIFY(r.ok);
        QVERIFY(r.raw.contains(QStringLiteral("pty=/dev/pts/7")));
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestReControlClient)
#include "tst_recontrol_client.moc"
```

- [ ] **Step 3: Add the lib + unit-test target to CMake and verify the test FAILS to build/link (no impl yet)**

Replace `tests/device-e2e/CMakeLists.txt` contents with:

```cmake
# End-to-end HotSync fidelity harness against a POSE64-emulated Palm.

# ReControlClient uses QTcpSocket; ensure the Qt Network component is available
# (idempotent if a parent CMakeLists already found it).
find_package(Qt6 REQUIRED COMPONENTS Network)

# Reusable, individually-unit-tested components.
add_library(WildPalmsDeviceE2E STATIC
    recontrolclient.cpp
    emulatorfixture.cpp
    pilotlinkdecoder.cpp
)
target_link_libraries(WildPalmsDeviceE2E
    PUBLIC
        Qt::Core
        Qt::Network
        pisock
)
add_dependencies(WildPalmsDeviceE2E pilot-link-external)

# --- unit tests (always run; no emulator needed) ---

add_executable(tst_recontrol_client tst_recontrol_client.cpp)
target_link_libraries(tst_recontrol_client PRIVATE Qt::Core Qt::Network Qt::Test WildPalmsDeviceE2E)
add_test(NAME tst_recontrol_client COMMAND tst_recontrol_client)
set_tests_properties(tst_recontrol_client PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- integration test (opt-in; CTest label device-e2e) ---

add_executable(tst_device_e2e_hotsync tst_device_e2e_hotsync.cpp)
target_link_libraries(tst_device_e2e_hotsync PRIVATE Qt::Core Qt::Test)
add_test(NAME tst_device_e2e_hotsync COMMAND tst_device_e2e_hotsync)
set_tests_properties(tst_device_e2e_hotsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "device-e2e"
)
```

Create empty stubs so the lib compiles in this task only for `recontrolclient.cpp` (the other two are created in their own tasks). To keep the lib compiling now, also create minimal empty translation units:

`tests/device-e2e/emulatorfixture.cpp`:
```cpp
// Implemented in Task 4.
```
`tests/device-e2e/pilotlinkdecoder.cpp`:
```cpp
// Implemented in Task 3.
```

Run:
```bash
cmake -S . -B build && cmake --build build -j 8 --target tst_recontrol_client 2>&1 | tail -20
```
Expected: link error — `ReControlClient` symbols undefined.

- [ ] **Step 4: Implement `ReControlClient`**

`tests/device-e2e/recontrolclient.cpp`:

```cpp
#include "recontrolclient.h"

#include <QTcpSocket>
#include <QElapsedTimer>

namespace WildPalms {
namespace DeviceE2E {

ReControlClient::ReControlClient() = default;

ReControlClient::~ReControlClient()
{
    disconnect();
}

bool ReControlClient::connectTo(quint16 port, int timeoutMs, const QString &host)
{
    disconnect();
    m_sock = new QTcpSocket();
    m_sock->connectToHost(host, port);
    if (!m_sock->waitForConnected(timeoutMs)) {
        delete m_sock;
        m_sock = nullptr;
        return false;
    }
    return true;
}

void ReControlClient::disconnect()
{
    if (m_sock) {
        m_sock->disconnectFromHost();
        m_sock->abort();
        delete m_sock;
        m_sock = nullptr;
    }
}

bool ReControlClient::isConnected() const
{
    return m_sock && m_sock->state() == QAbstractSocket::ConnectedState;
}

bool ReControlClient::readLineLatin1(QString &out, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!m_sock->canReadLine()) {
        const int remaining = timeoutMs - int(t.elapsed());
        if (remaining <= 0)
            return false;
        if (!m_sock->waitForReadyRead(remaining))
            return false;
    }
    out = QString::fromLatin1(m_sock->readLine());
    return true;
}

static ReControlReply parseFirstLine(const QString &line)
{
    ReControlReply r;
    const QString trimmed = QString(line).remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
    r.raw = trimmed;
    if (trimmed.startsWith(QLatin1String("OK"))) {
        r.ok = true;
        r.head = trimmed.mid(2).trimmed();
    } else if (trimmed.startsWith(QLatin1String("ERR"))) {
        r.ok = false;
        r.head = trimmed.mid(3).trimmed();
    }
    return r;
}

ReControlReply ReControlClient::command(const QString &cmd, int timeoutMs)
{
    ReControlReply r;
    if (!isConnected())
        return r;
    m_sock->write((cmd + QLatin1Char('\n')).toLatin1());
    m_sock->waitForBytesWritten(timeoutMs);
    QString line;
    if (!readLineLatin1(line, timeoutMs))
        return r;
    return parseFirstLine(line);
}

ReControlReply ReControlClient::commandMultiline(const QString &cmd, int timeoutMs)
{
    ReControlReply r;
    if (!isConnected())
        return r;
    m_sock->write((cmd + QLatin1Char('\n')).toLatin1());
    m_sock->waitForBytesWritten(timeoutMs);

    QString first;
    if (!readLineLatin1(first, timeoutMs))
        return r;
    r = parseFirstLine(first);
    r.raw = first.trimmed();

    // Read indented data lines until a line that is exactly "." (dot terminator).
    forever {
        QString line;
        if (!readLineLatin1(line, timeoutMs))
            break;
        const QString trimmed = QString(line).remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
        if (trimmed.trimmed() == QLatin1String("."))
            break;
        r.body.append(trimmed);
        r.raw += QLatin1Char('\n') + trimmed;
    }
    return r;
}

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 5: Build and run the unit test — expect PASS**

Run:
```bash
cmake --build build -j 8 --target tst_recontrol_client && ctest --test-dir build -R tst_recontrol_client -V 2>&1 | grep -E "PASS|FAIL|Passed|Failed"
```
Expected: 3 test functions PASS.

- [ ] **Step 6: Commit**

```bash
git add tests/device-e2e/recontrolclient.h tests/device-e2e/recontrolclient.cpp \
        tests/device-e2e/tst_recontrol_client.cpp tests/device-e2e/CMakeLists.txt \
        tests/device-e2e/emulatorfixture.cpp tests/device-e2e/pilotlinkdecoder.cpp
git commit -m "test(device-e2e): ReControlClient line-protocol client + framing unit tests"
```

---

## Task 3: `PilotLinkDecoder` — independent DatebookDB decode (TDD via pack→decode round-trip)

The load-bearing logic is `decodeAppointmentRecord(bytes)` (pi_buffer + `unpack_Appointment` + field extraction). It is tested with bytes produced by `pack_Appointment` — verified pilot-link 0.13.0 APIs, no `.pdb` file needed. `readAppointments(path)` is thin `pi_file_*` glue exercised end-to-end in Task 6.

**Files:**
- Create: `tests/device-e2e/pilotlinkdecoder.h`, overwrite `tests/device-e2e/pilotlinkdecoder.cpp`
- Create: `tests/device-e2e/tst_pilotlink_decoder.cpp`
- Modify: `tests/device-e2e/CMakeLists.txt`

- [ ] **Step 1: Write the header**

`tests/device-e2e/pilotlinkdecoder.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace WildPalms {
namespace DeviceE2E {

// One decoded Palm DatebookDB appointment. Field names reflect the PALM side:
// `description` is the Palm appointment description (= the canon summary/title);
// `note` is the Palm note (= the canon description).
struct DecodedAppointment {
    QString description;
    QString note;
    QDateTime begin;   // local time (Palm stores wall-clock; set TZ=UTC for determinism)
    QDateTime end;
    bool allDay = false;
    bool hasAlarm = false;
    int advance = 0;
    int advanceUnits = 0; // pilot-link advMinutes/advHours/advDays
    int category = 0;     // category index from the record header (Unfiled = 0)
};

// Decode a single raw DatebookDB record (the bytes of one appointment).
// `ok` is set false if unpack fails. `category` is the record's category index.
DecodedAppointment decodeAppointmentRecord(const QByteArray &raw, int category, bool *ok = nullptr);

// Decode all non-deleted appointment records from a DatebookDB .pdb file.
QList<DecodedAppointment> readAppointments(const QString &pdbPath);

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 2: Confirm the two `pi_file_*` write signatures used only by the test, then write the failing test**

Run (confirm the round-trip helper APIs exist with these signatures; both are standard 0.13.0):
```bash
grep -nE 'pack_Appointment|pi_buffer_new|pi_buffer_append' /usr/include/pi-datebook.h /usr/include/pi-buffer.h
```
Expected: `pack_Appointment(const struct Appointment*, pi_buffer_t*, datebookType)`, `pi_buffer_new(size_t)`, `pi_buffer_append(pi_buffer_t*, const void*, size_t)`.

`tests/device-e2e/tst_pilotlink_decoder.cpp`:

```cpp
#include <QTest>
#include "pilotlinkdecoder.h"
#include "../wildpalms_qtest_main.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-datebook.h>
}

using namespace WildPalms::DeviceE2E;

class TestPilotLinkDecoder : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("TZ", "UTC");
        tzset();
    }

    void decodesTimedAppointmentWithAlarmAndNote()
    {
        // Build a known appointment with pilot-link's own packer.
        Appointment_t a{};
        a.event = 0; // timed
        a.begin = tm{}; a.begin.tm_year = 126; a.begin.tm_mon = 6; a.begin.tm_mday = 1;
        a.begin.tm_hour = 9;  a.begin.tm_min = 0; a.begin.tm_sec = 0;
        a.end = tm{};   a.end.tm_year = 126;   a.end.tm_mon = 6;   a.end.tm_mday = 1;
        a.end.tm_hour = 10;   a.end.tm_min = 0;   a.end.tm_sec = 0;
        a.alarm = 1; a.advance = 10; a.advanceUnits = advMinutes;
        a.repeatType = repeatNone; a.repeatForever = 0; a.exceptions = 0; a.exception = nullptr;
        char desc[] = "Seeded Event";
        char note[] = "Note body text";
        a.description = desc;
        a.note = note;

        pi_buffer_t *buf = pi_buffer_new(256);
        QVERIFY(buf);
        const int packed = pack_Appointment(&a, buf, datebook_v1);
        QVERIFY(packed >= 0);
        const QByteArray raw(reinterpret_cast<const char *>(buf->data), int(buf->used));
        pi_buffer_free(buf);

        bool ok = false;
        const DecodedAppointment d = decodeAppointmentRecord(raw, /*category=*/0, &ok);
        QVERIFY(ok);
        QCOMPARE(d.description, QStringLiteral("Seeded Event"));
        QCOMPARE(d.note, QStringLiteral("Note body text"));
        QCOMPARE(d.allDay, false);
        QCOMPARE(d.begin, QDateTime(QDate(2026, 7, 1), QTime(9, 0, 0)));
        QCOMPARE(d.end, QDateTime(QDate(2026, 7, 1), QTime(10, 0, 0)));
        QCOMPARE(d.hasAlarm, true);
        QCOMPARE(d.advance, 10);
        QCOMPARE(d.advanceUnits, int(advMinutes));
        QCOMPARE(d.category, 0);
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestPilotLinkDecoder)
#include "tst_pilotlink_decoder.moc"
```

- [ ] **Step 3: Add the unit-test target; build to confirm it FAILS (decoder unimplemented)**

In `tests/device-e2e/CMakeLists.txt`, after the `tst_recontrol_client` block, add:

```cmake
add_executable(tst_pilotlink_decoder tst_pilotlink_decoder.cpp)
target_link_libraries(tst_pilotlink_decoder PRIVATE Qt::Core Qt::Test WildPalmsDeviceE2E)
add_test(NAME tst_pilotlink_decoder COMMAND tst_pilotlink_decoder)
set_tests_properties(tst_pilotlink_decoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Run:
```bash
cmake -S . -B build && cmake --build build -j 8 --target tst_pilotlink_decoder 2>&1 | tail -20
```
Expected: link error — `decodeAppointmentRecord`/`readAppointments` undefined.

- [ ] **Step 4: Implement the decoder**

Overwrite `tests/device-e2e/pilotlinkdecoder.cpp`:

```cpp
#include "pilotlinkdecoder.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-datebook.h>
#include <pi-file.h>
#include <pi-dlp.h>
}

#include <ctime>

namespace WildPalms {
namespace DeviceE2E {

static QDateTime tmToQDateTime(const struct tm &t)
{
    // pilot-link struct tm: tm_year is years since 1900, tm_mon is 0-based.
    const QDate date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    const QTime time(t.tm_hour, t.tm_min, t.tm_sec);
    return QDateTime(date, time); // Qt::LocalTime; TZ=UTC makes this deterministic
}

DecodedAppointment decodeAppointmentRecord(const QByteArray &raw, int category, bool *ok)
{
    DecodedAppointment d;
    d.category = category;
    if (ok)
        *ok = false;

    pi_buffer_t *buf = pi_buffer_new(size_t(raw.size()));
    if (!buf)
        return d;
    pi_buffer_append(buf, raw.constData(), size_t(raw.size()));

    Appointment_t a{};
    const int rc = unpack_Appointment(&a, buf, datebook_v1);
    if (rc < 0) {
        pi_buffer_free(buf);
        return d;
    }

    d.allDay = (a.event != 0);
    d.begin = tmToQDateTime(a.begin);
    if (!d.allDay)
        d.end = tmToQDateTime(a.end);
    if (a.description)
        d.description = QString::fromLatin1(a.description);
    if (a.note)
        d.note = QString::fromLatin1(a.note);
    d.hasAlarm = (a.alarm != 0);
    d.advance = a.advance;
    d.advanceUnits = a.advanceUnits;

    free_Appointment(&a);
    pi_buffer_free(buf);
    if (ok)
        *ok = true;
    return d;
}

QList<DecodedAppointment> readAppointments(const QString &pdbPath)
{
    QList<DecodedAppointment> out;
    pi_file_t *pf = pi_file_open(pdbPath.toLocal8Bit().constData());
    if (!pf)
        return out;

    int entries = 0;
    pi_file_get_entries(pf, &entries);
    for (int i = 0; i < entries; ++i) {
        void *rawBuf = nullptr;
        size_t size = 0;
        int attrs = 0;
        int category = 0;
        recordid_t uid = 0;
        if (pi_file_read_record(pf, i, &rawBuf, &size, &attrs, &category, &uid) < 0)
            continue;
        if (attrs & dlpRecAttrDeleted) // skip tombstones (raw belongs to pf; do not free)
            continue;
        const QByteArray raw(reinterpret_cast<const char *>(rawBuf), int(size));
        bool ok = false;
        const DecodedAppointment d = decodeAppointmentRecord(raw, category, &ok);
        if (ok)
            out.append(d);
    }

    pi_file_close(pf);
    return out;
}

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 5: Build and run — expect PASS**

Run:
```bash
cmake --build build -j 8 --target tst_pilotlink_decoder && ctest --test-dir build -R tst_pilotlink_decoder -V 2>&1 | grep -E "PASS|FAIL|Passed|Failed"
```
Expected: the round-trip test PASSes.

- [ ] **Step 6: Commit**

```bash
git add tests/device-e2e/pilotlinkdecoder.h tests/device-e2e/pilotlinkdecoder.cpp \
        tests/device-e2e/tst_pilotlink_decoder.cpp tests/device-e2e/CMakeLists.txt
git commit -m "test(device-e2e): independent pilot-link DatebookDB decoder + round-trip unit test"
```

---

## Task 4: `EmulatorFixture` — launch and control the POSE64 child

No standalone unit test (it needs the real binary); it is exercised by Task 6 and by a skip-gated smoke method here that also validates the baseline `.psf`.

**Files:**
- Create: `tests/device-e2e/emulatorfixture.h`, overwrite `tests/device-e2e/emulatorfixture.cpp`
- Modify: `tests/device-e2e/tst_device_e2e_hotsync.cpp` (add a skip-gated smoke method)

- [ ] **Step 1: Write the header**

`tests/device-e2e/emulatorfixture.h`:

```cpp
#pragma once

#include <QProcess>
#include <QString>
#include <memory>

namespace WildPalms {
namespace DeviceE2E {

class ReControlClient;

// Launches a headless POSE64 process with a HotSync pty transport and a known
// baseline .psf, polls until ReControl answers, and exposes a connected client.
// Resolves the binary and baseline from WILDPALMS_POSE64_BIN /
// WILDPALMS_PALM_BASELINE_PSF. configured() is false when either is unset.
class EmulatorFixture
{
public:
    EmulatorFixture();
    ~EmulatorFixture();

    static bool configured();

    // Launch the emulator on a free TCP port and wait until ReControl is ready.
    // Returns false (and sets lastError()) on failure.
    bool launch();

    // Reset to the baseline (load <psf>) and re-query the pty. Use between tests.
    bool loadBaseline();

    // The current pty slave path (e.g. /dev/pts/7), re-queried after launch/load.
    QString ptyPath() const { return m_pty; }

    ReControlClient *client() const { return m_client.get(); }
    quint16 port() const { return m_port; }
    QString lastError() const { return m_lastError; }

    // Convenience wrappers over ReControl.
    bool exportDatabase(const QString &dbName, const QString &hostPath);
    bool cradleTap();
    bool dismissProblemFormIfPresent(); // form id 12000 -> tap-id 12004

    void quit();

private:
    bool waitForReady(int timeoutMs);
    bool refreshPty(int timeoutMs);

    QProcess m_proc;
    std::unique_ptr<ReControlClient> m_client;
    quint16 m_port = 0;
    QString m_pty;
    QString m_lastError;
};

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 2: Implement the fixture**

Overwrite `tests/device-e2e/emulatorfixture.cpp`:

```cpp
#include "emulatorfixture.h"
#include "recontrolclient.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTcpServer>
#include <QThread>

namespace WildPalms {
namespace DeviceE2E {

EmulatorFixture::EmulatorFixture()
    : m_client(std::make_unique<ReControlClient>())
{
}

EmulatorFixture::~EmulatorFixture()
{
    quit();
}

bool EmulatorFixture::configured()
{
    return !qEnvironmentVariableIsEmpty("WILDPALMS_POSE64_BIN")
        && !qEnvironmentVariableIsEmpty("WILDPALMS_PALM_BASELINE_PSF");
}

static quint16 pickFreePort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 p = s.serverPort();
    s.close();
    return p;
}

bool EmulatorFixture::launch()
{
    const QString bin = qEnvironmentVariable("WILDPALMS_POSE64_BIN");
    const QString psf = qEnvironmentVariable("WILDPALMS_PALM_BASELINE_PSF");
    if (!QFileInfo::exists(bin)) { m_lastError = QStringLiteral("pose64 binary not found: %1").arg(bin); return false; }
    if (!QFileInfo::exists(psf)) { m_lastError = QStringLiteral("baseline psf not found: %1").arg(psf); return false; }

    m_port = pickFreePort();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    m_proc.setProcessEnvironment(env);
    m_proc.setProcessChannelMode(QProcess::MergedChannels);

    const QStringList args{
        QStringLiteral("-psf"), psf,
        QStringLiteral("--port"), QString::number(m_port),
        QStringLiteral("-preference"), QStringLiteral("PortSerial=serial:pty:HotSync"),
    };
    m_proc.start(bin, args);
    if (!m_proc.waitForStarted(5000)) { m_lastError = QStringLiteral("pose64 failed to start"); return false; }

    if (!waitForReady(25000)) { m_lastError = QStringLiteral("ReControl not ready: %1").arg(m_lastError); return false; }
    if (!m_client->connectTo(m_port, 10000)) { m_lastError = QStringLiteral("could not connect ReControl session"); return false; }
    if (!refreshPty(5000)) return false;
    return true;
}

bool EmulatorFixture::waitForReady(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        if (m_proc.state() != QProcess::Running) { m_lastError = QStringLiteral("pose64 exited during startup"); return false; }
        ReControlClient probe; // fully separate connection; closes on scope exit (one session at a time)
        if (probe.connectTo(m_port, 2000)) {
            const ReControlReply r = probe.command(QStringLiteral("state"), 2000);
            probe.disconnect();
            if (r.ok)
                return true;
        }
        QThread::msleep(400);
    }
    m_lastError = QStringLiteral("timed out");
    return false;
}

bool EmulatorFixture::refreshPty(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    static const QRegularExpression re(QStringLiteral("pty=(/dev/pts/[0-9]+)"));
    while (!deadline.hasExpired()) {
        const ReControlReply r = m_client->commandMultiline(QStringLiteral("info"), 5000);
        const QRegularExpressionMatch m = re.match(r.raw);
        if (m.hasMatch()) { m_pty = m.captured(1); return true; }
        QThread::msleep(250);
    }
    m_lastError = QStringLiteral("no pty= in info");
    return false;
}

bool EmulatorFixture::loadBaseline()
{
    const QString psf = qEnvironmentVariable("WILDPALMS_PALM_BASELINE_PSF");
    const ReControlReply r = m_client->command(QStringLiteral("load %1").arg(psf), 15000);
    if (!r.ok) { m_lastError = QStringLiteral("load failed: %1").arg(r.head); return false; }
    return refreshPty(5000);
}

bool EmulatorFixture::exportDatabase(const QString &dbName, const QString &hostPath)
{
    const ReControlReply r = m_client->command(QStringLiteral("export %1 %2").arg(dbName, hostPath), 30000);
    if (!r.ok) m_lastError = QStringLiteral("export failed: %1").arg(r.head);
    return r.ok;
}

bool EmulatorFixture::cradleTap()
{
    const ReControlReply r = m_client->command(QStringLiteral("button cradle tap"), 5000);
    if (!r.ok) m_lastError = QStringLiteral("cradle tap failed: %1").arg(r.head);
    return r.ok;
}

bool EmulatorFixture::dismissProblemFormIfPresent()
{
    const ReControlReply ui = m_client->commandMultiline(QStringLiteral("ui"), 5000);
    if (!ui.raw.contains(QStringLiteral("id=12000")))
        return false;
    m_client->command(QStringLiteral("tap-id 12004"), 5000);
    return true;
}

void EmulatorFixture::quit()
{
    if (m_client && m_client->isConnected())
        m_client->command(QStringLiteral("quit"), 2000);
    if (m_proc.state() != QProcess::NotRunning) {
        if (!m_proc.waitForFinished(3000)) {
            m_proc.kill();
            m_proc.waitForFinished(2000);
        }
    }
}

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 3: Add a skip-gated fixture smoke method (also validates the baseline)**

Replace `tests/device-e2e/tst_device_e2e_hotsync.cpp` with:

```cpp
#include <QTest>
#include <QtGlobal>
#include "emulatorfixture.h"
#include "recontrolclient.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::DeviceE2E;

class TestDeviceE2EHotSync : public QObject
{
    Q_OBJECT
private slots:
    void emulatorLaunchesAndExposesPtyAndDatebook()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        EmulatorFixture emu;
        QVERIFY2(emu.launch(), qPrintable(emu.lastError()));
        QVERIFY(!emu.ptyPath().isEmpty());
        // Baseline must expose a DatebookDB for export to work.
        const ReControlReply apps = emu.client()->commandMultiline(QStringLiteral("apps"), 5000);
        QVERIFY2(apps.raw.contains(QStringLiteral("DatebookDB")),
                 "baseline psf has no DatebookDB; pick/create a baseline where Datebook exists");
    }

    void hubToPalm_calendar_firstHotSync()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        QVERIFY(true); // replaced in Task 6
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestDeviceE2EHotSync)
#include "tst_device_e2e_hotsync.moc"
```

- [ ] **Step 4: Update the integration exe's link line to pull the lib + Network**

In `tests/device-e2e/CMakeLists.txt`, replace the `tst_device_e2e_hotsync` `target_link_libraries(...)` line with:

```cmake
target_link_libraries(tst_device_e2e_hotsync PRIVATE Qt::Core Qt::Network Qt::Test WildPalmsDeviceE2E)
```

- [ ] **Step 5: Build the whole subtree; confirm it still skips without env (no emulator yet)**

Run:
```bash
cmake -S . -B build && cmake --build build -j 8 --target tst_device_e2e_hotsync
ctest --test-dir build -R tst_device_e2e_hotsync -V 2>&1 | grep -E "SKIP|Passed|Failed"
```
Expected: both methods `QSKIP` (env unset) → test Passed.

- [ ] **Step 6: Run the fixture smoke against a real emulator (manual, on the dev box)**

Run:
```bash
WILDPALMS_POSE64_BIN=/home/clinton/dev/POSE64/build/pose64 \
WILDPALMS_PALM_BASELINE_PSF=/home/clinton/dev/POSE64/freshm515.psf \
QT_QPA_PLATFORM=offscreen \
./build/tests/device-e2e/tst_device_e2e_hotsync emulatorLaunchesAndExposesPtyAndDatebook -v2 2>&1 | tail -30
```
Expected: PASS — emulator launches headless, ReControl answers, `info` yields a pty, `apps` lists `DatebookDB`.

> **If `apps` lacks `DatebookDB`:** `freshm515.psf` is not a usable baseline. Create one once: launch `pose64 -psf <a clean m515 session>`, open the Datebook app (so `DatebookDB` is created), then `save <path>` via ReControl, and point `WILDPALMS_PALM_BASELINE_PSF` at it. Document the chosen baseline path in `docs/device-e2e-harness.md` (Task 7).

- [ ] **Step 7: Commit**

```bash
git add tests/device-e2e/emulatorfixture.h tests/device-e2e/emulatorfixture.cpp \
        tests/device-e2e/tst_device_e2e_hotsync.cpp tests/device-e2e/CMakeLists.txt
git commit -m "test(device-e2e): EmulatorFixture (launch/ready/pty/export) + baseline smoke method"
```

---

## Task 5: `canonseed` — build a canon calendar event for the hub (TDD on the byte builder)

**Files:**
- Create: `tests/device-e2e/canonseed.h`, `tests/device-e2e/canonseed.cpp`
- Create: `tests/device-e2e/tst_canon_calendar_seed.cpp`
- Modify: `tests/device-e2e/CMakeLists.txt`

- [ ] **Step 1: Write the header**

`tests/device-e2e/canonseed.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace WildPalms {
namespace DeviceE2E {

// A minimal calendar event to seed into the hub. Datetimes are UTC.
// categories are intentionally omitted (event lands in Unfiled / slot 0) to
// avoid the device AppInfo category-reconciliation dependency.
struct CanonCalendarEventSpec {
    QString uid = QStringLiteral("seed-event-001@wildpalms");
    QString summary = QStringLiteral("Seeded Event");        // -> Palm appointment description (title)
    QString description = QStringLiteral("Note body text");  // -> Palm note
    QDateTime start = QDateTime(QDate(2026, 7, 1), QTime(9, 0, 0), QTimeZone::utc());
    QDateTime end = QDateTime(QDate(2026, 7, 1), QTime(10, 0, 0), QTimeZone::utc());
    bool allDay = false;
    bool withAlarm = true;
    int alarmOffsetSeconds = -600; // 10 minutes before
};

// Returns the canon-envelope JSON bytes for the hub's "calendar" collection.
QByteArray buildCanonCalendarEvent(const CanonCalendarEventSpec &spec);

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 2: Write the failing unit test**

`tests/device-e2e/tst_canon_calendar_seed.cpp`:

```cpp
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "canonseed.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::DeviceE2E;

class TestCanonCalendarSeed : public QObject
{
    Q_OBJECT
private slots:
    void buildsValidCanonEnvelope()
    {
        CanonCalendarEventSpec spec; // defaults
        const QByteArray bytes = buildCanonCalendarEvent(spec);
        QVERIFY(!bytes.isEmpty());

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        const QJsonObject obj = doc.object();

        QCOMPARE(obj.value(QStringLiteral("_canon")).toObject().value(QStringLiteral("domain")).toString(),
                 QStringLiteral("calendar"));
        QCOMPARE(obj.value(QStringLiteral("uid")).toString(), QStringLiteral("seed-event-001@wildpalms"));
        QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("Seeded Event"));
        QCOMPARE(obj.value(QStringLiteral("description")).toString(), QStringLiteral("Note body text"));
        QCOMPARE(obj.value(QStringLiteral("allDay")).toBool(), false);
        QCOMPARE(obj.value(QStringLiteral("start")).toObject().value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-07-01T09:00:00Z"));
        QCOMPARE(obj.value(QStringLiteral("end")).toObject().value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-07-01T10:00:00Z"));
        const auto alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms.at(0).toObject().value(QStringLiteral("offset")).toInt(), -600);
        QVERIFY(!obj.contains(QStringLiteral("categories"))); // Unfiled by design
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestCanonCalendarSeed)
#include "tst_canon_calendar_seed.moc"
```

- [ ] **Step 3: Add the target; build to confirm it FAILS**

In `tests/device-e2e/CMakeLists.txt`, after the decoder test block, add:

```cmake
add_executable(tst_canon_calendar_seed tst_canon_calendar_seed.cpp canonseed.cpp)
target_link_libraries(tst_canon_calendar_seed PRIVATE Qt::Core Qt::Test Kalburator::Sync)
add_test(NAME tst_canon_calendar_seed COMMAND tst_canon_calendar_seed)
set_tests_properties(tst_canon_calendar_seed PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Run:
```bash
cmake -S . -B build && cmake --build build -j 8 --target tst_canon_calendar_seed 2>&1 | tail -20
```
Expected: link error — `buildCanonCalendarEvent` undefined.

- [ ] **Step 4: Implement the builder**

`tests/device-e2e/canonseed.cpp`:

```cpp
#include "canonseed.h"

#include <QJsonArray>
#include <QJsonObject>

#include <canonenvelope.h>

namespace WildPalms {
namespace DeviceE2E {

QByteArray buildCanonCalendarEvent(const CanonCalendarEventSpec &spec)
{
    using Kalburator::Shape::CanonEnvelope;

    QJsonObject ev;
    ev.insert(QStringLiteral("summary"), spec.summary);
    ev.insert(QStringLiteral("description"), spec.description);

    QJsonObject start;
    start.insert(QStringLiteral("dateTime"), spec.start.toUTC().toString(Qt::ISODate));
    start.insert(QStringLiteral("floating"), false);
    QJsonObject end;
    end.insert(QStringLiteral("dateTime"), spec.end.toUTC().toString(Qt::ISODate));
    end.insert(QStringLiteral("floating"), false);
    ev.insert(QStringLiteral("start"), start);
    ev.insert(QStringLiteral("end"), end);
    ev.insert(QStringLiteral("allDay"), spec.allDay);

    if (spec.withAlarm) {
        QJsonObject alarm;
        alarm.insert(QStringLiteral("type"), 1); // Display
        alarm.insert(QStringLiteral("offset"), spec.alarmOffsetSeconds);
        alarm.insert(QStringLiteral("text"), QStringLiteral("Reminder"));
        QJsonArray alarms;
        alarms.append(alarm);
        ev.insert(QStringLiteral("alarms"), alarms);
    }

    CanonEnvelope::stampEnvelope(ev, QStringLiteral("calendar"), spec.uid);
    return CanonEnvelope::serialize(ev);
}

} // namespace DeviceE2E
} // namespace WildPalms
```

- [ ] **Step 5: Build and run — expect PASS**

Run:
```bash
cmake --build build -j 8 --target tst_canon_calendar_seed && ctest --test-dir build -R tst_canon_calendar_seed -V 2>&1 | grep -E "PASS|FAIL|Passed|Failed"
```
Expected: PASS.

> **If the build fails on `<canonenvelope.h>`:** confirm the include is exposed by `Kalburator::Sync` with `grep -rn "stampEnvelope" build/_deps/libkalburator-src/src/shape/canonenvelope.h`; if the namespace differs from `Kalburator::Shape::CanonEnvelope`, use the exact one shown there.

- [ ] **Step 6: Commit**

```bash
git add tests/device-e2e/canonseed.h tests/device-e2e/canonseed.cpp \
        tests/device-e2e/tst_canon_calendar_seed.cpp tests/device-e2e/CMakeLists.txt
git commit -m "test(device-e2e): canon calendar event seed builder + JSON-shape unit test"
```

---

## Task 6: The integration test — real HotSync, hub→Palm calendar fidelity

Wires all components: launch → seed real hub → connect to pty (attach-then-tap) → `hotSync()` → export → decode → assert.

**Files:**
- Modify: `tests/device-e2e/tst_device_e2e_hotsync.cpp` (implement `hubToPalm_calendar_firstHotSync`)
- Modify: `tests/device-e2e/CMakeLists.txt` (link the Palm/runtime stack + canonseed)

- [ ] **Step 1: Link the integration exe against the full runtime + Palm stack**

In `tests/device-e2e/CMakeLists.txt`, replace the `tst_device_e2e_hotsync` target block with:

```cmake
add_executable(tst_device_e2e_hotsync tst_device_e2e_hotsync.cpp canonseed.cpp)
target_link_libraries(tst_device_e2e_hotsync
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Test
        WildPalmsDeviceE2E
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_device_e2e_hotsync COMMAND tst_device_e2e_hotsync)
set_tests_properties(tst_device_e2e_hotsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "device-e2e"
)
```

- [ ] **Step 2: Implement the integration test**

Replace `tests/device-e2e/tst_device_e2e_hotsync.cpp` with:

```cpp
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtGlobal>

#include "emulatorfixture.h"
#include "recontrolclient.h"
#include "pilotlinkdecoder.h"
#include "canonseed.h"
#include "../wildpalms_qtest_main.h"

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include "backendregistry.h"
#include "backendrecord.h"
#include <genericsqlitebackend.h>

using namespace WildPalms::DeviceE2E;
using namespace WildPalms::Runtime;

class TestDeviceE2EHotSync : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("TZ", "UTC"); // deterministic wall-clock for the fidelity assertion
        tzset();
    }

    void emulatorLaunchesAndExposesPtyAndDatebook()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        EmulatorFixture emu;
        QVERIFY2(emu.launch(), qPrintable(emu.lastError()));
        QVERIFY(!emu.ptyPath().isEmpty());
        const ReControlReply apps = emu.client()->commandMultiline(QStringLiteral("apps"), 5000);
        QVERIFY2(apps.raw.contains(QStringLiteral("DatebookDB")),
                 "baseline psf has no DatebookDB; see docs/device-e2e-harness.md");
    }

    void hubToPalm_calendar_firstHotSync()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");

        // 1. Launch a clean emulated Palm with a HotSync pty.
        EmulatorFixture emu;
        QVERIFY2(emu.launch(), qPrintable(emu.lastError()));
        QVERIFY2(emu.loadBaseline(), qPrintable(emu.lastError())); // isolation + fresh pty
        const QString pty = emu.ptyPath();
        QVERIFY(!pty.isEmpty());

        // 2. Headless PalmRuntime over a fresh profile dir.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        // 3. Seed the REAL hub's "calendar" collection with one canon event.
        auto *base = rt.backendRegistry().backendInstance(QStringLiteral("wp-hub"));
        auto *hub = dynamic_cast<Kalburator::Sinks::GenericSqliteBackend *>(base);
        QVERIFY2(hub, "wp-hub backend missing or wrong type");
        const CanonCalendarEventSpec spec;
        Kalburator::Sync::BackendRecord rec;
        rec.id = spec.uid;
        rec.type = QStringLiteral("calendar");
        rec.data = buildCanonCalendarEvent(spec);
        rec.lastModified = QDateTime::currentDateTimeUtc();
        const QString createdId = hub->createRecord(QStringLiteral("calendar"), rec);
        QVERIFY(!createdId.isEmpty());

        // 4. Connect over the pty (single-element list => WP skips its probe).
        //    Attach-then-tap: enter pi_accept_to, then send the cradle tap. Retry once.
        QSignalSpy ready(&rt, &PalmRuntime::readyForSync);
        QSignalSpy completed(&rt, &PalmRuntime::connectionComplete);
        bool connected = false;
        for (int attempt = 0; attempt < 2 && !connected; ++attempt) {
            rt.connectDevice(QStringList{pty});
            QTest::qWait(1000); // let the worker open the pty + enter pi_accept_to
            QVERIFY2(emu.cradleTap(), qPrintable(emu.lastError()));
            // Wait up to 30s for connectionComplete.
            QTRY_VERIFY_WITH_TIMEOUT(completed.count() >= 1, 30000);
            const auto args = completed.takeFirst();
            connected = args.at(0).toBool();
            if (!connected) {
                emu.dismissProblemFormIfPresent();
                QTest::qWait(500);
            }
        }
        QVERIFY2(connected, "device did not connect (attach-then-tap failed twice)");
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 5000); // finishConnect built palm<->hub mappings

        // 5. One HotSync.
        auto future = rt.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 60000);
        const PalmRunResult result = future.resultAt(0);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.perPluginStats.value(QStringLiteral("calendar")).created, 1);

        // 6. Export the on-device DatebookDB and decode it independently.
        const QString exported = profileDir.filePath(QStringLiteral("exported-DatebookDB.pdb"));
        QVERIFY2(emu.exportDatabase(QStringLiteral("DatebookDB"), exported), qPrintable(emu.lastError()));
        const QList<DecodedAppointment> appts = readAppointments(exported);

        // 7. Fidelity assertions: exactly the seeded event survived the wire.
        QCOMPARE(appts.size(), 1);
        const DecodedAppointment &a = appts.first();
        QCOMPARE(a.description, QStringLiteral("Seeded Event")); // canon summary -> Palm description
        QCOMPARE(a.note, QStringLiteral("Note body text"));      // canon description -> Palm note
        QCOMPARE(a.allDay, false);
        QCOMPARE(a.begin, QDateTime(QDate(2026, 7, 1), QTime(9, 0, 0)));
        QCOMPARE(a.end, QDateTime(QDate(2026, 7, 1), QTime(10, 0, 0)));
        QCOMPARE(a.category, 0); // Unfiled
        QVERIFY(a.hasAlarm);
        QCOMPARE(a.advance, 10);
        QCOMPARE(a.advanceUnits, 0); // advMinutes
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestDeviceE2EHotSync)
#include "tst_device_e2e_hotsync.moc"
```

- [ ] **Step 3: Build and confirm it still SKIPs without env**

Run:
```bash
cmake -S . -B build && cmake --build build -j 8 --target tst_device_e2e_hotsync
ctest --test-dir build -R tst_device_e2e_hotsync -V 2>&1 | grep -E "SKIP|Passed|Failed"
```
Expected: SKIP (env unset) → Passed. Plain `ctest --test-dir build -j 8` still green overall.

- [ ] **Step 4: Run the real integration test on the dev box**

Run:
```bash
WILDPALMS_POSE64_BIN=/home/clinton/dev/POSE64/build/pose64 \
WILDPALMS_PALM_BASELINE_PSF=/home/clinton/dev/POSE64/freshm515.psf \
ctest --test-dir build -L device-e2e -V 2>&1 | tail -40
```
Expected: PASS — the seeded calendar event lands on the emulated Palm and decodes with matching fields.

> **Troubleshooting (expected first-run friction — this is where the real integration debugging lives):**
> - **`created == 0` / no calendar mapping:** `finishConnect` didn't build the palm↔hub Star (it may require a profile). Add a minimal profile: `Profile p(profileDir.path()); p.initialize(); rt.setProfile(&p);` before `connectDevice` (keep `p` alive for the test), or inject the mapping via `rt.setMappingsForTest({...})`. Re-run.
> - **Connection times out:** WP's `pi_accept_to` window may be shorter than the qWait+tap gap. Reduce the `QTest::qWait` before the tap, or raise WP's accept timeout if a setter exists on `KPilotDeviceLink`. Confirm the pty is fresh after `loadBaseline` (the fixture re-queries `info`).
> - **`export` fails / 0 records:** ensure the sync actually ended (HotSync complete) before export; the `future.isFinished()` gate covers this. If `DatebookDB` is missing, fix the baseline (Task 4 Step 6 note).
> - **Datetime mismatch:** confirm `TZ=UTC` took effect (it is set in `initTestCase`); the emulator only stores the wall-clock WP writes.
> - **Use the emulator log:** on failure, capture POSE64 stderr (the fixture merges channels into the QProcess; add a `qDebug() << emu...` dump or temporarily redirect) and `ui`/`state`/`dialog` to see if a modal form blocked the sync.

- [ ] **Step 5: Commit**

```bash
git add tests/device-e2e/tst_device_e2e_hotsync.cpp tests/device-e2e/CMakeLists.txt
git commit -m "test(device-e2e): hub->Palm calendar HotSync fidelity over real POSE64 pty/DLP"
```

---

## Task 7: Docs + roadmap update

**Files:**
- Create: `docs/device-e2e-harness.md`
- Modify: `CLAUDE.md` (roadmap / status)

- [ ] **Step 1: Write the runbook**

`docs/device-e2e-harness.md`:

```markdown
# Device-e2e HotSync harness (POSE64)

Opt-in end-to-end tests that drive a real HotSync against a headless POSE64
emulator over a real pty/DLP link, and assert record-level fidelity with an
independent pilot-link decoder.

## Run

    WILDPALMS_POSE64_BIN=/home/clinton/dev/POSE64/build/pose64 \
    WILDPALMS_PALM_BASELINE_PSF=/home/clinton/dev/POSE64/freshm515.psf \
    ctest --test-dir build -L device-e2e -V

Without the two env vars the integration test `QSKIP`s (so plain `ctest` is
unaffected). The component unit tests (`tst_recontrol_client`,
`tst_pilotlink_decoder`, `tst_canon_calendar_seed`) always run.

## Baseline .psf

Must be a calibrated, HotSync-ready m515 session whose `apps` list includes
`DatebookDB`. If `freshm515.psf` does not qualify, create one: launch
`pose64 -psf <clean m515>`, open the Datebook app once, `save <path>` via
ReControl, and point `WILDPALMS_PALM_BASELINE_PSF` at it.

## Architecture

See `docs/superpowers/specs/2026-06-14-pose64-e2e-hotsync-harness-design.md`.
Phase 1 covers hub→Palm calendar. Next: the fidelity matrix (conduits × modes ×
edit/conflict/delete) and the three-tier remote leg.
```

- [ ] **Step 2: Add a status note to `CLAUDE.md`**

Add under the roadmap (near the hardware-verification notes) a short entry:

```markdown
### POSE64 e2e HotSync harness (Phase 1) — LANDED <date>

`tests/device-e2e/` drives a real HotSync against a headless POSE64 emulator over
its pty/DLP link and asserts calendar fidelity via an independent pilot-link
decoder. Opt-in: `ctest -L device-e2e` with `WILDPALMS_POSE64_BIN` +
`WILDPALMS_PALM_BASELINE_PSF`; skips otherwise (plain ctest unchanged). First
scenario: hub→Palm calendar, clean first HotSync. Next phases: fidelity matrix +
three-tier remote leg (retire the hardware-pending backlog). Spec/plan under
`docs/superpowers/{specs,plans}/2026-06-14-pose64-e2e-hotsync-harness*`.
```

- [ ] **Step 3: Final full-suite run**

Run:
```bash
ctest --test-dir build -j 8 2>&1 | tail -5
```
Expected: prior 126 + 3 new unit tests pass; `tst_device_e2e_hotsync` skips (or passes if env set).

- [ ] **Step 4: Commit**

```bash
git add docs/device-e2e-harness.md CLAUDE.md
git commit -m "docs(device-e2e): harness runbook + roadmap status"
```

---

## Self-review notes (for the implementer)

- **Spec coverage:** §1 goal → Task 6; §3 components → Tasks 2/3/4/5; §4 lifecycle (baseline, per-test load, retry-once, crash visibility) → Task 4 (`loadBaseline`, `dismissProblemFormIfPresent`, bounded `QTRY`/timeouts) + Task 6; §5 gating → Task 1 (label + `QSKIP`); §First scenario → Task 6. Category assertion is satisfied as `category==0` (Unfiled), with named-category fidelity deferred (it depends on the AppInfo reconciler, itself a hardware-gated item).
- **No `src/` change** is required; if Task 6 Step 4 reveals `finishConnect` needs a profile to emit mappings, the fix is test-side (`setProfile`/`setMappingsForTest`), documented in the troubleshooting note.
- **Type/name consistency:** `WildPalms::DeviceE2E` namespace throughout; `ReControlReply.head/.body/.raw`; `DecodedAppointment` fields used identically in Tasks 3 and 6; hub id `"wp-hub"`, collection `"calendar"`; `buildCanonCalendarEvent`/`CanonCalendarEventSpec` identical in Tasks 5 and 6.
- **API versions pinned:** pilot-link 0.13.0 `pi_buffer` `unpack_Appointment`; libkalburator `v0.77`.
```
