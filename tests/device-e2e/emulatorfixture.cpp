#include "emulatorfixture.h"
#include "recontrolclient.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTcpServer>
#include <QThread>

namespace WildPalms {
namespace DeviceE2E {

EmulatorFixture::EmulatorFixture()
    : m_client(std::make_unique<ReControlClient>())
{
}

EmulatorFixture::~EmulatorFixture()
{
    quit();
}

bool EmulatorFixture::configured()
{
    return !qEnvironmentVariableIsEmpty("WILDPALMS_POSE64_BIN")
        && !qEnvironmentVariableIsEmpty("WILDPALMS_PALM_BASELINE_PSF");
}

static quint16 pickFreePort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 p = s.serverPort();
    s.close();
    return p;
}

bool EmulatorFixture::launch()
{
    const QString bin = qEnvironmentVariable("WILDPALMS_POSE64_BIN");
    const QString psf = qEnvironmentVariable("WILDPALMS_PALM_BASELINE_PSF");
    if (!QFileInfo::exists(bin)) { m_lastError = QStringLiteral("pose64 binary not found: %1").arg(bin); return false; }
    if (!QFileInfo::exists(psf)) { m_lastError = QStringLiteral("baseline psf not found: %1").arg(psf); return false; }

    m_port = pickFreePort();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    m_proc.setProcessEnvironment(env);
    m_proc.setProcessChannelMode(QProcess::MergedChannels);

    const QStringList args{
        QStringLiteral("-psf"), psf,
        QStringLiteral("--port"), QString::number(m_port),
        QStringLiteral("-preference"), QStringLiteral("PortSerial=serial:pty:HotSync"),
    };
    m_proc.start(bin, args);
    if (!m_proc.waitForStarted(5000)) { m_lastError = QStringLiteral("pose64 failed to start: %1").arg(m_proc.errorString()); return false; }

    if (!waitForReady(25000)) { m_lastError = QStringLiteral("ReControl not ready: %1").arg(m_lastError); return false; }
    if (!m_client->connectTo(m_port, 10000)) { m_lastError = QStringLiteral("could not connect ReControl session on port %1").arg(m_port); return false; }
    if (!refreshPty(5000)) {
        m_lastError = QStringLiteral("initial pty query failed: %1").arg(m_lastError);
        return false;
    }
    return true;
}

bool EmulatorFixture::waitForReady(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        if (m_proc.state() != QProcess::Running) { m_lastError = QStringLiteral("pose64 exited during startup"); return false; }
        ReControlClient probe; // fully separate connection; closes on scope exit (one session at a time)
        if (probe.connectTo(m_port, 2000)) {
            const ReControlReply r = probe.command(QStringLiteral("state"), 2000);
            probe.disconnect();
            if (r.ok)
                return true;
        }
        if (!deadline.hasExpired())
            QThread::msleep(400);
    }
    m_lastError = QStringLiteral("timed out");
    return false;
}

bool EmulatorFixture::refreshPty(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    static const QRegularExpression re(QStringLiteral("pty=(/dev/pts/[0-9]+)"));
    while (!deadline.hasExpired()) {
        const int remaining = static_cast<int>(deadline.remainingTime());
        const ReControlReply r = m_client->commandMultiline(QStringLiteral("info"),
                                                             qMin(5000, remaining > 0 ? remaining : 1));
        const QRegularExpressionMatch m = re.match(r.raw);
        if (m.hasMatch()) { m_pty = m.captured(1); return true; }
        if (!deadline.hasExpired())
            QThread::msleep(250);
    }
    m_lastError = QStringLiteral("no pty= in info");
    return false;
}

bool EmulatorFixture::loadBaseline()
{
    const QString psf = qEnvironmentVariable("WILDPALMS_PALM_BASELINE_PSF");
    const ReControlReply r = m_client->command(QStringLiteral("load %1").arg(psf), 15000);
    if (!r.ok) { m_lastError = QStringLiteral("load failed: %1").arg(r.head); return false; }
    return refreshPty(5000);
}

bool EmulatorFixture::exportDatabase(const QString &dbName, const QString &hostPath)
{
    const ReControlReply r = m_client->command(QStringLiteral("export %1 %2").arg(dbName, hostPath), 30000);
    if (!r.ok) m_lastError = QStringLiteral("export failed: %1").arg(r.head);
    return r.ok;
}

bool EmulatorFixture::cradleTap()
{
    const ReControlReply r = m_client->command(QStringLiteral("button cradle tap"), 5000);
    if (!r.ok) m_lastError = QStringLiteral("cradle tap failed: %1").arg(r.head);
    return r.ok;
}

bool EmulatorFixture::dismissProblemFormIfPresent()
{
    const ReControlReply ui = m_client->commandMultiline(QStringLiteral("ui"), 5000);
    if (!ui.raw.contains(QStringLiteral("id=12000")))
        return false;
    const ReControlReply tap = m_client->command(QStringLiteral("tap-id 12004"), 5000);
    if (!tap.ok) {
        m_lastError = QStringLiteral("tap-id 12004 failed: %1").arg(tap.head);
        return false;
    }
    return true;
}

void EmulatorFixture::quit()
{
    if (m_client && m_client->isConnected())
        m_client->command(QStringLiteral("quit"), 2000);
    if (m_proc.state() != QProcess::NotRunning) {
        if (!m_proc.waitForFinished(3000)) {
            m_proc.kill();
            m_proc.waitForFinished(2000);
        }
    }
}

} // namespace DeviceE2E
} // namespace WildPalms
