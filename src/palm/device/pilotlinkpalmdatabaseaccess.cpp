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

QStringList PilotLinkPalmDatabaseAccess::availableDatabases() const
{
    if (!m_link) return {};
    return m_link->listDatabases();
}

bool PilotLinkPalmDatabaseAccess::hasDatabase(const QString &dbName) const
{
    if (!m_link) return false;
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
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return 0;

    PilotRecord bridged = toPilotRecord(record);
    if (!m_link->writeRecord(scope.handle(), &bridged)) return 0;
    return static_cast<std::uint32_t>(bridged.recordId());
}

bool PilotLinkPalmDatabaseAccess::updateRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return false;
    if (record.recordId == 0) return false;
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return false;

    PilotRecord bridged = toPilotRecord(record);
    return m_link->writeRecord(scope.handle(), &bridged);
}

bool PilotLinkPalmDatabaseAccess::deleteRecord(const QString &dbName,
                                               std::uint32_t recordId)
{
    if (!m_link) return false;
    DbScope scope(m_link, dbName, /*rw=*/true);
    if (!scope.ok()) return false;
    return m_link->deleteRecord(scope.handle(),
                                static_cast<int>(recordId));
}

QList<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::recordsModifiedSince(const QString &dbName,
                                                  const QDateTime &) const
{
    // `since` is ignored; DLP lacks per-record timestamps. The engine
    // falls back to baseline-based diff.
    if (!m_link) return {};
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

} // namespace WildPalms::PalmDevice
