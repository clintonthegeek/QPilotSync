#ifndef CALENDARCONDUIT_H
#define CALENDARCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"
#include <QByteArray>

namespace Sync {

/**
 * @brief Conduit for Palm Calendar ↔ iCalendar files
 *
 * Syncs:
 *   - Palm DatebookDB (binary format)
 *   - Local .ics files (iCalendar VEVENT format)
 *
 * Uses CalendarMapper for format conversion.
 * Supports bidirectional category sync.
 */
class CalendarConduit : public Conduit
{
    Q_OBJECT

public:
    explicit CalendarConduit(QObject *parent = nullptr);
    ~CalendarConduit() override;

    // ========== Conduit Identity ==========

    QString conduitId() const override { return "calendar"; }
    QString displayName() const override { return "Calendar"; }
    QString palmDatabaseName() const override { return "DatebookDB"; }
    QString fileExtension() const override { return ".ics"; }

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

#endif // CALENDARCONDUIT_H
