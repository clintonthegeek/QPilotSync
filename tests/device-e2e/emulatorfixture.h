#pragma once

#include <QProcess>
#include <QString>
#include <memory>

namespace WildPalms {
namespace DeviceE2E {

class ReControlClient;

// Launches a headless POSE64 process with a HotSync pty transport and a known
// baseline .psf, polls until ReControl answers, and exposes a connected client.
// Resolves the binary and baseline from WILDPALMS_POSE64_BIN /
// WILDPALMS_PALM_BASELINE_PSF. configured() is false when either is unset.
class EmulatorFixture
{
public:
    EmulatorFixture();
    ~EmulatorFixture();

    static bool configured();

    // Launch the emulator on a free TCP port and wait until ReControl is ready.
    bool launch();

    // Reset to the baseline (load <psf>) and re-query the pty. Use between tests.
    bool loadBaseline();

    // The current pty slave path (e.g. /dev/pts/7), re-queried after launch/load.
    QString ptyPath() const { return m_pty; }

    ReControlClient *client() const { return m_client.get(); }
    quint16 port() const { return m_port; }
    QString lastError() const { return m_lastError; }

    bool exportDatabase(const QString &dbName, const QString &hostPath);
    bool cradleTap();
    bool dismissProblemFormIfPresent(); // form id 12000 -> tap-id 12004

    void quit();

private:
    bool waitForReady(int timeoutMs);
    bool refreshPty(int timeoutMs);

    QProcess m_proc;
    std::unique_ptr<ReControlClient> m_client;
    quint16 m_port = 0;
    QString m_pty;
    QString m_lastError;
};

} // namespace DeviceE2E
} // namespace WildPalms
