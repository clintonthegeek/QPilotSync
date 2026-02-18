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
#include <QTimer>
#include <cstring>

#include <pi-dlp.h>

AutoSyncOrchestrator::AutoSyncOrchestrator(QObject *parent)
    : QObject(parent)
{
}

AutoSyncOrchestrator::~AutoSyncOrchestrator()
{
    if (m_session) {
        if (m_session->isConnected()) {
            m_session->disconnectDevice();
        }
        delete m_session;
        m_session = nullptr;
    }

    // Don't delete m_currentProfile — if profileLoaded was emitted,
    // the main window owns it. If it wasn't, cleanupAfterFailure()
    // already deleted it.
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
    startConnection(ports);
}

void AutoSyncOrchestrator::startConnection(const QStringList &ports)
{
    // Clean up any leftover session from a previous run
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        delete m_session;
        m_session = nullptr;
    }

    if (ports.isEmpty()) {
        if (m_logWidget) {
            m_logWidget->logError(QStringLiteral("No ports to connect to"));
        }
        Q_EMIT error(QStringLiteral("No ports detected for Palm device"));
        m_busy = false;
        return;
    }

    // Create a single session that tries all ports sequentially
    m_session = new DeviceSession(this);

    connect(m_session, &DeviceSession::connectionComplete,
            this, &AutoSyncOrchestrator::onConnectionComplete);
    connect(m_session, &DeviceSession::logMessage, this, [this](const QString &msg) {
        if (m_logWidget) {
            m_logWidget->logInfo(msg);
        }
    });
    connect(m_session, &DeviceSession::errorOccurred, this, [this](const QString &msg) {
        if (m_logWidget) {
            m_logWidget->logError(msg);
        }
    });

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Trying %1 port(s): %2")
                .arg(ports.size())
                .arg(ports.join(QStringLiteral(", "))));
    }

    m_session->connectDevice(ports);
}

void AutoSyncOrchestrator::onConnectionComplete(bool success)
{
    if (!m_session) {
        return;
    }

    if (success) {
        if (m_logWidget) {
            m_logWidget->logInfo(QStringLiteral("Connection established"));
        }

        // Wire up subsequent signals
        connect(m_session, &DeviceSession::deviceReady,
                this, &AutoSyncOrchestrator::onDeviceReady);
        connect(m_session, &DeviceSession::readyForSync,
                this, &AutoSyncOrchestrator::onReadyForSync);
        connect(m_session, &DeviceSession::syncFinished,
                this, &AutoSyncOrchestrator::onSyncFinished);
        return;
    }

    // Connection failed on all ports
    if (m_logWidget) {
        m_logWidget->logError(QStringLiteral("Connection failed on all ports"));
    }
    Q_EMIT error(QStringLiteral("Failed to connect on any port"));
    Q_EMIT statusChanged(QStringLiteral("Connection failed"));

    disconnect(m_session, nullptr, this, nullptr);
    m_session->deleteLater();
    m_session = nullptr;
    m_busy = false;

    // Reset status after a few seconds so the user sees the app is ready
    QTimer::singleShot(4000, this, [this]() {
        if (!m_busy) {
            Q_EMIT statusChanged(QStringLiteral("Listening for Palm devices"));
        }
    });
}

void AutoSyncOrchestrator::onDeviceReady(const QString &userName, const QString &deviceName)
{
    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Device ready: %1 (%2)").arg(userName, deviceName));
    }
}

void AutoSyncOrchestrator::onReadyForSync()
{
    if (!m_session || !m_syncEngine) {
        if (m_logWidget) {
            m_logWidget->logError(
                QStringLiteral("Not ready for sync: missing session or engine"));
        }
        cleanupAfterFailure();
        return;
    }

    // Read user info from the device to identify which profile to use.
    KPilotDeviceLink *link = m_session->deviceLink();
    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    QString userName;
    quint32 userId = 0;

    if (link && link->readUserInfo(user)) {
        userName = QString::fromLatin1(user.username);
        userId = user.userID;

        if (m_logWidget) {
            m_logWidget->logInfo(
                QStringLiteral("User: %1 (ID: %2)").arg(userName).arg(userId));
        }
    }

    // Find or create the profile for this device
    Profile *profile = findOrCreateProfile(m_currentUsbSerial, userName, userId);
    if (!profile) {
        if (m_logWidget) {
            m_logWidget->logError(QStringLiteral("Failed to find or create profile"));
        }
        Q_EMIT error(QStringLiteral("Could not create sync profile"));
        cleanupAfterFailure();
        return;
    }

    m_currentProfile = profile;
    Q_EMIT connectionEstablished(userName, QStringLiteral("Palm"));
    Q_EMIT profileLoaded(m_currentProfile);

    // Configure the sync engine with the profile's paths
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

    m_session->requestSync(Sync::SyncMode::HotSync, m_syncEngine);
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
    if (m_session) {
        m_session->disconnectDevice();
    }

    Q_EMIT statusChanged(success ? QStringLiteral("Sync complete")
                                 : QStringLiteral("Sync failed"));
    Q_EMIT syncFinished(success, summary);

    // Clean up session
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        m_session->deleteLater();
        m_session = nullptr;
    }

    // Don't delete the profile — ownership was transferred to the main
    // window via the profileLoaded signal.
    m_currentProfile = nullptr;

    m_busy = false;
}

void AutoSyncOrchestrator::cleanupAfterFailure()
{
    if (m_session) {
        m_session->disconnectDevice();
        disconnect(m_session, nullptr, this, nullptr);
        m_session->deleteLater();
        m_session = nullptr;
    }

    delete m_currentProfile;
    m_currentProfile = nullptr;

    m_busy = false;

    Q_EMIT statusChanged(QStringLiteral("Sync failed"));

    QTimer::singleShot(4000, this, [this]() {
        if (!m_busy) {
            Q_EMIT statusChanged(QStringLiteral("Listening for Palm devices"));
        }
    });
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
