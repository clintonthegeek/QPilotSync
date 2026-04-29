#ifndef WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H
#define WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H

#include "../sync/ipalmdatabaseaccess.h"

class KPilotLink;

namespace WildPalms::PalmDevice {

/**
 * @brief IPalmDatabaseAccess wrapping a KPilotLink.
 *
 * Per-operation open/close — each method resolves the database by
 * name, opens a DLP handle, runs one-or-two DLP calls, and closes.
 * Scaffold pattern; a production implementation should cache open
 * handles to avoid per-op overhead on real hardware.
 *
 * Does not own the KPilotLink. Caller is responsible for lifetime
 * (and for ensuring the link is connected before any method is
 * called).
 *
 * Semantic caveats versus the mock in WildPalmsPalmSync:
 *
 * - `supportsDeleteTracking()` returns false. The Palm DLP protocol
 *   has a "modified since last sync" flag (dlp_ReadNextModifiedRec)
 *   but not a per-record deletion log keyed by timestamp; the
 *   engine's baseline store (Phase B3) handles deletion detection.
 * - `recordsModifiedSince()` ignores the timestamp parameter and
 *   delegates to KPilotLink::readModifiedRecords(), which returns
 *   records with the Dirty attribute set. The engine treats the
 *   result as "candidate modifications"; the baseline compares
 *   content hashes for authoritative change detection.
 * - `recordsDeletedSince()` returns an empty list. The engine
 *   derives deletions from the baseline diff.
 * - `createDatabase()` returns false when the database does not
 *   already exist. Palm software rarely creates databases (apps do);
 *   adding dlp_CreateDB wiring is a follow-up.
 */
class PilotLinkPalmDatabaseAccess
    : public WildPalms::PalmSync::IPalmDatabaseAccess
{
public:
    explicit PilotLinkPalmDatabaseAccess(KPilotLink *link);
    ~PilotLinkPalmDatabaseAccess() override = default;

    QStringList availableDatabases() const override;
    bool hasDatabase(const QString &dbName) const override;
    bool createDatabase(const QString &dbName) override;

    QList<WildPalms::PalmSync::PalmRecord>
        readAllRecords(const QString &dbName) const override;
    std::optional<WildPalms::PalmSync::PalmRecord>
        readRecord(const QString &dbName,
                   std::uint32_t recordId) const override;

    std::uint32_t createRecord(
        const QString &dbName,
        const WildPalms::PalmSync::PalmRecord &record) override;
    bool updateRecord(
        const QString &dbName,
        const WildPalms::PalmSync::PalmRecord &record) override;
    bool deleteRecord(const QString &dbName,
                      std::uint32_t recordId) override;

    QList<WildPalms::PalmSync::PalmRecord>
        recordsModifiedSince(const QString &dbName,
                             const QDateTime &since) const override;
    QList<std::uint32_t>
        recordsDeletedSince(const QString &dbName,
                            const QDateTime &since) const override;
    bool supportsDeleteTracking() const override { return false; }

    QByteArray readAppBlock(const QString &dbName) const override;

private:
    // Scope guard opens a database on construction, closes on
    // destruction. Handle is -1 if the open failed.
    class DbScope {
    public:
        DbScope(KPilotLink *link, const QString &dbName, bool rw);
        ~DbScope();
        int handle() const { return m_handle; }
        bool ok() const { return m_handle >= 0; }
    private:
        KPilotLink *m_link;
        int         m_handle;
    };

    KPilotLink *m_link = nullptr;
};

} // namespace WildPalms::PalmDevice

#endif // WILDPALMS_DEVICE_PILOTLINKPALMDATABASEACCESS_H
