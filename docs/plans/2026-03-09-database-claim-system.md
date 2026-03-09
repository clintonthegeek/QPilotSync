# Database Claim System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the 1:1 conduit-to-database assumption with a claim system where multiple plugins can declare which Palm databases they handle, and the user chooses the active handler per-database.

**Architecture:** Sync conduits declare database claims (exact names or glob patterns) via JSON metadata. ConduitManager builds a claim map at discovery time. Profile stores per-database active handler selections. SyncEngine uses these selections to determine which conduits run and what databases they operate on. Standalone conduits (Install, Plucker, WebCalendar) keep simple enable/disable toggles.

**Tech Stack:** C++/Qt6, KDE Frameworks 6 (KPluginMetaData, KPageDialog), QSettings (INI format)

**Design doc:** `docs/plans/2026-03-09-database-claim-system-design.md`

---

### Task 1: Update ISyncConduit Interface

Replace the singular `palmDatabaseName()` with the plural `palmDatabaseNames()`.

**Files:**
- Modify: `src/core/isyncconduit.h:25`

**Step 1: Change the interface method**

In `src/core/isyncconduit.h`, replace:
```cpp
virtual QString palmDatabaseName() const = 0;
```
with:
```cpp
virtual QStringList palmDatabaseNames() const = 0;
```

The file already includes `<QList>` but add `<QStringList>` if not present (QStringList is implicitly available via Qt headers but explicit is better).

**Step 2: Verify the file compiles in isolation**

Run: `cd build && cmake --build . --target wildpalms_core 2>&1 | head -40`

Expected: Compilation errors in downstream files (conduit.h, plugin headers) because they still override `palmDatabaseName()`. This is expected — we'll fix those next.

---

### Task 2: Update SyncConduitBase and SyncContext

Update the base class and context to support multiple databases.

**Files:**
- Modify: `src/sync/conduit.h:50,119`
- Modify: `src/sync/conduit.cpp:41-44`

**Step 1: Add activeDatabases to SyncContext**

In `src/sync/conduit.h`, in the `SyncContext` class, after line 50 (`QString palmDatabase;`), add:

```cpp
QStringList activeDatabases;  ///< Databases this conduit is active for (subset of its claims)
```

Keep the existing `palmDatabase` field — it's still used by the sync algorithm to know which single database is currently being opened.

**Step 2: Replace palmDatabaseName() in SyncConduitBase**

In `src/sync/conduit.h`, replace line 119:
```cpp
QString palmDatabaseName() const override = 0;
```
with:
```cpp
QStringList palmDatabaseNames() const override = 0;
```

**Step 3: Update conduit.cpp to use the new method**

In `src/sync/conduit.cpp`, the `sync()` method at line 41 calls `palmDatabaseName()`. Since most current conduits handle a single database, the sync algorithm opens the first (or context-specified) database. Replace lines 41-44:

```cpp
m_dbHandle = context->deviceLink->openDatabase(palmDatabaseName(), true);
if (m_dbHandle < 0) {
    result.success = false;
    result.errorMessage = QString("Failed to open Palm database: %1").arg(palmDatabaseName());
```

with:

```cpp
// Use the database from context (set by SyncEngine), or fall back to first claimed database
QString dbName = context->palmDatabase;
if (dbName.isEmpty() && !palmDatabaseNames().isEmpty()) {
    dbName = palmDatabaseNames().first();
}
m_dbHandle = context->deviceLink->openDatabase(dbName, true);
if (m_dbHandle < 0) {
    result.success = false;
    result.errorMessage = QString("Failed to open Palm database: %1").arg(dbName);
```

**Step 4: Verify compilation**

Run: `cd build && cmake --build . --target wildpalms_sync 2>&1 | head -40`

Expected: Still errors in plugin conduit headers (they override the old method name). We fix those next.

---

### Task 3: Update All Plugin Conduit Implementations

Update each conduit class to implement `palmDatabaseNames()` instead of `palmDatabaseName()`.

**Files:**
- Modify: `src/plugins/calendar/calendarconduit.h:29`
- Modify: `src/plugins/contacts/contactconduit.h:29`
- Modify: `src/plugins/memo/memoconduit.h:29`
- Modify: `src/plugins/todos/todoconduit.h:29`
- Modify: `src/plugins/webcalendar/webcalendarconduit.h:67`

**Step 1: Update CalendarConduit**

Replace:
```cpp
QString palmDatabaseName() const override { return "DatebookDB"; }
```
with:
```cpp
QStringList palmDatabaseNames() const override { return {"DatebookDB"}; }
```

**Step 2: Update ContactConduit**

Same pattern — replace `palmDatabaseName()` returning `"AddressDB"` with `palmDatabaseNames()` returning `{"AddressDB"}`.

**Step 3: Update MemoConduit**

Same pattern — `"MemoDB"` → `{"MemoDB"}`.

**Step 4: Update TodoConduit**

Same pattern — `"ToDoDB"` → `{"ToDoDB"}`.

**Step 5: Update WebCalendarConduit**

Replace:
```cpp
QString palmDatabaseName() const override { return QString(); }
```
with:
```cpp
QStringList palmDatabaseNames() const override { return {}; }
```

**Step 6: Build all plugins**

Run: `cd build && cmake --build . 2>&1 | head -60`

Expected: Should compile now (may have errors in syncengine.cpp — fixed in Task 5).

---

### Task 4: Update Plugin JSON Metadata

Replace the singular `X-WildPalms-PalmDatabase` with the plural array `X-WildPalms-PalmDatabases` and add claim descriptions.

**Files:**
- Modify: `src/plugins/calendar/calendar-conduit.json`
- Modify: `src/plugins/contacts/contacts-conduit.json`
- Modify: `src/plugins/memo/memo-conduit.json`
- Modify: `src/plugins/todos/todos-conduit.json`
- Modify: `src/plugins/plucker/plucker-conduit.json`
- Modify: `src/plugins/webcalendar/webcalendar-conduit.json`

**Step 1: Update calendar-conduit.json**

Replace:
```json
"X-WildPalms-PalmDatabase": "DatebookDB",
```
with:
```json
"X-WildPalms-PalmDatabases": ["DatebookDB"],
"X-WildPalms-ClaimDescriptions": {
    "DatebookDB": "Syncs to local iCalendar files. Lightweight, no external dependencies."
},
```

Also update RunBefore/RunAfter — no changes needed for calendar.

**Step 2: Update contacts-conduit.json**

Replace `"X-WildPalms-PalmDatabase": "AddressDB"` with:
```json
"X-WildPalms-PalmDatabases": ["AddressDB"],
"X-WildPalms-ClaimDescriptions": {
    "AddressDB": "Syncs to local vCard files. Lightweight, no external dependencies."
},
```

**Step 3: Update memo-conduit.json**

Replace `"X-WildPalms-PalmDatabase": "MemoDB"` with:
```json
"X-WildPalms-PalmDatabases": ["MemoDB"],
"X-WildPalms-ClaimDescriptions": {
    "MemoDB": "Syncs to local Markdown files with YAML frontmatter."
},
```

**Step 4: Update todos-conduit.json**

Replace `"X-WildPalms-PalmDatabase": "ToDoDB"` with:
```json
"X-WildPalms-PalmDatabases": ["ToDoDB"],
"X-WildPalms-ClaimDescriptions": {
    "ToDoDB": "Syncs to local iCalendar VTODO files. Lightweight, no external dependencies."
},
```

**Step 5: Update plucker-conduit.json**

Remove `"X-WildPalms-PalmDatabase": ""` line entirely (standalone conduit, no database claims).

**Step 6: Update webcalendar-conduit.json**

Remove `"X-WildPalms-PalmDatabase": ""` line entirely.

Update `RunBefore` to use database sigil reference:
```json
"X-WildPalms-RunBefore": ["@DatebookDB"],
```

**Step 7: Update webcalendar's runBefore() in C++**

In `src/plugins/webcalendar/webcalendarconduit.h` line 90, update:
```cpp
QStringList runBefore() const override { return {"calendar"}; }
```
to:
```cpp
QStringList runBefore() const override { return {"@DatebookDB"}; }
```

**Step 8: Commit**

```bash
git add src/core/isyncconduit.h src/sync/conduit.h src/sync/conduit.cpp \
        src/plugins/*/calendarconduit.h src/plugins/*/contactconduit.h \
        src/plugins/*/memoconduit.h src/plugins/*/todoconduit.h \
        src/plugins/*/webcalendarconduit.h \
        src/plugins/*/*.json
git commit -m "refactor: replace palmDatabaseName with palmDatabaseNames array

Support multiple database claims per conduit. Each sync conduit now
returns a QStringList of claimed databases. JSON metadata uses
X-WildPalms-PalmDatabases (array) with optional ClaimDescriptions.
WebCalendar RunBefore now uses @DatebookDB sigil reference."
```

---

### Task 5: Update SyncEngine for New Interface

Update SyncEngine to work with `palmDatabaseNames()` and populate `activeDatabases` in context.

**Files:**
- Modify: `src/sync/syncengine.cpp:452-456`

**Step 1: Update syncConduit() palm database assignment**

In `src/sync/syncengine.cpp`, replace lines 452-456:
```cpp
// Only ISyncConduit-derived conduits have a Palm database name
ISyncConduit *syncCond = dynamic_cast<ISyncConduit*>(cond);
if (syncCond) {
    context.palmDatabase = syncCond->palmDatabaseName();
}
```
with:
```cpp
// Only ISyncConduit-derived conduits have Palm database names
ISyncConduit *syncCond = dynamic_cast<ISyncConduit*>(cond);
if (syncCond) {
    const QStringList dbNames = syncCond->palmDatabaseNames();
    if (!dbNames.isEmpty()) {
        context.palmDatabase = dbNames.first();
    }
    context.activeDatabases = dbNames;
}
```

Note: `context.activeDatabases` will be refined later (Task 8) when ConduitManager provides the actual active subset. For now, all claimed databases are treated as active (preserving current behavior).

**Step 2: Build and verify**

Run: `cd build && cmake --build . 2>&1 | tail -20`

Expected: Full successful build.

**Step 3: Commit**

```bash
git add src/sync/syncengine.cpp
git commit -m "fix: update SyncEngine to use palmDatabaseNames()"
```

---

### Task 6: Update ConduitManager for Database Claims

Add claim parsing, claim map building, and `@` sigil resolution.

**Files:**
- Modify: `src/kf6/conduitmanager.h`
- Modify: `src/kf6/conduitmanager.cpp`

**Step 1: Update PluginInfo struct**

In `conduitmanager.h`, add `databaseClaims` to `PluginInfo`:

```cpp
struct PluginInfo {
    KPluginMetaData metaData;
    IConduit *instance = nullptr;
    QString palmCreatorId;
    QStringList databaseClaims;    ///< Database names/patterns claimed (from X-WildPalms-PalmDatabases)
    bool defaultEnabled = false;
    int sortOrder = 0;
};
```

**Step 2: Add new public methods to ConduitManager**

After the existing query methods in `conduitmanager.h`, add:

```cpp
// ========== Database Claim System ==========

/** @brief Return database name → list of conduit IDs that claim it */
QMap<QString, QStringList> databaseClaimMap() const;

/** @brief Return the active conduit ID for a database, consulting the profile */
QString activeConduitForDatabase(const QString &dbName, const class Profile *profile) const;

/** @brief Return which databases a conduit is active for */
QStringList activeDatabasesForConduit(const QString &conduitId, const class Profile *profile) const;

/** @brief Check if a conduit has any database claims */
bool hasDatabaseClaims(const QString &conduitId) const;

/** @brief Get claim description for a conduit's database claim */
QString claimDescription(const QString &conduitId, const QString &dbName) const;
```

Add forward declaration for `Profile` near the top of the file.

**Step 3: Read X-WildPalms-PalmDatabases during discovery**

In `conduitmanager.cpp`, in `discoverConduits()`, after reading `palmCreatorId` (line 59), add:

```cpp
info.databaseClaims = metaStringList(md, QStringLiteral("X-WildPalms-PalmDatabases"));
```

Update the debug log to show database claims instead of/in addition to creatorId.

**Step 4: Implement databaseClaimMap()**

```cpp
QMap<QString, QStringList> ConduitManager::databaseClaimMap() const
{
    QMap<QString, QStringList> map;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        for (const QString &claim : it->databaseClaims) {
            map[claim].append(it.key());
        }
    }
    return map;
}
```

**Step 5: Implement hasDatabaseClaims()**

```cpp
bool ConduitManager::hasDatabaseClaims(const QString &conduitId) const
{
    auto it = m_plugins.constFind(conduitId);
    if (it != m_plugins.constEnd()) {
        return !it->databaseClaims.isEmpty();
    }
    return false;
}
```

**Step 6: Implement activeConduitForDatabase()**

```cpp
QString ConduitManager::activeConduitForDatabase(const QString &dbName, const Profile *profile) const
{
    if (!profile) return QString();

    // Check profile's explicit selection
    QString selected = profile->activeDatabaseHandler(dbName);
    if (!selected.isEmpty() && m_plugins.contains(selected)) {
        return selected;
    }

    // Auto-select if only one conduit claims this database
    QStringList claimants;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        for (const QString &claim : it->databaseClaims) {
            if (claim == dbName) {
                claimants.append(it.key());
            } else if (claim.contains('*') || claim.contains('?')) {
                // Glob pattern matching
                QRegularExpression re(QRegularExpression::wildcardToRegularExpression(claim));
                if (re.match(dbName).hasMatch()) {
                    claimants.append(it.key());
                }
            }
        }
    }

    if (claimants.size() == 1) {
        return claimants.first();
    }

    return QString(); // No selection or ambiguous
}
```

Add `#include <QRegularExpression>` at the top of the .cpp file.

**Step 7: Implement activeDatabasesForConduit()**

```cpp
QStringList ConduitManager::activeDatabasesForConduit(const QString &conduitId, const Profile *profile) const
{
    auto it = m_plugins.constFind(conduitId);
    if (it == m_plugins.constEnd()) return {};

    QStringList active;
    for (const QString &claim : it->databaseClaims) {
        if (activeConduitForDatabase(claim, profile) == conduitId) {
            active.append(claim);
        }
    }
    return active;
}
```

**Step 8: Implement claimDescription()**

```cpp
QString ConduitManager::claimDescription(const QString &conduitId, const QString &dbName) const
{
    auto it = m_plugins.constFind(conduitId);
    if (it == m_plugins.constEnd()) return QString();

    const QJsonObject raw = it->metaData.rawData();
    const QJsonObject descriptions = raw.value(QStringLiteral("X-WildPalms-ClaimDescriptions")).toObject();
    QString desc = descriptions.value(dbName).toString();
    if (desc.isEmpty()) {
        // Fall back to KPlugin.Description
        desc = it->metaData.description();
    }
    return desc;
}
```

**Step 9: Update resolveExecutionOrder() for @ sigil**

In `conduitmanager.cpp`, in `resolveExecutionOrder()`, add a helper lambda before the dependency graph construction to expand `@` references. Before line 196 (the loop that builds edges), add:

```cpp
// Helper: expand @ sigil database references to conduit IDs
auto expandRef = [this, &profile = *static_cast<const Profile*>(nullptr)](const QString &ref) -> QString {
    // This won't work with a null profile — we need to pass Profile* in.
    // See the method signature change below.
    return ref;
};
```

Actually, this requires changing the method signature. Update `resolveExecutionOrder` to accept a `const Profile *profile` parameter:

In `conduitmanager.h`, change:
```cpp
QStringList resolveExecutionOrder(const QStringList &enabledConduitIds) const;
```
to:
```cpp
QStringList resolveExecutionOrder(const QStringList &enabledConduitIds,
                                   const Profile *profile = nullptr) const;
```

In `conduitmanager.cpp`, update the method signature and add `@` sigil expansion in the dependency resolution loop. In the two inner loops that process `RunBefore` and `RunAfter` entries, wrap the reference resolution:

```cpp
for (const QString &id : conduitIds) {
    const PluginInfo &info = m_plugins[id];

    const QStringList before =
        metaStringList(info.metaData, QStringLiteral("X-WildPalms-RunBefore"));
    for (const QString &rawRef : before) {
        // Expand @ sigil: @DatabaseName -> active conduit for that database
        QString beforeId = rawRef;
        if (rawRef.startsWith('@') && profile) {
            beforeId = activeConduitForDatabase(rawRef.mid(1), profile);
            if (beforeId.isEmpty()) continue;  // No active handler, skip
        }
        if (conduitIds.contains(beforeId)) {
            mustRunBefore[id].append(beforeId);
            inDegree[beforeId]++;
        }
    }

    const QStringList after =
        metaStringList(info.metaData, QStringLiteral("X-WildPalms-RunAfter"));
    for (const QString &rawRef : after) {
        QString afterId = rawRef;
        if (rawRef.startsWith('@') && profile) {
            afterId = activeConduitForDatabase(rawRef.mid(1), profile);
            if (afterId.isEmpty()) continue;
        }
        if (conduitIds.contains(afterId)) {
            mustRunBefore[afterId].append(id);
            inDegree[id]++;
        }
    }
}
```

**Step 10: Build and verify**

Run: `cd build && cmake --build . 2>&1 | tail -20`

Expected: May need to fix call sites that pass arguments to `resolveExecutionOrder()`. Check if KF6MainWindow or DeviceSession call it — the existing call is in `conduitmanager.cpp` itself and in `syncengine.cpp:resolveConduitOrder()`. The SyncEngine has its own internal ordering method, so ConduitManager's is fine.

**Step 11: Commit**

```bash
git add src/kf6/conduitmanager.h src/kf6/conduitmanager.cpp
git commit -m "feat: add database claim system to ConduitManager

ConduitManager now reads X-WildPalms-PalmDatabases from plugin metadata,
builds a claim map, and resolves active handlers per-database. The @
sigil in RunBefore/RunAfter references is expanded to the active conduit
for that database."
```

---

### Task 7: Update Profile for Database Handler Preferences

Add per-database active handler storage to Profile.

**Files:**
- Modify: `src/profile.h`
- Modify: `src/profile.cpp`

**Step 1: Add database handler methods and storage to Profile**

In `profile.h`, after the conduit enable/disable methods (around line 240), add:

```cpp
// Database handler selection (for sync conduits)
QString activeDatabaseHandler(const QString &dbName) const;
void setActiveDatabaseHandler(const QString &dbName, const QString &conduitId);
QMap<QString, QString> allDatabaseHandlers() const;
```

Add a member variable after `m_conduitSettings` (around line 289):

```cpp
QMap<QString, QString> m_databaseHandlers;  ///< dbName -> conduitId
```

**Step 2: Implement the methods in profile.cpp**

```cpp
QString Profile::activeDatabaseHandler(const QString &dbName) const
{
    return m_databaseHandlers.value(dbName);
}

void Profile::setActiveDatabaseHandler(const QString &dbName, const QString &conduitId)
{
    if (conduitId.isEmpty()) {
        m_databaseHandlers.remove(dbName);
    } else {
        m_databaseHandlers[dbName] = conduitId;
    }
}

QMap<QString, QString> Profile::allDatabaseHandlers() const
{
    return m_databaseHandlers;
}
```

**Step 3: Update Profile::load() to read database handlers**

In `profile.cpp`, in the `load()` method, after the conduit settings loading (around line 305), add:

```cpp
// Database handler selections
settings.beginGroup("databases");
const QStringList dbKeys = settings.childGroups();
for (const QString &dbName : dbKeys) {
    settings.beginGroup(dbName);
    QString handler = settings.value("activeConduit").toString();
    if (!handler.isEmpty()) {
        m_databaseHandlers[dbName] = handler;
    }
    settings.endGroup();
}
settings.endGroup();
```

**Step 4: Update Profile::save() to write database handlers**

In `profile.cpp`, in the `save()` method, after saving conduit settings (around line 398), add:

```cpp
// Database handler selections
for (auto it = m_databaseHandlers.constBegin(); it != m_databaseHandlers.constEnd(); ++it) {
    settings.setValue(QString("databases/%1/activeConduit").arg(it.key()), it.value());
}
```

**Step 5: Remove hardcoded ALL_CONDUITS**

The `ALL_CONDUITS` static member in `profile.h` (line 295) and `profile.cpp` (line 13) hardcodes `{"memos", "contacts", "calendar", "todos", "webcalendar"}`. This is used in the constructor to initialize `m_conduitEnabled`, in `enabledConduits()`, and in `load()`/`save()` to iterate conduit settings.

Replace the hardcoded list with dynamic behavior:
- Remove the `static const QStringList ALL_CONDUITS` declaration from `profile.h`
- Remove the `const QStringList Profile::ALL_CONDUITS = {...}` definition from `profile.cpp`
- In the constructor, remove the loop that initializes `m_conduitEnabled` from `ALL_CONDUITS`
- Update `enabledConduits()` to return all conduits in `m_conduitEnabled` that are true:

```cpp
QStringList Profile::enabledConduits() const
{
    QStringList enabled;
    for (auto it = m_conduitEnabled.constBegin(); it != m_conduitEnabled.constEnd(); ++it) {
        if (it.value()) {
            enabled << it.key();
        }
    }
    return enabled;
}
```

- In `load()`, replace the `ALL_CONDUITS`-based conduit settings loop with dynamic key reading:

```cpp
// Conduit settings (standalone conduits: enable/disable + per-conduit config)
settings.beginGroup("conduits");
const QStringList conduitKeys = settings.childGroups();
for (const QString &conduit : conduitKeys) {
    settings.beginGroup(conduit);
    m_conduitEnabled[conduit] = settings.value("enabled", true).toBool();

    QString settingsStr = settings.value("settings").toString();
    if (!settingsStr.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(settingsStr.toUtf8());
        if (!doc.isNull() && doc.isObject()) {
            m_conduitSettings[conduit] = doc.object();
        }
    }
    settings.endGroup();
}
settings.endGroup();
```

- In `save()`, replace the `ALL_CONDUITS`-based loop similarly:

```cpp
// Conduit settings
for (auto it = m_conduitEnabled.constBegin(); it != m_conduitEnabled.constEnd(); ++it) {
    settings.setValue(QString("conduits/%1/enabled").arg(it.key()), it.value());
}
for (auto it = m_conduitSettings.constBegin(); it != m_conduitSettings.constEnd(); ++it) {
    QJsonDocument doc(it.value());
    settings.setValue(QString("conduits/%1/settings").arg(it.key()),
                      QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}
```

**Step 6: Build and verify**

Run: `cd build && cmake --build . 2>&1 | tail -20`

Expected: Clean build. Check for any references to `ALL_CONDUITS` that need updating.

**Step 7: Commit**

```bash
git add src/profile.h src/profile.cpp
git commit -m "feat: add per-database handler preferences to Profile

Profile now stores which conduit handles each Palm database. Removes
hardcoded ALL_CONDUITS list in favor of dynamic conduit key reading
from the config file."
```

---

### Task 8: Update SyncEngine and KF6MainWindow Integration

Wire up the database claim system into the sync flow.

**Files:**
- Modify: `src/sync/syncengine.h`
- Modify: `src/sync/syncengine.cpp`
- Modify: `src/kf6/kf6mainwindow.cpp`
- Modify: `src/palm/devicesession.cpp`

**Step 1: Add ConduitManager awareness to SyncEngine**

The SyncEngine currently doesn't know about database claims. Rather than making it claim-aware (that's ConduitManager's job), we update **KF6MainWindow** to compute which conduits should be enabled based on claims, then set that on SyncEngine via the existing `setConduitEnabled()`.

In `src/kf6/kf6mainwindow.cpp`, update the profile application block (around line 729). Replace:

```cpp
// Apply profile's conduit enabled settings
for (const QString &conduitId : m_syncEngine->registeredConduits()) {
    m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));
```

with:

```cpp
// Apply conduit activation: database-driven for sync conduits, toggle for standalone
for (const QString &conduitId : m_syncEngine->registeredConduits()) {
    if (m_conduitManager && m_conduitManager->hasDatabaseClaims(conduitId)) {
        // Sync conduit: enabled if it's the active handler for any of its claimed databases
        QStringList activeDBs = m_conduitManager->activeDatabasesForConduit(conduitId, m_currentProfile);
        m_syncEngine->setConduitEnabled(conduitId, !activeDBs.isEmpty());
    } else {
        // Standalone conduit: use simple enable/disable toggle
        m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));
    }
```

Apply the same change to the `onProfileSettings()` lambda (around line 1614).

**Step 2: Populate activeDatabases in SyncEngine::syncConduit()**

In `src/sync/syncengine.cpp`, update the block where we set context.activeDatabases (from Task 5). If we have access to a ConduitManager, we should use the actual active subset. For now, just pass all claimed databases — the ConduitManager refinement happens at the enable/disable level in KF6MainWindow.

The code from Task 5 already handles this correctly — `context.activeDatabases = dbNames` passes the full claim list, and the conduit is only running if at least one is active (checked by the enable/disable).

**Step 3: Update DeviceSession**

In `src/palm/devicesession.cpp` (around line 103), the enabled conduits list is built the same way. No changes needed — it reads from SyncEngine's enabled state which is already set correctly by KF6MainWindow.

**Step 4: Build and verify**

Run: `cd build && cmake --build . 2>&1 | tail -20`

Expected: Clean build.

**Step 5: Commit**

```bash
git add src/kf6/kf6mainwindow.cpp
git commit -m "feat: wire database claims into conduit activation

KF6MainWindow now determines sync conduit activation by checking
database claim selections via ConduitManager, instead of using
simple enable/disable for all conduits."
```

---

### Task 9: Redesign ProfilePropertiesDialog Conduits Page

Replace the checkbox list with a database-centric handler selection UI.

**Files:**
- Modify: `src/widgets/dialogs/profilepropertiesdialog.h`
- Modify: `src/widgets/dialogs/profilepropertiesdialog.cpp`

**Step 1: Update the header**

In `profilepropertiesdialog.h`, replace the conduit checks map:

```cpp
// Conduits page
QMap<QString, QCheckBox*> m_conduitChecks;
```

with:

```cpp
// Database Handlers section
QMap<QString, QComboBox*> m_databaseHandlerCombos;  ///< dbName -> handler dropdown
QMap<QString, QLabel*> m_claimDescriptionLabels;     ///< dbName -> description label

// Standalone Conduits section
QMap<QString, QCheckBox*> m_standaloneChecks;         ///< conduitId -> enable checkbox
```

**Step 2: Rewrite createConduitsPage()**

Replace the entire `createConduitsPage()` method in the .cpp:

```cpp
QWidget* ProfilePropertiesDialog::createConduitsPage()
{
    auto *page = new QWidget;
    auto *outerLayout = new QVBoxLayout(page);

    if (!m_conduitManager) {
        outerLayout->addWidget(new QLabel(i18n("No conduit plugins found"), page));
        outerLayout->addStretch();
        return page;
    }

    const QList<ConduitManager::PluginInfo> plugins = m_conduitManager->conduitList();
    if (plugins.isEmpty()) {
        outerLayout->addWidget(new QLabel(i18n("No conduit plugins found"), page));
        outerLayout->addStretch();
        return page;
    }

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *inner = new QWidget;
    auto *innerLayout = new QVBoxLayout(inner);

    // === Database Handlers section ===
    const QMap<QString, QStringList> claimMap = m_conduitManager->databaseClaimMap();

    if (!claimMap.isEmpty()) {
        auto *dbGroup = new QGroupBox(i18n("Database Handlers"), inner);
        auto *dbLayout = new QFormLayout(dbGroup);

        for (auto it = claimMap.constBegin(); it != claimMap.constEnd(); ++it) {
            const QString &dbName = it.key();
            const QStringList &claimants = it.value();

            auto *combo = new QComboBox(dbGroup);

            // Add "None" option if multiple claimants
            if (claimants.size() > 1) {
                combo->addItem(i18n("— Choose handler —"), QString());
            }

            for (const QString &conduitId : claimants) {
                KPluginMetaData md = m_conduitManager->conduitMetaData(conduitId);
                combo->addItem(md.name(), conduitId);
            }

            // Auto-select if only one claimant
            if (claimants.size() == 1) {
                combo->setCurrentIndex(0);
            }

            m_databaseHandlerCombos.insert(dbName, combo);

            // Description label (shown below dropdown)
            auto *descLabel = new QLabel(dbGroup);
            descLabel->setWordWrap(true);
            descLabel->setStyleSheet(QStringLiteral("color: #666; font-size: 11px; margin-bottom: 8px;"));
            m_claimDescriptionLabels.insert(dbName, descLabel);

            // Update description when selection changes
            connect(combo, &QComboBox::currentIndexChanged, this, [this, dbName, combo, descLabel]() {
                QString conduitId = combo->currentData().toString();
                if (conduitId.isEmpty()) {
                    descLabel->clear();
                } else {
                    descLabel->setText(m_conduitManager->claimDescription(conduitId, dbName));
                }
            });

            dbLayout->addRow(dbName, combo);
            dbLayout->addRow(QString(), descLabel);
        }

        innerLayout->addWidget(dbGroup);
    }

    // === Standalone Conduits section ===
    bool hasStandalone = false;
    auto *standaloneGroup = new QGroupBox(i18n("Standalone Conduits"), inner);
    auto *standaloneLayout = new QVBoxLayout(standaloneGroup);

    for (const ConduitManager::PluginInfo &info : plugins) {
        QString conduitId = info.metaData.value(QStringLiteral("X-WildPalms-ConduitId"));
        if (conduitId.isEmpty()) {
            conduitId = info.metaData.pluginId();
        }
        if (conduitId.isEmpty()) continue;

        // Skip conduits with database claims — they appear in the handlers section
        if (!info.databaseClaims.isEmpty()) continue;

        hasStandalone = true;
        auto *cb = new QCheckBox(info.metaData.name(), standaloneGroup);
        standaloneLayout->addWidget(cb);
        m_standaloneChecks.insert(conduitId, cb);
    }

    if (hasStandalone) {
        innerLayout->addWidget(standaloneGroup);
    } else {
        delete standaloneGroup;
    }

    innerLayout->addStretch();
    scrollArea->setWidget(inner);
    outerLayout->addWidget(scrollArea);

    return page;
}
```

**Step 3: Update loadSettings()**

Replace the conduit loading block:

```cpp
// Conduits page
for (auto it = m_conduitChecks.constBegin(); it != m_conduitChecks.constEnd(); ++it) {
    it.value()->setChecked(m_profile->conduitEnabled(it.key()));
}
```

with:

```cpp
// Database Handlers
for (auto it = m_databaseHandlerCombos.constBegin(); it != m_databaseHandlerCombos.constEnd(); ++it) {
    const QString &dbName = it.key();
    QComboBox *combo = it.value();
    QString activeHandler = m_profile->activeDatabaseHandler(dbName);

    if (activeHandler.isEmpty()) {
        // Auto-select if only one option (no "Choose handler" item)
        if (combo->count() == 1) {
            combo->setCurrentIndex(0);
        } else {
            combo->setCurrentIndex(0);  // "Choose handler" placeholder
        }
    } else {
        int idx = combo->findData(activeHandler);
        if (idx >= 0) combo->setCurrentIndex(idx);
    }

    // Trigger description update
    emit combo->currentIndexChanged(combo->currentIndex());
}

// Standalone Conduits
for (auto it = m_standaloneChecks.constBegin(); it != m_standaloneChecks.constEnd(); ++it) {
    it.value()->setChecked(m_profile->conduitEnabled(it.key()));
}
```

**Step 4: Update saveSettings()**

Replace the conduit saving block:

```cpp
// Conduits page
for (auto it = m_conduitChecks.constBegin(); it != m_conduitChecks.constEnd(); ++it) {
    m_profile->setConduitEnabled(it.key(), it.value()->isChecked());
}
```

with:

```cpp
// Database Handlers
for (auto it = m_databaseHandlerCombos.constBegin(); it != m_databaseHandlerCombos.constEnd(); ++it) {
    const QString &dbName = it.key();
    QString conduitId = it.value()->currentData().toString();
    m_profile->setActiveDatabaseHandler(dbName, conduitId);
}

// Standalone Conduits
for (auto it = m_standaloneChecks.constBegin(); it != m_standaloneChecks.constEnd(); ++it) {
    m_profile->setConduitEnabled(it.key(), it.value()->isChecked());
}
```

**Step 5: Build and verify**

Run: `cd build && cmake --build . 2>&1 | tail -20`

Expected: Clean build.

**Step 6: Commit**

```bash
git add src/widgets/dialogs/profilepropertiesdialog.h src/widgets/dialogs/profilepropertiesdialog.cpp
git commit -m "feat: redesign conduits settings with database handler dropdowns

Settings dialog now shows a Database Handlers section with per-database
dropdowns (with claim descriptions) and a Standalone Conduits section
with enable/disable toggles."
```

---

### Task 10: Update SyncEngine Dependency Resolution for @ Sigil

The SyncEngine has its own `resolveConduitOrder()` and `checkCircularDependencies()` methods that read `runBefore()`/`runAfter()` from conduits. Since webcalendar now returns `{"@DatebookDB"}` from `runBefore()`, these methods need to expand the `@` sigil.

**Files:**
- Modify: `src/sync/syncengine.h`
- Modify: `src/sync/syncengine.cpp`

**Step 1: Add a reference resolver to SyncEngine**

In `syncengine.h`, add a new member and setter:

```cpp
/** @brief Set a function to resolve @ sigil database references to conduit IDs */
void setDatabaseResolver(std::function<QString(const QString &dbName)> resolver);
```

Add member:
```cpp
std::function<QString(const QString &dbName)> m_dbResolver;
```

**Step 2: Implement the setter**

In `syncengine.cpp`:

```cpp
void SyncEngine::setDatabaseResolver(std::function<QString(const QString &dbName)> resolver)
{
    m_dbResolver = resolver;
}
```

**Step 3: Update resolveConduitOrder() to expand @ references**

In `syncengine.cpp`, in `resolveConduitOrder()`, in the loop that builds edges (lines 708-738), wrap the inner loops to expand `@` sigil:

Replace the inner loops:
```cpp
for (const QString &beforeId : beforeList) {
    if (conduitIds.contains(beforeId)) {
```
with:
```cpp
for (const QString &rawRef : beforeList) {
    QString beforeId = rawRef;
    if (rawRef.startsWith('@') && m_dbResolver) {
        beforeId = m_dbResolver(rawRef.mid(1));
        if (beforeId.isEmpty()) continue;
    }
    if (conduitIds.contains(beforeId)) {
```

Do the same for the `afterList` loop.

**Step 4: Update checkCircularDependencies() similarly**

Apply the same `@` sigil expansion in `checkCircularDependencies()` (lines 790-814).

**Step 5: Wire the resolver in KF6MainWindow**

In `src/kf6/kf6mainwindow.cpp`, after setting up the sync engine (in `loadProfile()`, after the conduit enabled/disabled loop), add:

```cpp
// Set up database reference resolver for @ sigil in dependency ordering
if (m_conduitManager && m_currentProfile) {
    m_syncEngine->setDatabaseResolver([this](const QString &dbName) -> QString {
        return m_conduitManager->activeConduitForDatabase(dbName, m_currentProfile);
    });
}
```

**Step 6: Build and verify**

Run: `cd build && cmake --build . 2>&1 | tail -20`

Expected: Clean build.

**Step 7: Commit**

```bash
git add src/sync/syncengine.h src/sync/syncengine.cpp src/kf6/kf6mainwindow.cpp
git commit -m "feat: support @ sigil database references in SyncEngine ordering

SyncEngine now expands @DatabaseName references in runBefore/runAfter
to the active conduit for that database, using a resolver function
provided by KF6MainWindow."
```

---

### Task 11: Final Build, Test, and Cleanup

**Step 1: Full clean build**

Run: `cd build && cmake --build . 2>&1 | tail -30`

Expected: Clean build with no errors or warnings related to the changes.

**Step 2: Check for remaining references to old API**

Search for any remaining references to the old `palmDatabaseName` (singular) or `X-WildPalms-PalmDatabase` (singular):

Run: `grep -r "palmDatabaseName\b" src/` (should find nothing)
Run: `grep -r "X-WildPalms-PalmDatabase\"" src/` (should find nothing — only `PalmDatabases` plural)

**Step 3: Verify JSON metadata is valid**

Run: `python3 -c "import json; [json.load(open(f)) for f in __import__('glob').glob('src/plugins/*/*.json')]"`

**Step 4: Commit any cleanup**

```bash
git add -A
git commit -m "chore: cleanup remaining references to old database API"
```
