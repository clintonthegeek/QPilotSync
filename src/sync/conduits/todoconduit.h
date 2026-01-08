#ifndef TODOCONDUIT_H
#define TODOCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"
#include <QByteArray>

namespace Sync {

/**
 * @brief Conduit for Palm ToDos ↔ iCalendar VTODO files
 *
 * Syncs:
 *   - Palm ToDoDB (binary format)
 *   - Local .ics files (iCalendar VTODO format)
 *
 * Uses TodoMapper for format conversion.
 * Supports bidirectional category sync.
 */
class TodoConduit : public Conduit
{
    Q_OBJECT

public:
    explicit TodoConduit(QObject *parent = nullptr);
    ~TodoConduit() override;

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

protected:
    bool writeModifiedCategories(SyncContext *context) override;

private:
    CategoryInfo *m_categories = nullptr;
    QByteArray m_originalAppInfo;  // Store original AppInfo block for write-back

    void loadCategories(SyncContext *context);
    QString categoryName(int categoryIndex) const;
};

} // namespace Sync

#endif // TODOCONDUIT_H
