#ifndef WILDPALMS_PALM_CONTACTS_PALMCONTACTSBACKEND_H
#define WILDPALMS_PALM_CONTACTS_PALMCONTACTSBACKEND_H

#include "syncbackend.h"

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PalmContacts {

/**
 * @brief SyncBackend for the Palm AddressDB (contacts).
 *
 * Native shape: (contacts, palm-address). Records are stored as raw Palm
 * DLP bytes inside BackendRecord::data so BlobDomainAdapter can compare
 * them by content hash. The deviceId constructor parameter sets resourceId()
 * and is used by PalmRuntime to identify the Palm resource.
 *
 * Calendar-specific pure virtuals are stubbed: they do nothing and emit
 * no signals (the engine routes this backend through dispatchBlobSync,
 * not the calendar pipeline).
 *
 * G.7 Task 50.
 */
class PalmContactsBackend : public Kalburator::Sync::SyncBackend
{
    Q_OBJECT
public:
    static constexpr const char *DatabaseName  = "AddressDB";
    static constexpr const char *CollectionId  = "palm:address";

    explicit PalmContactsBackend(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        const QString &deviceId,
        QObject *parent = nullptr);
    ~PalmContactsBackend() override;

    // --- Identity ---
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;
    QString resourceId() const override;

    // --- Blob operations (IBlobBackend) ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(
        const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(
        const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;

    // --- Calendar discovery (stubs) ---
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(
        const QString &collectionId,
        const QList<KCalendarCore::MemoryCalendar *> &calendars) override;
    void startSync(
        const QString &collectionId,
        KCalendarCore::MemoryCalendar *calendar,
        const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
        const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
        const QMap<QString, QString> &stagedDeletions,
        const Kalburator::Sync::TranscodingPlan &plan) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

private:
    static QString encodeRecordId(std::uint32_t palmId);
    static bool decodeRecordId(const QString &encoded, std::uint32_t *palmIdOut);

    WildPalms::PalmSync::IPalmDatabaseAccess *m_device   = nullptr;
    QString                                    m_deviceId;
};

} // namespace WildPalms::PalmContacts

#endif // WILDPALMS_PALM_CONTACTS_PALMCONTACTSBACKEND_H
