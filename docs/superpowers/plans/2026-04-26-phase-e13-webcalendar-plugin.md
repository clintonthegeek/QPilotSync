# Phase E.13 — WebCalendar Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the WebCalendar conduit as the fifth new-ABI `IBackendPlugin`, sourcing iCalendar feeds via a new libkalburator `IcsFeedFetcher` and one-way mirroring them into per-feed-dedicated Palm Datebook category slots through `BlobSyncEngine::mirror`.

**Architecture:** `IcsFeedFetcher` (libkalburator, `src/calendar/`) wraps `QNetworkAccessManager` + `KCalendarCore::ICalFormat`, replacing legacy regex splitting and ad-hoc RRULE parsing. `WebcalBackendPlugin` (WP, claims no Palm DB) returns `ProvidedBackends.blob = WebcalBlobBackend`, the source side of mirror. Each feed targets a dedicated `palm:calendar/<slot>` collection. Cache-on-failure plus `lastFetchSucceeded(slot)` gate prevents the empty-source-deletes-target footgun. Behind CMake toggle `WILDPALMS_WEBCALENDAR_PLUGIN_V2=ON`; legacy `WebCalendarConduit` keeps building when off.

**Tech Stack:** C++20, Qt6 (Core, Network, Test), KF6::CoreAddons (`KPluginFactory`, `kcoreaddons_add_plugin`), KF6::CalendarCore (`ICalFormat`, `MemoryCalendar`, `Incidence`), `Kalburator::Sync` (`IBlobBackend`, `BlobSyncEngine::mirror`, `MockBlobBackend`, `BackendRecord`, `CollectionInfo`).

**Parent spec:** `docs/superpowers/specs/2026-04-26-phase-e13-webcalendar-plugin-design.md`.

**Repos:**
- libkalburator at `~/dev/libkalburator/` (Tasks 1, 2). Tests gated by `KALBURATOR_BUILD_TESTS`.
- WildPalms parent at `~/dev/WildPalms/`. Build dir: `build-dev/`.
- webcalendar submodule at `src/plugins/webcalendar/` (`wildpalms-conduit-webcalendar.git`). Currently on detached HEAD `7673879`; Task 3 puts it on a working branch before committing.

**Repo split:**
- Tasks 1, 2: commits inside `~/dev/libkalburator/`.
- Tasks 3, 4, 5: commits inside `src/plugins/webcalendar/` submodule.
- Task 6: parent repo (tests).
- Task 7: parent repo wrap-up + submodule pointer bump.

**Pre-existing assets (do NOT re-create):**
- libkalburator already links `Qt6::Network` and uses `KCalendarCore::ICalFormat` widely (`src/journal/calendarjournal.cpp`, `src/transcoding/syncdiff.cpp`).
- libkalburator's `src/calendar/` uses `file(GLOB ... CONFIGURE_DEPENDS)` — new `.h`/`.cpp` files in that dir are picked up automatically; no `add_library(...)` edit needed.
- `Kalburator::Sync::BlobSyncEngine::mirror` (Phase B2) at `libkalburator/src/blob/blobsyncengine.h:52`.
- `Kalburator::Sync::IBlobBackend` at `libkalburator/src/blob/iblobbackend.h:28`.
- `Kalburator::Sync::BackendRecord` at `libkalburator/src/types/backendrecord.h:14`.
- `Kalburator::Sync::CollectionInfo` at `libkalburator/src/types/collectioninfo.h:9`.
- `Kalburator::Sync::MockBlobBackend` at `libkalburator/src/blob/mockblobbackend.h`.
- `WildPalms::IBackendPlugin` at `src/core/ibackendplugin.h:38`.
- `PalmDeviceConnection` (forward-declared in `ibackendplugin.h:22`; concrete type in `src/palm/palmdeviceconnection.h`).

---

## Scope explicitly excluded

- Settings widget — `createSettingsWidget` returns `nullptr`. UI work lands in E.17.
- HTTP conditional GET (`If-Modified-Since`, `ETag`). Defer.
- Authentication (basic, OAuth, tokens). Defer.
- Auto-migration of legacy `category` (text) → `palm_slot` (int). Legacy-format feeds load disabled with a warning; user reconfigures once.
- Runtime cross-plugin mirror pairing — invocation happens in tests via `tst_webcal_v2_e2e`. Production runtime pairing is E.15+.
- Conflict handler — mirror does not consult the registry. `createConflictHandler() == nullptr`.
- AppInfo write-on-first-sync (slot naming). User pre-names slots via the Palm itself.
- `LocalBlobBackend` real-target tests — id-space cutover is E.15+; e2e uses `MockBlobBackend`.
- Live-device POSE64 integration — E.18.
- Legacy `WebCalendarConduit` removal — E.16.
- Persistent per-feed `lastFetchTime` — in-memory only for E.13.
- `KCalendarCore::Recurrence::timesInInterval` perf optimisation — day-by-day `recursOn` loop is fine for E.13's expected feed sizes (sub-millisecond on 100-event feeds).
- `WebcalSubscriptionBackend` (full `SubscriptionBackend` subclass in libkalburator) — only the free `IcsFeedFetcher` ships now.

---

## File Structure

**Files to CREATE in libkalburator — Tasks 1, 2:**

- `src/calendar/icsfeedfetcher.h` (Task 1)
- `src/calendar/icsfeedfetcher.cpp` (Task 1)
- `tests/calendar/CMakeLists.txt` (Task 1)
- `tests/calendar/tst_icsfeedfetcher.cpp` (Task 1)
- `tests/calendar/fixtures/single_event.ics` (Task 1)
- `tests/calendar/fixtures/many_events.ics` (Task 1)
- `tests/calendar/fixtures/recurring_event.ics` (Task 1)
- `tests/calendar/fixtures/with_vtimezone.ics` (Task 1)
- `tests/calendar/fixtures/malformed.ics` (Task 1)

**Files to MODIFY in libkalburator — Task 1:**

- `tests/CMakeLists.txt` — add `add_subdirectory(calendar)`.

**Files to CREATE in webcalendar submodule — Tasks 3–5:**

- `webcalfeed.h`, `webcalfeed.cpp` (Task 3) — POD + JSON serialiser.
- `webcalblobbackend.h`, `webcalblobbackend.cpp` (Task 4) — IBlobBackend source side.
- `webcalbackendplugin.h`, `webcalbackendplugin.cpp` (Task 5) — IBackendPlugin shell + factory.
- `webcal-backend-plugin.json` (Task 5) — new manifest.

**Files to MODIFY in webcalendar submodule — Task 5:**

- `CMakeLists.txt` — add `WILDPALMS_WEBCALENDAR_PLUGIN_V2` toggle; build new plugin when on, legacy when off.

**Files to CREATE in WildPalms parent — Tasks 3–6:**

- `tests/plugins/webcalendar/CMakeLists.txt` (Task 3, grown per task)
- `tests/plugins/webcalendar/tst_webcalfeed.cpp` (Task 3)
- `tests/plugins/webcalendar/tst_webcalblobbackend.cpp` (Task 4)
- `tests/plugins/webcalendar/tst_webcalbackendplugin.cpp` (Task 5)
- `tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp` (Task 6)
- `tests/plugins/webcalendar/fixtures/three_events.ics` (Task 4)
- `tests/plugins/webcalendar/fixtures/two_events.ics` (Task 4)

**Files to MODIFY in WildPalms parent — Tasks 3, 7:**

- `tests/plugins/CMakeLists.txt` (Task 3) — add `add_subdirectory(webcalendar)`.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (Task 7) — flip row E.13 to `✅ **E.13**`.
- Submodule pointer bump for `src/plugins/webcalendar/` (Task 7).
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + new `project_phase_e13_webcalendar.md` (Task 7).

---

## Conventions used throughout

- **libkalburator namespace:** `Kalburator::Sync` (matches existing `IcsTranscoder`/`SubscriptionBackend` neighbourhood).
- **WildPalms plugin namespace:** `WildPalms::WebcalPlugin` (mirrors `WildPalms::TodoPlugin`, `WildPalms::ContactsPlugin`).
- **Plugin id:** `webcalendar`.
- **Plugin install namespace:** `wildpalms/plugins`.
- **Backend id:** `webcal`.
- **Collection id namespace:** `palm:calendar/<N>` — webcal source emits the **same** collection ids as `CalendarBlobBackend`'s target, so mirror is plug-and-play.
- **MIME type / record `type` field:** `event` (matches Calendar plugin's `BackendRecord.type` field).
- **Feed validity rule:** `name` non-empty AND `url.isValid()` AND `palm_slot ∈ [1, 15]` AND `enabled == true`.

---

## Task 1: `IcsFeedFetcher` in libkalburator

**Why:** Move iCalendar URL fetching + parsing out of the WildPalms plugin and into libkalburator, where iCalendar work belongs (per `feedback_library_vs_backend_responsibility.md`). Replaces the legacy regex-based event splitting and ad-hoc `RRULE`/`UNTIL` parsing with `KCalendarCore::ICalFormat`. Reusable by future PlanStan/ShadowStan webcal features.

**Files:**
- Create: `~/dev/libkalburator/src/calendar/icsfeedfetcher.h`
- Create: `~/dev/libkalburator/src/calendar/icsfeedfetcher.cpp`
- Create: `~/dev/libkalburator/tests/calendar/CMakeLists.txt`
- Create: `~/dev/libkalburator/tests/calendar/tst_icsfeedfetcher.cpp`
- Create: 5 fixture files under `~/dev/libkalburator/tests/calendar/fixtures/`
- Modify: `~/dev/libkalburator/tests/CMakeLists.txt`

- [ ] **Step 1: Create `src/calendar/icsfeedfetcher.h`**

```cpp
#ifndef KALBURATOR_CALENDAR_ICSFEEDFETCHER_H
#define KALBURATOR_CALENDAR_ICSFEEDFETCHER_H

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <KCalendarCore/Incidence>

class QNetworkAccessManager;

namespace Kalburator::Sync {

/**
 * @brief Synchronous fetcher for iCalendar feeds over HTTP(S) and file://.
 *
 * Wraps QNetworkAccessManager + KCalendarCore::ICalFormat into a single
 * blocking call returning the parsed Incidences (with optional date-range
 * filtering, RRULE expansion handled by KCalendarCore).
 *
 * The caller owns the QNetworkAccessManager (lets the caller centralise
 * proxy/redirect policy and keeps the fetcher trivially mockable).
 *
 * Designed for sibling-of-HolidaySubscriptionBackend reuse: a future
 * WebcalSubscriptionBackend can wrap this fetcher; the WildPalms webcal
 * plugin uses it directly as the source side of BlobSyncEngine::mirror.
 */
class IcsFeedFetcher : public QObject
{
    Q_OBJECT
public:
    struct Result {
        bool success = false;
        QString errorMessage;
        QList<KCalendarCore::Incidence::Ptr> incidences;
    };

    explicit IcsFeedFetcher(QNetworkAccessManager *network,
                            QObject *parent = nullptr);
    ~IcsFeedFetcher() override;

    /**
     * @brief Fetch and parse a feed.
     *
     * If both startDate and endDate are valid, only incidences active in
     * [startDate, endDate] are returned (single-occurrence: dtStart in
     * range; recurring: any occurrence in range, RRULE/EXDATE expanded
     * by KCalendarCore).
     *
     * Synchronous: spins a local QEventLoop until the reply finishes or
     * the timeout fires.
     */
    Result fetch(const QUrl &url,
                 const QDate &startDate = {},
                 const QDate &endDate   = {},
                 int timeoutMs = 30000);

Q_SIGNALS:
    void progress(const QString &message);

private:
    QNetworkAccessManager *m_network; // borrowed
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 2: Create `src/calendar/icsfeedfetcher.cpp`**

```cpp
#include "icsfeedfetcher.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QTimer>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

IcsFeedFetcher::IcsFeedFetcher(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{
}

IcsFeedFetcher::~IcsFeedFetcher() = default;

IcsFeedFetcher::Result IcsFeedFetcher::fetch(const QUrl &url,
                                              const QDate &startDate,
                                              const QDate &endDate,
                                              int timeoutMs)
{
    Result r;
    if (!url.isValid()) {
        r.errorMessage = QStringLiteral("Invalid URL");
        return r;
    }
    if (!m_network) {
        r.errorMessage = QStringLiteral("No QNetworkAccessManager");
        return r;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("libkalburator/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    Q_EMIT progress(QStringLiteral("Fetching %1").arg(url.toString()));

    QNetworkReply *reply = m_network->get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        r.errorMessage = QStringLiteral("Timeout");
        return r;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        r.errorMessage = reply->errorString();
        reply->deleteLater();
        return r;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty()) {
        r.errorMessage = QStringLiteral("Empty response");
        return r;
    }

    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromRawString(cal, data)) {
        r.errorMessage = QStringLiteral("Failed to parse iCalendar");
        return r;
    }

    const auto raw = cal->rawIncidences();

    if (!startDate.isValid() && !endDate.isValid()) {
        r.incidences = raw;
    } else {
        const QDate start = startDate.isValid() ? startDate : QDate(1970, 1, 1);
        const QDate end   = endDate.isValid()   ? endDate   : QDate(9999, 12, 31);
        for (const auto &inc : raw) {
            if (inc->recurs()) {
                bool active = false;
                for (QDate d = start; d <= end; d = d.addDays(1)) {
                    if (inc->recursOn(d, QTimeZone::utc())) {
                        active = true;
                        break;
                    }
                }
                if (active) {
                    r.incidences.append(inc);
                }
            } else {
                const QDate d = inc->dtStart().date();
                if (d.isValid() && d >= start && d <= end) {
                    r.incidences.append(inc);
                }
            }
        }
    }

    r.success = true;
    return r;
}

} // namespace Kalburator::Sync
```

- [ ] **Step 3: Create test fixtures**

Create `~/dev/libkalburator/tests/calendar/fixtures/single_event.ics`:

```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//libkalburator//tst_icsfeedfetcher//EN
BEGIN:VEVENT
UID:single-event-001@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260615T100000Z
DTEND:20260615T110000Z
SUMMARY:Single test event
END:VEVENT
END:VCALENDAR
```

Create `~/dev/libkalburator/tests/calendar/fixtures/many_events.ics`:

```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//libkalburator//tst_icsfeedfetcher//EN
BEGIN:VEVENT
UID:event-001@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260601T100000Z
DTEND:20260601T110000Z
SUMMARY:Event one
END:VEVENT
BEGIN:VEVENT
UID:event-002@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260615T100000Z
DTEND:20260615T110000Z
SUMMARY:Event two
END:VEVENT
BEGIN:VEVENT
UID:event-003@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260701T100000Z
DTEND:20260701T110000Z
SUMMARY:Event three
END:VEVENT
END:VCALENDAR
```

Create `~/dev/libkalburator/tests/calendar/fixtures/recurring_event.ics`:

```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//libkalburator//tst_icsfeedfetcher//EN
BEGIN:VEVENT
UID:weekly-001@example.com
DTSTAMP:20200101T120000Z
DTSTART:20200107T100000Z
DTEND:20200107T110000Z
RRULE:FREQ=WEEKLY
SUMMARY:Weekly meeting
END:VEVENT
BEGIN:VEVENT
UID:past-recurring-001@example.com
DTSTAMP:20100101T120000Z
DTSTART:20100107T100000Z
DTEND:20100107T110000Z
RRULE:FREQ=WEEKLY;UNTIL=20120101T000000Z
SUMMARY:Old weekly that ended
END:VEVENT
END:VCALENDAR
```

Create `~/dev/libkalburator/tests/calendar/fixtures/with_vtimezone.ics`:

```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//libkalburator//tst_icsfeedfetcher//EN
BEGIN:VTIMEZONE
TZID:America/New_York
BEGIN:STANDARD
DTSTART:20071104T020000
RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU
TZOFFSETFROM:-0400
TZOFFSETTO:-0500
TZNAME:EST
END:STANDARD
BEGIN:DAYLIGHT
DTSTART:20070311T020000
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU
TZOFFSETFROM:-0500
TZOFFSETTO:-0400
TZNAME:EDT
END:DAYLIGHT
END:VTIMEZONE
BEGIN:VEVENT
UID:tz-event-001@example.com
DTSTAMP:20260601T120000Z
DTSTART;TZID=America/New_York:20260615T100000
DTEND;TZID=America/New_York:20260615T110000
SUMMARY:Event with TZ
END:VEVENT
END:VCALENDAR
```

Create `~/dev/libkalburator/tests/calendar/fixtures/malformed.ics`:

```
This is not iCalendar.
Just plain text.
```

- [ ] **Step 4: Write the failing test file**

Create `~/dev/libkalburator/tests/calendar/tst_icsfeedfetcher.cpp`:

```cpp
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <KCalendarCore/Event>

#include "icsfeedfetcher.h"

using namespace Kalburator::Sync;

namespace {
QUrl fixtureUrl(const QString &name)
{
    const QString fixtureDir = QStringLiteral(KALBURATOR_CALENDAR_FIXTURE_DIR);
    return QUrl::fromLocalFile(QDir(fixtureDir).absoluteFilePath(name));
}
} // namespace

class TestIcsFeedFetcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_network = new QNetworkAccessManager(this);
        m_fetcher = new IcsFeedFetcher(m_network, this);
    }

    void cleanupTestCase()
    {
        delete m_fetcher;
        m_fetcher = nullptr;
        delete m_network;
        m_network = nullptr;
    }

    // ----- Single-event fetch -----
    void singleEvent_fetchesOneIncidence()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("single_event.ics"));
        QVERIFY(r.success);
        QCOMPARE(r.incidences.size(), 1);
        QCOMPARE(r.incidences.first()->uid(),
                 QStringLiteral("single-event-001@example.com"));
        QCOMPARE(r.incidences.first()->summary(),
                 QStringLiteral("Single test event"));
    }

    // ----- Multi-event fetch -----
    void manyEvents_fetchesAllIncidences()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("many_events.ics"));
        QVERIFY(r.success);
        QCOMPARE(r.incidences.size(), 3);
    }

    // ----- Date-range filter, single-occurrence -----
    void dateRange_filtersSingleOccurrenceOutsideRange()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("many_events.ics"),
                                         QDate(2026, 6, 10),
                                         QDate(2026, 6, 20));
        QVERIFY(r.success);
        QCOMPARE(r.incidences.size(), 1);
        QCOMPARE(r.incidences.first()->uid(),
                 QStringLiteral("event-002@example.com"));
    }

    // ----- Date-range filter, recurring kept -----
    void dateRange_keepsRecurringWithOccurrenceInRange()
    {
        // recurring_event.ics: weekly starting 2020-01-07, no UNTIL → still
        // recurring in 2026.
        const auto r = m_fetcher->fetch(fixtureUrl("recurring_event.ics"),
                                         QDate(2026, 6, 10),
                                         QDate(2026, 6, 20));
        QVERIFY(r.success);
        // The endless weekly should be kept; the past-recurring (UNTIL=2012)
        // should be filtered out.
        QCOMPARE(r.incidences.size(), 1);
        QCOMPARE(r.incidences.first()->uid(),
                 QStringLiteral("weekly-001@example.com"));
    }

    // ----- Date-range filter, recurring with UNTIL in past dropped -----
    void dateRange_dropsRecurringEndedBeforeRange()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("recurring_event.ics"),
                                         QDate(2026, 6, 10),
                                         QDate(2026, 6, 20));
        QVERIFY(r.success);
        for (const auto &inc : r.incidences) {
            QVERIFY(inc->uid() != QStringLiteral("past-recurring-001@example.com"));
        }
    }

    // ----- VTIMEZONE handles -----
    void vtimezone_parsesWithoutError()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("with_vtimezone.ics"));
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QCOMPARE(r.incidences.size(), 1);
    }

    // ----- Error path: invalid URL -----
    void invalidUrl_returnsFailure()
    {
        const auto r = m_fetcher->fetch(QUrl("not a url"));
        QVERIFY(!r.success);
        QCOMPARE(r.errorMessage, QStringLiteral("Invalid URL"));
    }

    // ----- Error path: malformed iCal -----
    void malformedIcal_returnsParseFailure()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("malformed.ics"));
        QVERIFY(!r.success);
        QVERIFY(r.errorMessage.contains(QStringLiteral("parse"),
                                         Qt::CaseInsensitive));
    }

    // ----- Error path: missing file -----
    void missingFile_returnsNetworkFailure()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("does_not_exist.ics"));
        QVERIFY(!r.success);
        QVERIFY(!r.errorMessage.isEmpty());
    }

    // ----- progress signal fires -----
    void progressSignal_emittedOnFetch()
    {
        QSignalSpy spy(m_fetcher, &IcsFeedFetcher::progress);
        m_fetcher->fetch(fixtureUrl("single_event.ics"));
        QVERIFY(spy.count() >= 1);
    }

private:
    QNetworkAccessManager *m_network = nullptr;
    IcsFeedFetcher *m_fetcher = nullptr;
};

QTEST_MAIN(TestIcsFeedFetcher)
#include "tst_icsfeedfetcher.moc"
```

- [ ] **Step 5: Create `tests/calendar/CMakeLists.txt`**

```cmake
# Phase E.13 — calendar-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

set(KALBURATOR_CALENDAR_FIXTURE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

function(kalburator_add_calendar_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_compile_definitions(${TEST_NAME}
        PRIVATE
            KALBURATOR_CALENDAR_FIXTURE_DIR="${KALBURATOR_CALENDAR_FIXTURE_DIR}"
    )
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            KF6::CalendarCore
            Kalburator::Sync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_calendar_test(tst_icsfeedfetcher)
```

- [ ] **Step 6: Wire the new test subdir into `tests/CMakeLists.txt`**

Modify `~/dev/libkalburator/tests/CMakeLists.txt`:

Replace:
```
add_subdirectory(blob)
add_subdirectory(journal)
```

With:
```
add_subdirectory(blob)
add_subdirectory(journal)
add_subdirectory(calendar)
```

- [ ] **Step 7: Configure libkalburator build (top-level)**

```bash
cmake -S ~/dev/libkalburator -B ~/dev/libkalburator/build-dev \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configures successfully; `tst_icsfeedfetcher` listed as new test target.

- [ ] **Step 8: Build the test**

```bash
cmake --build ~/dev/libkalburator/build-dev --target tst_icsfeedfetcher -j4
```

Expected: SUCCESS.

- [ ] **Step 9: Run the test**

```bash
ctest --test-dir ~/dev/libkalburator/build-dev -R tst_icsfeedfetcher --output-on-failure
```

Expected: PASS, 10 sub-tests (`singleEvent_*`, `manyEvents_*`, four date-range tests, `vtimezone_*`, three error-path tests, `progressSignal_*`).

- [ ] **Step 10: Run the full libkalburator test suite to confirm no regression**

```bash
ctest --test-dir ~/dev/libkalburator/build-dev --output-on-failure
```

Expected: PASS, no regressions in `blob` or `journal` test groups.

- [ ] **Step 11: Stage the new files**

```bash
cd ~/dev/libkalburator
git add src/calendar/icsfeedfetcher.h src/calendar/icsfeedfetcher.cpp \
        tests/CMakeLists.txt \
        tests/calendar/CMakeLists.txt \
        tests/calendar/tst_icsfeedfetcher.cpp \
        tests/calendar/fixtures/single_event.ics \
        tests/calendar/fixtures/many_events.ics \
        tests/calendar/fixtures/recurring_event.ics \
        tests/calendar/fixtures/with_vtimezone.ics \
        tests/calendar/fixtures/malformed.ics
git status
```

Expected: 10 new files staged.

(Do NOT commit yet — Task 2 runs the PlanStan baseline gate first.)

---

## Task 2: PlanStan baseline gate + commit

**Why:** Per `feedback_planstan_pretest_for_upstream.md`, every libkalburator commit must pass PlanStan's ctest baseline before landing. This task runs PlanStan against the staged changes and, if green, commits.

**Files:**
- Modify: working tree of `~/dev/PlanStan/` (build only, no source edits expected).

- [ ] **Step 1: Locate PlanStan and confirm it tracks libkalburator as add_subdirectory or via local install**

```bash
grep -n "libkalburator\|add_subdirectory.*kalburator\|find_package.*Kalburator" \
    ~/dev/PlanStan/CMakeLists.txt 2>&1 | head -10
```

Expected: at least one line referencing libkalburator's integration.

- [ ] **Step 2: Configure PlanStan against the in-place libkalburator (with new fetcher)**

```bash
cmake -S ~/dev/PlanStan -B ~/dev/PlanStan/build-dev \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configures cleanly, picks up the new `IcsFeedFetcher` source via libkalburator's GLOB.

- [ ] **Step 3: Build PlanStan**

```bash
cmake --build ~/dev/PlanStan/build-dev -j4
```

Expected: SUCCESS. If a build error references `IcsFeedFetcher`, the new public header pulled an unintended symbol; revisit Task 1 step 1 (only forward-declare types in the header that aren't already public via libkalburator's umbrella).

- [ ] **Step 4: Run PlanStan ctest baseline**

```bash
ctest --test-dir ~/dev/PlanStan/build-dev --output-on-failure
```

Expected: 86/26/112 (the baseline noted in `project_plugin_abi_e8.md`) or equivalent — *no new failures vs. PlanStan's pre-Task-1 state*. If failures appear that name `IcsFeedFetcher` or include any header from `src/calendar/`, the change is not actually additive — STOP and investigate before committing.

- [ ] **Step 5: Commit the libkalburator change**

```bash
cd ~/dev/libkalburator
git commit -m "$(cat <<'EOF'
feat(calendar): add IcsFeedFetcher

Synchronous URL-to-Incidences fetcher built on QNetworkAccessManager
+ KCalendarCore::ICalFormat. Optional date-range filter expands RRULE
via KCalendarCore (no ad-hoc UNTIL parsing). Sibling-of-Holiday
position in the SubscriptionBackend docstring's named cohort
(holiday/webcal/rss); free class rather than SubscriptionBackend
subclass since the consumers it has today (WildPalms webcal plugin)
operate at the IBlobBackend layer, not the SubscriptionBackend layer.

Tested against five fixture .ics files (single, multi, recurring,
VTIMEZONE, malformed) with 10 QTest cases covering happy path, range
filtering, RRULE expansion, and three error paths. PlanStan ctest
baseline green.

Refs: WildPalms phase E.13.
EOF
)"
git status
```

Expected: commit created; tree clean.

---

## Task 3: `WebcalFeed` POD (submodule) + tests

**Why:** Settings schema for the new plugin. Strictly typed `palm_slot` (int) replaces legacy `category` (text). Legacy-format JSON loads disabled with a warning.

**Files (submodule):**
- Create: `src/plugins/webcalendar/webcalfeed.h`
- Create: `src/plugins/webcalendar/webcalfeed.cpp`

**Files (parent):**
- Create: `tests/plugins/webcalendar/CMakeLists.txt`
- Create: `tests/plugins/webcalendar/tst_webcalfeed.cpp`
- Modify: `tests/plugins/CMakeLists.txt`

- [ ] **Step 1: Put the submodule on a working branch**

```bash
cd ~/dev/WildPalms/src/plugins/webcalendar
git checkout -b phase-e13-webcalendar-plugin
git status
```

Expected: detached HEAD becomes `phase-e13-webcalendar-plugin` branch.

- [ ] **Step 2: Create `webcalfeed.h`**

```cpp
#ifndef WILDPALMS_WEBCAL_WEBCALFEED_H
#define WILDPALMS_WEBCAL_WEBCALFEED_H

#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace WildPalms::WebcalPlugin {

/**
 * @brief Configuration for one webcal feed.
 *
 * Strictly 1:1 with a Palm Datebook category slot (slot 0 reserved for
 * "Unfiled" / hand-entered events; slots 1..15 may each be reserved by
 * at most one feed).
 *
 * Legacy schema migration: a JSON object carrying `category` (text) but
 * not `palm_slot` is loaded with `palmSlot = -1` and `enabled = false`,
 * and the loader logs a warning. The user reconfigures once.
 */
struct WebcalFeed {
    QString name;
    QUrl    url;
    int     palmSlot   = -1;          ///< 1..15; -1 = unconfigured
    bool    enabled    = true;
    QString fetchPolicy = QStringLiteral("weekly");
        ///< "every_sync" | "daily" | "weekly" | "monthly"

    QJsonObject toJson() const;
    static WebcalFeed fromJson(const QJsonObject &obj);

    /// Returns true if the feed is fully configured: name non-empty,
    /// URL valid, palmSlot in [1, 15].
    bool isValid() const;
};

} // namespace WildPalms::WebcalPlugin

#endif
```

- [ ] **Step 3: Create `webcalfeed.cpp`**

```cpp
#include "webcalfeed.h"

#include <QJsonValue>
#include <QLoggingCategory>

namespace WildPalms::WebcalPlugin {

namespace {
Q_LOGGING_CATEGORY(lc, "wildpalms.webcal")
}

QJsonObject WebcalFeed::toJson() const
{
    QJsonObject obj;
    obj["name"]         = name;
    obj["url"]          = url.toString();
    obj["palm_slot"]    = palmSlot;
    obj["enabled"]      = enabled;
    obj["fetch_policy"] = fetchPolicy;
    return obj;
}

WebcalFeed WebcalFeed::fromJson(const QJsonObject &obj)
{
    WebcalFeed f;
    f.name        = obj.value(QStringLiteral("name")).toString();
    f.url         = QUrl(obj.value(QStringLiteral("url")).toString());
    f.enabled     = obj.value(QStringLiteral("enabled")).toBool(true);
    f.fetchPolicy = obj.value(QStringLiteral("fetch_policy"))
                        .toString(QStringLiteral("weekly"));

    const QJsonValue slot = obj.value(QStringLiteral("palm_slot"));
    if (slot.isDouble()) {
        f.palmSlot = slot.toInt(-1);
    } else if (obj.contains(QStringLiteral("category"))) {
        // Legacy schema: text category, no slot. Disable + warn.
        qCWarning(lc).noquote()
            << "Feed" << f.name
            << "uses legacy 'category' field — disabled. Reconfigure with palm_slot.";
        f.palmSlot = -1;
        f.enabled  = false;
    }
    return f;
}

bool WebcalFeed::isValid() const
{
    return !name.isEmpty()
        && url.isValid()
        && palmSlot >= 1 && palmSlot <= 15;
}

} // namespace WildPalms::WebcalPlugin
```

- [ ] **Step 4: Create `tests/plugins/webcalendar/tst_webcalfeed.cpp` (parent)**

```cpp
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "webcalfeed.h"

using WildPalms::WebcalPlugin::WebcalFeed;

class TestWebcalFeed : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip_preservesAllFields()
    {
        WebcalFeed f;
        f.name        = QStringLiteral("US Holidays");
        f.url         = QUrl(QStringLiteral("https://example.com/us.ics"));
        f.palmSlot    = 5;
        f.enabled     = true;
        f.fetchPolicy = QStringLiteral("daily");

        const auto obj = f.toJson();
        const auto g   = WebcalFeed::fromJson(obj);

        QCOMPARE(g.name, f.name);
        QCOMPARE(g.url, f.url);
        QCOMPARE(g.palmSlot, f.palmSlot);
        QCOMPARE(g.enabled, f.enabled);
        QCOMPARE(g.fetchPolicy, f.fetchPolicy);
    }

    void legacyCategoryField_disablesFeed()
    {
        QJsonObject obj;
        obj[QStringLiteral("name")]     = QStringLiteral("Old Format");
        obj[QStringLiteral("url")]      = QStringLiteral("https://example.com/x.ics");
        obj[QStringLiteral("category")] = QStringLiteral("Work");
        obj[QStringLiteral("enabled")]  = true;

        const auto f = WebcalFeed::fromJson(obj);
        QVERIFY(!f.enabled);
        QCOMPARE(f.palmSlot, -1);
    }

    void slotOutOfRange_invalid()
    {
        WebcalFeed f;
        f.name     = QStringLiteral("x");
        f.url      = QUrl(QStringLiteral("https://example.com/x.ics"));
        f.palmSlot = 0;
        QVERIFY(!f.isValid());
        f.palmSlot = 16;
        QVERIFY(!f.isValid());
        f.palmSlot = 1;
        QVERIFY(f.isValid());
        f.palmSlot = 15;
        QVERIFY(f.isValid());
    }

    void emptyName_invalid()
    {
        WebcalFeed f;
        f.url      = QUrl(QStringLiteral("https://example.com/x.ics"));
        f.palmSlot = 5;
        QVERIFY(!f.isValid());
    }

    void invalidUrl_invalid()
    {
        WebcalFeed f;
        f.name     = QStringLiteral("x");
        f.url      = QUrl();
        f.palmSlot = 5;
        QVERIFY(!f.isValid());
    }

    void defaultFetchPolicy_isWeekly()
    {
        QJsonObject obj;
        obj[QStringLiteral("name")] = QStringLiteral("x");
        obj[QStringLiteral("url")]  = QStringLiteral("https://example.com/x.ics");
        obj[QStringLiteral("palm_slot")] = 5;
        // No fetch_policy set.
        const auto f = WebcalFeed::fromJson(obj);
        QCOMPARE(f.fetchPolicy, QStringLiteral("weekly"));
    }
};

QTEST_GUILESS_MAIN(TestWebcalFeed)
#include "tst_webcalfeed.moc"
```

- [ ] **Step 5: Create `tests/plugins/webcalendar/CMakeLists.txt` (parent)**

```cmake
# Phase E.13 — WebCalendar plugin tests.
# Tasks 3-6 build test binaries directly against the source files in
# the webcalendar submodule.

set(WEBCAL_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/webcalendar)

# --- Task 3: WebcalFeed ---
add_executable(tst_webcalfeed
    tst_webcalfeed.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_include_directories(tst_webcalfeed
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_webcalfeed
    PRIVATE
        Qt::Test
        Qt::Core
)
add_test(NAME tst_webcalfeed COMMAND tst_webcalfeed)
```

- [ ] **Step 6: Wire `tests/plugins/webcalendar` into `tests/plugins/CMakeLists.txt`**

Modify `~/dev/WildPalms/tests/plugins/CMakeLists.txt`. Append:

```cmake
# Phase E.13 — WebCalendar plugin tests.
add_subdirectory(webcalendar)
```

- [ ] **Step 7: Configure WildPalms**

```bash
cmake -S ~/dev/WildPalms -B ~/dev/WildPalms/build-dev \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configures cleanly; new `tst_webcalfeed` target picked up.

- [ ] **Step 8: Build the test**

```bash
cmake --build ~/dev/WildPalms/build-dev --target tst_webcalfeed -j4
```

Expected: SUCCESS.

- [ ] **Step 9: Run the test**

```bash
ctest --test-dir ~/dev/WildPalms/build-dev -R tst_webcalfeed --output-on-failure
```

Expected: PASS, 6 sub-tests.

- [ ] **Step 10: Commit submodule**

```bash
cd ~/dev/WildPalms/src/plugins/webcalendar
git add webcalfeed.h webcalfeed.cpp
git commit -m "feat(webcal): add WebcalFeed POD + JSON serializer (Phase E.13 Task 3)"
git status
```

Expected: clean tree at new commit on `phase-e13-webcalendar-plugin`.

- [ ] **Step 11: Commit parent**

```bash
cd ~/dev/WildPalms
git add tests/plugins/CMakeLists.txt \
        tests/plugins/webcalendar/CMakeLists.txt \
        tests/plugins/webcalendar/tst_webcalfeed.cpp
git commit -m "test(webcal): tst_webcalfeed (Phase E.13 Task 3)"
git status
```

Expected: clean tree.

---

## Task 4: `WebcalBlobBackend` (submodule) + tests

**Why:** The IBlobBackend implementation that drives the source side of `BlobSyncEngine::mirror`. Wraps `IcsFeedFetcher`, exposes one `palm:calendar/<slot>` collection per enabled+valid feed, caches-on-failure, and offers `lastFetchSucceeded(slot)` so callers can avoid the empty-source-deletes-target footgun.

**Files (submodule):**
- Create: `src/plugins/webcalendar/webcalblobbackend.h`
- Create: `src/plugins/webcalendar/webcalblobbackend.cpp`

**Files (parent):**
- Create: `tests/plugins/webcalendar/tst_webcalblobbackend.cpp`
- Create: `tests/plugins/webcalendar/fixtures/three_events.ics`
- Create: `tests/plugins/webcalendar/fixtures/two_events.ics`
- Modify: `tests/plugins/webcalendar/CMakeLists.txt`

- [ ] **Step 1: Create `webcalblobbackend.h`**

```cpp
#ifndef WILDPALMS_WEBCAL_WEBCALBLOBBACKEND_H
#define WILDPALMS_WEBCAL_WEBCALBLOBBACKEND_H

#include <QHash>
#include <QList>
#include <QObject>

#include <iblobbackend.h>

#include "webcalfeed.h"

namespace Kalburator::Sync {
class IcsFeedFetcher;
}

namespace WildPalms::WebcalPlugin {

/**
 * @brief Source-side IBlobBackend for webcal feeds.
 *
 * Each enabled+valid feed exposes one `palm:calendar/<slot>` collection.
 * `loadRecords` fetches the feed via IcsFeedFetcher, serializes each
 * Incidence as a single-event VCALENDAR blob, and returns them.
 *
 * On fetch failure, the backend returns the cached last-successful
 * fetch (or empty list if none); callers should consult
 * `lastFetchSucceeded(slot)` before invoking BlobSyncEngine::mirror to
 * avoid the empty-source-deletes-target footgun on first-cold failure.
 *
 * Read-only on the IBlobBackend interface — createRecord, updateRecord,
 * and deleteRecord are no-ops returning failure values. Mirror only
 * writes to the target side.
 */
class WebcalBlobBackend : public Kalburator::Sync::IBlobBackend
{
    Q_OBJECT
public:
    WebcalBlobBackend(QList<WebcalFeed> feeds,
                      Kalburator::Sync::IcsFeedFetcher *fetcher,
                      QObject *parent = nullptr);
    ~WebcalBlobBackend() override;

    // ===== Identity =====
    QString backendId() const override   { return QStringLiteral("webcal"); }
    QString displayName() const override { return QStringLiteral("Web Calendar Subscriptions"); }
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

    // ===== Webcal-specific =====
    /// True if the most recent fetch for `slot` succeeded. Callers
    /// should check this before invoking BlobSyncEngine::mirror with
    /// this backend as source.
    bool lastFetchSucceeded(int slot) const;

    QList<WebcalFeed> feeds() const { return m_feeds; }

private:
    static QString collectionIdForSlot(int slot);
    static int     slotFromCollectionId(const QString &id);
    const WebcalFeed *findFeedBySlot(int slot) const;

    QList<WebcalFeed> m_feeds;
    Kalburator::Sync::IcsFeedFetcher *m_fetcher;  // borrowed
    QHash<int, QList<Kalburator::Sync::BackendRecord>> m_lastSuccessfulFetch;
        ///< Cache by slot
    QHash<int, bool> m_lastFetchOk;
};

} // namespace WildPalms::WebcalPlugin

#endif
```

- [ ] **Step 2: Create `webcalblobbackend.cpp`**

```cpp
#include "webcalblobbackend.h"

#include <QCryptographicHash>
#include <QDate>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimeZone>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <icsfeedfetcher.h>

namespace WildPalms::WebcalPlugin {

namespace {
Q_LOGGING_CATEGORY(lc, "wildpalms.webcal")

constexpr int kDateRangeDays = 365;

QByteArray encodeIncidenceToVcal(const KCalendarCore::Incidence::Ptr &inc)
{
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    cal->addIncidence(inc->clone());
    KCalendarCore::ICalFormat fmt;
    return fmt.toString(cal).toUtf8();
}
} // namespace

WebcalBlobBackend::WebcalBlobBackend(QList<WebcalFeed> feeds,
                                      Kalburator::Sync::IcsFeedFetcher *fetcher,
                                      QObject *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_feeds(std::move(feeds))
    , m_fetcher(fetcher)
{
}

WebcalBlobBackend::~WebcalBlobBackend() = default;

QString WebcalBlobBackend::collectionIdForSlot(int slot)
{
    return QStringLiteral("palm:calendar/%1").arg(slot);
}

int WebcalBlobBackend::slotFromCollectionId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^palm:calendar/(\\d+)$"));
    const auto m = re.match(id);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    return ok ? n : -1;
}

const WebcalFeed *WebcalBlobBackend::findFeedBySlot(int slot) const
{
    for (const auto &f : m_feeds) {
        if (f.palmSlot == slot && f.enabled && f.isValid()) {
            return &f;
        }
    }
    return nullptr;
}

QList<Kalburator::Sync::CollectionInfo> WebcalBlobBackend::availableCollections()
{
    QList<Kalburator::Sync::CollectionInfo> out;
    for (const auto &f : m_feeds) {
        if (!f.enabled || !f.isValid()) continue;
        Kalburator::Sync::CollectionInfo c;
        c.id   = collectionIdForSlot(f.palmSlot);
        c.name = f.name;
        c.type = QStringLiteral("calendar");
        out.append(c);
    }
    return out;
}

Kalburator::Sync::CollectionInfo WebcalBlobBackend::collectionInfo(
    const QString &collectionId)
{
    const int slot = slotFromCollectionId(collectionId);
    if (const auto *f = findFeedBySlot(slot)) {
        Kalburator::Sync::CollectionInfo c;
        c.id   = collectionIdForSlot(f->palmSlot);
        c.name = f->name;
        c.type = QStringLiteral("calendar");
        return c;
    }
    return {};
}

QString WebcalBlobBackend::createCollection(const Kalburator::Sync::CollectionInfo &)
{
    qCWarning(lc) << "createCollection ignored — webcal is read-only";
    return {};
}

QList<Kalburator::Sync::BackendRecord> WebcalBlobBackend::loadRecords(
    const QString &collectionId)
{
    using namespace Kalburator::Sync;

    const int slot = slotFromCollectionId(collectionId);
    const WebcalFeed *feed = findFeedBySlot(slot);
    if (!feed) {
        qCWarning(lc) << "loadRecords: no feed for collection" << collectionId;
        m_lastFetchOk.insert(slot, false);
        return {};
    }

    if (!m_fetcher) {
        qCWarning(lc) << "loadRecords: no fetcher";
        m_lastFetchOk.insert(slot, false);
        return m_lastSuccessfulFetch.value(slot);
    }

    const QDate today = QDate::currentDate();
    const auto r = m_fetcher->fetch(feed->url, today,
                                     today.addDays(kDateRangeDays));

    if (!r.success) {
        qCWarning(lc) << "loadRecords:" << feed->name
                       << "fetch failed:" << r.errorMessage;
        m_lastFetchOk.insert(slot, false);
        return m_lastSuccessfulFetch.value(slot);
    }

    QList<BackendRecord> records;
    records.reserve(r.incidences.size());
    for (const auto &inc : r.incidences) {
        BackendRecord rec;
        rec.id           = inc->uid();
        rec.type         = QStringLiteral("event");
        rec.displayName  = inc->summary();
        rec.data         = encodeIncidenceToVcal(inc);
        rec.contentHash  = QCryptographicHash::hash(rec.data,
                                                     QCryptographicHash::Sha256)
                                .toHex();
        rec.lastModified = inc->lastModified();
        records.append(rec);
    }

    m_lastSuccessfulFetch.insert(slot, records);
    m_lastFetchOk.insert(slot, true);
    return records;
}

std::optional<Kalburator::Sync::BackendRecord> WebcalBlobBackend::loadRecord(
    const QString &recordId)
{
    for (auto it = m_lastSuccessfulFetch.constBegin();
         it != m_lastSuccessfulFetch.constEnd(); ++it) {
        for (const auto &r : it.value()) {
            if (r.id == recordId) return r;
        }
    }
    return std::nullopt;
}

QString WebcalBlobBackend::createRecord(const QString &,
                                         const Kalburator::Sync::BackendRecord &)
{
    qCWarning(lc) << "createRecord ignored — webcal is read-only";
    return {};
}

bool WebcalBlobBackend::updateRecord(const Kalburator::Sync::BackendRecord &)
{
    qCWarning(lc) << "updateRecord ignored — webcal is read-only";
    return false;
}

bool WebcalBlobBackend::deleteRecord(const QString &)
{
    qCWarning(lc) << "deleteRecord ignored — webcal is read-only";
    return false;
}

QList<Kalburator::Sync::BackendRecord> WebcalBlobBackend::modifiedSince(
    const QString &collectionId, const QDateTime &since)
{
    QList<Kalburator::Sync::BackendRecord> out;
    for (const auto &r : loadRecords(collectionId)) {
        if (r.lastModified > since) out.append(r);
    }
    return out;
}

QStringList WebcalBlobBackend::deletedSince(const QString &, const QDateTime &)
{
    return {};
}

bool WebcalBlobBackend::lastFetchSucceeded(int slot) const
{
    return m_lastFetchOk.value(slot, false);
}

} // namespace WildPalms::WebcalPlugin
```

- [ ] **Step 3: Create test fixtures**

Create `~/dev/WildPalms/tests/plugins/webcalendar/fixtures/three_events.ics`:

```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//WildPalms//tst_webcalblobbackend//EN
BEGIN:VEVENT
UID:wb-001@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260615T100000Z
DTEND:20260615T110000Z
SUMMARY:Webcal A
END:VEVENT
BEGIN:VEVENT
UID:wb-002@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260616T100000Z
DTEND:20260616T110000Z
SUMMARY:Webcal B
END:VEVENT
BEGIN:VEVENT
UID:wb-003@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260617T100000Z
DTEND:20260617T110000Z
SUMMARY:Webcal C
END:VEVENT
END:VCALENDAR
```

Create `~/dev/WildPalms/tests/plugins/webcalendar/fixtures/two_events.ics`:

```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//WildPalms//tst_webcalblobbackend//EN
BEGIN:VEVENT
UID:wb-101@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260615T100000Z
DTEND:20260615T110000Z
SUMMARY:Other A
END:VEVENT
BEGIN:VEVENT
UID:wb-102@example.com
DTSTAMP:20260601T120000Z
DTSTART:20260616T100000Z
DTEND:20260616T110000Z
SUMMARY:Other B
END:VEVENT
END:VCALENDAR
```

- [ ] **Step 4: Create `tests/plugins/webcalendar/tst_webcalblobbackend.cpp`**

```cpp
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>

#include <icsfeedfetcher.h>

#include "webcalblobbackend.h"
#include "webcalfeed.h"

using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

namespace {
QUrl fixtureUrl(const QString &name)
{
    const QString fixtureDir = QStringLiteral(WEBCAL_FIXTURE_DIR);
    return QUrl::fromLocalFile(QDir(fixtureDir).absoluteFilePath(name));
}
} // namespace

class TestWebcalBlobBackend : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_network = new QNetworkAccessManager(this);
        m_fetcher = new Kalburator::Sync::IcsFeedFetcher(m_network, this);
    }

    void availableCollections_oneEntryPerEnabledValidFeed()
    {
        WebcalFeed a;
        a.name = QStringLiteral("A"); a.url = fixtureUrl("three_events.ics");
        a.palmSlot = 5; a.enabled = true;
        WebcalFeed b;
        b.name = QStringLiteral("B"); b.url = fixtureUrl("two_events.ics");
        b.palmSlot = 6; b.enabled = true;
        WebcalFeed c;
        c.name = QStringLiteral("Disabled"); c.url = fixtureUrl("two_events.ics");
        c.palmSlot = 7; c.enabled = false;
        WebcalFeed d;
        d.name = QStringLiteral("BadSlot"); d.url = fixtureUrl("two_events.ics");
        d.palmSlot = 0; d.enabled = true;

        WebcalBlobBackend backend({a, b, c, d}, m_fetcher);
        const auto cols = backend.availableCollections();
        QCOMPARE(cols.size(), 2);
        QCOMPARE(cols[0].id, QStringLiteral("palm:calendar/5"));
        QCOMPARE(cols[1].id, QStringLiteral("palm:calendar/6"));
    }

    void loadRecords_returnsOneRecordPerVEVENT()
    {
        WebcalFeed f;
        f.name = QStringLiteral("Three"); f.url = fixtureUrl("three_events.ics");
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        const auto records = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 3);
        for (const auto &r : records) {
            QCOMPARE(r.type, QStringLiteral("event"));
            QVERIFY(r.data.contains("BEGIN:VEVENT"));
            QVERIFY(!r.contentHash.isEmpty());
        }
        QVERIFY(backend.lastFetchSucceeded(5));
    }

    void loadRecords_independentSlotsHitIndependentFeeds()
    {
        WebcalFeed a, b;
        a.name = QStringLiteral("Three"); a.url = fixtureUrl("three_events.ics");
        a.palmSlot = 5; a.enabled = true;
        b.name = QStringLiteral("Two"); b.url = fixtureUrl("two_events.ics");
        b.palmSlot = 6; b.enabled = true;

        WebcalBlobBackend backend({a, b}, m_fetcher);
        QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/5")).size(), 3);
        QCOMPARE(backend.loadRecords(QStringLiteral("palm:calendar/6")).size(), 2);
    }

    void loadRecords_firstCallFailureReturnsEmpty()
    {
        WebcalFeed f;
        f.name = QStringLiteral("Missing");
        f.url = fixtureUrl("does_not_exist.ics");
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        const auto records = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 0);
        QVERIFY(!backend.lastFetchSucceeded(5));
    }

    void loadRecords_failureWithCacheReturnsCachedList()
    {
        // Copy a fixture to a temp file, load successfully, then delete
        // the temp file and load again — the second load should fail
        // (file::// to nonexistent path) but return the cached list
        // because of cache-on-failure semantics.
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.close();
        const QString tmpPath = tmp.fileName();
        QFile fixtureFile(QDir(QStringLiteral(WEBCAL_FIXTURE_DIR))
                              .absoluteFilePath(QStringLiteral("three_events.ics")));
        QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        const QByteArray bytes = fixtureFile.readAll();
        QFile out(tmpPath);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        out.close();

        WebcalFeed f;
        f.name = QStringLiteral("Temp");
        f.url = QUrl::fromLocalFile(tmpPath);
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        const auto first = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(first.size(), 3);
        QVERIFY(backend.lastFetchSucceeded(5));

        // Delete the temp file → next fetch fails → cache returned.
        QVERIFY(QFile::remove(tmpPath));
        const auto second = backend.loadRecords(QStringLiteral("palm:calendar/5"));
        QVERIFY(!backend.lastFetchSucceeded(5));
        QCOMPARE(second.size(), 3); // cache preserved
        QCOMPARE(second[0].id, first[0].id);
    }

    void writeMethods_areNoOpsOrFalse()
    {
        WebcalFeed f;
        f.name = QStringLiteral("Three"); f.url = fixtureUrl("three_events.ics");
        f.palmSlot = 5; f.enabled = true;

        WebcalBlobBackend backend({f}, m_fetcher);
        Kalburator::Sync::BackendRecord rec;
        rec.id = QStringLiteral("x");
        QVERIFY(backend.createRecord(QStringLiteral("palm:calendar/5"), rec).isEmpty());
        QVERIFY(!backend.updateRecord(rec));
        QVERIFY(!backend.deleteRecord(QStringLiteral("x")));
        QVERIFY(backend.createCollection({}).isEmpty());
    }

private:
    QNetworkAccessManager *m_network = nullptr;
    Kalburator::Sync::IcsFeedFetcher *m_fetcher = nullptr;
};

QTEST_MAIN(TestWebcalBlobBackend)
#include "tst_webcalblobbackend.moc"
```

- [ ] **Step 5: Extend `tests/plugins/webcalendar/CMakeLists.txt`**

Append:

```cmake
# --- Task 4: WebcalBlobBackend ---
set(WEBCAL_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_executable(tst_webcalblobbackend
    tst_webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_compile_definitions(tst_webcalblobbackend
    PRIVATE
        WEBCAL_FIXTURE_DIR="${WEBCAL_FIXTURE_DIR}"
)
target_include_directories(tst_webcalblobbackend
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_webcalblobbackend
    PRIVATE
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CalendarCore
        Kalburator::Sync
)
add_test(NAME tst_webcalblobbackend COMMAND tst_webcalblobbackend)
```

- [ ] **Step 6: Configure + build**

```bash
cmake -S ~/dev/WildPalms -B ~/dev/WildPalms/build-dev
cmake --build ~/dev/WildPalms/build-dev --target tst_webcalblobbackend -j4
```

Expected: SUCCESS.

- [ ] **Step 7: Run the test**

```bash
ctest --test-dir ~/dev/WildPalms/build-dev -R tst_webcalblobbackend --output-on-failure
```

Expected: PASS, 6 sub-tests (the cache-on-failure test only documents the contract; primary cache exercising happens in tst_webcal_v2_e2e).

- [ ] **Step 8: Commit submodule**

```bash
cd ~/dev/WildPalms/src/plugins/webcalendar
git add webcalblobbackend.h webcalblobbackend.cpp
git commit -m "feat(webcal): add WebcalBlobBackend (Phase E.13 Task 4)"
```

- [ ] **Step 9: Commit parent**

```bash
cd ~/dev/WildPalms
git add tests/plugins/webcalendar/CMakeLists.txt \
        tests/plugins/webcalendar/tst_webcalblobbackend.cpp \
        tests/plugins/webcalendar/fixtures/three_events.ics \
        tests/plugins/webcalendar/fixtures/two_events.ics
git commit -m "test(webcal): tst_webcalblobbackend (Phase E.13 Task 4)"
```

---

## Task 5: `WebcalBackendPlugin` (submodule) + plugin JSON + CMake toggle + tests

**Why:** The IBackendPlugin shell exposed to `BackendPluginManager`. Claims no Palm DB (per Decision #4 in design); returns `WebcalBlobBackend` as the source `blob`; null calendar; null conflict handler; no main view. Loads feeds from settings JSON. Lands behind `WILDPALMS_WEBCALENDAR_PLUGIN_V2` toggle.

**Files (submodule):**
- Create: `src/plugins/webcalendar/webcalbackendplugin.h`
- Create: `src/plugins/webcalendar/webcalbackendplugin.cpp`
- Create: `src/plugins/webcalendar/webcal-backend-plugin.json`
- Modify: `src/plugins/webcalendar/CMakeLists.txt`

**Files (parent):**
- Create: `tests/plugins/webcalendar/tst_webcalbackendplugin.cpp`
- Modify: `tests/plugins/webcalendar/CMakeLists.txt`

- [ ] **Step 1: Create `webcalbackendplugin.h`**

```cpp
#ifndef WILDPALMS_WEBCAL_WEBCALBACKENDPLUGIN_H
#define WILDPALMS_WEBCAL_WEBCALBACKENDPLUGIN_H

#include <QJsonObject>
#include <QList>
#include <QObject>

#include <core/ibackendplugin.h>

#include "webcalfeed.h"

class QNetworkAccessManager;

namespace Kalburator::Sync {
class IcsFeedFetcher;
}

namespace WildPalms::WebcalPlugin {

class WebcalBlobBackend;

class WebcalBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
    Q_PLUGIN_METADATA(IID "ca.vibekoder.WildPalms.IBackendPlugin/1.0"
                      FILE "webcal-backend-plugin.json")

public:
    explicit WebcalBackendPlugin(QObject *parent = nullptr);
    ~WebcalBackendPlugin() override;

    // ===== IPlugin =====
    QString pluginId() const override     { return QStringLiteral("webcalendar"); }
    QString displayName() const override  { return QStringLiteral("Web Calendar Subscriptions"); }
    QString description() const override
    { return QStringLiteral("Subscribe to remote iCalendar feeds (read-only)"); }
    QString version() const override      { return QStringLiteral("2.0.0"); }
    QIcon   icon() const override;

    // ===== IBackendPlugin =====
    QStringList claimedDatabases() const override { return {}; }
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                     PalmDeviceConnection         *device) override;
    Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler() override
    { return nullptr; }

    bool hasMainView() const override { return false; }

    QStringList runBefore() const override { return {}; }
    QStringList runAfter() const override  { return {}; }

    // ===== Settings (JSON only; no widget) =====
    bool hasSettings() const { return true; }
    void loadSettings(const QJsonObject &settings);
    QJsonObject saveSettings() const;

    // Read-back for tests / future runtime wiring.
    QList<WebcalFeed> feeds() const { return m_feeds; }
    WebcalBlobBackend *currentBackend() const { return m_backend; }

private:
    /// Validates 1:1 slot allocation; logs error + disables both feeds
    /// on conflict. Mutates m_feeds in place.
    void enforceSlotUniqueness();

    QList<WebcalFeed>                  m_feeds;
    QNetworkAccessManager             *m_network = nullptr;  // owned (parented)
    Kalburator::Sync::IcsFeedFetcher  *m_fetcher = nullptr;  // owned (parented)
    WebcalBlobBackend                 *m_backend = nullptr;  // owned by manager
};

} // namespace WildPalms::WebcalPlugin

#endif
```

- [ ] **Step 2: Create `webcalbackendplugin.cpp`**

```cpp
#include "webcalbackendplugin.h"

#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QNetworkAccessManager>

#include <icsfeedfetcher.h>

#include "webcalblobbackend.h"

namespace WildPalms::WebcalPlugin {

namespace {
Q_LOGGING_CATEGORY(lc, "wildpalms.webcal")
}

WebcalBackendPlugin::WebcalBackendPlugin(QObject *parent)
    : QObject(parent)
{
}

WebcalBackendPlugin::~WebcalBackendPlugin() = default;

QIcon WebcalBackendPlugin::icon() const
{
    return QIcon::fromTheme(QStringLiteral("internet-web-browser"));
}

void WebcalBackendPlugin::enforceSlotUniqueness()
{
    QHash<int, int> slotCount;
    for (const auto &f : m_feeds) {
        if (f.enabled && f.palmSlot >= 1 && f.palmSlot <= 15) {
            ++slotCount[f.palmSlot];
        }
    }
    for (auto &f : m_feeds) {
        if (f.enabled && slotCount.value(f.palmSlot, 0) > 1) {
            qCWarning(lc) << "Multiple feeds claim slot" << f.palmSlot
                          << "— disabling" << f.name;
            f.enabled = false;
        }
    }
}

void WebcalBackendPlugin::loadSettings(const QJsonObject &settings)
{
    m_feeds.clear();
    const QJsonArray arr = settings.value(QStringLiteral("feeds")).toArray();
    for (const auto &val : arr) {
        m_feeds.append(WebcalFeed::fromJson(val.toObject()));
    }
    enforceSlotUniqueness();
}

QJsonObject WebcalBackendPlugin::saveSettings() const
{
    QJsonArray arr;
    for (const auto &f : m_feeds) arr.append(f.toJson());
    QJsonObject obj;
    obj[QStringLiteral("feeds")] = arr;
    return obj;
}

WildPalms::IBackendPlugin::ProvidedBackends WebcalBackendPlugin::createBackends(
    Kalburator::Sync::ISyncHost *host, PalmDeviceConnection *device)
{
    Q_UNUSED(host);
    Q_UNUSED(device);  // Webcal claims no Palm DB; device unused for source side.

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    if (!m_fetcher) {
        m_fetcher = new Kalburator::Sync::IcsFeedFetcher(m_network, this);
    }

    m_backend = new WebcalBlobBackend(m_feeds, m_fetcher);

    ProvidedBackends out;
    out.blob     = m_backend;
    out.calendar = nullptr;
    return out;
}

} // namespace WildPalms::WebcalPlugin

#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(WebcalBackendPluginFactory,
                            "webcal-backend-plugin.json",
                            registerPlugin<WildPalms::WebcalPlugin::WebcalBackendPlugin>();)

#include "webcalbackendplugin.moc"
```

- [ ] **Step 3: Create `webcal-backend-plugin.json`**

```json
{
    "KPlugin": {
        "Name": "Web Calendar Subscriptions",
        "Description": "Subscribe to remote iCalendar feeds (read-only)",
        "Icon": "internet-web-browser",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0.0",
        "Category": "Sync",
        "Id": "webcalendar"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": [],
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 50
}
```

- [ ] **Step 4: Replace `src/plugins/webcalendar/CMakeLists.txt`**

```cmake
option(WILDPALMS_WEBCALENDAR_PLUGIN_V2
    "Build the new IBackendPlugin-based WebCalendar plugin" ON)

if (WILDPALMS_WEBCALENDAR_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_webcalendar_v2
        SOURCES
            webcalbackendplugin.cpp  webcalbackendplugin.h
            webcalblobbackend.cpp    webcalblobbackend.h
            webcalfeed.cpp           webcalfeed.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_webcalendar_v2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    # See calendar plugin's CMakeLists for why this BEFORE include is
    # required (Kalburator::Sync ordering vs WildPalmsCore's legacy
    # ::Sync include).
    target_include_directories(wildpalms_webcalendar_v2 BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(wildpalms_webcalendar_v2
        PRIVATE
            WildPalmsCore
            WildPalmsRuntime
            KF6::CoreAddons
            KF6::CalendarCore
            KF6::I18n
            Qt::Network
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_webcalendar
        SOURCES
            webcalendarconduit.cpp
            webcalendarconduit.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_webcalendar
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        Qt::Network
        Qt::Widgets
    )
endif ()
```

- [ ] **Step 5: Create `tests/plugins/webcalendar/tst_webcalbackendplugin.cpp` (parent)**

```cpp
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "webcalbackendplugin.h"
#include "webcalblobbackend.h"
#include "webcalfeed.h"

using WildPalms::WebcalPlugin::WebcalBackendPlugin;
using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

class TestWebcalBackendPlugin : public QObject
{
    Q_OBJECT

private slots:
    void metadata_correct()
    {
        WebcalBackendPlugin p;
        QCOMPARE(p.pluginId(), QStringLiteral("webcalendar"));
        QCOMPARE(p.claimedDatabases().size(), 0);
        QVERIFY(!p.hasMainView());
        QVERIFY(!p.createConflictHandler());
        QCOMPARE(p.version(), QStringLiteral("2.0.0"));
    }

    void createBackends_returnsBlobNullCalendar()
    {
        WebcalBackendPlugin p;
        const auto out = p.createBackends(nullptr, nullptr);
        QVERIFY(out.blob != nullptr);
        QVERIFY(out.calendar == nullptr);
        QVERIFY(qobject_cast<WebcalBlobBackend *>(out.blob) != nullptr);
        delete out.blob;
    }

    void loadSettings_parsesFeeds()
    {
        QJsonArray feeds;
        QJsonObject f1;
        f1[QStringLiteral("name")]      = QStringLiteral("US");
        f1[QStringLiteral("url")]       = QStringLiteral("https://x.com/us.ics");
        f1[QStringLiteral("palm_slot")] = 5;
        f1[QStringLiteral("enabled")]   = true;
        feeds.append(f1);
        QJsonObject f2;
        f2[QStringLiteral("name")]      = QStringLiteral("UK");
        f2[QStringLiteral("url")]       = QStringLiteral("https://x.com/uk.ics");
        f2[QStringLiteral("palm_slot")] = 6;
        f2[QStringLiteral("enabled")]   = true;
        feeds.append(f2);

        QJsonObject settings;
        settings[QStringLiteral("feeds")] = feeds;

        WebcalBackendPlugin p;
        p.loadSettings(settings);
        QCOMPARE(p.feeds().size(), 2);
        QCOMPARE(p.feeds()[0].palmSlot, 5);
        QCOMPARE(p.feeds()[1].palmSlot, 6);
    }

    void slotCollision_disablesBothFeeds()
    {
        QJsonArray feeds;
        QJsonObject f1;
        f1[QStringLiteral("name")]      = QStringLiteral("A");
        f1[QStringLiteral("url")]       = QStringLiteral("https://x.com/a.ics");
        f1[QStringLiteral("palm_slot")] = 5;
        f1[QStringLiteral("enabled")]   = true;
        feeds.append(f1);
        QJsonObject f2 = f1;
        f2[QStringLiteral("name")] = QStringLiteral("B");
        feeds.append(f2);

        QJsonObject settings;
        settings[QStringLiteral("feeds")] = feeds;

        WebcalBackendPlugin p;
        p.loadSettings(settings);
        QCOMPARE(p.feeds().size(), 2);
        QVERIFY(!p.feeds()[0].enabled);
        QVERIFY(!p.feeds()[1].enabled);
    }

    void saveSettings_roundTrip()
    {
        QJsonArray feeds;
        QJsonObject f1;
        f1[QStringLiteral("name")]      = QStringLiteral("US");
        f1[QStringLiteral("url")]       = QStringLiteral("https://x.com/us.ics");
        f1[QStringLiteral("palm_slot")] = 5;
        f1[QStringLiteral("enabled")]   = true;
        f1[QStringLiteral("fetch_policy")] = QStringLiteral("daily");
        feeds.append(f1);
        QJsonObject in;
        in[QStringLiteral("feeds")] = feeds;

        WebcalBackendPlugin p;
        p.loadSettings(in);
        const auto out = p.saveSettings();
        const auto outFeeds = out.value(QStringLiteral("feeds")).toArray();
        QCOMPARE(outFeeds.size(), 1);
        QCOMPARE(outFeeds[0].toObject().value("palm_slot").toInt(), 5);
        QCOMPARE(outFeeds[0].toObject().value("fetch_policy").toString(),
                 QStringLiteral("daily"));
    }
};

QTEST_GUILESS_MAIN(TestWebcalBackendPlugin)
#include "tst_webcalbackendplugin.moc"
```

- [ ] **Step 6: Extend `tests/plugins/webcalendar/CMakeLists.txt`**

Append:

```cmake
# --- Task 5: WebcalBackendPlugin ---
add_executable(tst_webcalbackendplugin
    tst_webcalbackendplugin.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalbackendplugin.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_include_directories(tst_webcalbackendplugin
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_webcalbackendplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_webcalbackendplugin
    PRIVATE
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        Kalburator::Sync
)
add_test(NAME tst_webcalbackendplugin COMMAND tst_webcalbackendplugin)
```

- [ ] **Step 7: Configure + build the new plugin .so**

```bash
cmake -S ~/dev/WildPalms -B ~/dev/WildPalms/build-dev
cmake --build ~/dev/WildPalms/build-dev --target wildpalms_webcalendar_v2 -j4
```

Expected: SUCCESS, `wildpalms_webcalendar_v2.so` produced under
`build-dev/src/plugins/webcalendar/wildpalms/plugins/`.

- [ ] **Step 8: Build the test**

```bash
cmake --build ~/dev/WildPalms/build-dev --target tst_webcalbackendplugin -j4
```

Expected: SUCCESS.

- [ ] **Step 9: Run the test**

```bash
ctest --test-dir ~/dev/WildPalms/build-dev -R tst_webcalbackendplugin --output-on-failure
```

Expected: PASS, 5 sub-tests.

- [ ] **Step 10: Confirm legacy build still works (toggle OFF)**

```bash
cmake -S ~/dev/WildPalms -B ~/dev/WildPalms/build-dev \
      -DWILDPALMS_WEBCALENDAR_PLUGIN_V2=OFF
cmake --build ~/dev/WildPalms/build-dev --target wildpalms_webcalendar -j4
```

Expected: SUCCESS, legacy `.so` builds. Then restore the default:

```bash
cmake -S ~/dev/WildPalms -B ~/dev/WildPalms/build-dev \
      -DWILDPALMS_WEBCALENDAR_PLUGIN_V2=ON
```

- [ ] **Step 11: Commit submodule**

```bash
cd ~/dev/WildPalms/src/plugins/webcalendar
git add webcalbackendplugin.h webcalbackendplugin.cpp \
        webcal-backend-plugin.json CMakeLists.txt
git commit -m "feat(webcal): add WebcalBackendPlugin + V2 toggle (Phase E.13 Task 5)"
```

- [ ] **Step 12: Commit parent**

```bash
cd ~/dev/WildPalms
git add tests/plugins/webcalendar/CMakeLists.txt \
        tests/plugins/webcalendar/tst_webcalbackendplugin.cpp
git commit -m "test(webcal): tst_webcalbackendplugin (Phase E.13 Task 5)"
```

---

## Task 6: `tst_webcal_v2_e2e` end-to-end (parent)

**Why:** Exercise the full mirror pairing — `WebcalBlobBackend` (source) ↔ `MockBlobBackend` (target stand-in for `CalendarBlobBackend`) via `BlobSyncEngine::mirror`. Covers the four scenarios from the design's Testing section: empty target gains records, stale target loses then gains records, hash-identical no-ops, fetch-failure-with-skip.

**Files (parent only):**
- Create: `tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp`
- Modify: `tests/plugins/webcalendar/CMakeLists.txt`

- [ ] **Step 1: Create the test**

```cpp
#include <QCoreApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTest>
#include <QUrl>

#include <blobsyncengine.h>
#include <icsfeedfetcher.h>
#include <mockblobbackend.h>

#include "webcalbackendplugin.h"
#include "webcalblobbackend.h"
#include "webcalfeed.h"

using namespace Kalburator::Sync;
using WildPalms::WebcalPlugin::WebcalBackendPlugin;
using WildPalms::WebcalPlugin::WebcalBlobBackend;
using WildPalms::WebcalPlugin::WebcalFeed;

namespace {
QUrl fixtureUrl(const QString &name)
{
    const QString fixtureDir = QStringLiteral(WEBCAL_FIXTURE_DIR);
    return QUrl::fromLocalFile(QDir(fixtureDir).absoluteFilePath(name));
}

WebcalFeed makeFeed(int slot, const QString &fixtureName,
                     const QString &name = QStringLiteral("Feed"))
{
    WebcalFeed f;
    f.name      = name;
    f.url       = fixtureUrl(fixtureName);
    f.palmSlot  = slot;
    f.enabled   = true;
    return f;
}
} // namespace

class TestWebcalV2E2E : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_network = new QNetworkAccessManager(this);
        m_fetcher = new IcsFeedFetcher(m_network, this);
    }

    void mirror_emptyTargetGainsAllSourceRecords()
    {
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("three_events.ics"))},
                                m_fetcher);
        MockBlobBackend dst;
        CollectionInfo c;
        c.id   = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        QVERIFY2(src.lastFetchSucceeded(5)
                     || !src.loadRecords(QStringLiteral("palm:calendar/5")).isEmpty(),
                 "source should fetch successfully");

        BlobSyncEngine engine;
        const auto r = engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        QVERIFY(src.lastFetchSucceeded(5));

        const auto records = dst.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 3);
    }

    void mirror_staleTargetLosesAndGainsCorrectly()
    {
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("three_events.ics"))},
                                m_fetcher);

        // Pre-populate target with 2 stale records that are NOT in source,
        // plus 1 record that overlaps with source by UID.
        MockBlobBackend dst;
        CollectionInfo c;
        c.id = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        BackendRecord s1; s1.id = QStringLiteral("stale-001@example.com");
        s1.type = QStringLiteral("event"); s1.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
        s1.contentHash = QStringLiteral("aa");
        BackendRecord s2; s2.id = QStringLiteral("stale-002@example.com");
        s2.type = QStringLiteral("event"); s2.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
        s2.contentHash = QStringLiteral("bb");

        dst.createRecord(QStringLiteral("palm:calendar/5"), s1);
        dst.createRecord(QStringLiteral("palm:calendar/5"), s2);
        QCOMPARE(dst.loadRecords(QStringLiteral("palm:calendar/5")).size(), 2);

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));

        const auto records = dst.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(records.size(), 3);
        for (const auto &r : records) {
            QVERIFY(r.id != QStringLiteral("stale-001@example.com"));
            QVERIFY(r.id != QStringLiteral("stale-002@example.com"));
        }
    }

    void mirror_identicalContentNoOp()
    {
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("three_events.ics"))},
                                m_fetcher);
        MockBlobBackend dst;
        CollectionInfo c;
        c.id = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        BlobSyncEngine engine;
        engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        const auto first = dst.loadRecords(QStringLiteral("palm:calendar/5"));

        // Second mirror against identical source → target unchanged.
        const auto r2 = engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        const auto second = dst.loadRecords(QStringLiteral("palm:calendar/5"));
        QCOMPARE(second.size(), first.size());
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(second[i].contentHash, first[i].contentHash);
        }
    }

    void mirror_firstFetchFailureGate_skipsMirror()
    {
        // Configure feed with a non-existent URL.
        WebcalBlobBackend src({makeFeed(5, QStringLiteral("does_not_exist.ics"))},
                                m_fetcher);
        MockBlobBackend dst;
        CollectionInfo c;
        c.id = QStringLiteral("palm:calendar/5");
        c.name = QStringLiteral("Slot 5");
        c.type = QStringLiteral("calendar");
        dst.createCollection(c);

        // Pre-populate target with a record. If the gate is honoured,
        // the target is preserved; if violated (caller naively calls
        // mirror), the empty-source-deletes-target footgun fires.
        BackendRecord r;
        r.id = QStringLiteral("preserved-001@example.com");
        r.type = QStringLiteral("event");
        r.data = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
        r.contentHash = QStringLiteral("cc");
        dst.createRecord(QStringLiteral("palm:calendar/5"), r);
        QCOMPARE(dst.loadRecords(QStringLiteral("palm:calendar/5")).size(), 1);

        // Drive a fetch (populates lastFetchOk for slot 5, will be false).
        src.loadRecords(QStringLiteral("palm:calendar/5"));
        QVERIFY(!src.lastFetchSucceeded(5));

        // Caller (this test, standing in for the runtime) honours the
        // gate by skipping the mirror call.
        if (src.lastFetchSucceeded(5)) {
            BlobSyncEngine engine;
            engine.mirror(&src, &dst, QStringLiteral("palm:calendar/5"));
        }

        // Target preserved.
        QCOMPARE(dst.loadRecords(QStringLiteral("palm:calendar/5")).size(), 1);
    }

private:
    QNetworkAccessManager *m_network = nullptr;
    IcsFeedFetcher *m_fetcher = nullptr;
};

QTEST_MAIN(TestWebcalV2E2E)
#include "tst_webcal_v2_e2e.moc"
```

- [ ] **Step 2: Extend `tests/plugins/webcalendar/CMakeLists.txt`**

Append:

```cmake
# --- Task 6: end-to-end ---
add_executable(tst_webcal_v2_e2e
    tst_webcal_v2_e2e.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalbackendplugin.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_compile_definitions(tst_webcal_v2_e2e
    PRIVATE
        WEBCAL_FIXTURE_DIR="${WEBCAL_FIXTURE_DIR}"
)
target_include_directories(tst_webcal_v2_e2e
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_webcal_v2_e2e BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_webcal_v2_e2e
    PRIVATE
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        Kalburator::Sync
)
add_test(NAME tst_webcal_v2_e2e COMMAND tst_webcal_v2_e2e)
```

- [ ] **Step 3: Build**

```bash
cmake -S ~/dev/WildPalms -B ~/dev/WildPalms/build-dev
cmake --build ~/dev/WildPalms/build-dev --target tst_webcal_v2_e2e -j4
```

Expected: SUCCESS.

- [ ] **Step 4: Run the test**

```bash
ctest --test-dir ~/dev/WildPalms/build-dev -R tst_webcal_v2_e2e --output-on-failure
```

Expected: PASS, 4 sub-tests.

- [ ] **Step 5: Run the full WP test suite to confirm no regressions**

```bash
ctest --test-dir ~/dev/WildPalms/build-dev --output-on-failure
```

Expected: PASS (all webcal tests + all prior plugin tests). If any pre-existing plugin test fails, investigate before proceeding — the toggle should be a strict superset.

- [ ] **Step 6: Commit parent**

```bash
cd ~/dev/WildPalms
git add tests/plugins/webcalendar/CMakeLists.txt \
        tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp
git commit -m "test(webcal): tst_webcal_v2 end-to-end (Phase E.13 Task 6)"
```

---

## Task 7: Wrap-up — submodule pointer bump, parent spec flip, memory

**Why:** Land the submodule pointer in the parent repo, flip row E.13 of the parent rewrite spec to landed, and update the memory index.

**Files (submodule):**
- Push `phase-e13-webcalendar-plugin` branch to origin (operation, not a file).

**Files (parent):**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (flip E.13 row).
- Modify: submodule pointer at `src/plugins/webcalendar/`.

**Files (memory):**
- Create: `~/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e13_webcalendar.md`.
- Modify: `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` (add index entry).

- [ ] **Step 1: Push the libkalburator commit**

```bash
cd ~/dev/libkalburator
git push origin HEAD
```

Expected: push succeeds. (If the user is on a feature branch in libkalburator, push that branch.)

- [ ] **Step 2: Push the webcalendar submodule branch**

```bash
cd ~/dev/WildPalms/src/plugins/webcalendar
git push -u origin phase-e13-webcalendar-plugin
```

Expected: branch pushed.

- [ ] **Step 3: Read the parent spec line near row E.13**

```bash
grep -n "^| .*E\.13\|^| .*E\.14\|^| .*E\.12" ~/dev/WildPalms/docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
```

Expected: shows the table rows. The E.13 line currently begins `| **E.13** |`.

- [ ] **Step 4: Flip the E.13 row in the parent spec**

Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. Replace:

```
| **E.13** | Rewrite **WebCalendar** as `IBackendPlugin`. Fetch-side is an `IBlobBackend` reading a remote iCal URL (following `SubscriptionBackend` pattern). Palm-side is `PalmBackend`. Engine runs `mirror` (one-way). | WP | E.12 | Smoke passes with a local test iCal file. |
```

With:

```
| ✅ **E.13** | WebCalendar rewritten as `IBackendPlugin` (`WebcalBackendPlugin` + `WebcalBlobBackend` + `WebcalFeed`). New `Kalburator::Sync::IcsFeedFetcher` lands upstream (sibling-of-Holiday slot in `SubscriptionBackend` doc cohort) replacing legacy regex/RRULE parsing with `KCalendarCore::ICalFormat`. Plugin claims no Palm DB; mirror engine targets `palm:calendar/<slot>` per feed (strictly 1:1 slot allocation). Cache-on-failure + `lastFetchSucceeded(slot)` gate prevents empty-source-deletes-target. Settings JSON: `palm_slot` (int) replaces legacy `category` (text); legacy-format feeds load disabled with warning. CMake toggle `WILDPALMS_WEBCALENDAR_PLUGIN_V2=ON`; legacy `WebCalendarConduit` remains buildable. Runtime cross-plugin pairing deferred to E.15+. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e13-webcalendar-plugin.md`. | WP | E.12 | WP ctest passes; libkalburator ctest passes (PlanStan baseline pre-validated); ~21 tests across fetcher/feed/blob-backend/plugin/e2e. |
```

- [ ] **Step 5: Stage submodule pointer + spec change in parent**

```bash
cd ~/dev/WildPalms
git add src/plugins/webcalendar
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git status
```

Expected: shows the submodule pointer move and the spec edit.

- [ ] **Step 6: Commit parent**

```bash
git commit -m "$(cat <<'EOF'
docs(phase-e13): land WebCalendar plugin

- Bump src/plugins/webcalendar submodule pointer to phase-e13 branch.
- Flip parent rewrite spec row E.13 to landed.

Refs: docs/superpowers/plans/2026-04-26-phase-e13-webcalendar-plugin.md
EOF
)"
```

- [ ] **Step 7: Create memory pointer file**

Write `~/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e13_webcalendar.md`:

```markdown
---
name: phase_e13_webcalendar
description: Phase E.13 landed 2026-04-26; WebCalendar is fifth new-ABI plugin; IcsFeedFetcher in libkalburator; per-feed dedicated Palm slot
type: project
---

E.13 landed 2026-04-26. WebCalendar is the fifth new-ABI `IBackendPlugin`,
after Memo (E.9), Calendar (E.10), ToDo (E.11), Contacts (E.12).

**Why:** mirror engine, web URL is source of truth, Palm-side edits silently
overwritten. Plugin claims no Palm DB; shares Datebook with Calendar plugin
via per-feed dedicated category slot (strictly 1:1).

**How to apply:** when working on webcal feeds, slot allocation is in JSON
settings (`palm_slot` int). Two feeds for the same slot are auto-disabled
on load. Legacy `category` (text) → `palm_slot` (int) requires user
reconfiguration once; auto-migration was rejected as fragile.

**Where it lives:**
- libkalburator: `src/calendar/icsfeedfetcher.{h,cpp}` — sibling of
  `HolidaySubscriptionBackend` in the SubscriptionBackend doc cohort.
- WildPalms submodule `src/plugins/webcalendar/`:
  `webcalbackendplugin`, `webcalblobbackend`, `webcalfeed`.
- Tests: `tests/plugins/webcalendar/` plus `libkalburator/tests/calendar/`.

**Deferred:**
- Runtime cross-plugin pairing (mirror src=webcal-plugin to dst=calendar-plugin)
  is E.15+ work. E.13 ships test-driver pairing against `MockBlobBackend`.
- Settings widget — E.17 UI cleanup.
- Live-device POSE64 — E.18.
- Legacy `WebCalendarConduit` removal — E.16.
- Persistent `lastFetchTime` — currently in-memory only.
```

- [ ] **Step 8: Add memory index entry**

Edit `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`. Append:

```
- [project_phase_e13_webcalendar.md](project_phase_e13_webcalendar.md) — E.13 landed 2026-04-26; WebCalendar is fifth new-ABI plugin; IcsFeedFetcher in libkalburator
```

- [ ] **Step 9: Final verification**

```bash
ctest --test-dir ~/dev/WildPalms/build-dev --output-on-failure
ctest --test-dir ~/dev/libkalburator/build-dev --output-on-failure
```

Expected: both PASS, no regressions.

- [ ] **Step 10: Push parent**

```bash
cd ~/dev/WildPalms
git push origin HEAD
```

Expected: push succeeds.

---

## Self-Review

- [x] **Spec coverage:**
  - IcsFeedFetcher in libkalburator → Task 1
  - PlanStan baseline pre-validation → Task 2
  - WebcalFeed POD with legacy-`category` migration → Task 3
  - WebcalBlobBackend with cache-on-failure + `lastFetchSucceeded` → Task 4
  - WebcalBackendPlugin (claims = {}, no conflict handler, no main view) → Task 5
  - CMake toggle `WILDPALMS_WEBCALENDAR_PLUGIN_V2` → Task 5
  - Plugin JSON metadata → Task 5
  - 4 mirror e2e scenarios (empty target, stale target, identical no-op, fetch-failure-gate) → Task 6
  - Submodule pointer bump + spec flip + memory → Task 7
- [x] **Placeholder scan:** none — every code block is concrete; commit messages templated.
- [x] **Type consistency:** `WebcalFeed::palmSlot` (int), `palm_slot` (JSON key), `palm:calendar/<N>` (collection id) used uniformly. `lastFetchSucceeded(int slot)` signature consistent across header and tests. `BackendRecord.type == "event"` matches Calendar plugin's convention.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-26-phase-e13-webcalendar-plugin.md`. Per the user's "implement it with any method you'd like", proceeding with **subagent-driven-development** so each task gets a fresh context (these are large tasks with lots of code) and the main session can review between tasks.
