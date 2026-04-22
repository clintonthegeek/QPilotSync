#ifndef WILDPALMS_CALENDAR_CATEGORYMAPPINGSTORE_H
#define WILDPALMS_CALENDAR_CATEGORYMAPPINGSTORE_H

#include <QHash>
#include <QList>
#include <QString>

namespace WildPalms::PalmCalendar {

/**
 * @brief In-memory slot → display-name store for Palm categories.
 *
 * Keyed by Palm database name (e.g. "DatebookDB") because each Palm
 * database carries its own `CategoryAppInfo_t` with independent slot
 * assignments. Slot 0 is reserved for "Unfiled" across all databases.
 *
 * Callers populate the store from the AppInfo block at session start
 * (real parsing lands later; tests use setSlotName directly). The
 * `PalmCalendarBackend` borrows a pointer for display-name lookups
 * during `loadCalendars`. Non-owning — must outlive the backend.
 */
class CategoryMappingStore {
public:
    static constexpr const char *UnfiledName = "Unfiled";
    static constexpr int UnfiledSlot = 0;
    static constexpr int MaxSlots = 16;  // Palm supports 0..15

    CategoryMappingStore() = default;

    /// Set display name for (dbName, slot). Empty name removes the
    /// entry. Slot 0 only accepts "Unfiled" — any other name is ignored
    /// (returns false). Valid slot range is 1..15 for user-defined names.
    bool setSlotName(const QString &dbName, int slot, const QString &name);

    /// Display name for (dbName, slot). Slot 0 always returns "Unfiled".
    /// Slots 1..15 return the stored name, or empty if unset.
    QString slotName(const QString &dbName, int slot) const;

    /// All slots in [1..15] with non-empty names for dbName, sorted
    /// ascending. Slot 0 is NOT included (callers add it unconditionally).
    QList<int> populatedSlots(const QString &dbName) const;

    /// Remove all entries for dbName.
    void clear(const QString &dbName);

private:
    // dbName → (slot → name). slot keys only ever in 1..15.
    QHash<QString, QHash<int, QString>> m_slots;
};

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_CALENDAR_CATEGORYMAPPINGSTORE_H
