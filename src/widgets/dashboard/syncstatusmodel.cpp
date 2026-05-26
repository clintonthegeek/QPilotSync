#include "syncstatusmodel.h"

SyncStatusModel::SyncStatusModel(QObject *parent)
    : QObject(parent)
{
}

void SyncStatusModel::setState(LinkState s)
{
    if (m_linkState == s)
        return;
    m_linkState = s;
    Q_EMIT changed();
}

SyncStatusModel::Conduit *SyncStatusModel::findConduit(const QString &mappingId)
{
    for (auto &c : m_conduits)
        if (c.mappingId == mappingId)
            return &c;
    return nullptr;
}

void SyncStatusModel::setDeviceInfo(const QString &name, const QString &details)
{
    m_deviceName = name;
    m_deviceDetails = details;
    Q_EMIT changed();
}

void SyncStatusModel::setProfileInfo(const QString &profileName,
                                     const QDateTime &lastSync,
                                     const QString &autoSyncPlan)
{
    m_profileName = profileName;
    m_lastSyncTime = lastSync;
    m_autoSyncPlan = autoSyncPlan;
    Q_EMIT changed();
}

void SyncStatusModel::seedConduits(const QVector<ConduitSeed> &conduits)
{
    m_conduits.clear();
    for (const auto &s : conduits) {
        Conduit c;
        c.mappingId = s.mappingId;
        c.label = s.label;
        c.iconName = s.iconName;
        c.state = ChipState::Pending;
        m_conduits.append(c);
    }
    Q_EMIT changed();
}

void SyncStatusModel::onDeviceDetected()
{
    if (m_linkState == LinkState::Listening || m_linkState == LinkState::Disconnected)
        setState(LinkState::Detected);
}

void SyncStatusModel::onDeviceLost()
{
    if (m_linkState == LinkState::Syncing) {
        for (auto &c : m_conduits)
            if (c.state == ChipState::Active)
                c.state = ChipState::Interrupted;
    }
    setState(LinkState::Disconnected);
}

void SyncStatusModel::onConnectionStarted()
{
    setState(LinkState::Handshaking);
}

void SyncStatusModel::onConnectionComplete(bool success, const QString &error)
{
    if (success) {
        m_errorText.clear();
        setState(LinkState::Connected);
    } else {
        m_errorText = error;
        setState(LinkState::Listening);
    }
}

void SyncStatusModel::onConflictCountChanged(int count)
{
    if (m_conflictCount == count)
        return;
    m_conflictCount = count;
    Q_EMIT changed();
}

void SyncStatusModel::triggerPrimaryAction()
{
    if (m_linkState == LinkState::Connected)
        Q_EMIT syncRequested();
    else if (m_linkState == LinkState::Syncing)
        Q_EMIT cancelRequested();
}

void SyncStatusModel::triggerResolveConflicts()
{
    Q_EMIT resolveConflictsRequested();
}

QString SyncStatusModel::primaryActionLabel() const
{
    switch (m_linkState) {
    case LinkState::Connected: return QStringLiteral("Sync Now");
    case LinkState::Syncing:   return QStringLiteral("Cancel");
    default:                   return QString();
    }
}

QString SyncStatusModel::headline() const
{
    switch (m_linkState) {
    case LinkState::Listening:
        return m_errorText.isEmpty()
            ? QStringLiteral("Listening for Palm devices…\nPress HotSync on your Palm.")
            : m_errorText;
    case LinkState::Detected:    return QStringLiteral("Palm device detected…");
    case LinkState::Handshaking: return QStringLiteral("Connecting…");
    case LinkState::Connected:
        if (m_digest.valid)
            return m_digest.success
                ? QStringLiteral("%1 complete").arg(m_digest.modeLabel)
                : QStringLiteral("%1 finished with errors").arg(m_digest.modeLabel);
        return QStringLiteral("Ready to sync");
    case LinkState::Syncing:
        return m_progressMessage.isEmpty()
            ? QStringLiteral("Syncing…") : m_progressMessage;
    case LinkState::Disconnected:
        return m_errorText.isEmpty()
            ? QStringLiteral("Disconnected")
            : QStringLiteral("Disconnected — %1").arg(m_errorText);
    }
    return QString();
}

void SyncStatusModel::onRunStarted(const QString &modeLabel)
{
    m_currentRunLabel = modeLabel;
    m_errorText.clear();
    m_runChanges = 0;
    m_progressCurrent = m_progressTotal = 0;
    m_progressMessage.clear();
    for (auto &c : m_conduits) {
        c.state = ChipState::Pending;
        c.current = c.total = c.created = c.modified = c.deleted = 0;
    }
    m_digest.valid = false;
    setState(LinkState::Syncing);
}

void SyncStatusModel::onRunProgress(int, int, const QString &) {}
void SyncStatusModel::onRunFinished(const WildPalms::Runtime::PalmRunResult &) {}
void SyncStatusModel::onMappingSyncStarted(const QString &, const QString &, const QString &) {}
void SyncStatusModel::onMappingSyncProgress(const QString &, int, int, int) {}
void SyncStatusModel::onMappingSyncFinished(const QString &, int, int, int, bool) {}
