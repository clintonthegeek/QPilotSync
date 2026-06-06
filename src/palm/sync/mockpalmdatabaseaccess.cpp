#include "mockpalmdatabaseaccess.h"

namespace WildPalms::PalmSync {

QStringList MockPalmDatabaseAccess::availableDatabases() const
{
    return QStringList(m_dbs.keyBegin(), m_dbs.keyEnd());
}

bool MockPalmDatabaseAccess::hasDatabase(const QString &dbName) const
{
    return m_dbs.contains(dbName);
}

bool MockPalmDatabaseAccess::createDatabase(const QString &dbName)
{
    if (!m_dbs.contains(dbName)) {
        m_dbs.insert(dbName, Database{});
    }
    return true;
}

bool MockPalmDatabaseAccess::deleteDatabase(const QString &dbName)
{
    // Idempotent: absent → success, present → drop everything (records,
    // deletion log, appInfo). Mirrors DLP dlp_DeleteDB semantics where
    // the database itself goes away.
    m_dbs.remove(dbName);
    return true;
}

QList<PalmRecord> MockPalmDatabaseAccess::readAllRecords(
    const QString &dbName) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    return it->records.values();
}

std::optional<PalmRecord> MockPalmDatabaseAccess::readRecord(
    const QString &dbName, std::uint32_t recordId) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return std::nullopt;
    const auto rIt = it->records.constFind(recordId);
    if (rIt == it->records.constEnd()) return std::nullopt;
    return *rIt;
}

std::uint32_t MockPalmDatabaseAccess::createRecord(
    const QString &dbName, const PalmRecord &record)
{
    if (!m_dbs.contains(dbName)) return 0;
    Database &db = m_dbs[dbName];
    PalmRecord stored = record;
    if (stored.recordId == 0) {
        stored.recordId = db.nextId++;
    } else {
        db.nextId = std::max<std::uint32_t>(db.nextId, stored.recordId + 1);
    }
    if (!stored.lastModified.isValid()) {
        stored.lastModified = QDateTime::currentDateTimeUtc();
    }
    db.records.insert(stored.recordId, stored);
    return stored.recordId;
}

bool MockPalmDatabaseAccess::updateRecord(const QString &dbName,
                                          const PalmRecord &record)
{
    if (!m_dbs.contains(dbName)) return false;
    Database &db = m_dbs[dbName];
    if (record.recordId == 0) return false;
    if (!db.records.contains(record.recordId)) return false;
    PalmRecord stored = record;
    if (!stored.lastModified.isValid()) {
        stored.lastModified = QDateTime::currentDateTimeUtc();
    }
    db.records[record.recordId] = stored;
    return true;
}

bool MockPalmDatabaseAccess::deleteRecord(const QString &dbName,
                                          std::uint32_t recordId)
{
    if (!m_dbs.contains(dbName)) return false;
    Database &db = m_dbs[dbName];
    if (db.records.remove(recordId) == 0) return false;
    db.deletionLog.insert(QDateTime::currentDateTimeUtc(), recordId);
    return true;
}

QList<PalmRecord> MockPalmDatabaseAccess::recordsModifiedSince(
    const QString &dbName, const QDateTime &since) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    QList<PalmRecord> out;
    for (const auto &rec : it->records) {
        if (rec.lastModified > since) out.append(rec);
    }
    return out;
}

QList<std::uint32_t> MockPalmDatabaseAccess::recordsDeletedSince(
    const QString &dbName, const QDateTime &since) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    QList<std::uint32_t> out;
    for (auto dIt = it->deletionLog.upperBound(since);
         dIt != it->deletionLog.constEnd(); ++dIt) {
        out.append(dIt.value());
    }
    return out;
}

QByteArray MockPalmDatabaseAccess::readAppBlock(const QString &dbName) const
{
    auto it = m_dbs.constFind(dbName);
    return (it == m_dbs.cend()) ? QByteArray() : it->appInfo;
}

void MockPalmDatabaseAccess::setAppBlock(const QString &dbName,
                                         const QByteArray &bytes)
{
    // Auto-create the database if absent so test setup order doesn't
    // matter.
    if (!m_dbs.contains(dbName)) {
        m_dbs.insert(dbName, Database{});
    }
    m_dbs[dbName].appInfo = bytes;
}

} // namespace WildPalms::PalmSync
