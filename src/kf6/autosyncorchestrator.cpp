#include "autosyncorchestrator.h"
#include "kf6settings.h"
#include "runtime/profileregistry.h"

#include "../palm/palmdevicemonitor.h"
#include "../profile.h"
#include "../app/logwidget.h"

#include <QDir>
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

void AutoSyncOrchestrator::setProfileRegistry(WildPalms::Runtime::ProfileRegistry *registry)
{
    m_profileRegistry = registry;
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

    if (!usbSerial.isEmpty() && m_profileRegistry) {
        const auto entry = m_profileRegistry->findBySerial(usbSerial);
        if (entry.isValid() && QDir(entry.path).exists()) {
            auto *p = new Profile(entry.path);
            if (p->exists()) {
                p->load();
                profile = p;
                if (m_logWidget) {
                    m_logWidget->logInfo(
                        QStringLiteral("Found profile by serial: %1").arg(entry.path));
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
    // 1. Look up by USB serial number (most reliable)
    if (!usbSerial.isEmpty() && m_profileRegistry) {
        const auto entry = m_profileRegistry->findBySerial(usbSerial);
        if (entry.isValid() && QDir(entry.path).exists()) {
            if (m_logWidget) {
                m_logWidget->logInfo(
                    QStringLiteral("Found profile by serial: %1").arg(entry.path));
            }
            auto *profile = new Profile(entry.path);
            if (profile->exists()) {
                profile->load();
                return profile;
            }
            delete profile;
        }
    }

    // 2. No profile found -- auto-create one via the now-shared path.
    return createProfileForDevice(usbSerial, userName, userId);
}

Profile* AutoSyncOrchestrator::createProfileForDevice(const QString &usbSerial,
                                                       const QString &userName,
                                                       quint32 userId)
{
    Q_UNUSED(userId)
    if (!m_profileRegistry) return nullptr;

    QString safeName = userName.isEmpty() ? QStringLiteral("PalmUser") : userName;
    safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                     QStringLiteral("_"));

    const auto entry = m_profileRegistry->registerNew(safeName);
    if (!entry.isValid()) {
        if (m_logWidget) {
            m_logWidget->logError(
                QStringLiteral("Failed to create profile for: %1").arg(safeName));
        }
        return nullptr;
    }

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Creating new profile at: %1").arg(entry.path));
    }

    if (!usbSerial.isEmpty())
        m_profileRegistry->bindSerial(entry.id, usbSerial);

    auto *profile = new Profile(entry.path);
    if (!profile->exists()) {
        if (m_logWidget) {
            m_logWidget->logError(
                QStringLiteral("Profile not found at: %1").arg(entry.path));
        }
        delete profile;
        return nullptr;
    }
    profile->load();

    KF6Settings &settings = KF6Settings::instance();
    settings.addRecentProfile(entry.path);
    settings.setDefaultProfilePath(entry.path);
    settings.sync();

    Q_EMIT profileCreated(entry.path, userName);

    return profile;
}
