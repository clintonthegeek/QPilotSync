# Phase E.13 — WebCalendar Plugin Design

**Status:** Draft, 2026-04-26
**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.13
**Predecessor:** Phase E.12 (Contacts plugin landed 2026-04-25)
**Successor:** Phase E.14 (Plucker plugin)

## Goal

Rewrite the WebCalendar conduit as the fifth new-ABI `IBackendPlugin`,
after Memo (E.9), Calendar (E.10), ToDo (E.11), and Contacts (E.12). Unlike
its four predecessors, WebCalendar is a *one-way read-only mirror*: each
configured feed pulls iCalendar bytes from a remote URL and propagates them
into a dedicated Palm Datebook category slot via
`BlobSyncEngine::mirror()`. Edits made on the Palm to webcal-managed slots
are silently overwritten on the next sync — the URL is the source of truth.

The first feature of E.13 is upstream: libkalburator gains
`Kalburator::Sync::IcsFeedFetcher`, the long-anticipated sibling of
`HolidaySubscriptionBackend`'s URL-feed slot in the `SubscriptionBackend`
docstring. The fetcher is reusable by PlanStan/ShadowStan; the legacy
WildPalms code's regex-based event splitting and ad-hoc `RRULE`/`UNTIL`
parsing are replaced by `KCalendarCore::ICalFormat`.

Land behind a new `WILDPALMS_WEBCALENDAR_PLUGIN_V2` toggle (default ON)
following Memo's, Calendar's, ToDo's, and Contacts' pattern; the legacy
`WebCalendarConduit` stays buildable in tree until E.16 deletes the old
surface.

## Decisions

The four questions answered during brainstorming, plus three architectural
details surfaced while drafting.

### Decision #1 — Read-only mirror to Palm

The plugin uses `BlobSyncEngine::mirror(webcalSource, palmTarget, collectionId)`
per feed. Mirror semantics: target records absent from source are deleted;
content-hash matches are left untouched; source-only and content-changed
records are pushed to target. Palm-side edits to webcal-managed slots are
discarded on next sync. The web URL is the source of truth.

**Why:** Two-way sync against a third-party calendar feed is incoherent —
Palm-side edits cannot be pushed back to a URL we don't own. Mirror is the
only consistent story. Matches `SubscriptionBackend`'s read-only semantics
in libkalburator.

**Alternative considered:** twoWayWithBaseline with a "read-only" flag on
the feed-sourced records that suppresses Palm→target propagation. Rejected:
introduces a new engine-level concept (read-only records) for one consumer
when `mirror()` already expresses the semantics directly. Also (B) and (C)
from the brainstorming Q1 — both rejected on the same grounds.

### Decision #2 — Fetcher helper in libkalburator, BlobBackend in WP

Add `Kalburator::Sync::IcsFeedFetcher` to libkalburator (sibling to
`HolidaySubscriptionBackend`, both extending nothing — fetcher is a free
class, not a `SubscriptionBackend` subclass). API:

```cpp
namespace Kalburator::Sync {
class IcsFeedFetcher : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool success = false;
        QString errorMessage;
        QList<KCalendarCore::Incidence::Ptr> incidences;
    };
    explicit IcsFeedFetcher(QNetworkAccessManager *network,
                            QObject *parent = nullptr);

    /// Fetch and parse a feed. If `startDate`/`endDate` are valid,
    /// only incidences active in [startDate, endDate] are returned
    /// (recurring events expanded; RRULE/EXDATE handled by KCalendarCore).
    Result fetch(const QUrl &url,
                 const QDate &startDate = {},
                 const QDate &endDate   = {},
                 int timeoutMs = 30000);

Q_SIGNALS:
    void progress(const QString &message);
};
}
```

Synchronous (uses `QEventLoop` internally; matches legacy webcal's pattern
and the tests stay simple). Caller owns the `QNetworkAccessManager` —
fetcher does not create one (lets the caller centralise proxy/redirect
policy and keeps the fetcher trivially mockable by passing a
`QNetworkAccessManager` subclass).

WP-side `WebcalBlobBackend` calls `IcsFeedFetcher::fetch()` per feed and
emits each `KCalendarCore::Incidence` as a single-event VCALENDAR
`BackendRecord` via `KCalendarCore::ICalFormat::toString` over a fresh
`MemoryCalendar`.

**Why:** The library/backend principle (`feedback_library_vs_backend_responsibility.md`)
says iCalendar parsing belongs in libkalburator. The legacy WP code does its
own regex splitting and ad-hoc `RRULE`/`UNTIL` parsing; both are replaced by
`KCalendarCore::ICalFormat`, which is correct (handles VTIMEZONE,
RECURRENCE-ID overrides, multi-VEVENT-shared-UID expansion).

**Why a free fetcher rather than a full `WebcalSubscriptionBackend` subclass:**
`SubscriptionBackend`'s shape is `KCalendarCore::Calendar`-shaped
(`loadCalendars` / `loadItems` / `fetchItems(calendarId)`); WildPalms's
plugin layer is `IBlobBackend`-shaped (collection-id + bytes). Wrapping
`SubscriptionBackend` in an adapter to fit `IBlobBackend` fights the API
on both sides. The reusable piece is the URL→Incidences logic, exposed as
a free fetcher; PlanStan can wrap it in its own `WebcalSubscriptionBackend`
later if the full SubscriptionBackend shape ever pays its way there.

**Alternative considered:** all iCal logic in WildPalms (option C from Q2).
Rejected — the URL-fetching plus `KCalendarCore::ICalFormat` plumbing is
exactly the kind of iCalendar work libkalburator is for, and a future
PlanStan webcal feature wouldn't get to reuse it.

### Decision #3 — Per-feed dedicated Palm category slot, strictly 1:1

Each feed configuration carries `palm_slot ∈ {1..15}` (slot 0 reserved for
"Unfiled" / hand-entered events). Strictly one feed per slot — sharing is
forbidden by config validation, not by runtime check (mirror's
delete-records-not-in-source semantics actively break under shared slots,
since each feed's mirror call would erase the other's events).

**Why:** Mirror is scoped to one `collectionId` per call. Per-feed
dedicated slots map cleanly onto E.10's `palm:calendar/<slot>` virtual
sub-collections. Slot 0 (and any user-owned slot not reserved by a feed) is
guaranteed-untouched by webcal — the user's hand-entered calendar is safe.

**Constraint not enforced at runtime:** if the user manually re-categorises
a hand-entered event into a webcal-reserved slot via the Palm, that event
is treated as "extra at target" by mirror and deleted. Documented as a
"don't do that" — formal protection deferred to a later phase if it
matters.

**Alternative considered:** shared "Subscriptions" slot for all feeds, with
per-event source tagging via a custom property (option B in Q3). Palm has
~15 user slots so the cap is the same, and the multi-feed-into-one-slot
mechanics need each feed's mirror call to filter target records by source
tag — undoing the engine's clean per-collection contract. Rejected.

**Alternative considered:** route per-event by the feed's own `CATEGORIES:`
property (option C in Q3). Forces per-event partial mirror, which `mirror()`
doesn't express; rebuilds two-way semantics by the back door. Rejected.

### Decision #4 — Plugin claims no Palm database

`WebcalBackendPlugin::claimedDatabases() == {}`. The plugin shares
DatebookDB with the Calendar plugin (E.10), but doesn't claim it.
`BackendPluginManager::pluginForDatabase("DatebookDB")` continues to return
the Calendar plugin — webcal isn't a "calendar plugin" from the Palm
device's point of view; it's a feed source whose target *happens to be* the
Datebook.

The plugin still needs `PalmDeviceConnection` (for the eventual target-side
Palm wrapper), and `createBackends(host, device)` provides it like normal.

**Why:** `BackendPluginManager::pluginForDatabase` returns the *first*
match, so co-claiming DatebookDB silently breaks dispatch in unpredictable
order. The webcal plugin doesn't need exclusive DatebookDB ownership — it
needs to push records into specific slots. Empty claim is the honest
declaration.

**Alternative considered:** claim a synthetic database name (e.g.
`"DatebookDB.Subscriptions"`). Rejected: invents a fake DB name to
suppress a real check, papering over the question of what claimed-databases
means.

### Decision #5 — Pairing model: plugin provides source only; target wired by tests in E.13, by runtime in E.15+

`IBackendPlugin::createBackends` returns `ProvidedBackends.blob =
WebcalBlobBackend` as the *source* of mirror. The *target* —
`CalendarBlobBackend` wrapping Palm Datebook — is owned by the Calendar
plugin's session.

For E.13's exit gate ("Smoke passes with a local test iCal file"), the
test driver pairs the two sides directly. For consistency with the prior
plugin tests' id-space deferral, the target is `MockBlobBackend` rather
than a live `CalendarBlobBackend` — but the pairing pattern is identical
to what the runtime will eventually do with `CalendarBlobBackend`:

```cpp
// In tst_webcal_v2_e2e:
auto webcalPlugin = make<WebcalBackendPlugin>();
webcalPlugin->createBackends(host, &device);
auto *src = webcalPlugin->blobBackend();   // WebcalBlobBackend
auto  dst = MockBlobBackend{};             // stands in for CalendarBlobBackend
BlobSyncEngine engine;
if (src->lastFetchSucceeded(5))
    engine.mirror(src, &dst, "palm:calendar/5");
```

Production runtime pairing — "for each enabled webcal feed, run mirror
from this plugin's source to the Calendar plugin's target on the feed's
slot collection-id" — is part of the unified runtime work in E.15/E.16.
E.13 ships the plugin shape and the test wiring; runtime integration is
documented as deferred.

**Why:** the new ABI's `IBackendPlugin` returns one `IBlobBackend`, not a
pair. Cross-plugin pairing requires runtime coordination that doesn't
exist yet. E.13's exit gate is "smoke passes against a local test iCal" —
a self-contained test that pairs the backends directly satisfies it without
prematurely coupling to a runtime that's still under construction.

**Alternative considered:** plugin owns its own internal target backend (a
separate `CalendarBlobBackend` instance configured for the plugin's
reserved slots) and runs mirror itself, ignoring the runtime. Rejected:
two `CalendarBlobBackend` instances reading the same Datebook concurrently
(one in the calendar plugin's session, one in webcal's) is a recipe for
read-after-write surprise. One target instance, runtime-paired by E.15+.

### Decision #6 — Settings schema + migration: feed → slot is hand-managed

Settings JSON:

```json
{
  "feeds": [
    {
      "name": "US Holidays",
      "url": "https://example.com/us-holidays.ics",
      "palm_slot": 5,
      "enabled": true,
      "fetch_policy": "weekly"
    }
  ]
}
```

Legacy schema carries `category` (text) instead of `palm_slot` (int). On
first load with V2, if a legacy `category` field is present and `palm_slot`
is absent, log a warning and disable the feed. The user reconfigures by
picking the slot once.

**Why:** The legacy `category` text would map to a slot via
`CategoryAppInfoReader`, but only if the slot is already named on the
device matching the text. That's a fragile lookup — different Palms have
different category sets, and a typo silently moves the feed. Forcing
explicit re-selection is one-time pain for many years of "where did my
holiday calendar go" questions. The migration is documented in the plan
header for the user, not auto-applied.

**Alternative considered:** look up `category` text in the
`CategoryMappingStore` and auto-promote to `palm_slot`. Rejected per above.

**On `fetch_policy`:** keep the legacy field shape (`every_sync` / `daily`
/ `weekly` / `monthly`) and the legacy `shouldRun` gating logic. No
scheduler integration in E.13 — the plugin is invoked when the runtime
runs sync, and `shouldRun` decides whether to actually fetch. Same pattern
as legacy.

### Decision #7 — Date filtering: helper accepts range, default is "recurring + future"

`IcsFeedFetcher::fetch(url, startDate, endDate)` — both dates optional, both
default-constructed (invalid). When valid, KCalendarCore expands recurring
events and returns only those active in the range.

The plugin's blob backend uses `today` and `today + 365 days` by default
(matches legacy `RecurringAndFuture`'s spirit; the legacy `FutureOnly` and
`All` modes are not carried forward — they were essentially debugging
options in the legacy code).

**Why:** The legacy regex-based RRULE/UNTIL logic is buggy by construction
(it doesn't actually evaluate recurrences, only checks UNTIL). Letting
KCalendarCore expand recurrences correctly is a strict improvement, and the
range-filter is the right place for it (in libkalburator, where iCalendar
semantics live).

**Open question (low-impact):** is `today + 365 days` the right default?
Legacy webcal had no upper bound. Picking a year keeps the Palm DatebookDB
small (a 5-year holiday feed expanded across 100 region holidays = ~500
events) and matches typical user "what's coming up" usage. Documented as
configurable in a follow-up if needed.

---

## Scope

**In scope (E.13):**

- Upstream (libkalburator):
  - `Kalburator::Sync::IcsFeedFetcher` at `src/calendar/icsfeedfetcher.{h,cpp}`.
  - Tests at `tests/calendar/tst_icsfeedfetcher.cpp` covering URL fetch
    (against `file://` fixtures), date-range filtering, recurring-event
    expansion, error paths (timeout, malformed iCal, network failure).
  - Pre-validation against PlanStan ctest baseline (per
    `feedback_planstan_pretest_for_upstream.md`) before landing.
- Downstream (WildPalms):
  - New plugin under `src/plugins/webcalendar/` alongside the legacy:
    - `WebcalBackendPlugin` (`IBackendPlugin` implementation, claims = {}).
    - `WebcalBlobBackend` (`IBlobBackend` source-side, wraps `IcsFeedFetcher`).
    - `WebcalFeed` POD + JSON serialiser.
    - `webcal-backend-plugin.json` (plugin metadata).
  - CMake toggle `WILDPALMS_WEBCALENDAR_PLUGIN_V2` in
    `src/plugins/webcalendar/CMakeLists.txt` (default ON).
  - `tst_webcal_v2` test executable at `tests/plugins/webcalendar/`
    covering blob backend, plugin metadata, and 3–4 e2e scenarios pairing
    `WebcalBlobBackend` source with `MockBlobBackend` target via
    `BlobSyncEngine::mirror`.

**Out of scope (deferred, called out in plan header):**

- New settings widget. Legacy returns nullptr today; user edits JSON; same
  posture in V2 until E.17 UI cleanup.
- HTTP conditional GET (`If-Modified-Since`, `ETag`) caching. `fetch_policy`
  throttling stays the only freshness gate, matching legacy.
- Authentication (basic, OAuth, tokens). Legacy is URL-only; V2 same.
- Auto-migration of legacy `category` (text) → `palm_slot` (int). Disable
  legacy-format feeds on load with a warning; user reconfigures once. (See
  Decision #6.)
- Runtime cross-plugin pairing — i.e., the runtime invoking
  `mirror(webcalPlugin->blob(), calendarPlugin->blob(), feedSlotId)`
  automatically. E.13 ships the plugin and the test pairing; runtime
  integration is E.15/E.16 work. (See Decision #5.)
- ConflictHandler. Mirror doesn't consult the conflict handler registry,
  so no `WebcalConflictHandler` ships.
- AppInfo write-on-first-sync. If a feed targets slot 5 with name "US
  Holidays", we don't currently write the slot-5 name back into the Palm
  Datebook AppInfo block. The user pre-names the slot via the Palm itself.
  Documented as a known limitation in the plan; AppInfo writes are an
  open WP-wide question (E.10 reads but doesn't write either).
- Live-device integration test in POSE64. Deferred to E.18 alongside the
  other plugins.
- Legacy `WebCalendarConduit` removal. Deferred to E.16.

**Pre-existing pieces reused unchanged:**

- `src/plugins/calendar/icstranscoder.{h,cpp}` (E.10) — Incidence ↔ ICS
  bytes; the webcal blob backend uses it for the source-side serialisation
  (one VEVENT per BackendRecord, in canonical VCALENDAR shape).
- `src/palm/calendar/palmcalendarbackend.{h,cpp}` (E.6) — Palm Datebook
  access; reached via the Calendar plugin's `CalendarBlobBackend` target
  during e2e tests.
- `KCalendarCore::ICalFormat`, `KCalendarCore::MemoryCalendar` — for
  parsing fetched feeds and serialising single-event blobs.
- `Kalburator::Sync::BlobSyncEngine::mirror` (Phase B2) — the engine path.
  Already exists, no changes needed.

---

## Architecture

### End-state shape

```
                    WebcalBackendPlugin (IBackendPlugin)
                              │
                              │ createBackends(host, device)
                              ▼
                   ┌─────────────────┐
                   │ WebcalBlobBackend│  (source side of mirror)
                   └────────┬─────────┘
                            │ owns
                            ▼
                   ┌─────────────────┐
                   │ IcsFeedFetcher  │  (libkalburator)
                   └────────┬─────────┘
                            │ uses
                            ▼
                   ┌─────────────────┐
                   │QNetworkAccessMgr│  (Qt)
                   └─────────────────┘

   Mirror pairing (E.13: in tests; E.15+: in runtime):

   WebcalBlobBackend.collections() → [palm:calendar/<slot> per feed]
                    │
                    │  BlobSyncEngine::mirror(src, dst, "palm:calendar/<slot>")
                    ▼
   CalendarBlobBackend (E.10, target side, owned by Calendar plugin)
                    │
                    ▼
              PalmCalendarBackend → IPalmDatabaseAccess → DatebookDB
```

### Class layout

- **`Kalburator::Sync::IcsFeedFetcher`** at
  `libkalburator/src/calendar/icsfeedfetcher.{h,cpp}`. Public API as in
  Decision #2. Implementation: `QNetworkRequest` with `NoLessSafeRedirectPolicy`,
  `QEventLoop` + `QTimer` for synchronous wait, fed bytes parsed via
  `KCalendarCore::ICalFormat::fromRawString` into a `MemoryCalendar`,
  date-range filter via `MemoryCalendar::rawIncidences()` +
  `KCalendarCore::Calendar::sortIncidencesList` + per-incidence
  `recursOn(date)` checks. Parented under user-supplied parent QObject;
  takes a borrowed `QNetworkAccessManager*`. No singletons, no lazy
  per-call allocation in ctor — caller controls lifetime.

- **`WildPalms::WebcalFeed`** POD at
  `src/plugins/webcalendar/webcalfeed.{h,cpp}` (separate from legacy
  `Sync::WebCalendarFeed`):

  ```cpp
  struct WebcalFeed {
      QString name;
      QUrl    url;
      int     palmSlot = -1;        // 1..15; -1 = unconfigured
      bool    enabled  = true;
      QString fetchPolicy = "weekly"; // every_sync|daily|weekly|monthly

      QJsonObject toJson() const;
      static WebcalFeed fromJson(const QJsonObject &);
      bool isValid() const;          // url+palmSlot+name all set
  };
  ```

- **`WildPalms::WebcalBlobBackend : Kalburator::Sync::IBlobBackend`** at
  `src/plugins/webcalendar/webcalblobbackend.{h,cpp}`:

  ```cpp
  class WebcalBlobBackend : public Kalburator::Sync::IBlobBackend {
  public:
      WebcalBlobBackend(QList<WebcalFeed> feeds,
                        Kalburator::Sync::IcsFeedFetcher *fetcher,
                        QObject *parent = nullptr);

      QString backendId() const override { return "webcal"; }
      QList<CollectionInfo> collections() const override;
      QList<BackendRecord>  loadRecords(const QString &collectionId) override;
      std::optional<BackendRecord> loadRecord(const QString &recordId) override;

      // Read-only on the source side; mirror engine never writes.
      BackendRecord createRecord(const QString &, const QByteArray &) override
          { return {}; }
      bool updateRecord(const BackendRecord &) override { return false; }
      bool deleteRecord(const QString &, const QString &) override { return false; }
      QList<BackendRecord> modifiedSince(const QString &, qint64) override;
  };
  ```

  - `collections()`: one `palm:calendar/<slot>` per `enabled && isValid`
    feed. Display name = feed.name. Slot 0 never emitted by webcal —
    that's the user's "Unfiled" calendar.
  - `loadRecords(collectionId)`: extract slot from suffix; find matching
    feed; check `shouldFetchNow(fetchPolicy, lastFetch)` (per-feed
    last-fetch tracked in-memory; persistence deferred); call
    `fetcher->fetch(feed.url, today, today+365d)`; for each Incidence,
    serialise via fresh `MemoryCalendar` + `ICalFormat::toString`, build a
    `BackendRecord{ collectionId, recordId=incidence.uid, content=icsBytes,
    contentHash=sha256(icsBytes), lastModified=incidence.lastModified }`.
  - On fetch failure: cache last-successful-fetch in memory and return
    the cached list (with a warning); on first-call failure with no
    cache, return empty list *and* expose `lastFetchSucceeded(slot)
    == false` so the caller can skip the mirror call rather than
    interpreting the empty source as "delete everything in the slot."
    See "Error handling" for the full posture.
  - Additional method: `bool lastFetchSucceeded(int slot) const` —
    queried by the test driver / runtime before invoking
    `BlobSyncEngine::mirror` to avoid the empty-source-deletes-slot
    footgun.

- **`WildPalms::WebcalBackendPlugin : QObject, IBackendPlugin`** at
  `src/plugins/webcalendar/webcalbackendplugin.{h,cpp}`. Mirrors the
  metadata-and-create shape of the four prior plugins. Notable differences:

  - `claimedDatabases() == {}` (Decision #4).
  - `createBackends(host, device)`: instantiates a `QNetworkAccessManager`
    parented to the plugin (one per session — same model as legacy webcal,
    but lifetime is tied to plugin instead of per-sync), wraps it in
    `IcsFeedFetcher`, builds `WebcalBlobBackend` with the feeds loaded
    from settings. Returns `{ blob: backend, calendar: nullptr }`.
  - `createConflictHandler() == nullptr` (mirror doesn't consult the
    registry).
  - `hasMainView() == false`.
  - `runAfter() == {"calendar"}` is *not* set — the runtime hasn't yet
    been built to honour it for this kind of cross-plugin pairing. Logic
    will land in E.15+ runtime work.
  - Settings load/save: `loadSettings(QJsonObject) → m_feeds`, with the
    legacy-`category`-field warning per Decision #6.

- **`webcal-backend-plugin.json`** — `KPlugin.Id == "webcalendar"`,
  `X-WildPalms-PluginType == "backend"`, `X-WildPalms-PalmDatabases == []`,
  `X-WildPalms-DefaultEnabled: true`, `X-WildPalms-SortOrder: 50` (after
  contacts=40).

### CMake toggle plumbing

`src/plugins/webcalendar/CMakeLists.txt` becomes:

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
        PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(wildpalms_webcalendar_v2
        PRIVATE
            WildPalmsCore
            WildPalmsRuntime
            KF6::CoreAddons KF6::CalendarCore KF6::I18n
            Qt::Network
            Kalburator::Sync
    )
else ()
    # Legacy: kcoreaddons_add_plugin(wildpalms_webcalendar … webcalendarconduit)
endif ()
```

The legacy branch is the file's existing top-level command, demoted into
the `else ()`. Both targets never co-installed (V2 → `wildpalms/plugins/`,
legacy → `wildpalms/conduits/`).

---

## Components

### `IcsFeedFetcher` (libkalburator)

Public API in Decision #2. Implementation outline:

```cpp
IcsFeedFetcher::Result IcsFeedFetcher::fetch(const QUrl &url,
                                              const QDate &startDate,
                                              const QDate &endDate,
                                              int timeoutMs)
{
    Result r;
    if (!url.isValid()) {
        r.errorMessage = "Invalid URL";
        return r;
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "libkalburator/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
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
        r.errorMessage = "Timeout";
        return r;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        r.errorMessage = reply->errorString();
        reply->deleteLater();
        return r;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromRawString(cal, data)) {
        r.errorMessage = "Failed to parse iCalendar";
        return r;
    }

    if (!startDate.isValid() && !endDate.isValid()) {
        r.incidences = cal->rawIncidences();
    } else {
        const QDate start = startDate.isValid() ? startDate : QDate(1970,1,1);
        const QDate end   = endDate.isValid()   ? endDate   : QDate(9999,12,31);
        for (const auto &inc : cal->rawIncidences()) {
            if (inc->recurs()) {
                // KCalendarCore handles RRULE/EXDATE expansion.
                bool active = false;
                for (QDate d = start; d <= end && !active; d = d.addDays(1)) {
                    if (inc->recursOn(d, QTimeZone::utc())) active = true;
                }
                if (active) r.incidences.append(inc);
            } else {
                // Single occurrence: start date in range.
                const QDate d = inc->dtStart().date();
                if (d.isValid() && d >= start && d <= end)
                    r.incidences.append(inc);
            }
        }
    }
    r.success = true;
    return r;
}
```

(The day-by-day `recursOn` loop is not the most efficient; for a
365-day window over a couple-hundred-event holiday feed it's still <1ms.
A future optimisation can use `KCalendarCore::Recurrence::timesInInterval`.
Documented in the plan.)

### `WebcalBlobBackend`

Per Decision #5 and Decision #7, with the cache-on-failure detail from
the architecture section. Per-feed last-fetch tracking is in-memory only
(persistence to settings JSON deferred — same posture as legacy).

`loadRecords` cache: `QHash<QString /*collectionId*/, QList<BackendRecord>>`
m_lastSuccessfulFetch. On successful fetch, replace; on failure, return
the previous entry (or empty list if none).

`modifiedSince(collectionId, sinceMs)`: filter `loadRecords(collectionId)`
by `lastModified > sinceMs`. Mirror doesn't actually use this method (mirror
is a full-set diff), but the IBlobBackend contract requires it; we provide
the trivial implementation for completeness.

### `WebcalBackendPlugin`

Mirrors `MemoBackendPlugin`'s shape (it's the plugin most similar to
webcal in structure — neither has typed-side calendar adapters and neither
needs a CategoryAppInfoReader). Single member: `QList<WebcalFeed> m_feeds`,
loaded from settings.

`enrichConflictSnapshot` / `formatConflictRecordHtml` — default
implementations; mirror doesn't surface conflicts, so these never trigger.

---

## Data flow

**Sync, mirror direction (per feed):**

1. Test driver / runtime calls
   `BlobSyncEngine::mirror(webcalPlugin.blob(), calendarPlugin.blob(),
                           "palm:calendar/<feed.palmSlot>")`.
2. Engine calls `webcal.loadRecords("palm:calendar/<slot>")`:
   a. Backend extracts slot, finds feed, calls
      `fetcher->fetch(feed.url, today, today+365d)`.
   b. Each `Incidence` is serialised as a single-event VCALENDAR blob.
   c. Returned as `BackendRecord` list.
3. Engine calls `calendar.loadRecords("palm:calendar/<slot>")` — returns
   the current Datebook records in slot N, transcoded to VCALENDAR by
   `IcsTranscoder` (E.10).
4. Engine diffs by `recordId` (= UID) and `contentHash`:
   - Source-only records → `calendar.createRecord(collectionId, bytes)`
     → IcsTranscoder decodes → `PalmCalendarBackend::pushItems` → device.
   - Target-only records → `calendar.deleteRecord(collectionId, recordId)`
     → device.
   - Hash-mismatch records → `calendar.updateRecord(BackendRecord)` →
     device.
5. Result accumulated as `BlobSyncResult`; per-feed metrics surfaced in
   the test/runtime log.

**Plugin lifecycle:**

1. `BackendPluginManager::loadAll()` discovers
   `wildpalms_webcalendar_v2.so` via KCoreAddons.
2. Plugin metadata accepted (`PluginType: backend`, claims = []).
3. On profile build: `createBackends(host, device)` invoked once.
4. On profile teardown: plugin destructor releases the
   `QNetworkAccessManager` (parented to plugin), `WebcalBlobBackend`
   (parented to manager), and `IcsFeedFetcher` (parented to plugin).

---

## Error handling

- `IcsFeedFetcher` URL invalid: returns `Result{success=false, errorMessage="Invalid URL"}`.
- Network timeout: aborts reply, returns `Result{success=false, errorMessage="Timeout"}`.
- HTTP error / network failure: `Result{success=false, errorMessage=<reply errorString>}`.
- Malformed iCalendar bytes: returns
  `Result{success=false, errorMessage="Failed to parse iCalendar"}`. Empty
  bytes → same.
- `WebcalBlobBackend::loadRecords` on fetch failure: returns the cached
  last-successful-fetch result (or empty list). Mirror sees no source
  diff, target unchanged. Logged at warning level.
- `WebcalBlobBackend::loadRecords` first-call failure (no cache): empty
  list returned, but plan-time decision: also raise an out-of-band signal
  so the test driver / runtime can detect first-call failure and skip the
  mirror call entirely. Otherwise the engine deletes the entire target
  slot on the first failed fetch — a footgun.

  Implementation: `WebcalBlobBackend` exposes `bool lastFetchSucceeded(slot)`;
  callers who care check before invoking mirror. The runtime wiring in
  E.15+ will respect it; tests in E.13 exercise both branches.

- Settings JSON contains a feed with `category` (legacy text) instead of
  `palm_slot`: feed marked as `enabled = false`, log warning. User
  reconfigures.
- Two feeds configured with the same `palm_slot`: settings loader logs an
  error and disables both. Strictly 1:1 enforcement at config-load time.

---

## Testing

### libkalburator: `tests/calendar/tst_icsfeedfetcher.cpp` — ~10 tests

- Fetch from `file://` fixture: parses one VEVENT, parses many.
- Date-range filter: filters single-occurrence events outside range; keeps
  recurring events whose RRULE produces an occurrence in range; drops
  recurring events whose UNTIL is before range.
- Recurring expansion: weekly RRULE without UNTIL across a 365-day range
  returns parent incidence (one Incidence per RRULE, KCalendarCore
  semantics — *not* one Incidence per occurrence).
- VTIMEZONE handling: feed with `TZID=America/New_York` parses without
  error.
- Error paths: invalid URL, malformed iCal, empty body, timeout (mock
  `QNetworkAccessManager` that never finishes).
- `Result.errorMessage` populated correctly for each failure mode.

### WildPalms: `tests/plugins/webcalendar/tst_webcal_v2.cpp` — ~15 tests

- `tst_webcalfeed.cpp` — 3 tests:
  - `toJson`/`fromJson` round-trip preserves all fields.
  - Legacy `category` field present, `palm_slot` absent → loaded with
    `enabled = false` (and warning logged).
  - `palm_slot` out of range → `isValid() == false`.
- `tst_webcalblobbackend.cpp` — 6 tests:
  - `collections()` enumerates one entry per enabled+valid feed.
  - `loadRecords` against a `file://` fixture returns one
    `BackendRecord` per VEVENT, with `recordId == UID` and content as
    valid VCALENDAR.
  - Two feeds → independent `loadRecords` calls hit independent slots.
  - Fetch failure on first call → empty list, `lastFetchSucceeded(slot)
    == false`.
  - Fetch failure with cached prior-success → cached list returned.
  - `createRecord` / `updateRecord` / `deleteRecord` are no-ops/false.
- `tst_webcalbackendplugin.cpp` — 3 tests:
  - Metadata: `pluginId`, `claimedDatabases() == {}`, sort order, default
    enabled.
  - `createBackends` returns a `WebcalBlobBackend` and null calendar.
  - `createConflictHandler() == nullptr`, `hasMainView() == false`.
- `tst_webcal_v2_e2e.cpp` — 4 e2e scenarios via
  `BlobSyncEngine::mirror(webcalSrc, mockTarget, "palm:calendar/<slot>")`:
  1. Empty target + 3-event feed → target gains all 3 events in the slot.
  2. Target has 2 stale events not in feed + feed has 3 events → target
     ends with the 3 new events; the 2 stale ones are deleted.
  3. Hash-identical content on both sides → no writes.
  4. First-fetch-failure path → driver checks
     `lastFetchSucceeded(slot)`, skips mirror, target unchanged.

E2E uses `MockBlobBackend` as the target (matching prior plugin tests'
id-space deferral). `LocalBlobBackend` integration is deferred to E.15+.
Live-device `CalendarBlobBackend` pairing is deferred to E.18 (POSE64).

---

## Exit gate

Per parent spec row E.13 ("Smoke passes with a local test iCal file"),
plus the per-plugin bar from prior phases:

- libkalburator `ctest` passes; `tst_icsfeedfetcher` (~10 tests) added,
  no regressions. Pre-validated against PlanStan's ctest baseline before
  landing.
- WP `ctest` passes; ~15 new tests added, no regressions.
- `tst_webcal_v2_e2e` covers the 4 mirror scenarios above against
  `MockBlobBackend` source-paired with `WebcalBlobBackend`.
- `WILDPALMS_WEBCALENDAR_PLUGIN_V2=ON` (default) builds + installs
  `wildpalms_webcalendar_v2.so` to `wildpalms/plugins/`.
  `WILDPALMS_WEBCALENDAR_PLUGIN_V2=OFF` builds the legacy
  `wildpalms_webcalendar.so` to `wildpalms/conduits/` exactly as before.
- Plan header documents the deferrals (settings widget, conditional GET,
  auth, auto-migration, runtime cross-plugin pairing, AppInfo write,
  live-device, legacy removal).

---

## Out of scope (deferred to later phases)

| Item                                                | Lands in                              |
| --------------------------------------------------- | ------------------------------------- |
| Settings widget                                     | E.17 (UI cleanup)                     |
| Conditional GET / `If-Modified-Since` / `ETag`      | post-E.18 if needed                   |
| Authentication (basic, OAuth, tokens)               | post-E.18 if needed                   |
| Auto-migration of legacy `category` → `palm_slot`   | Not planned (user reconfigures once)  |
| Runtime cross-plugin mirror pairing                 | E.15 / E.16                           |
| AppInfo write-on-first-sync (slot naming)           | Open WP-wide question; not webcal-specific |
| `LocalBlobBackend` real-target tests                | E.15+                                 |
| Live-device integration test in POSE64              | E.18                                  |
| Legacy `WebCalendarConduit` removal                 | E.16                                  |
| Persistent per-feed `lastFetchTime`                 | post-E.17 (currently in-memory)       |
| `KCalendarCore::Recurrence::timesInInterval`        | post-E.13 perf follow-up              |
|     optimisation in fetcher                         |                                       |
| `WebcalSubscriptionBackend` (full `SubscriptionBackend` | If a PlanStan webcal feature ever   |
|     subclass in libkalburator)                      | needs the calendar-shaped surface     |

---

## Open questions

None blocking implementation. Two stylistic calls left to plan execution:

1. Default date range — `today + 365 days` is a guess. The legacy code
   defaulted to "all" with a `RecurringAndFuture` flag that still had no
   upper bound. If tests against a 5-year holiday feed produce more events
   than is comfortable on a Palm, we narrow it. Documented as
   configurable in a follow-up rather than baked into the wire schema.
2. Cache-on-failure semantics — current plan caches in-memory only.
   Persisting last-successful-fetch to a sidecar file would survive plugin
   restart, at the cost of more I/O. Lean toward "in-memory is enough";
   first cold fetch's failure is rare and the user-visible cost is
   "nothing happens until next sync", not data loss.
