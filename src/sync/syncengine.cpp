#include "syncengine.h"
#include "conduit.h"
#include "localfilebackend.h"
#include "../palm/kpilotdevicelink.h"
#include "qsynccore/conflictpolicy.h"

#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFileInfo>

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
    // Conduits are NOT owned by SyncEngine — just clear the map.
    // Plugin conduits are owned by ConduitManager; built-in conduits
    // are owned by their respective creators.
    m_conduits.clear();

    qDeleteAll(m_states);
    delete m_backend;
    // Note: m_deviceLink may be shared, so don't delete it
}

// ========== Device Management ==========

void SyncEngine::setDeviceLink(KPilotDeviceLink *link)
{
    m_deviceLink = link;
    // Username is set separately via setPalmUserName() from handshake data,
    // avoiding a DLP call on the main thread while tickle may be running.
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

void SyncEngine::registerConduit(IConduit *conduit)
{
    if (!conduit) return;

    QString id = conduit->conduitId();

    // Remove existing entry (non-owning — don't delete)
    m_conduits.remove(id);

    m_conduits[id] = conduit;
    m_conduitEnabled[id] = true;

    // NOTE: SyncEngine does NOT own conduits. They are owned by
    // ConduitManager (for plugin conduits) or KF6MainWindow (for
    // built-in conduits). Do not re-parent or delete.

    connectConduitSignals(conduit);

    emit logMessage(QString("Registered conduit: %1").arg(conduit->displayName()));
}

void SyncEngine::unregisterConduit(const QString &conduitId)
{
    // Non-owning: just remove from the map, don't delete.
    // The conduit is owned by ConduitManager (plugins) or its creator.
    m_conduits.remove(conduitId);
    m_conduitEnabled.remove(conduitId);
    m_conduitRunBefore.remove(conduitId);
    m_conduitRunAfter.remove(conduitId);
}

void SyncEngine::setConduitOrdering(const QString &conduitId,
                                     const QStringList &runBefore,
                                     const QStringList &runAfter)
{
    m_conduitRunBefore[conduitId] = runBefore;
    m_conduitRunAfter[conduitId] = runAfter;
}

IConduit* SyncEngine::conduit(const QString &conduitId) const
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
    m_pendingInstalls.clear();
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

        IConduit *cond = m_conduits[id];

        // Check if conduit should run (interval-based conduits may skip)
        SyncContext preCheckContext;
        preCheckContext.mode = mode;
        if (auto *localBackend = dynamic_cast<LocalFileBackend*>(m_backend)) {
            preCheckContext.syncFolderPath = localBackend->basePath();
        }
        if (!cond->shouldRun(&preCheckContext)) {
            emit logMessage(QString("Skipping %1 (not due yet)").arg(cond->displayName()));
            conduitIndex++;
            continue;
        }

        emit progressUpdated(conduitIndex, orderedConduits.size(),
            QString("Syncing %1...").arg(cond->displayName()));

        SyncResult conduitResult = syncConduit(id, mode);

        // Update conduit's last run time on success (SyncConduitBase only)
        if (conduitResult.success) {
            auto *syncBase = dynamic_cast<SyncConduitBase*>(cond);
            if (syncBase) {
                syncBase->setLastRunTime(QDateTime::currentDateTime());
            }
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

    // Post-conduit install phase: process any files queued by tool conduits
    if (!m_pendingInstalls.isEmpty() && m_deviceLink && m_deviceLink->isConnected()) {
        emit logMessage(QString("Installing %1 queued file(s)...")
                        .arg(m_pendingInstalls.size()));

        int installed = 0;
        int failed = 0;
        for (const QString &filePath : m_pendingInstalls) {
            if (m_cancelled || (m_cancelCheck && m_cancelCheck())) break;

            QFileInfo fi(filePath);
            emit logMessage(QString("  Installing %1...").arg(fi.fileName()));

            if (m_deviceLink->installFile(filePath)) {
                installed++;
            } else {
                failed++;
                emit logMessage(QString("  Failed to install %1").arg(fi.fileName()));
            }
        }

        emit logMessage(QString("Install phase: %1 installed, %2 failed")
                        .arg(installed).arg(failed));
        m_pendingInstalls.clear();
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

SyncResult SyncEngine::syncAllOrdered(const QStringList &orderedIds, SyncMode mode)
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
    m_pendingInstalls.clear();
    emit syncStarted();
    emit logMessage(QString("Starting sync for user: %1").arg(m_palmUserName));
    emit logMessage(QString("Conduit order: %1").arg(orderedIds.join(QStringLiteral(" \u2192 "))));

    int conduitIndex = 0;
    for (const QString &id : orderedIds) {
        // Check both internal flag and external cancel callback
        if (m_cancelled || (m_cancelCheck && m_cancelCheck())) {
            emit logMessage("Sync cancelled by user");
            break;
        }

        IConduit *cond = m_conduits.value(id);
        if (!cond) {
            emit logMessage(QString("Warning: Unknown conduit '%1' in ordered list, skipping").arg(id));
            conduitIndex++;
            continue;
        }

        // Check if conduit should run (interval-based conduits may skip)
        SyncContext preCheckContext;
        preCheckContext.mode = mode;
        if (auto *localBackend = dynamic_cast<LocalFileBackend*>(m_backend)) {
            preCheckContext.syncFolderPath = localBackend->basePath();
        }
        if (!cond->shouldRun(&preCheckContext)) {
            emit logMessage(QString("Skipping %1 (not due yet)").arg(cond->displayName()));
            conduitIndex++;
            continue;
        }

        emit progressUpdated(conduitIndex, orderedIds.size(),
            QString("Syncing %1...").arg(cond->displayName()));

        SyncResult conduitResult = syncConduit(id, mode);

        // Update conduit's last run time on success (SyncConduitBase only)
        if (conduitResult.success) {
            auto *syncBase = dynamic_cast<SyncConduitBase*>(cond);
            if (syncBase) {
                syncBase->setLastRunTime(QDateTime::currentDateTime());
            }
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

    // Post-conduit install phase: process any files queued by tool conduits
    if (!m_pendingInstalls.isEmpty() && m_deviceLink && m_deviceLink->isConnected()) {
        emit logMessage(QString("Installing %1 queued file(s)...")
                        .arg(m_pendingInstalls.size()));

        int installed = 0;
        int failed = 0;
        for (const QString &filePath : m_pendingInstalls) {
            if (m_cancelled || (m_cancelCheck && m_cancelCheck())) break;

            QFileInfo fi(filePath);
            emit logMessage(QString("  Installing %1...").arg(fi.fileName()));

            if (m_deviceLink->installFile(filePath)) {
                installed++;
            } else {
                failed++;
                emit logMessage(QString("  Failed to install %1").arg(fi.fileName()));
            }
        }

        emit logMessage(QString("Install phase: %1 installed, %2 failed")
                        .arg(installed).arg(failed));
        m_pendingInstalls.clear();
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

    IConduit *cond = m_conduits.value(conduitId);
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
    context.userName = m_palmUserName;

    // Only ISyncConduit-derived conduits have Palm database names
    ISyncConduit *syncCond = dynamic_cast<ISyncConduit*>(cond);
    if (syncCond) {
        const QStringList dbNames = syncCond->palmDatabaseNames();
        if (!dbNames.isEmpty()) {
            context.palmDatabase = dbNames.first();
        }
        context.activeDatabases = dbNames;
    }

    // Determine collection ID for this conduit
    // For now, use conduit ID as collection ID
    context.collectionId = conduitId;

    // Populate sync folder path from backend (if local file backend)
    if (auto *localBackend = dynamic_cast<LocalFileBackend*>(m_backend)) {
        context.syncFolderPath = localBackend->basePath();
    }

    // Set up conflict handling system
    // Use external handler (e.g. InteractiveConflictHandler) if provided,
    // otherwise fall back to a local AutomaticConflictHandler
    QSyncCore::AutomaticConflictHandler autoHandler(state->conflictStore());
    QSyncCore::ConflictHandler *conflictHandler = m_externalHandler ? m_externalHandler : &autoHandler;

    // Configure conflict policy from engine settings
    QSyncCore::ConflictPolicy conflictSettings;

    // Auto-resolve strategy
    if (m_conflictAutoResolve == "palm_wins") {
        conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::SourceAlwaysWins;
    } else if (m_conflictAutoResolve == "pc_wins") {
        conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::TargetAlwaysWins;
    } else if (m_conflictAutoResolve == "newer_wins") {
        conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::NewerWins;
    } else if (m_conflictAutoResolve == "older_wins") {
        conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::OlderWins;
    } else if (m_conflictAutoResolve == "duplicate") {
        conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::DuplicateAll;
    } else {
        conflictSettings.autoResolve = QSyncCore::AutoResolveStrategy::None;
    }

    // Fallback behavior
    if (m_conflictFallback == "skip") {
        conflictSettings.fallback = QSyncCore::FallbackBehavior::Skip;
    } else if (m_conflictFallback == "use_default") {
        conflictSettings.fallback = QSyncCore::FallbackBehavior::UseDefault;
    } else {
        conflictSettings.fallback = QSyncCore::FallbackBehavior::Defer;
    }

    // Prompt strategy
    if (m_conflictPromptStrategy == "first_only") {
        conflictSettings.promptStrategy = QSyncCore::PromptStrategy::OnFirstConflict;
    } else if (m_conflictPromptStrategy == "batch_at_end") {
        conflictSettings.promptStrategy = QSyncCore::PromptStrategy::Never;
        conflictSettings.fallback = QSyncCore::FallbackBehavior::Defer;
    } else {
        conflictSettings.promptStrategy = QSyncCore::PromptStrategy::Always;
    }

    // Connection behavior
    if (m_conflictConnectionBehavior == "disconnect_and_defer") {
        conflictSettings.connectionBehavior = QSyncCore::ConnectionBehavior::DisconnectAndDefer;
    } else if (m_conflictConnectionBehavior == "timeout_and_defer") {
        conflictSettings.connectionBehavior = QSyncCore::ConnectionBehavior::TimeoutThenDefer;
    } else {
        conflictSettings.connectionBehavior = QSyncCore::ConnectionBehavior::KeepAlive;
    }

    // Timeout
    conflictSettings.promptTimeoutSeconds = m_conflictTimeoutSeconds;

    context.conflictHandler = conflictHandler;
    context.conflictSettings = conflictSettings;

    // Pass cancellation check to conduit (SyncConduitBase only)
    auto *syncBase = dynamic_cast<SyncConduitBase*>(cond);
    if (syncBase && m_cancelCheck) {
        syncBase->setCancelCheck(m_cancelCheck);
    }

    // Run the sync
    result = cond->sync(&context);

    // Capture any files queued for installation by this conduit
    if (!context.installQueue.isEmpty()) {
        m_pendingInstalls.append(context.installQueue);
        emit logMessage(QString("%1 queued %2 file(s) for installation")
                        .arg(cond->displayName()).arg(context.installQueue.size()));
    }

    // Clear cancellation check
    if (syncBase) {
        syncBase->setCancelCheck(nullptr);
    }

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

void SyncEngine::setConflictAutoResolve(const QString &strategy)
{
    m_conflictAutoResolve = strategy;
}

void SyncEngine::setConflictFallback(const QString &fallback)
{
    m_conflictFallback = fallback;
}

void SyncEngine::setConflictHandler(QSyncCore::ConflictHandler *handler)
{
    m_externalHandler = handler;
}

void SyncEngine::setConflictPromptStrategy(const QString &strategy)
{
    m_conflictPromptStrategy = strategy;
}

void SyncEngine::setConflictConnectionBehavior(const QString &behavior)
{
    m_conflictConnectionBehavior = behavior;
}

void SyncEngine::setConflictTimeoutSeconds(int seconds)
{
    m_conflictTimeoutSeconds = qBound(15, seconds, 300);
}

void SyncEngine::setDatabaseResolver(std::function<QString(const QString &dbName)> resolver)
{
    m_dbResolver = resolver;
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

void SyncEngine::connectConduitSignals(IConduit *conduit)
{
    // Signals (logMessage, errorOccurred, progressUpdated, conflictDetected)
    // are defined on SyncConduitBase, not IConduit. For SyncConduitBase-derived
    // conduits, cast and connect. For IConduit-only conduits (tool conduits),
    // skip signal connections for now.
    auto *syncBase = dynamic_cast<SyncConduitBase*>(conduit);
    if (syncBase) {
        connect(syncBase, &SyncConduitBase::progressUpdated,
                this, &SyncEngine::onConduitProgress);
        connect(syncBase, &SyncConduitBase::logMessage,
                this, &SyncEngine::onConduitLog);
        connect(syncBase, &SyncConduitBase::errorOccurred,
                this, &SyncEngine::onConduitError);
        connect(syncBase, &SyncConduitBase::conflictDetected,
                this, &SyncEngine::onConduitConflict);
    }

    // For tool conduits (IToolConduit), check for signals by QObject metadata
    auto *obj = dynamic_cast<QObject*>(conduit);
    if (obj && !syncBase) {
        if (obj->metaObject()->indexOfSignal("logMessage(QString)") >= 0) {
            connect(obj, SIGNAL(logMessage(QString)),
                    this, SIGNAL(logMessage(QString)));
        }
        if (obj->metaObject()->indexOfSignal("errorOccurred(QString)") >= 0) {
            connect(obj, SIGNAL(errorOccurred(QString)),
                    this, SIGNAL(errorOccurred(QString)));
        }
        if (obj->metaObject()->indexOfSignal("progressUpdated(int,int,QString)") >= 0) {
            connect(obj, SIGNAL(progressUpdated(int,int,QString)),
                    this, SIGNAL(progressUpdated(int,int,QString)));
        }
    }
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
        QStringList beforeList;
        QStringList afterList;

        // SyncConduitBase has runBefore()/runAfter() methods
        auto *cond = dynamic_cast<SyncConduitBase*>(m_conduits.value(id));
        if (cond) {
            beforeList = cond->runBefore();
            afterList = cond->runAfter();
        } else {
            // Non-SyncConduitBase conduits use stored ordering hints
            beforeList = m_conduitRunBefore.value(id);
            afterList = m_conduitRunAfter.value(id);
        }

        // "I must run before X" means edge: id -> X
        for (const QString &rawRef : beforeList) {
            QString beforeId = rawRef;
            if (rawRef.startsWith(QLatin1Char('@')) && m_dbResolver) {
                beforeId = m_dbResolver(rawRef.mid(1));
                if (beforeId.isEmpty()) continue;
            }
            if (conduitIds.contains(beforeId)) {
                mustRunBefore[id].append(beforeId);
                inDegree[beforeId]++;
            }
        }

        // "I must run after X" means edge: X -> id
        for (const QString &rawRef : afterList) {
            QString afterId = rawRef;
            if (rawRef.startsWith(QLatin1Char('@')) && m_dbResolver) {
                afterId = m_dbResolver(rawRef.mid(1));
                if (afterId.isEmpty()) continue;
            }
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
        QStringList beforeList;
        QStringList afterList;

        auto *cond = dynamic_cast<SyncConduitBase*>(m_conduits.value(id));
        if (cond) {
            beforeList = cond->runBefore();
            afterList = cond->runAfter();
        } else {
            beforeList = m_conduitRunBefore.value(id);
            afterList = m_conduitRunAfter.value(id);
        }

        for (const QString &rawRef : beforeList) {
            QString beforeId = rawRef;
            if (rawRef.startsWith(QLatin1Char('@')) && m_dbResolver) {
                beforeId = m_dbResolver(rawRef.mid(1));
                if (beforeId.isEmpty()) continue;
            }
            if (conduitIds.contains(beforeId)) {
                edges[id].append(beforeId);
            }
        }

        for (const QString &rawRef : afterList) {
            QString afterId = rawRef;
            if (rawRef.startsWith(QLatin1Char('@')) && m_dbResolver) {
                afterId = m_dbResolver(rawRef.mid(1));
                if (afterId.isEmpty()) continue;
            }
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
