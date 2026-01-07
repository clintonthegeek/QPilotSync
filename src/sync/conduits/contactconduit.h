#ifndef CONTACTCONDUIT_H
#define CONTACTCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"

namespace Sync {

/**
 * @brief Conduit for Palm Contacts ↔ vCard files
 *
 * Syncs:
 *   - Palm AddressDB (binary format)
 *   - Local .vcf files (vCard 4.0 format)
 *
 * Uses ContactMapper for format conversion.
 */
class ContactConduit : public Conduit
{
    Q_OBJECT

public:
    explicit ContactConduit(QObject *parent = nullptr);
    ~ContactConduit() override = default;

    // ========== Conduit Identity ==========

    QString conduitId() const override { return "contacts"; }
    QString displayName() const override { return "Contacts"; }
    QString palmDatabaseName() const override { return "AddressDB"; }
    QString fileExtension() const override { return ".vcf"; }

    // ========== Record Conversion ==========

    BackendRecord* palmToBackend(PilotRecord *palmRecord,
                                  SyncContext *context) override;

    PilotRecord* backendToPalm(BackendRecord *backendRecord,
                                SyncContext *context) override;

    bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override;

    QString palmRecordDescription(PilotRecord *record) const override;

private:
    CategoryInfo *m_categories = nullptr;

    void loadCategories(SyncContext *context);
    QString categoryName(int categoryIndex) const;
};

} // namespace Sync

#endif // CONTACTCONDUIT_H
