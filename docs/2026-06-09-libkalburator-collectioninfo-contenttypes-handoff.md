# Handoff to libkalburator: DAV providers drop per-calendar component capabilities from `CollectionInfo` (RFC)

**Date:** 2026-06-09
**From:** WildPalms (`feature/three-tier-sync` @ `c349f3e`)
**To:** libkalburator maintainer (+ PlanStan as co-consumer for green-gate)
**Status:** open RFC. Root cause confirmed; requesting a two-line fix in
each of two providers.

> **Process note.** This follows the standard
> WP-writes-RFC / lib-team-lands flow
> (`feedback_libkalburator_handoff_workflow`). PlanStan must stay green
> per `feedback_planstan_pretest_for_upstream` before tagging.

---

## 0. TL;DR

`Kalburator::Sync::CollectionInfo` has a `QStringList contentTypes`
field documented as the `"VEVENT", "VTODO", "VCARD"` subset
(`src/types/collectioninfo.h:21`). Discovery populates per-calendar
`supportsVEvent` / `supportsVTodo` booleans correctly — but **neither
`CalDavProvider` nor `MultiProtocolDavProvider` copies them into the
`CollectionInfo` rows they expose via `collections()`**. Every CalDAV
collection arrives at the consumer with `type == "calendar"` and an
empty `contentTypes`.

Consumer-visible symptom in WildPalms: the New Profile wizard's
Bindings page filters candidate collections per conduit domain
(`src/app/wizard/domainfilter.cpp`):

```cpp
if (pluginId == QStringLiteral("todo"))
    return c.type == QStringLiteral("todos")
        || c.contentTypes.contains(QStringLiteral("VTODO"));
```

With `contentTypes` always empty, the **todo conduit can never be bound
to any CalDAV task list**, even though discovery logged the capability
correctly moments earlier:

```
CalDavCapabilityDiscovery: Calendar "tasksOnlyOrange" components - VEVENT: false VTODO: true
```

Meanwhile the calendar conduit matches *all* CalDAV collections
(including VTODO-only ones) because `type == "calendar"` is
unconditional.

---

## 1. Evidence

Observed at v0.66 (line numbers from
`v0.66-provider-dialog-polish` @ `bb98ef9`; the code is unchanged from
v0.66).

### 1.1 The capability data exists at the right moment

`CalDavProvider::onDiscoveryFinished`,
`src/sync/caldavprovider.cpp:117-127`:

```cpp
for (auto it = caps.perCalendarCapabilities.constBegin();
     it != caps.perCalendarCapabilities.constEnd(); ++it) {
    CollectionInfo ci;
    ci.id   = it.key();
    ci.name = it.value().serverDisplayName.isEmpty() ? it.key()
                                                      : it.value().serverDisplayName;
    ci.type = QStringLiteral("calendar");
    ci.isDefault = false;
    ci.readOnly = !it.value().writable;
    m_collections.append(ci);            // <-- ci.contentTypes never set
}
```

`it.value()` is a `PerCalendarCapabilities`
(`src/typesupport/backendconfiguration.h:20`) and carries
`supportsVEvent` / `supportsVTodo` right there. The same function's
sibling `createBackend()` path *does* consume them (via
`contentTypesFromCaps()` at `caldavprovider.cpp:18-23`) to prime
`RemoteCalendarBackend` — only the `CollectionInfo` projection drops
them.

### 1.2 Same gap in the multi-protocol provider

`MultiProtocolDavProvider`, CalDAV leg,
`src/sync/multiprotocoldavprovider.cpp:229-235` — identical
`CollectionInfo` construction, identical omission. (The CardDAV leg is
unaffected in practice because its discovery returns `CollectionInfo`
rows typed `"contacts"`, which consumers match on `type`.)

---

## 2. Requested fix

Two lines in each provider, inside the `CollectionInfo` construction
loops cited above:

```cpp
if (it.value().supportsVEvent) ci.contentTypes << QStringLiteral("VEVENT");
if (it.value().supportsVTodo)  ci.contentTypes << QStringLiteral("VTODO");
```

Optionally, for symmetry, the CardDAV discovery path could set
`contentTypes << "VCARD"` on its rows — WildPalms' domain filter
already accepts `type == "contacts"`, so this is cosmetic, not
required.

### 2.1 Non-goals / things we are NOT asking for

- No change to `ci.type` — `"calendar"` stays as-is. Consumers that
  want to distinguish events-only / tasks-only / mixed collections can
  now do so via `contentTypes`; existing `type`-based consumers are
  untouched.
- No change to `contentTypesFromCaps()` / backend priming — that path
  is already correct.

### 2.2 Suggested regression test (lib-side)

A unit test that feeds `onDiscoveryFinished` (or the discovery fake) a
capability set containing one VEVENT-only, one VTODO-only, and one
mixed calendar, then asserts the three `collections()` rows carry the
matching `contentTypes`. Mirrors the existing per-calendar capability
tests around `CalDavCapabilityDiscovery`.

---

## 3. WP-side context (why now)

The accounts-first wizard (landed 2026-06-09) made `contentTypes` the
load-bearing filter for conduit↔collection candidate lists. First live
run against a Nextcloud account with 12 calendars (7 of them VTODO-
capable, 4 tasks-only) produced an empty dropdown for the todo conduit
and a 12-entry dropdown for the calendar conduit that includes
tasks-only calendars.

Once this lands and the pin bumps, the WP-side filter needs **no
changes** — it was written against the documented `CollectionInfo`
contract and starts working immediately. We may separately tighten the
calendar-conduit filter to prefer `contentTypes.contains("VEVENT")`
over bare `type == "calendar"` so tasks-only calendars stop appearing
there, but that is WP-side and out of scope here.
