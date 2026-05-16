#ifndef WILDPALMS_SYNC_MOCKPALMDATABASEACCESS_H
#define WILDPALMS_SYNC_MOCKPALMDATABASEACCESS_H

#include <QHash>
#include <QMultiMap>

#include "ipalmdatabaseaccess.h"

namespace WildPalms::PalmSync {

/**
 * @brief In-memory IPalmDatabaseAccess for tests.
 *
 * Stores records in per-database hash maps keyed by recordId. Assigns
 * new record IDs as monotonically increasing 32-bit counters per
 * database, mirroring Palm DLP's assignment semantics closely enough
 * for scaffold-level tests.
 *
 * Tracks deletions by keeping a per-database list of
 * (recordId, deletedAt) pairs so recordsDeletedSince() can answer
 * queries without a full scan. Not thread-safe; PalmBackend serialises
 * access.
 */
class MockPalmDatabaseAccess : public IPalmDatabaseAccess {
public:
    MockPalmDatabaseAccess() = default;

    QStringList availableDatabases() const override;
    bool hasDatabase(const QString &dbName) const override;
    bool createDatabase(const QString &dbName) override;

    QList<PalmRecord> readAllRecords(const QString &dbName) const override;
    std::optional<PalmRecord> readRecord(const QString &dbName,
                                         std::uint32_t recordId) const override;

    std::uint32_t createRecord(const QString &dbName,
                               const PalmRecord &record) override;
    bool updateRecord(const QString &dbName,
                      const PalmRecord &record) override;
    bool deleteRecord(const QString &dbName,
                      std::uint32_t recordId) override;

    QList<PalmRecord> recordsModifiedSince(
        const QString &dbName, const QDateTime &since) const override;
    QList<std::uint32_t> recordsDeletedSince(
        const QString &dbName, const QDateTime &since) const override;
    bool supportsDeleteTracking() const override { return true; }
    bool isConnected() const override { return m_connected; }

    QByteArray readAppBlock(const QString &dbName) const override;

    /// Test setter: stores `bytes` under `dbName`. Subsequent
    /// readAppBlock(dbName) returns `bytes` verbatim.
    void setAppBlock(const QString &dbName, const QByteArray &bytes);

    /// Test setter: controls what isConnected() returns. Defaults to true.
    void setConnected(bool c) { m_connected = c; }

private:
    struct Database {
        QHash<std::uint32_t, PalmRecord>   records;
        // Multimap so concurrent same-millisecond deletions don't
        // collapse onto one key (QDateTime resolution is ms).
        QMultiMap<QDateTime, std::uint32_t> deletionLog;
        std::uint32_t                       nextId = 1;
        QByteArray                          appInfo;
    };

    QHash<QString, Database> m_dbs;
    bool                     m_connected = true;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_MOCKPALMDATABASEACCESS_H
