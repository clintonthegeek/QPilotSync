#ifndef WILDPALMS_PALM_PALMDEVICECONNECTION_H
#define WILDPALMS_PALM_PALMDEVICECONNECTION_H

#include <QObject>

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
class IPalmFileInstaller;
class PalmBackend;
}

/**
 * @brief Aggregator passed to plugins via IBackendPlugin::createBackends
 *        and to actions via IPluginAction::execute.
 *
 * Owns a PalmBackend wrapping the caller-supplied IPalmDatabaseAccess.
 * Borrows the IPalmFileInstaller (Phase E.15a). Does NOT own the
 * IPalmDatabaseAccess or IPalmFileInstaller — the caller (application
 * runtime) keeps both alive for the connection's lifetime.
 *
 * Lives in the global namespace to match the forward declaration in
 * src/core/ibackendplugin.h (which stays Kalburator-free and
 * namespace-lean).
 */
class PalmDeviceConnection : public QObject
{
    Q_OBJECT
public:
    explicit PalmDeviceConnection(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        QObject *parent = nullptr);

    /// Phase E.15a — overload that wires an installer for the install
    /// action.
    PalmDeviceConnection(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        WildPalms::PalmSync::IPalmFileInstaller  *fileInstaller,
        QObject *parent = nullptr);

    ~PalmDeviceConnection() override;

    WildPalms::PalmSync::IPalmDatabaseAccess *device() const;
    WildPalms::PalmSync::PalmBackend         *palmBackend() const;
    WildPalms::PalmSync::IPalmFileInstaller  *fileInstaller() const;

signals:
    void connected();     // wired in a future sub-phase (E.17)
    void disconnected();  // wired in a future sub-phase (E.17)

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device        = nullptr;
    WildPalms::PalmSync::PalmBackend         *m_palmBackend   = nullptr;
    WildPalms::PalmSync::IPalmFileInstaller  *m_fileInstaller = nullptr;
};

#endif // WILDPALMS_PALM_PALMDEVICECONNECTION_H
