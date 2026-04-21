#ifndef WILDPALMS_SYNC_PALMBACKEND_H
#define WILDPALMS_SYNC_PALMBACKEND_H

#include "iblobbackend.h"

namespace WildPalms::PalmSync {

class IPalmDatabaseAccess;

/**
 * @brief Kalburator::Sync::IBlobBackend implementation backed by a
 *        Palm device abstraction.
 *
 * One instance is intended to be owned by the application runtime and
 * shared across plugins. Each Palm database is surfaced as a
 * CollectionInfo whose id is "palm:<dbname>" (lowercase, no "DB"
 * suffix; e.g. "palm:memo" for MemoDB). Record IDs are encoded as
 * "palm:<dbname>:<numericId>".
 *
 * Does not own the IPalmDatabaseAccess; caller is responsible for
 * keeping it alive for the backend's lifetime.
 */
class PalmBackend : public Kalburator::Sync::IBlobBackend {
    Q_OBJECT
public:
    explicit PalmBackend(IPalmDatabaseAccess *device,
                         QObject *parent = nullptr);
    ~PalmBackend() override;

    // --- Identity ---
    QString backendId() const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- Collections ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(
        const QString &collectionId) override;
    QString createCollection(
        const Kalburator::Sync::CollectionInfo &info) override;

    // --- Records ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(
        const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(
        const QString &recordId) override;
    QString createRecord(
        const QString &collectionId,
        const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(
        const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    // --- Change detection ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;
    bool supportsDeleteTracking() const override;

    // --- ID encoding (exposed for tests and for callers that need to
    //     round-trip between PalmRecord and BackendRecord IDs).   ---
    static QString  encodeRecordId(const QString &dbName,
                                   std::uint32_t recordId);
    static bool     decodeRecordId(const QString &encoded,
                                   QString *dbNameOut,
                                   std::uint32_t *recordIdOut);
    static QString  encodeCollectionId(const QString &dbName);
    static bool     decodeCollectionId(const QString &collectionId,
                                       QString *dbNameOut);

private:
    IPalmDatabaseAccess *m_device = nullptr;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_PALMBACKEND_H
