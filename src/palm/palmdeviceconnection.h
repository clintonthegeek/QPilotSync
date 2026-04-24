#ifndef WILDPALMS_PALM_PALMDEVICECONNECTION_H
#define WILDPALMS_PALM_PALMDEVICECONNECTION_H

#include <QObject>

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
class PalmBackend;
}

/**
 * @brief Aggregator passed to plugins via IBackendPlugin::createBackends.
 *
 * Owns a PalmBackend wrapping the caller-supplied IPalmDatabaseAccess.
 * Does NOT own the IPalmDatabaseAccess — the caller (application
 * runtime) must keep it alive for the connection's lifetime.
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
    ~PalmDeviceConnection() override;

    WildPalms::PalmSync::IPalmDatabaseAccess *device() const;
    WildPalms::PalmSync::PalmBackend         *palmBackend() const;

signals:
    void connected();     // wired in a future sub-phase (E.15/E.17)
    void disconnected();  // wired in a future sub-phase (E.15/E.17)

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    WildPalms::PalmSync::PalmBackend         *m_palmBackend = nullptr;
};

#endif // WILDPALMS_PALM_PALMDEVICECONNECTION_H
