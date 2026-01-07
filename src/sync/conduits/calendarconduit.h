#ifndef CALENDARCONDUIT_H
#define CALENDARCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"

namespace Sync {

/**
 * @brief Conduit for Palm Calendar ↔ iCalendar files
 *
 * Syncs:
 *   - Palm DatebookDB (binary format)
 *   - Local .ics files (iCalendar VEVENT format)
 *
 * Uses CalendarMapper for format conversion.
 * For complex iCalendar parsing, will integrate KCalendarCore.
 */
class CalendarConduit : public Conduit
{
    Q_OBJECT

public:
    explicit CalendarConduit(QObject *parent = nullptr);
    ~CalendarConduit() override = default;

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

private:
    CategoryInfo *m_categories = nullptr;

    void loadCategories(SyncContext *context);
    QString categoryName(int categoryIndex) const;
};

} // namespace Sync

#endif // CALENDARCONDUIT_H
