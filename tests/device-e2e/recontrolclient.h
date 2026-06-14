#pragma once

#include <QString>
#include <QStringList>

class QTcpSocket;

namespace WildPalms {
namespace DeviceE2E {

// One ReControl reply. `ok` is true iff the first line starts with "OK".
// `head` is the remainder of the first line after "OK "/"ERR ".
// `body` holds the indented data lines of a multi-line (dot-terminated) reply.
struct ReControlReply {
    bool ok = false;
    QString head;
    QStringList body;
    QString raw;
};

// Minimal synchronous client for POSE64's ReControl TCP protocol.
// Single-line replies: read until the first '\n'. Multi-line replies
// (info/apps/ui/dialog): read indented lines until a line that is just ".".
// All bytes are decoded latin-1 (Palm OS text is Latin-1).
class ReControlClient
{
public:
    ReControlClient();
    ~ReControlClient();

    bool connectTo(quint16 port, int timeoutMs = 5000, const QString &host = QStringLiteral("localhost"));
    void disconnect();
    bool isConnected() const;

    // Send a single-line command; read a single-line reply.
    ReControlReply command(const QString &cmd, int timeoutMs = 10000);
    // Send a command whose reply is multi-line dot-terminated (info/apps/ui/dialog).
    ReControlReply commandMultiline(const QString &cmd, int timeoutMs = 10000);

private:
    bool readLineLatin1(QString &out, int timeoutMs);
    QTcpSocket *m_sock = nullptr;
};

} // namespace DeviceE2E
} // namespace WildPalms
