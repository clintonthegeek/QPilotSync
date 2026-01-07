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

    // Run each enabled conduit
    QStringList enabledConduits;
    for (const QString &id : m_conduits.keys()) {
        if (m_conduitEnabled.value(id, true)) {
            enabledConduits << id;
        }
    }

    int conduitIndex = 0;
    for (const QString &id : enabledConduits) {
        if (m_cancelled) {
            emit logMessage("Sync cancelled by user");
            break;
        }

        emit progressUpdated(conduitIndex, enabledConduits.size(),
            QString("Syncing %1...").arg(m_conduits[id]->displayName()));

        SyncResult conduitResult = syncConduit(id, mode);

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

    // Run the sync
    result = cond->sync(&context);

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

} // namespace Sync
