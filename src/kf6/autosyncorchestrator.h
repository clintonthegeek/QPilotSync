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
 * 1. Races parallel connections on all detected ports
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
    void startParallelConnections(const QStringList &ports);
    void cleanupLosingConnection(int index);

    PalmDeviceMonitor *m_monitor = nullptr;
    Sync::SyncEngine *m_syncEngine = nullptr;
    LogWidget *m_logWidget = nullptr;

    // Parallel connection state
    QList<DeviceSession*> m_racingSessions;
    DeviceSession *m_winningSession = nullptr;
    QString m_currentUsbSerial;
    bool m_busy = false;

    // Profile for current sync
    Profile *m_currentProfile = nullptr;
};

#endif // AUTOSYNCORCHESTRATOR_H
