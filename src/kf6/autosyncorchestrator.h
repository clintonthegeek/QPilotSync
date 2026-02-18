#ifndef AUTOSYNCORCHESTRATOR_H
#define AUTOSYNCORCHESTRATOR_H

#include <QObject>

class PalmDeviceMonitor;
class DeviceSession;
class KPilotDeviceLink;
class Profile;
class LogWidget;

namespace Sync { class SyncEngine; }

/**
 * @brief Orchestrates the auto-detect -> connect -> sync pipeline.
 *
 * When PalmDeviceMonitor detects a Palm:
 * 1. Tries detected ports sequentially (bounded timeout per port)
 * 2. Reads device identity (user info, serial number)
 * 3. Looks up or auto-creates a profile
 * 4. Runs HotSync
 * 5. Disconnects cleanly
 */
class AutoSyncOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit AutoSyncOrchestrator(QObject *parent = nullptr);
    ~AutoSyncOrchestrator() override;

    void setDeviceMonitor(PalmDeviceMonitor *monitor);
    void setSyncEngine(Sync::SyncEngine *engine);
    void setLogWidget(LogWidget *logWidget);

    bool isBusy() const { return m_busy; }
    DeviceSession* activeSession() const { return m_session; }

Q_SIGNALS:
    void syncStarted(const QString &userName);
    void syncFinished(bool success, const QString &summary);
    void profileCreated(const QString &profilePath, const QString &userName);
    void profileLoaded(Profile *profile);
    void connectionEstablished(const QString &userName, const QString &deviceName);
    void error(const QString &message);
    void statusChanged(const QString &status);

private Q_SLOTS:
    void onPalmDetected(const QStringList &ports, const QString &usbSerial);
    void onConnectionComplete(bool success);
    void onDeviceReady(const QString &userName, const QString &deviceName);
    void onReadyForSync();
    void onSyncFinished(bool success, const QString &summary);

private:
    Profile* findOrCreateProfile(const QString &usbSerial,
                                  const QString &userName, quint32 userId);
    void startConnection(const QStringList &ports);
    void cleanupAfterFailure();

    PalmDeviceMonitor *m_monitor = nullptr;
    Sync::SyncEngine *m_syncEngine = nullptr;
    LogWidget *m_logWidget = nullptr;

    // Connection state
    DeviceSession *m_session = nullptr;
    QString m_currentUsbSerial;
    bool m_busy = false;

    // Profile for current sync
    Profile *m_currentProfile = nullptr;
};

#endif // AUTOSYNCORCHESTRATOR_H
