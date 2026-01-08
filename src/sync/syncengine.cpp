#include "syncengine.h"
#include "../palm/kpilotdevicelink.h"

#include <QStandardPaths>
#include <QDir>
#include <QDebug>

#include <pi-dlp.h>

namespace Sync {

SyncEngine::SyncEngine(QObject *parent)
    : QObject(parent)
{
    // Default state directory
    m_stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

SyncEngine::~SyncEngine()
{
    // Clean up owned objects
    qDeleteAll(m_conduits);
    qDeleteAll(m_states);
    delete m_backend;
    // Note: m_deviceLink may be shared, so don't delete it
}

// ========== Device Management ==========

void SyncEngine::setDeviceLink(KPilotDeviceLink *link)
{
    m_deviceLink = link;

    // Read username when connected
    if (m_deviceLink && m_deviceLink->isConnected()) {
        PilotUser user;
        if (m_deviceLink->readUserInfo(user)) {
            m_palmUserName = QString::fromUtf8(user.username);
        }
    }
}

// ========== Backend Configuration ==========

void SyncEngine::setBackend(SyncBackend *backend)
{
    delete m_backend;
    m_backend = backend;

    if (m_backend) {
        m_backend->setParent(this);
    }
}

// ========== Conduit Management ==========

void SyncEngine::registerConduit(Conduit *conduit)
{
    if (!conduit) return;

    QString id = conduit->conduitId();

    // Remove existing conduit with same ID
    if (m_conduits.contains(id)) {
        delete m_conduits[id];
    }

    m_conduits[id] = conduit;
    m_conduitEnabled[id] = true;
    conduit->setParent(this);

    connectConduitSignals(conduit);

    emit logMessage(QString("Registered conduit: %1").arg(conduit->displayName()));
}

void SyncEngine::unregisterConduit(const QString &conduitId)
{
    if (m_conduits.contains(conduitId)) {
        delete m_conduits[conduitId];
        m_conduits.remove(conduitId);
        m_conduitEnabled.remove(conduitId);
    }
}

Conduit* SyncEngine::conduit(const QString &conduitId) const
{
    return m_conduits.value(conduitId);
}

QStringList SyncEngine::registeredConduits() const
{
    return m_conduits.keys();
}

bool SyncEngine::isConduitEnabled(const QString &conduitId) const
{
    return m_conduitEnabled.value(conduitId, false);
}

void SyncEngine::setConduitEnabled(const QString &conduitId, bool enabled)
{
    m_conduitEnabled[conduitId] = enabled;
}

// ========== Sync Operations ==========

SyncResult SyncEngine::syncAll(SyncMode mode)
{
    SyncResult totalResult;
    totalResult.startTime = QDateTime::currentDateTime();
    totalResult.success = true;

    if (!m_deviceLink || !m_deviceLink->isConnected()) {
        totalResult.success = false;
        totalResult.errorMessage = "No device connected";
        totalResult.endTime = QDateTime::currentDateTime();
        emit errorOccurred(totalResult.errorMessage);
        return totalResult;
    }

    if (!m_backend) {
        totalResult.success = false;
        totalResult.errorMessage = "No backend configured";
        totalResult.endTime = QDateTime::currentDateTime();
        emit errorOccurred(totalResult.errorMessage);
        return totalResult;
    }

    // Get Palm username
    PilotUser user;
    if (m_deviceLink->readUserInfo(user)) {
        m_palmUserName = QString::fromUtf8(user.username);
    }

    if (m_palmUserName.isEmpty()) {
        m_palmUserName = "default";
    }

    m_syncing = true;
    m_cancelled = false;
    emit syncStarted();
    emit logMessage(QString("Starting sync for user: %1").arg(m_palmUserName));

    // Get enabled conduits
    QStringList enabledConduits;
    for (const QString &id : m_conduits.keys()) {
        if (m_conduitEnabled.value(id, true)) {
            enabledConduits << id;
        }
    }

    // Resolve dependency order
    QString depError = checkCircularDependencies(enabledConduits);
    if (!depError.isEmpty()) {
        totalResult.success = false;
        totalResult.errorMessage = depError;
        totalResult.endTime = QDateTime::currentDateTime();
        emit errorOccurred(depError);
        m_syncing = false;
        return totalResult;
    }

    QStringList orderedConduits = resolveConduitOrder(enabledConduits);
    emit logMessage(QString("Conduit order: %1").arg(orderedConduits.join(" → ")));

    int conduitIndex = 0;
    for (const QString &id : orderedConduits) {
        // Check both internal flag and external cancel callback
        if (m_cancelled || (m_cancelCheck && m_cancelCheck())) {
            emit logMessage("Sync cancelled by user");
            break;
        }

        Conduit *cond = m_conduits[id];

        // Check if conduit should run (interval-based conduits may skip)
        SyncContext preCheckContext;
        preCheckContext.mode = mode;
        if (!cond->shouldRun(&preCheckContext)) {
            emit logMessage(QString("Skipping %1 (not due yet)").arg(cond->displayName()));
            conduitIndex++;
            continue;
        }

        emit progressUpdated(conduitIndex, orderedConduits.size(),
            QString("Syncing %1...").arg(cond->displayName()));

        SyncResult conduitResult = syncConduit(id, mode);

        // Update conduit's last run time on success
        if (conduitResult.success) {
            cond->setLastRunTime(QDateTime::currentDateTime());
        }

        // Accumulate results
        totalResult.palmStats.created += conduitResult.palmStats.created;
        totalResult.palmStats.updated += conduitResult.palmStats.updated;
        totalResult.palmStats.deleted += conduitResult.palmStats.deleted;
        totalResult.palmStats.unchanged += conduitResult.palmStats.unchanged;
        totalResult.palmStats.conflicts += conduitResult.palmStats.conflicts;
        totalResult.palmStats.errors += conduitResult.palmStats.errors;

        totalResult.pcStats.created += conduitResult.pcStats.created;
        totalResult.pcStats.updated += conduitResult.pcStats.updated;
        totalResult.pcStats.deleted += conduitResult.pcStats.deleted;
        totalResult.pcStats.unchanged += conduitResult.pcStats.unchanged;
        totalResult.pcStats.conflicts += conduitResult.pcStats.conflicts;
        totalResult.pcStats.errors += conduitResult.pcStats.errors;

        totalResult.warnings.append(conduitResult.warnings);

        if (!conduitResult.success) {
            totalResult.success = false;
            if (totalResult.errorMessage.isEmpty()) {
                totalResult.errorMessage = conduitResult.errorMessage;
            }
        }

        conduitIndex++;
    }

    totalResult.endTime = QDateTime::currentDateTime();
    m_syncing = false;

    emit syncFinished(totalResult);
    emit logMessage(QString("Sync complete. Palm: %1. PC: %2. Duration: %3ms")
        .arg(totalResult.palmStats.summary())
        .arg(totalResult.pcStats.summary())
        .arg(totalResult.durationMs()));

    return totalResult;
}

SyncResult SyncEngine::syncConduit(const QString &conduitId, SyncMode mode)
{
    SyncResult result;
    result.startTime = QDateTime::currentDateTime();

    Conduit *cond = m_conduits.value(conduitId);
    if (!cond) {
        result.success = false;
        result.errorMessage = QString("Unknown conduit: %1").arg(conduitId);
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    m_currentConduit = conduitId;
    emit conduitStarted(conduitId);
    emit logMessage(QString("=== %1 ===").arg(cond->displayName()));

    // Get or create sync state for this conduit
    SyncState *state = stateForConduit(conduitId);

    // Build sync context
    SyncContext context;
    context.deviceLink = m_deviceLink;
    context.backend = m_backend;
    context.state = state;
    context.mode = mode;
    context.conflictPolicy = m_conflictPolicy;
    context.palmDatabase = cond->palmDatabaseName();
    context.userName = m_palmUserName;

    // Determine collection ID for this conduit
    // For now, use conduit ID as collection ID
    context.collectionId = conduitId;

    // Pass cancellation check to conduit
    if (m_cancelCheck) {
        cond->setCancelCheck(m_cancelCheck);
    }

    // Run the sync
    result = cond->sync(&context);

    // Clear cancellation check
    cond->setCancelCheck(nullptr);

    result.endTime = QDateTime::currentDateTime();
    m_currentConduit.clear();

    emit conduitFinished(conduitId, result);

    return result;
}

void SyncEngine::cancelSync()
{
    m_cancelled = true;
    emit logMessage("Cancel requested...");
}

void SyncEngine::setProgressCallback(std::function<void(int, int, const QString&)> callback)
{
    m_progressCallback = callback;
}

void SyncEngine::setCancelCheck(std::function<bool()> callback)
{
    m_cancelCheck = callback;
}

// ========== Configuration ==========

void SyncEngine::setConflictPolicy(ConflictResolution policy)
{
    m_conflictPolicy = policy;
}

void SyncEngine::setStateDirectory(const QString &path)
{
    m_stateDirectory = path;
}

SyncState* SyncEngine::stateForConduit(const QString &conduitId)
{
    if (!m_states.contains(conduitId)) {
        QString userName = m_palmUserName.isEmpty() ? "default" : m_palmUserName;
        SyncState *state = new SyncState(userName, conduitId, this);

        // Use the configured state directory (within PalmSync/.state/)
        if (!m_stateDirectory.isEmpty()) {
            state->setStateDirectory(m_stateDirectory);
        }

        state->load();
        m_states[conduitId] = state;
    }
    return m_states[conduitId];
}

// ========== Private Slots ==========

void SyncEngine::connectConduitSignals(Conduit *conduit)
{
    connect(conduit, &Conduit::progressUpdated,
            this, &SyncEngine::onConduitProgress);
    connect(conduit, &Conduit::logMessage,
            this, &SyncEngine::onConduitLog);
    connect(conduit, &Conduit::errorOccurred,
            this, &SyncEngine::onConduitError);
    connect(conduit, &Conduit::conflictDetected,
            this, &SyncEngine::onConduitConflict);
}

void SyncEngine::onConduitProgress(int current, int total, const QString &message)
{
    emit progressUpdated(current, total, message);

    // Also call external callback if set (for worker thread integration)
    if (m_progressCallback) {
        m_progressCallback(current, total, message);
    }
}

void SyncEngine::onConduitLog(const QString &message)
{
    emit logMessage(message);
}

void SyncEngine::onConduitError(const QString &error)
{
    emit errorOccurred(error);
}

void SyncEngine::onConduitConflict(const QString &palmDesc, const QString &pcDesc)
{
    emit conflictDetected(m_currentConduit, palmDesc, pcDesc);
}

// ========== Dependency Resolution ==========

QStringList SyncEngine::resolveConduitOrder(const QStringList &conduitIds)
{
    // Build dependency graph
    // Edge A -> B means "A must run before B"
    QMap<QString, QStringList> mustRunBefore;  // conduit -> list of conduits it must run before
    QMap<QString, int> inDegree;               // how many conduits must run before this one

    // Initialize
    for (const QString &id : conduitIds) {
        inDegree[id] = 0;
        mustRunBefore[id] = QStringList();
    }

    // Build edges from runBefore() and runAfter()
    for (const QString &id : conduitIds) {
        Conduit *cond = m_conduits.value(id);
        if (!cond) continue;

        // "I must run before X" means edge: id -> X
        for (const QString &beforeId : cond->runBefore()) {
            if (conduitIds.contains(beforeId)) {
                mustRunBefore[id].append(beforeId);
                inDegree[beforeId]++;
            }
        }

        // "I must run after X" means edge: X -> id
        for (const QString &afterId : cond->runAfter()) {
            if (conduitIds.contains(afterId)) {
                mustRunBefore[afterId].append(id);
                inDegree[id]++;
            }
        }
    }

    // Kahn's algorithm for topological sort
    QStringList result;
    QStringList queue;

    // Start with nodes that have no dependencies
    for (const QString &id : conduitIds) {
        if (inDegree[id] == 0) {
            queue.append(id);
        }
    }

    while (!queue.isEmpty()) {
        // Sort queue for deterministic ordering (alphabetical among equals)
        queue.sort();
        QString current = queue.takeFirst();
        result.append(current);

        // Remove edges from current
        for (const QString &next : mustRunBefore[current]) {
            inDegree[next]--;
            if (inDegree[next] == 0) {
                queue.append(next);
            }
        }
    }

    // If we didn't process all conduits, there's a cycle
    // (This shouldn't happen if checkCircularDependencies was called first)
    if (result.size() != conduitIds.size()) {
        emit logMessage("Warning: Could not resolve all conduit dependencies");
        // Return whatever we have plus the remaining ones
        for (const QString &id : conduitIds) {
            if (!result.contains(id)) {
                result.append(id);
            }
        }
    }

    return result;
}

QString SyncEngine::checkCircularDependencies(const QStringList &conduitIds)
{
    // Build adjacency list for DFS cycle detection
    QMap<QString, QStringList> edges;  // conduit -> conduits that must run after it

    for (const QString &id : conduitIds) {
        edges[id] = QStringList();
    }

    for (const QString &id : conduitIds) {
        Conduit *cond = m_conduits.value(id);
        if (!cond) continue;

        // "I must run before X" means edge: id -> X
        for (const QString &beforeId : cond->runBefore()) {
            if (conduitIds.contains(beforeId)) {
                edges[id].append(beforeId);
            }
        }

        // "I must run after X" means edge: X -> id
        for (const QString &afterId : cond->runAfter()) {
            if (conduitIds.contains(afterId)) {
                edges[afterId].append(id);
            }
        }
    }

    // DFS to detect cycles
    // States: 0 = unvisited, 1 = visiting (in current path), 2 = visited
    QMap<QString, int> state;
    for (const QString &id : conduitIds) {
        state[id] = 0;
    }

    std::function<QString(const QString&, QStringList&)> dfs;
    dfs = [&](const QString &node, QStringList &path) -> QString {
        if (state[node] == 1) {
            // Found cycle - build error message
            int cycleStart = path.indexOf(node);
            QStringList cycle = path.mid(cycleStart);
            cycle.append(node);
            return QString("Circular dependency detected: %1").arg(cycle.join(" → "));
        }
        if (state[node] == 2) {
            return QString();  // Already fully explored
        }

        state[node] = 1;
        path.append(node);

        for (const QString &next : edges[node]) {
            QString error = dfs(next, path);
            if (!error.isEmpty()) {
                return error;
            }
        }

        path.removeLast();
        state[node] = 2;
        return QString();
    };

    for (const QString &id : conduitIds) {
        if (state[id] == 0) {
            QStringList path;
            QString error = dfs(id, path);
            if (!error.isEmpty()) {
                return error;
            }
        }
    }

    return QString();  // No cycles found
}

} // namespace Sync
