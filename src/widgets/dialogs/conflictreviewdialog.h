#ifndef CONFLICTREVIEWDIALOG_H
#define CONFLICTREVIEWDIALOG_H

/**
 * @file conflictreviewdialog.h
 * @brief Post-sync dialog for reviewing and resolving deferred conflicts
 *
 * Wraps ConflictReviewWidget in a modal dialog for use from the
 * Device -> Show Conflicts menu action.
 */

#include <QDialog>
#include "../../core/isyncconduit.h"

class ConflictReviewWidget;

namespace QSyncCore {
class ConflictStore;
}

/**
 * @brief Dialog for post-sync conflict review
 *
 * Shows a ConflictReviewWidget with grouped conflicts, side-by-side
 * preview, and per-conflict resolution controls.
 */
class ConflictReviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConflictReviewDialog(QSyncCore::ConflictStore *store,
                                   ConduitLookupFn conduitLookup = nullptr,
                                   QWidget *parent = nullptr);

    /**
     * @brief Get count of pending conflicts
     */
    int pendingCount() const;

    /**
     * @brief Get count of resolved (unapplied) conflicts
     */
    int resolvedCount() const;

Q_SIGNALS:
    /**
     * @brief Emitted when user wants to apply resolutions via sync
     */
    void applyResolutionsRequested();

private:
    ConflictReviewWidget *m_reviewWidget;
};

#endif // CONFLICTREVIEWDIALOG_H
