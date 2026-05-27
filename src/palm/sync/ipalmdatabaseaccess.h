#ifndef WILDPALMS_SYNC_IPALMDATABASEACCESS_H
#define WILDPALMS_SYNC_IPALMDATABASEACCESS_H

#include <optional>

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "palmrecord.h"

namespace WildPalms::PalmSync {

/**
 * @brief Synchronous device-facing API used by PalmBackend.
 *
 * Abstracts the Palm DLP operations PalmBackend needs, so the backend
 * is testable without pilot-link and so the real DLP adapter (E.4)
 * and the mock (E.3) share a single contract.
 *
 * Methods are blocking. PalmBackend is expected to run on a worker
 * thread when a real device is in play; the E.3 mock is fast enough
 * to call directly from tests.
 */
class IPalmDatabaseAccess {
public:
    virtual ~IPalmDatabaseAccess() = default;

    /// Databases visible on the device. Each entry is a Palm DB name
    /// ("DatebookDB", "MemoDB", "AddressDB", "ToDoDB", ...).
    virtual QStringList availableDatabases() const = 0;

    /// True if the device currently exposes the named database.
    virtual bool hasDatabase(const QString &dbName) const = 0;

    /// Create an empty database. Returns true on success. No-op if the
    /// database already exists.
    virtual bool createDatabase(const QString &dbName) = 0;

    /// All records from a database. Order is implementation-defined;
    /// PalmBackend does not rely on ordering.
    virtual QList<PalmRecord> readAllRecords(const QString &dbName) const = 0;

    /// A specific record, or nullopt if missing.
    virtual std::optional<PalmRecord> readRecord(
        const QString &dbName, std::uint32_t recordId) const = 0;

    /// Create a record. If `record.recordId == 0`, the implementation
    /// assigns an ID (matches Palm DLP semantics where the device
    /// allocates IDs). Returns the ID actually assigned, or 0 on
    /// failure.
    virtual std::uint32_t createRecord(const QString &dbName,
                                       const PalmRecord &record) = 0;

    /// Update in place. recordId must be non-zero and must exist.
    /// Returns true on success.
    virtual bool updateRecord(const QString &dbName,
                              const PalmRecord &record) = 0;

    /// Delete a record by ID. Returns true on success; false if the
    /// record did not exist.
    virtual bool deleteRecord(const QString &dbName,
                              std::uint32_t recordId) = 0;

    /// Records modified strictly after `since`. Optional capability —
    /// implementations that can't distinguish return the full list
    /// (the engine's baseline store compensates).
    virtual QList<PalmRecord> recordsModifiedSince(
        const QString &dbName, const QDateTime &since) const = 0;

    /// Record IDs deleted strictly after `since`. Optional capability.
    virtual QList<std::uint32_t> recordsDeletedSince(
        const QString &dbName, const QDateTime &since) const = 0;

    /// Read the database's AppInfo block (raw bytes, layout
    /// database-specific). Returns empty QByteArray if the database
    /// has no AppInfo or on read error. PalmBackend forwards calls
    /// straight through; plugins (e.g. CalendarBackendPlugin) parse
    /// the bytes via a database-specific reader (e.g. CategoryAppInfo
    /// for Datebook/Address/Memo/Todo).
    virtual QByteArray readAppBlock(const QString &dbName) const = 0;

    /// Whether the impl tracks deletions natively. PalmBackend surfaces
    /// this via IBlobBackend::supportsDeleteTracking().
    virtual bool supportsDeleteTracking() const = 0;

    /// Whether the underlying transport is currently live.
    /// For pilot-link, reflects whether the USB link has not dropped since
    /// the last successful call. Backends override
    /// IBlobBackend::loadRecordsOrError and consult this after each read to
    /// distinguish "0 records" from "couldn't read."
    /// Layer B silent-success fix (2026-05-16).
    virtual bool isConnected() const = 0;

    /// Flush any cached write handle (close the open DLP database). Default
    /// no-op. Real DLP impls that batch writes override this; the runtime
    /// calls it at end-of-sync so the final mapping's DB is closed before
    /// dlp_EndOfSync.
    virtual void flushPendingWrites() {}
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_IPALMDATABASEACCESS_H
