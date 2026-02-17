#include "autosyncorchestrator.h"
#include "kf6settings.h"

#include "../palm/palmdevicemonitor.h"
#include "../palm/devicesession.h"
#include "../palm/kpilotdevicelink.h"
#include "../profile.h"
#include "../app/logwidget.h"
#include "../sync/syncengine.h"
#include "../sync/synctypes.h"
#include "../sync/localfilebackend.h"

#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include <cstring>

#include <pi-dlp.h>

AutoSyncOrchestrator::AutoSyncOrchestrator(QObject *parent)
    : QObject(parent)
{
}

AutoSyncOrchestrator::~AutoSyncOrchestrator()
{
    // Clean up any racing sessions still alive
    for (DeviceSession *session : m_racingSessions) {
        if (!session) {
            continue;
        }
        if (session->isConnected()) {
            session->disconnectDevice();
        }
        delete session;
    }
    m_racingSessions.clear();

    delete m_currentProfile;
    m_currentProfile = nullptr;
}

void AutoSyncOrchestrator::setDeviceMonitor(PalmDeviceMonitor *monitor)
{
    if (m_monitor) {
        disconnect(m_monitor, &PalmDeviceMonitor::palmDetected,
                   this, &AutoSyncOrchestrator::onPalmDetected);
    }

    m_monitor = monitor;

    if (m_monitor) {
        connect(m_monitor, &PalmDeviceMonitor::palmDetected,
                this, &AutoSyncOrchestrator::onPalmDetected);
    }
}

void AutoSyncOrchestrator::setSyncEngine(Sync::SyncEngine *engine)
{
    m_syncEngine = engine;
}

void AutoSyncOrchestrator::setLogWidget(LogWidget *logWidget)
{
    m_logWidget = logWidget;
}

// ========== Private Slots ==========

void AutoSyncOrchestrator::onPalmDetected(const QStringList &ports, const QString &usbSerial)
{
    if (m_busy) {
        if (m_logWidget) {
            m_logWidget->logWarning(
                QStringLiteral("Palm detected but already syncing - ignoring"));
        }
        return;
    }

    m_busy = true;
    m_currentUsbSerial = usbSerial;

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Palm detected (S/N: %1) on %2 port(s)")
                .arg(usbSerial.isEmpty() ? QStringLiteral("unknown") : usbSerial)
                .arg(ports.size()));
    }

    Q_EMIT statusChanged(QStringLiteral("Connecting..."));
    startParallelConnections(ports);
}

void AutoSyncOrchestrator::startParallelConnections(const QStringList &ports)
{
    // Clean up any leftover sessions from a previous run
    for (DeviceSession *session : m_racingSessions) {
        delete session;
    }
    m_racingSessions.clear();
    m_winningSession = nullptr;

    if (ports.isEmpty()) {
        if (m_logWidget) {
            m_logWidget->logError(QStringLiteral("No ports to connect to"));
        }
        Q_EMIT error(QStringLiteral("No ports detected for Palm device"));
        m_busy = false;
        return;
    }

    // Create one DeviceSession per port and race them
    for (const QString &port : ports) {
        auto *session = new DeviceSession(this);

        connect(session, &DeviceSession::connectionComplete,
                this, &AutoSyncOrchestrator::onConnectionComplete);
        connect(session, &DeviceSession::logMessage, this, [this](const QString &msg) {
            if (m_logWidget) {
                m_logWidget->logInfo(msg);
            }
        });
        connect(session, &DeviceSession::errorOccurred, this, [this](const QString &msg) {
            if (m_logWidget) {
                m_logWidget->logError(msg);
            }
        });

        m_racingSessions.append(session);

        if (m_logWidget) {
            m_logWidget->logInfo(QStringLiteral("  Racing connection on %1").arg(port));
        }

        session->connectDevice(port);
    }
}

void AutoSyncOrchestrator::onConnectionComplete(bool success)
{
    auto *session = qobject_cast<DeviceSession*>(sender());
    if (!session) {
        return;
    }

    int index = m_racingSessions.indexOf(session);
    if (index < 0) {
        // Session not in the race (already cleaned up)
        return;
    }

    if (success && !m_winningSession) {
        // First successful connection wins the race
        m_winningSession = session;

        if (m_logWidget) {
            m_logWidget->logInfo(QStringLiteral("Connection established (winner on port #%1)")
                                     .arg(index + 1));
        }

        // Connect winner's subsequent signals
        connect(m_winningSession, &DeviceSession::deviceReady,
                this, &AutoSyncOrchestrator::onDeviceReady);
        connect(m_winningSession, &DeviceSession::readyForSync,
                this, &AutoSyncOrchestrator::onReadyForSync);
        connect(m_winningSession, &DeviceSession::syncFinished,
                this, &AutoSyncOrchestrator::onSyncFinished);

        // Clean up all the losers
        for (int i = 0; i < m_racingSessions.size(); ++i) {
            if (i != index) {
                cleanupLosingConnection(i);
            }
        }

        return;
    }

    if (success && m_winningSession) {
        // We already have a winner; this late success is a loser
        cleanupLosingConnection(index);
        return;
    }

    // This connection failed. Check if all have failed.
    if (!m_winningSession) {
        bool allDone = true;
        for (int i = 0; i < m_racingSessions.size(); ++i) {
            DeviceSession *s = m_racingSessions[i];
            if (s && s != session && s->isBusy()) {
                allDone = false;
                break;
            }
        }

        if (allDone) {
            if (m_logWidget) {
                m_logWidget->logError(
                    QStringLiteral("All connection attempts failed"));
            }
            Q_EMIT error(QStringLiteral("Failed to connect on any port"));
            Q_EMIT statusChanged(QStringLiteral("Connection failed"));

            // Clean up all sessions
            for (DeviceSession *s : m_racingSessions) {
                delete s;
            }
            m_racingSessions.clear();
            m_busy = false;
        }
    }
}

void AutoSyncOrchestrator::cleanupLosingConnection(int index)
{
    if (index < 0 || index >= m_racingSessions.size()) {
        return;
    }

    DeviceSession *loser = m_racingSessions[index];
    if (!loser) {
        return;
    }

    // Disconnect all signals to prevent late callbacks
    disconnect(loser, nullptr, this, nullptr);

    if (loser->isConnected()) {
        loser->disconnectDevice();
    }

    // Don't delete immediately -- schedule for safe deletion
    loser->deleteLater();
    m_racingSessions[index] = nullptr;
}

void AutoSyncOrchestrator::onDeviceReady(const QString &userName, const QString &deviceName)
{
    if (!m_winningSession) {
        return;
    }

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Device ready: %1 (%2)").arg(userName, deviceName));
    }

    // Read full user info from the device link
    KPilotDeviceLink *link = m_winningSession->deviceLink();
    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    QString resolvedUserName = userName;
    quint32 userId = 0;

    if (link && link->readUserInfo(user)) {
        resolvedUserName = QString::fromLatin1(user.username);
        userId = user.userID;

        if (m_logWidget) {
            m_logWidget->logInfo(
                QStringLiteral("User: %1 (ID: %2)").arg(resolvedUserName).arg(userId));
        }
    }

    // Find or create the profile for this device
    Profile *profile = findOrCreateProfile(m_currentUsbSerial, resolvedUserName, userId);
    if (!profile) {
        if (m_logWidget) {
            m_logWidget->logError(QStringLiteral("Failed to find or create profile"));
        }
        Q_EMIT error(QStringLiteral("Could not create sync profile"));

        m_winningSession->disconnectDevice();
        m_busy = false;
        return;
    }

    m_currentProfile = profile;
    Q_EMIT connectionEstablished(resolvedUserName, deviceName);
    Q_EMIT profileLoaded(m_currentProfile);
}

void AutoSyncOrchestrator::onReadyForSync()
{
    if (!m_winningSession || !m_syncEngine || !m_currentProfile) {
        if (m_logWidget) {
            m_logWidget->logError(
                QStringLiteral("Not ready for sync: missing session, engine, or profile"));
        }
        m_busy = false;
        return;
    }

    // Configure the sync engine with the profile's paths
    KPilotDeviceLink *link = m_winningSession->deviceLink();
    m_syncEngine->setDeviceLink(link);
    m_syncEngine->setStateDirectory(m_currentProfile->stateDirectoryPath());

    // Create and set the backend for the profile's sync folder
    auto *backend = new Sync::LocalFileBackend(
        m_currentProfile->syncFolderPath(), m_syncEngine);
    m_syncEngine->setBackend(backend);

    if (m_logWidget) {
        m_logWidget->logInfo(QStringLiteral("=== Starting Auto-HotSync ==="));
        m_logWidget->logInfo(
            QStringLiteral("Sync folder: %1").arg(m_currentProfile->syncFolderPath()));
    }

    Q_EMIT statusChanged(QStringLiteral("Syncing..."));
    Q_EMIT syncStarted(m_currentProfile->deviceFingerprint().userName);

    m_winningSession->requestSync(Sync::SyncMode::HotSync, m_syncEngine);
}

void AutoSyncOrchestrator::onSyncFinished(bool success, const QString &summary)
{
    if (m_logWidget) {
        if (success) {
            m_logWidget->logInfo(
                QStringLiteral("=== Auto-HotSync Complete ==="));
        } else {
            m_logWidget->logError(
                QStringLiteral("=== Auto-HotSync Failed: %1 ===").arg(summary));
        }
    }

    // Save the profile after sync
    if (m_currentProfile) {
        m_currentProfile->save();
    }

    // Disconnect the device cleanly
    if (m_winningSession) {
        m_winningSession->disconnectDevice();
    }

    Q_EMIT statusChanged(success ? QStringLiteral("Sync complete")
                                 : QStringLiteral("Sync failed"));
    Q_EMIT syncFinished(success, summary);

    // Clean up session state
    // The winning session is still in m_racingSessions; clean up everything
    for (int i = 0; i < m_racingSessions.size(); ++i) {
        DeviceSession *s = m_racingSessions[i];
        if (s) {
            disconnect(s, nullptr, this, nullptr);
            s->deleteLater();
            m_racingSessions[i] = nullptr;
        }
    }
    m_racingSessions.clear();
    m_winningSession = nullptr;

    delete m_currentProfile;
    m_currentProfile = nullptr;

    m_busy = false;
}

// ========== Private Helpers ==========

Profile* AutoSyncOrchestrator::findOrCreateProfile(const QString &usbSerial,
                                                     const QString &userName,
                                                     quint32 userId)
{
    KF6Settings &settings = KF6Settings::instance();

    // 1. Look up by USB serial number (most reliable)
    if (!usbSerial.isEmpty()) {
        QString profilePath = settings.findProfileBySerial(usbSerial);
        if (!profilePath.isEmpty() && QDir(profilePath).exists()) {
            if (m_logWidget) {
                m_logWidget->logInfo(
                    QStringLiteral("Found profile by serial: %1").arg(profilePath));
            }

            auto *profile = new Profile(profilePath);
            if (profile->exists()) {
                profile->load();
                return profile;
            }
            delete profile;
        }
    }

    // 2. Look up by device fingerprint (fallback)
    DeviceFingerprint fingerprint;
    fingerprint.userId = userId;
    fingerprint.userName = userName;
    fingerprint.usbSerialNumber = usbSerial;

    QString profilePath = settings.findProfileForDevice(fingerprint);
    if (!profilePath.isEmpty() && QDir(profilePath).exists()) {
        if (m_logWidget) {
            m_logWidget->logInfo(
                QStringLiteral("Found profile by fingerprint: %1").arg(profilePath));
        }

        auto *profile = new Profile(profilePath);
        if (profile->exists()) {
            profile->load();
            return profile;
        }
        delete profile;
    }

    // 3. No profile found -- auto-create one
    QString safeName = userName.isEmpty() ? QStringLiteral("PalmUser") : userName;
    // Sanitize the username for use as a directory name
    safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                     QStringLiteral("_"));

    QString basePath = QDir::homePath() + QStringLiteral("/PalmSync/") + safeName;

    // Ensure uniqueness if the directory already exists with a different device
    QString finalPath = basePath;
    int suffix = 1;
    while (QDir(finalPath).exists()) {
        // Check if the existing directory is actually a profile for a different device
        Profile existingProfile(finalPath);
        if (existingProfile.exists()) {
            existingProfile.load();
            DeviceFingerprint existingFp = existingProfile.deviceFingerprint();
            if (existingFp.isEmpty() || existingFp.matches(fingerprint)) {
                // Same device or unregistered -- reuse this profile
                break;
            }
        } else {
            // Directory exists but no profile config -- safe to use
            break;
        }
        finalPath = basePath + QStringLiteral("_%1").arg(suffix++);
    }

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Creating new profile at: %1").arg(finalPath));
    }

    auto *profile = new Profile(finalPath);
    profile->setName(safeName);
    profile->setDeviceFingerprint(fingerprint);

    if (!profile->initialize()) {
        if (m_logWidget) {
            m_logWidget->logError(
                QStringLiteral("Failed to initialize profile at: %1").arg(finalPath));
        }
        delete profile;
        return nullptr;
    }

    profile->save();

    // Register the device in settings
    settings.registerDevice(fingerprint, finalPath);
    if (!usbSerial.isEmpty()) {
        settings.registerDeviceBySerial(usbSerial, finalPath);
    }
    settings.addRecentProfile(finalPath);
    settings.setDefaultProfilePath(finalPath);
    settings.sync();

    Q_EMIT profileCreated(finalPath, userName);

    return profile;
}
