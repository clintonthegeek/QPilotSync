#ifndef TODOCONDUIT_H
#define TODOCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"

namespace Sync {

/**
 * @brief Conduit for Palm ToDos ↔ iCalendar VTODO files
 *
 * Syncs:
 *   - Palm ToDoDB (binary format)
 *   - Local .ics files (iCalendar VTODO format)
 *
 * Uses TodoMapper for format conversion.
 */
class TodoConduit : public Conduit
{
    Q_OBJECT

public:
    explicit TodoConduit(QObject *parent = nullptr);
    ~TodoConduit() override = default;

    // ========== Conduit Identity ==========

    QString conduitId() const override { return "todos"; }
    QString displayName() const override { return "Tasks"; }
    QString palmDatabaseName() const override { return "ToDoDB"; }
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

#endif // TODOCONDUIT_H
