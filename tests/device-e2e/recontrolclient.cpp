#include "recontrolclient.h"

#include <QTcpSocket>
#include <QElapsedTimer>

namespace WildPalms {
namespace DeviceE2E {

ReControlClient::ReControlClient() = default;

ReControlClient::~ReControlClient()
{
    disconnect();
}

bool ReControlClient::connectTo(quint16 port, int timeoutMs, const QString &host)
{
    disconnect();
    m_sock = new QTcpSocket();
    m_sock->connectToHost(host, port);
    if (!m_sock->waitForConnected(timeoutMs)) {
        delete m_sock;
        m_sock = nullptr;
        return false;
    }
    return true;
}

void ReControlClient::disconnect()
{
    if (m_sock) {
        m_sock->disconnectFromHost();
        m_sock->abort();
        delete m_sock;
        m_sock = nullptr;
    }
}

bool ReControlClient::isConnected() const
{
    return m_sock && m_sock->state() == QAbstractSocket::ConnectedState;
}

bool ReControlClient::readLineLatin1(QString &out, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!m_sock->canReadLine()) {
        const int remaining = timeoutMs - int(t.elapsed());
        if (remaining <= 0)
            return false;
        if (!m_sock->waitForReadyRead(remaining))
            return false;
    }
    out = QString::fromLatin1(m_sock->readLine());
    return true;
}

static ReControlReply parseFirstLine(const QString &line)
{
    ReControlReply r;
    const QString trimmed = QString(line).remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
    r.raw = trimmed;
    if (trimmed.startsWith(QLatin1String("OK"))) {
        r.ok = true;
        r.head = trimmed.mid(2).trimmed();
    } else if (trimmed.startsWith(QLatin1String("ERR"))) {
        r.ok = false;
        r.head = trimmed.mid(3).trimmed();
    }
    return r;
}

ReControlReply ReControlClient::command(const QString &cmd, int timeoutMs)
{
    ReControlReply r;
    if (!isConnected())
        return r;
    m_sock->write((cmd + QLatin1Char('\n')).toLatin1());
    if (!m_sock->waitForBytesWritten(timeoutMs))
        return r;
    QString line;
    if (!readLineLatin1(line, timeoutMs))
        return r;
    return parseFirstLine(line);
}

ReControlReply ReControlClient::commandMultiline(const QString &cmd, int timeoutMs)
{
    ReControlReply r;
    if (!isConnected())
        return r;
    m_sock->write((cmd + QLatin1Char('\n')).toLatin1());
    if (!m_sock->waitForBytesWritten(timeoutMs))
        return r;

    QString first;
    if (!readLineLatin1(first, timeoutMs))
        return r;
    r = parseFirstLine(first);

    // Read indented data lines until a line that is exactly "." (dot terminator).
    forever {
        QString line;
        if (!readLineLatin1(line, timeoutMs))
            break;
        const QString trimmed = QString(line).remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
        if (trimmed == QLatin1String("."))
            break;
        r.body.append(trimmed);
        r.raw += QLatin1Char('\n') + trimmed;
    }
    return r;
}

} // namespace DeviceE2E
} // namespace WildPalms
