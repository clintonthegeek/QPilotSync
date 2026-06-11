#include "pilotlinkpalmdatabaseaccess.h"

#include <memory>

#include "kpilotlink.h"
#include "palmrecord_bridge.h"
#include "pilotrecord.h"

namespace WildPalms::PalmDevice {

PilotLinkPalmDatabaseAccess::DbScope::DbScope(KPilotLink *link,
                                              const QString &dbName,
                                              bool rw)
    : m_link(link)
    , m_handle(link ? link->openDatabase(dbName, rw) : -1)
{
}

PilotLinkPalmDatabaseAccess::DbScope::~DbScope()
{
    if (m_link && m_handle >= 0) {
        m_link->closeDatabase(m_handle);
    }
}

PilotLinkPalmDatabaseAccess::PilotLinkPalmDatabaseAccess(KPilotLink *link)
    : m_link(link)
{
}

PilotLinkPalmDatabaseAccess::~PilotLinkPalmDatabaseAccess()
{
    flushWriteHandle();
}

int PilotLinkPalmDatabaseAccess::ensureWriteHandle(const QString &dbName)
{
    if (m_writeHandle >= 0 && m_writeDbName == dbName)
        return m_writeHandle;
    flushWriteHandle();
    if (!m_link) return -1;
    const int h = m_link->openDatabase(dbName, /*rw=*/true);
    if (h >= 0) {
        m_writeHandle = h;
        m_writeDbName = dbName;
    }
    return h;
}

void PilotLinkPalmDatabaseAccess::flushWriteHandle() const
{
    if (m_link && m_writeHandle >= 0)
        m_link->closeDatabase(m_writeHandle);
    m_writeHandle = -1;
    m_writeDbName.clear();
}

void PilotLinkPalmDatabaseAccess::flushPendingWrites()
{
    flushWriteHandle();
}

bool PilotLinkPalmDatabaseAccess::isConnected() const
{
    return m_link && m_link->isConnected();
}

QStringList PilotLinkPalmDatabaseAccess::availableDatabases() const
{
    if (!m_link) return {};
    flushWriteHandle();
    return m_link->listDatabases();
}

bool PilotLinkPalmDatabaseAccess::hasDatabase(const QString &dbName) const
{
    if (!m_link) return false;
    flushWriteHandle();
    return m_link->listDatabases().contains(dbName);
}

bool PilotLinkPalmDatabaseAccess::createDatabase(const QString &dbName)
{
    // See header: Palm DB creation is not wired in the scaffold.
    // Treat as a no-op success for databases that already exist.
    return hasDatabase(dbName);
}

QList<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::readAllRecords(const QString &dbName) const
{
    if (!m_link) return {};
    flushWriteHandle();
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return {};

    const auto raw = m_link->readAllRecords(scope.handle());
    QList<WildPalms::PalmSync::PalmRecord> out;
    out.reserve(raw.size());
    for (auto *rec : raw) {
        out.append(fromPilotRecord(*rec));
        delete rec;
    }
    return out;
}

std::optional<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::readRecord(const QString &dbName,
                                        std::uint32_t recordId) const
{
    if (!m_link) return std::nullopt;
    flushWriteHandle();
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return std::nullopt;

    std::unique_ptr<PilotRecord> rec(
        m_link->readRecordById(scope.handle(),
                               static_cast<int>(recordId)));
    if (!rec) return std::nullopt;
    return fromPilotRecord(*rec);
}

std::uint32_t PilotLinkPalmDatabaseAccess::createRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return 0;
    const int handle = ensureWriteHandle(dbName);
    if (handle < 0) return 0;
    PilotRecord bridged = toPilotRecord(record);
    if (!m_link->writeRecord(handle, &bridged)) return 0;
    return static_cast<std::uint32_t>(bridged.recordId());
}

bool PilotLinkPalmDatabaseAccess::updateRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return false;
    if (record.recordId == 0) return false;
    const int handle = ensureWriteHandle(dbName);
    if (handle < 0) return false;
    PilotRecord bridged = toPilotRecord(record);
    return m_link->writeRecord(handle, &bridged);
}

bool PilotLinkPalmDatabaseAccess::deleteRecord(const QString &dbName,
                                               std::uint32_t recordId)
{
    if (!m_link) return false;
    const int handle = ensureWriteHandle(dbName);
    if (handle < 0) return false;
    return m_link->deleteRecord(handle, static_cast<int>(recordId));
}

QList<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::recordsModifiedSince(const QString &dbName,
                                                  const QDateTime &) const
{
    // `since` is ignored; DLP lacks per-record timestamps. The engine
    // falls back to baseline-based diff.
    if (!m_link) return {};
    flushWriteHandle();
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return {};

    const auto raw = m_link->readModifiedRecords(scope.handle());
    QList<WildPalms::PalmSync::PalmRecord> out;
    out.reserve(raw.size());
    for (auto *rec : raw) {
        out.append(fromPilotRecord(*rec));
        delete rec;
    }
    return out;
}

QList<std::uint32_t>
PilotLinkPalmDatabaseAccess::recordsDeletedSince(const QString &,
                                                 const QDateTime &) const
{
    // See header: deletion tracking is baseline-based, not DLP-based.
    return {};
}

QByteArray PilotLinkPalmDatabaseAccess::readAppBlock(const QString &dbName) const
{
    if (!m_link) return {};
    flushWriteHandle();
    DbScope scope(m_link, dbName, /*rw=*/false);
    if (!scope.ok()) return {};

    // Pisock convention: try a generous buffer; the actual returned
    // size determines what we keep. AppInfo blocks are typically
    // < 1 KiB; 4 KiB is plenty for any pathological case.
    QByteArray buf(4096, '\0');
    std::size_t actualSize = static_cast<std::size_t>(buf.size());
    const bool ok = m_link->readAppBlock(scope.handle(),
        reinterpret_cast<unsigned char *>(buf.data()), &actualSize);
    if (!ok) return {};
    buf.resize(static_cast<int>(actualSize));
    return buf;
}

bool PilotLinkPalmDatabaseAccess::writeAppBlock(const QString &dbName,
                                                const QByteArray &block)
{
    // Substrate A3: exact inverse of readAppBlock — open read-write, write the
    // AppInfo bytes via the low-level DLP link, close (DbScope RAII).
    if (!m_link) return false;
    flushWriteHandle();
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return false;
    return m_link->writeAppBlock(scope.handle(),
        reinterpret_cast<const unsigned char *>(block.constData()),
        static_cast<std::size_t>(block.size()));
}

} // namespace WildPalms::PalmDevice
