#ifndef WILDPALMS_RUNTIME_CATEGORYRECONCILER_H
#define WILDPALMS_RUNTIME_CATEGORYRECONCILER_H

#include <QByteArray>
#include <QHash>
#include <QStringList>

namespace WildPalms::Runtime {

struct CategoryReconcileResult {
    QHash<QString, int> bound;       ///< desired name -> device slot
    QStringList noFreeSlot;          ///< names that could not be placed
    QByteArray updatedAppInfoBlock;  ///< full block to write back; empty = no write
};

/// Pure function over AppInfo bytes (substrate A3). Matches desired names
/// case-insensitively against the device category table; claims free slots
/// for missing names. Device categories NOT in the desired set are left
/// alone (the wizard's clobber path replaces the table wholesale — that is
/// sub-project B, not this function). The category region occupies the leading
/// wire bytes; any app-specific tail is preserved verbatim.
CategoryReconcileResult reconcileCategories(const QByteArray &appInfoBlock,
                                            const QStringList &desiredNames);

} // namespace WildPalms::Runtime
#endif
