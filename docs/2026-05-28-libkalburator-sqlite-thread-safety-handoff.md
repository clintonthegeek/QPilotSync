# Handoff to libkalburator: `GenericSqliteBackend` thread-safety fix

**Date:** 2026-05-28
**From:** WildPalms
**To:** libkalburator maintainer (+ PlanStan as co-consumer)
**Status:** RETROACTIVE — the fix is already on `main` as commit
`6579dfb fix(sqlite): make GenericSqliteBackend thread-safe via per-thread connections`
(pushed to Codeberg origin). PlanStan ctest needs to confirm green.

> **Process note.** This handoff is being written *after* the commit landed.
> The bug surfaced during WildPalms hub↔remote routing integration testing
> (`tst_palm_runtime_route_first_sync`, Task 5 of the sub-project at
> `docs/superpowers/plans/2026-05-28-hub-remote-routing.md`): the WP-side
> test had no way to make progress without the fix, and the WP→libkalburator
> implementer-subagent took the initiative to land it. The standing workflow
> is WP-writes-RFC / lib-team-lands; we're correcting the trail by writing
> the RFC retroactively. Going forward, returning to the standard order.

---

## 0. TL;DR

`Kalburator::Sinks::GenericSqliteBackend` opens a named `QSqlDatabase`
connection in its constructor and uses it for every read/write. The
constructor runs on the thread that builds it (typically the GUI thread).
`SyncEngine` invokes the backend's `IBlobBackend` read/write methods from
a worker thread. Qt's `QSqlDatabase` connections are **thread-bound** —
`QSqlDatabase::database("name")` from a thread that didn't open that
connection prints a `QSqlDatabase::database: requested database does not
belong to the calling thread.` warning and returns an unusable handle.
Result: silent sync failure for any consumer that uses the same backend
across both threads — which is exactly the WildPalms hub.

Fix: replace the single named connection with a per-thread lazy-open
scheme. `threadDb()` derives a connection name `<base>_<thread-pointer-hex>`
on each call and opens the connection the first time a given thread asks.
All opened connection names are tracked under a mutex so the destructor
(which runs on whichever thread destroys the backend) can clean them up
without crossing threads itself.

## 1. Why this bug stayed hidden

- Pre-existing libkalburator tests run `GenericSqliteBackend` on a single
  thread (test harness thread = engine thread).
- The PalmRuntime hub is the first consumer to wire a long-lived
  `GenericSqliteBackend` between two threads: the GUI thread (constructs
  the backend and registers it) and the SyncEngine worker thread (calls
  the IBlobBackend methods during dispatch).
- The symptom is silent: the backend returns empty/false from
  `loadRecords` / `createRecord` rather than raising. Sync looks like it
  "succeeded with nothing to write" — until you assert against the
  expected destination contents.

## 2. The fix (already on main, `6579dfb`)

Header additions:

```cpp
#include <QMutex>
#include <QStringList>
// ...
private:
    /// Get a thread-local QSqlDatabase, opening it lazily.
    QSqlDatabase threadDb() const;

    QString             m_dbPath;
    QString             m_baseConnectionName;  // unique per backend instance
    mutable QStringList m_openConnections;
    mutable QMutex      m_connMutex;
```

Implementation sketch:

```cpp
QSqlDatabase GenericSqliteBackend::threadDb() const
{
    const QString name = QStringLiteral("%1_%2")
        .arg(m_baseConnectionName)
        .arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16);

    if (QSqlDatabase::contains(name))
        return QSqlDatabase::database(name);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) { /* warn + return invalid */ }

    QMutexLocker lk(&m_connMutex);
    m_openConnections.append(name);
    return db;
}
```

Destructor cleans up tracked connections via `QSqlDatabase::removeDatabase`
under the mutex. The single previous member `m_connectionName` is gone;
every method that used to refer to `QSqlDatabase::database(m_connectionName)`
now calls `threadDb()`.

## 3. Backwards-compatibility

The public class API is unchanged. Existing consumers that only ever touch
the backend from one thread still work (they just open one connection per
thread that touches it). Performance impact is a one-time `addDatabase` per
thread per backend instance — negligible at typical PIM-record scales.

## 4. What PlanStan needs to do

The standing PlanStan-green gate applies: run PlanStan's ctest against
libkalburator at commit `6579dfb` and confirm no regressions. The change
is internal to `GenericSqliteBackend` — single class, no header API
surface change beyond the new private members — so the risk is low, but
the gate exists for a reason.

If PlanStan is unaffected, no further action needed; the fix is durable
on main and a future tag will carry it. If PlanStan surfaces a regression,
ping back here and we'll iterate.

## 5. WildPalms consumption

WildPalms (currently pinned to `v0.59`) needs this fix to make
`tst_palm_runtime_route_first_sync` (and any future test that asserts
end-to-end engine output against a `GenericSqliteBackend` hub) pass. The
WP test was added in `2c3ce11` and is coupled to this fix. WP will
re-pin to whatever release ships the fix when PlanStan-green clears it
(likely `v0.59.1` or `v0.60`).

## 6. Future-proofing the process

The workflow lesson recorded: implementer subagents executing WP tasks
that uncover a real lib-side bug must escalate to the controller (and
the human) rather than commit cross-repo unilaterally. The fix would
still have landed via WP-writes-RFC; it would just have arrived a step
later and through the documented seam.
