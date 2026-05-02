#include "palmdeviceaccess.h"

#include <QMetaObject>

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

PalmDeviceAccess::~PalmDeviceAccess() {
    if (m_linkThread && m_linkThread->isRunning()) {
        QMetaObject::invokeMethod(m_implOwner,
            [this]() { m_impl.reset(); },
            Qt::BlockingQueuedConnection);
        m_linkThread->quit();
        m_linkThread->wait();
        // m_implOwner cleaned up via QThread::finished → deleteLater
    } else {
        // Thread never started or already stopped; clean up directly.
        delete m_implOwner;
        m_implOwner = nullptr;
    }
}

QStringList PalmDeviceAccess::availableDatabases() const {
    QStringList result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &result]() { result = m_impl->availableDatabases(); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::hasDatabase(const QString &dbName) const {
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->hasDatabase(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::createDatabase(const QString &dbName) {
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->createDatabase(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

QList<WildPalms::PalmSync::PalmRecord>
PalmDeviceAccess::readAllRecords(const QString &dbName) const {
    QList<WildPalms::PalmSync::PalmRecord> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->readAllRecords(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

std::optional<WildPalms::PalmSync::PalmRecord>
PalmDeviceAccess::readRecord(const QString &dbName, std::uint32_t recordId) const {
    std::optional<WildPalms::PalmSync::PalmRecord> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, recordId, &result]() { result = m_impl->readRecord(dbName, recordId); },
        Qt::BlockingQueuedConnection);
    return result;
}

std::uint32_t PalmDeviceAccess::createRecord(const QString &dbName,
                                              const WildPalms::PalmSync::PalmRecord &record) {
    std::uint32_t result = 0;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &record, &result]() { result = m_impl->createRecord(dbName, record); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::updateRecord(const QString &dbName,
                                    const WildPalms::PalmSync::PalmRecord &record) {
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &record, &result]() { result = m_impl->updateRecord(dbName, record); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::deleteRecord(const QString &dbName, std::uint32_t recordId) {
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, recordId, &result]() { result = m_impl->deleteRecord(dbName, recordId); },
        Qt::BlockingQueuedConnection);
    return result;
}

QList<WildPalms::PalmSync::PalmRecord>
PalmDeviceAccess::recordsModifiedSince(const QString &dbName,
                                       const QDateTime &since) const {
    QList<WildPalms::PalmSync::PalmRecord> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &since, &result]() { result = m_impl->recordsModifiedSince(dbName, since); },
        Qt::BlockingQueuedConnection);
    return result;
}

QList<std::uint32_t>
PalmDeviceAccess::recordsDeletedSince(const QString &dbName,
                                      const QDateTime &since) const {
    QList<std::uint32_t> result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &since, &result]() { result = m_impl->recordsDeletedSince(dbName, since); },
        Qt::BlockingQueuedConnection);
    return result;
}

QByteArray PalmDeviceAccess::readAppBlock(const QString &dbName) const {
    QByteArray result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->readAppBlock(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::supportsDeleteTracking() const {
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &result]() { result = m_impl->supportsDeleteTracking(); },
        Qt::BlockingQueuedConnection);
    return result;
}

} // namespace WildPalms::Runtime
