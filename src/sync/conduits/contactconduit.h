#ifndef CONTACTCONDUIT_H
#define CONTACTCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"
#include <QByteArray>

namespace Sync {

/**
 * @brief Conduit for Palm Contacts ↔ vCard files
 *
 * Syncs:
 *   - Palm AddressDB (binary format)
 *   - Local .vcf files (vCard 4.0 format)
 *
 * Uses ContactMapper for format conversion.
 * Supports bidirectional category sync.
 */
class ContactConduit : public Conduit
{
    Q_OBJECT

public:
    explicit ContactConduit(QObject *parent = nullptr);
    ~ContactConduit() override;

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

protected:
    bool writeModifiedCategories(SyncContext *context) override;

private:
    CategoryInfo *m_categories = nullptr;
    QByteArray m_originalAppInfo;  // Store original AppInfo block for write-back

    void loadCategories(SyncContext *context);
    QString categoryName(int categoryIndex) const;
};

} // namespace Sync

#endif // CONTACTCONDUIT_H
