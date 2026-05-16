#include "autosyncorchestrator.h"
#include "kf6settings.h"

#include "../palm/palmdevicemonitor.h"
#include "../profile.h"
#include "../app/logwidget.h"

#include <QDir>
#include <QDebug>
#include <QRegularExpression>

AutoSyncOrchestrator::AutoSyncOrchestrator(QObject *parent)
    : QObject(parent)
{
}

AutoSyncOrchestrator::~AutoSyncOrchestrator()
{
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
                QStringLiteral("Palm detected but already busy - ignoring"));
        }
        return;
    }

    m_busy = true;
    m_currentUsbSerial = usbSerial;

    Q_EMIT statusChanged(tr("Palm device detected..."));

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Palm detected (S/N: %1) on %2 port(s)")
                .arg(usbSerial.isEmpty() ? QStringLiteral("unknown") : usbSerial)
                .arg(ports.size()));
    }

    // Resolve profile by USB serial only (fingerprint-keyed registry
    // removed in Task 0.B; the serial registry is the sole lookup).
    Profile *profile = nullptr;
    KF6Settings &settings = KF6Settings::instance();

    if (!usbSerial.isEmpty()) {
        QString profilePath = settings.findProfileBySerial(usbSerial);
        if (!profilePath.isEmpty() && QDir(profilePath).exists()) {
            auto *p = new Profile(profilePath);
            if (p->exists()) {
                p->load();
                profile = p;
                if (m_logWidget) {
                    m_logWidget->logInfo(
                        QStringLiteral("Found profile by serial: %1").arg(profilePath));
                }
            } else {
                delete p;
            }
        }
    }

    if (profile) {
        Q_EMIT deviceDetected(profile, ports);
    } else {
        // Unrecognised device: do NOT silently auto-create. UI prompts.
        if (m_logWidget) {
            m_logWidget->logInfo(
                QStringLiteral("Unrecognised Palm (S/N: %1) — prompting user").arg(usbSerial));
        }
        Q_EMIT unregisteredDeviceDetected(usbSerial, QString(), 0u);
        Q_EMIT deviceDetected(nullptr, ports);
    }

    m_busy = false;
}

// ========== Public Helpers ==========

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

    // 3. No profile found -- auto-create one via the now-shared path.
    return createProfileForDevice(usbSerial, userName, userId);
}

Profile* AutoSyncOrchestrator::createProfileForDevice(const QString &usbSerial,
                                                       const QString &userName,
                                                       quint32 userId)
{
    KF6Settings &settings = KF6Settings::instance();

    DeviceFingerprint fingerprint;
    fingerprint.userId = userId;
    fingerprint.userName = userName;
    fingerprint.usbSerialNumber = usbSerial;

    QString safeName = userName.isEmpty() ? QStringLiteral("PalmUser") : userName;
    safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                     QStringLiteral("_"));

    QString basePath = QDir::homePath() + QStringLiteral("/PalmSync/") + safeName;
    QString finalPath = basePath;
    int suffix = 1;
    while (QDir(finalPath).exists()) {
        Profile existingProfile(finalPath);
        if (existingProfile.exists()) {
            existingProfile.load();
            DeviceFingerprint existingFp = existingProfile.deviceFingerprint();
            if (existingFp.isEmpty() || existingFp.matches(fingerprint)) {
                break;
            }
        } else {
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

    // Register only by USB serial (Task 0.B removes the fingerprint-keyed registry).
    if (!usbSerial.isEmpty()) {
        settings.registerDeviceBySerial(usbSerial, finalPath);
    }
    settings.addRecentProfile(finalPath);
    settings.setDefaultProfilePath(finalPath);
    settings.sync();

    Q_EMIT profileCreated(finalPath, userName);

    return profile;
}
