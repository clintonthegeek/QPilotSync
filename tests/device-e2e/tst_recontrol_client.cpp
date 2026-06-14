#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include "recontrolclient.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::DeviceE2E;

// A tiny in-process ReControl-like server: for each line received, sends a
// canned reply. Single-line for most; dot-terminated multi-line for "info".
//
// Runs its accept + IO loop in a worker thread so the server can respond while
// the ReControlClient blocks on waitForReadyRead in the test thread.
class FakeReControlServer : public QObject
{
    Q_OBJECT
public:
    FakeReControlServer()
    {
        m_thread = new QThread(this);
    }

    ~FakeReControlServer()
    {
        m_thread->quit();
        m_thread->wait();
    }

    quint16 start()
    {
        // Listen on the calling thread before moving to worker, so the port
        // is available immediately when start() returns.
        m_server.listen(QHostAddress::LocalHost, 0);
        const quint16 port = m_server.serverPort();

        // Move to worker thread so newConnection / readyRead signals fire
        // on a separate event loop while the test thread blocks in waitFor*.
        m_server.moveToThread(m_thread);
        moveToThread(m_thread);

        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *c = m_server.nextPendingConnection();
            connect(c, &QTcpSocket::disconnected, c, &QObject::deleteLater);
            connect(c, &QTcpSocket::readyRead, this, [this, c] {
                while (c->canReadLine()) {
                    const QString line = QString::fromLatin1(c->readLine()).trimmed();
                    if (line == QLatin1String("button cradle tap"))
                        c->write("OK\n");
                    else if (line == QLatin1String("info"))
                        c->write("OK POSE64 0.9.1\n serial=serial:pty:HotSync pty=/dev/pts/7\n.\n");
                    else if (line == QLatin1String("bogus"))
                        c->write("ERR usage: unknown command\n");
                    else
                        c->write("OK\n");
                }
            });
        });

        m_thread->start();
        return port;
    }

private:
    QThread *m_thread = nullptr;
    QTcpServer m_server;
};

class TestReControlClient : public QObject
{
    Q_OBJECT
private slots:
    void singleLineOk()
    {
        FakeReControlServer srv;
        const quint16 port = srv.start();
        ReControlClient c;
        QVERIFY(c.connectTo(port));
        const ReControlReply r = c.command(QStringLiteral("button cradle tap"));
        QVERIFY(r.ok);
    }

    void singleLineErr()
    {
        FakeReControlServer srv;
        const quint16 port = srv.start();
        ReControlClient c;
        QVERIFY(c.connectTo(port));
        const ReControlReply r = c.command(QStringLiteral("bogus"));
        QVERIFY(!r.ok);
        QVERIFY(r.head.contains(QStringLiteral("unknown command")));
    }

    void multilineInfoCarriesPty()
    {
        FakeReControlServer srv;
        const quint16 port = srv.start();
        ReControlClient c;
        QVERIFY(c.connectTo(port));
        const ReControlReply r = c.commandMultiline(QStringLiteral("info"));
        QVERIFY(r.ok);
        QVERIFY(r.raw.contains(QStringLiteral("pty=/dev/pts/7")));
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestReControlClient)
#include "tst_recontrol_client.moc"
