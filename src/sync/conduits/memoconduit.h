#ifndef MEMOCONDUIT_H
#define MEMOCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"

namespace Sync {

/**
 * @brief Conduit for Palm Memos ↔ Markdown files
 *
 * Syncs:
 *   - Palm MemoDB (binary format)
 *   - Local .md files with YAML frontmatter
 *
 * Uses MemoMapper for format conversion.
 */
class MemoConduit : public Conduit
{
    Q_OBJECT

public:
    explicit MemoConduit(QObject *parent = nullptr);
    ~MemoConduit() override = default;

    // ========== Conduit Identity ==========

    QString conduitId() const override { return "memos"; }
    QString displayName() const override { return "Memos"; }
    QString palmDatabaseName() const override { return "MemoDB"; }
    QString fileExtension() const override { return ".md"; }

    // ========== Record Conversion ==========

    BackendRecord* palmToBackend(PilotRecord *palmRecord,
                                  SyncContext *context) override;

    PilotRecord* backendToPalm(BackendRecord *backendRecord,
                                SyncContext *context) override;

    bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override;

    QString palmRecordDescription(PilotRecord *record) const override;

private:
    // Category info cache for the current sync session
    CategoryInfo *m_categories = nullptr;

    void loadCategories(SyncContext *context);
    QString categoryName(int categoryIndex) const;
};

} // namespace Sync

#endif // MEMOCONDUIT_H
