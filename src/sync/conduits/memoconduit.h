#ifndef MEMOCONDUIT_H
#define MEMOCONDUIT_H

#include "../conduit.h"
#include "../../palm/categoryinfo.h"
#include <QByteArray>

namespace Sync {

/**
 * @brief Conduit for Palm Memos ↔ Markdown files
 *
 * Syncs:
 *   - Palm MemoDB (binary format)
 *   - Local .md files with YAML frontmatter
 *
 * Uses MemoMapper for format conversion.
 * Supports bidirectional category sync.
 */
class MemoConduit : public Conduit
{
    Q_OBJECT

public:
    explicit MemoConduit(QObject *parent = nullptr);
    ~MemoConduit() override;

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

protected:
    bool writeModifiedCategories(SyncContext *context) override;

private:
    CategoryInfo *m_categories = nullptr;
    QByteArray m_originalAppInfo;  // Store original AppInfo block for write-back

    void loadCategories(SyncContext *context);
    QString categoryName(int categoryIndex) const;
};

} // namespace Sync

#endif // MEMOCONDUIT_H
