#ifndef WILDPALMS_RUNTIME_PALMDEVICEACCESS_H
#define WILDPALMS_RUNTIME_PALMDEVICEACCESS_H

#include <QObject>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <memory>

#include "palm/kpilotdevicelink.h"   // for HandshakeResult
#include "palm/sync/ipalmdatabaseaccess.h"

class KPilotLink;

namespace WildPalms::Runtime {

class PalmTickle;

class PalmDeviceAccess : public QObject,
                         public WildPalms::PalmSync::IPalmDatabaseAccess
{
    Q_OBJECT
public:
    explicit PalmDeviceAccess(
        std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> impl,
        QObject *parent = nullptr);

    /// Empty constructor: no IPalmDatabaseAccess yet. Call connectDevice()
    /// to open a real Palm device. Used by the post-M6b PalmRuntime path.
    explicit PalmDeviceAccess(QObject *parent = nullptr);

    ~PalmDeviceAccess() override;

    // ── Connect lifecycle (M6b) ─────────────────────────────────────────────
    /// Open a Palm device on one of the supplied paths. Async — emits
    /// connectionComplete(true, "") on success or connectionComplete(false,
    /// error) on failure. Constructs a KPilotDeviceLink internally on
    /// m_linkThread, listens for its connectionEstablished/connectionFailed
    /// signals, then sets up m_impl + PalmTickle.
    void connectDevice(const QStringList &devicePaths);

    /// Cancel an in-progress connect. No-op if not connecting.
    void cancelConnect();

    /// Tear down the link, m_impl, and tickle. Safe to call repeatedly.
    /// Blocks until link-thread cleanup completes.
    void disconnectDevice();

    bool isConnected()  const;
    bool isConnecting() const;

    /// Handshake info — only valid after connectionComplete(true, "").
    QString handshakeUserName()   const;
    quint32 handshakeUserId()     const;
    QString handshakeProductId()  const;
    QString handshakeCardName()   const;
    quint32 handshakeRomVersion() const;

    /// Borrowed pointer to the underlying link. Only valid while isConnected().
    /// Plugins should NOT use this — they get IPalmDatabaseAccess via *this.
    KPilotLink *link() const;

    QStringList availableDatabases() const override;
    bool        hasDatabase(const QString &dbName) const override;
    bool        createDatabase(const QString &dbName) override;
    QList<WildPalms::PalmSync::PalmRecord>
                readAllRecords(const QString &dbName) const override;
    std::optional<WildPalms::PalmSync::PalmRecord>
                readRecord(const QString &dbName, std::uint32_t recordId) const override;
    std::uint32_t createRecord(const QString &dbName,
                               const WildPalms::PalmSync::PalmRecord &record) override;
    bool        updateRecord(const QString &dbName,
                             const WildPalms::PalmSync::PalmRecord &record) override;
    bool        deleteRecord(const QString &dbName,
                             std::uint32_t recordId) override;
    QList<WildPalms::PalmSync::PalmRecord>
                recordsModifiedSince(const QString &dbName,
                                     const QDateTime &since) const override;
    QList<std::uint32_t>
                recordsDeletedSince(const QString &dbName,
                                    const QDateTime &since) const override;
    QByteArray  readAppBlock(const QString &dbName) const override;
    bool        supportsDeleteTracking() const override;

    QThread *linkThread() const { return m_linkThread.get(); }

signals:
    void connectionStarted();
    void connectionComplete(bool success, QString error);
    void deviceDisconnected();   // emitted from disconnectDevice() OR from
                                 // PalmTickle::connectionLost
    void logMessage(QString message);

private slots:
    Q_INVOKABLE void doConnect(const QStringList &devicePaths);
    Q_INVOKABLE void doDisconnect();
    Q_INVOKABLE void doCancelConnect();

private:
    void onLinkConnectionEstablished(const HandshakeResult &result);
    void onLinkConnectionFailed(const QString &error);

    std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> m_impl;
    std::unique_ptr<QThread>                                  m_linkThread;
    QObject                                                   *m_implOwner = nullptr;

    KPilotLink         *m_link    = nullptr;   // owned via deleteLater on disconnect
    PalmTickle         *m_tickle  = nullptr;   // owned, parented to m_implOwner
    HandshakeResult     m_handshake;
    QString             m_pendingError;        // captured from errorOccurred during connect
    std::atomic<bool>   m_connecting { false };
    std::atomic<bool>   m_connected  { false };
};

} // namespace WildPalms::Runtime

#endif
