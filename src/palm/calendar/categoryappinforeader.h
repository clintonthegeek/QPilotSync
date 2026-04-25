#ifndef WILDPALMS_PALMCALENDAR_CATEGORYAPPINFOREADER_H
#define WILDPALMS_PALMCALENDAR_CATEGORYAPPINFOREADER_H

#include <array>
#include <optional>

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCalendar {

class CategoryMappingStore;

/**
 * @brief 16 Palm category names parsed from a CategoryAppInfo block.
 *
 * Slot 0 is forced to "Unfiled" if blank; slots 1..15 are returned
 * verbatim (may be empty when the user hasn't named the slot).
 */
struct CategoryNames {
    std::array<QString, 16> names;
};

/**
 * @brief Parse any Palm CategoryAppInfo block into 16 category names.
 *
 * Wraps pisock's `unpack_CategoryAppInfo` (pi-appinfo.h). Generic
 * across DatebookDB / ToDoDB / AddressDB / MemoDB — all four use the
 * same `CategoryAppInfo_t` schema.
 *
 * Returns std::nullopt if the bytes don't unpack. Pure function.
 */
std::optional<CategoryNames>
parseCategoryAppInfo(const QByteArray &appInfoBytes);

/**
 * @brief Populate `store` with the named slots from `appInfoBytes`.
 *
 * Calls `parseCategoryAppInfo`, then for every non-empty name in
 * slots 1..15 invokes `store.setSlotName(dbName, slot, name)`. Slot 0
 * is intentionally skipped.
 *
 * Returns false if parsing failed (store left untouched), true
 * otherwise.
 */
bool populateFromAppInfo(CategoryMappingStore &store,
                         const QString &dbName,
                         const QByteArray &appInfoBytes);

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_PALMCALENDAR_CATEGORYAPPINFOREADER_H
