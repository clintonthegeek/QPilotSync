#include "palmdeviceaccess.h"

#include <QMetaObject>

#include "palm/kpilotdevicelink.h"
#include "palm/device/pilotlinkpalmdatabaseaccess.h"
#include "palmtickle.h"

namespace WildPalms::Runtime {

PalmDeviceAccess::PalmDeviceAccess(
    std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> impl,
    QObject *parent)
    : QObject(parent)
    , m_impl(std::move(impl))
    , m_linkThread(std::make_unique<QThread>())
{
    Q_ASSERT(m_impl);
    m_linkThread->setObjectName(QStringLiteral("PalmLinkThread"));

    m_implOwner = new QObject();
    m_implOwner->moveToThread(m_linkThread.get());
    connect(m_linkThread.get(), &QThread::finished,
            m_implOwner, &QObject::deleteLater);

    m_linkThread->start();
}

PalmDeviceAccess::PalmDeviceAccess(QObject *parent)
    : QObject(parent)
    , m_linkThread(std::make_unique<QThread>())
{
    m_linkThread->setObjectName(QStringLiteral("PalmLinkThread"));

    m_implOwner = new QObject();
    m_implOwner->moveToThread(m_linkThread.get());
    connect(m_linkThread.get(), &QThread::finished,
            m_implOwner, &QObject::deleteLater);

    m_linkThread->start();
}

PalmDeviceAccess::~PalmDeviceAccess() {
    // Defensive: disconnect any link signals before anything else, so
    // queued events from the link thread don't deliver to a half-destroyed
    // PalmDeviceAccess.
    if (m_link) {
        disconnect(m_link, nullptr, this, nullptr);
    }

    if (m_linkThread && m_linkThread->isRunning()) {
        // New-path teardown if connected.
        if (m_connected.load() || m_link) {
            QMetaObject::invokeMethod(m_implOwner,
                [this]() { doDisconnect(); },
                Qt::BlockingQueuedConnection);
        }
        // Legacy-path m_impl teardown if any remains.
        if (m_impl) {
            QMetaObject::invokeMethod(m_implOwner,
                [this]() { m_impl.reset(); },
                Qt::BlockingQueuedConnection);
        }
        m_linkThread->quit();
        m_linkThread->wait();
        // m_implOwner cleaned up via QThread::finished → deleteLater
    } else {
        // Thread never started or already stopped; clean up directly.
        delete m_implOwner;
        m_implOwner = nullptr;
    }
}

void PalmDeviceAccess::connectDevice(const QStringList &devicePaths)
{
    if (m_connecting.exchange(true)) {
        emit logMessage(QStringLiteral("connectDevice ignored: already connecting"));
        return;
    }
    if (m_connected.load()) {
        m_connecting = false;
        emit logMessage(QStringLiteral("connectDevice ignored: already connected"));
        return;
    }
    emit connectionStarted();

    QMetaObject::invokeMethod(m_implOwner,
        [this, devicePaths]() { doConnect(devicePaths); },
        Qt::QueuedConnection);
}

void PalmDeviceAccess::doConnect(const QStringList &devicePaths)
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());

    auto *link = new KPilotDeviceLink(devicePaths);
    link->moveToThread(m_linkThread.get());
    link->setParent(m_implOwner);  // ensures cleanup if thread is torn down
    m_link = link;

    connect(link, &KPilotLink::logMessage, this,
            [this](const QString &m) { emit logMessage(m); },
            Qt::QueuedConnection);

    // Capture errorOccurred synchronously so we can include the error
    // text in connectionComplete(false, ...).
    m_pendingError.clear();
    connect(link, &KPilotLink::errorOccurred, this,
            [this](const QString &error) { m_pendingError = error; },
            Qt::DirectConnection);

    // KPilotDeviceLink emits connectionComplete(bool) once the inner
    // ConnectionWorker finishes. On success we read the handshake data
    // back from the link's own getters; on failure we read the error
    // string via KPilotLink::lastError().
    connect(link, &KPilotDeviceLink::connectionComplete, this,
            [this, link](bool success) {
                if (success) {
                    HandshakeResult result;
                    result.socket           = link->socketDescriptor();
                    result.userInfoValid    = link->handshakeUserInfoValid();
                    result.userName         = link->handshakeUserName();
                    result.userId           = link->handshakeUserId();
                    result.sysInfoValid     = link->handshakeSysInfoValid();
                    result.romVersion       = link->handshakeRomVersion();
                    result.productId        = link->handshakeProductId();
                    result.storageInfoValid = link->handshakeStorageInfoValid();
                    result.cardName         = link->handshakeCardName();
                    result.manufacturer     = link->handshakeManufacturer();
                    result.romSize          = link->handshakeRomSize();
                    result.ramSize          = link->handshakeRamSize();
                    result.ramFree          = link->handshakeRamFree();
                    onLinkConnectionEstablished(result);
                } else {
                    onLinkConnectionFailed(m_pendingError.isEmpty()
                        ? QStringLiteral("connection failed")
                        : m_pendingError);
                }
            },
            Qt::DirectConnection);   // we're already on m_linkThread

    if (!link->openConnection()) {
        onLinkConnectionFailed(QStringLiteral("openConnection() returned false"));
    }
}

void PalmDeviceAccess::onLinkConnectionEstablished(const HandshakeResult &result)
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());
    m_handshake = result;

    // Construct the IPalmDatabaseAccess impl wrapping the live link.
    // Note: this lives on m_linkThread because we're on m_linkThread now.
    m_impl = std::make_unique<WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess>(m_link);

    // Wire tickle on the link thread.
    m_tickle = new PalmTickle(m_link, result.socket, m_implOwner);
    auto *deviceLink = qobject_cast<KPilotDeviceLink*>(m_link);
    Q_ASSERT(deviceLink);
    connect(deviceLink, &KPilotDeviceLink::ticklePauseRequested,
            m_tickle, &PalmTickle::stop, Qt::QueuedConnection);
    connect(deviceLink, &KPilotDeviceLink::tickleResumeRequested,
            m_tickle, &PalmTickle::start, Qt::QueuedConnection);
    connect(m_tickle, &PalmTickle::connectionLost, this,
            [this]() { disconnectDevice(); }, Qt::QueuedConnection);
    m_tickle->start();

    m_connected = true;
    m_connecting = false;
    emit connectionComplete(true, QString());
}

void PalmDeviceAccess::onLinkConnectionFailed(const QString &error)
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());
    if (m_link) {
        m_link->deleteLater();
        m_link = nullptr;
    }
    m_connecting = false;
    m_connected = false;
    emit connectionComplete(false, error);
}

void PalmDeviceAccess::cancelConnect()
{
    QMetaObject::invokeMethod(m_implOwner,
        [this]() { doCancelConnect(); },
        Qt::QueuedConnection);
}

void PalmDeviceAccess::doCancelConnect()
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());
    if (auto *link = qobject_cast<KPilotDeviceLink*>(m_link)) {
        link->cancelConnection();
    }
}

void PalmDeviceAccess::disconnectDevice()
{
    if (!m_linkThread || !m_linkThread->isRunning()) return;
    QMetaObject::invokeMethod(m_implOwner,
        [this]() { doDisconnect(); },
        Qt::BlockingQueuedConnection);
}

void PalmDeviceAccess::doDisconnect()
{
    Q_ASSERT(QThread::currentThread() == m_linkThread.get());
    if (m_tickle) {
        m_tickle->stop();
        m_tickle->deleteLater();
        m_tickle = nullptr;
    }
    m_impl.reset();   // destructs PilotLinkPalmDatabaseAccess on link thread
    if (m_link) {
        m_link->closeConnection();
        m_link->deleteLater();
        m_link = nullptr;
    }
    if (m_connected.exchange(false)) {
        emit deviceDisconnected();
    }
    m_connecting = false;
}

bool PalmDeviceAccess::isConnected()  const { return m_connected.load(); }
bool PalmDeviceAccess::isConnecting() const { return m_connecting.load(); }

KPilotLink *PalmDeviceAccess::link() const { return m_link; }

QString PalmDeviceAccess::handshakeUserName()   const { return m_handshake.userName; }
quint32 PalmDeviceAccess::handshakeUserId()     const { return m_handshake.userId; }
QString PalmDeviceAccess::handshakeProductId()  const { return m_handshake.productId; }
QString PalmDeviceAccess::handshakeCardName()   const { return m_handshake.cardName; }
quint32 PalmDeviceAccess::handshakeRomVersion() const { return m_handshake.romVersion; }

QStringList PalmDeviceAccess::availableDatabases() const {
    if (!m_impl) return {};
    QStringList result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &result]() { result = m_impl->availableDatabases(); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::hasDatabase(const QString &dbName) const {
    if (!m_impl) return false;
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->hasDatabase(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::createDatabase(const QString &dbName) {
    if (!m_impl) return false;
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->createDatabase(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

QList<WildPalms::PalmSync::PalmRecord>
PalmDeviceAccess::readAllRecords(const QString &dbName) const {
    if (!m_impl) return {};
    QList<WildPalms::PalmSync::PalmRecord> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->readAllRecords(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

std::optional<WildPalms::PalmSync::PalmRecord>
PalmDeviceAccess::readRecord(const QString &dbName, std::uint32_t recordId) const {
    if (!m_impl) return std::nullopt;
    std::optional<WildPalms::PalmSync::PalmRecord> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, recordId, &result]() { result = m_impl->readRecord(dbName, recordId); },
        Qt::BlockingQueuedConnection);
    return result;
}

std::uint32_t PalmDeviceAccess::createRecord(const QString &dbName,
                                              const WildPalms::PalmSync::PalmRecord &record) {
    if (!m_impl) return 0;
    std::uint32_t result = 0;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &record, &result]() { result = m_impl->createRecord(dbName, record); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::updateRecord(const QString &dbName,
                                    const WildPalms::PalmSync::PalmRecord &record) {
    if (!m_impl) return false;
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &record, &result]() { result = m_impl->updateRecord(dbName, record); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::deleteRecord(const QString &dbName, std::uint32_t recordId) {
    if (!m_impl) return false;
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, recordId, &result]() { result = m_impl->deleteRecord(dbName, recordId); },
        Qt::BlockingQueuedConnection);
    return result;
}

QList<WildPalms::PalmSync::PalmRecord>
PalmDeviceAccess::recordsModifiedSince(const QString &dbName,
                                       const QDateTime &since) const {
    if (!m_impl) return {};
    QList<WildPalms::PalmSync::PalmRecord> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &since, &result]() { result = m_impl->recordsModifiedSince(dbName, since); },
        Qt::BlockingQueuedConnection);
    return result;
}

QList<std::uint32_t>
PalmDeviceAccess::recordsDeletedSince(const QString &dbName,
                                      const QDateTime &since) const {
    if (!m_impl) return {};
    QList<std::uint32_t> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &since, &result]() { result = m_impl->recordsDeletedSince(dbName, since); },
        Qt::BlockingQueuedConnection);
    return result;
}

QByteArray PalmDeviceAccess::readAppBlock(const QString &dbName) const {
    if (!m_impl) return {};
    QByteArray result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->readAppBlock(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::supportsDeleteTracking() const {
    if (!m_impl) return false;
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &result]() { result = m_impl->supportsDeleteTracking(); },
        Qt::BlockingQueuedConnection);
    return result;
}

} // namespace WildPalms::Runtime
