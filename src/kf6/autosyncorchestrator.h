#ifndef AUTOSYNCORCHESTRATOR_H
#define AUTOSYNCORCHESTRATOR_H

#include <QObject>

class PalmDeviceMonitor;
class Profile;
class LogWidget;

namespace WildPalms::Runtime { class ProfileRegistry; }

/**
 * @brief Lightweight USB device detection and profile resolution.
 *
 * When PalmDeviceMonitor detects a Palm:
 * 1. Looks up or auto-creates a profile based on USB serial / fingerprint
 * 2. Emits deviceDetected() so KF6MainWindow can handle the session
 *
 * Does NOT manage sync lifecycle (PalmRuntime owns that, M6b+).
 */
class AutoSyncOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit AutoSyncOrchestrator(QObject *parent = nullptr);
    ~AutoSyncOrchestrator() override;

    void setDeviceMonitor(PalmDeviceMonitor *monitor);
    void setLogWidget(LogWidget *logWidget);
    void setProfileRegistry(WildPalms::Runtime::ProfileRegistry *registry);

    bool isBusy() const { return m_busy; }

    /**
     * @brief Find or auto-create a profile for a device
     *
     * Called by KF6MainWindow after connecting to read device identity.
     * Looks up by USB serial, then by fingerprint, then auto-creates.
     */
    Profile* findOrCreateProfile(const QString &usbSerial,
                                  const QString &userName, quint32 userId);

    /**
     * @brief Create + register a profile for a confirmed-new device.
     *
     * Called by the UI after the user confirms creation in the
     * unregistered-device dialog. Does NOT prompt itself.
     * @return the newly-created Profile (caller takes ownership), or
     *         nullptr if profile init failed.
     */
    Profile* createProfileForDevice(const QString &usbSerial,
                                    const QString &userName,
                                    quint32 userId);

    QString currentUsbSerial() const { return m_currentUsbSerial; }

Q_SIGNALS:
    /** Emitted when a device is detected and profile resolved (profile may be nullptr for unknown devices) */
    void deviceDetected(Profile *profile, const QStringList &ports);

    /** Emitted when a new profile is auto-created */
    void profileCreated(const QString &profilePath, const QString &userName);

    /**
     * @brief Emitted when an unrecognised Palm device is detected.
     *
     * Replaces the previous silent auto-create-profile behavior. The UI is
     * expected to connect to this signal and prompt the user; on consent,
     * it calls createProfileForDevice() to actually create the profile.
     */
    void unregisteredDeviceDetected(const QString &usbSerial,
                                    const QString &userName,
                                    quint32 userId);

    void error(const QString &message);
    void statusChanged(const QString &status);

private Q_SLOTS:
    void onPalmDetected(const QStringList &ports, const QString &usbSerial);

private:
    PalmDeviceMonitor *m_monitor = nullptr;
    LogWidget *m_logWidget = nullptr;
    WildPalms::Runtime::ProfileRegistry *m_profileRegistry = nullptr;
    QString m_currentUsbSerial;
    bool m_busy = false;
};

#endif // AUTOSYNCORCHESTRATOR_H
