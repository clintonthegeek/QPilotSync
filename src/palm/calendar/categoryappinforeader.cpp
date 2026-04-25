#include "categoryappinforeader.h"

#include <cstring>

#include <pi-appinfo.h>

#include "categorymappingstore.h"

namespace WildPalms::PalmCalendar {

std::optional<CategoryNames>
parseCategoryAppInfo(const QByteArray &appInfoBytes)
{
    // CategoryAppInfo on-wire layout: 2 (renamed bitmask) + 16*16
    // (names) + 16 (IDs) + 1 (lastUniqueID) + 1 (padding) = 276 bytes
    // (matches WildPalms::CategoryInfo::parse). Note the in-memory
    // sizeof(CategoryAppInfo_t) is much larger (~340) because pisock
    // expands the 2-byte renamed bitmask into unsigned int[16].
    static constexpr int kMinSize = 276;
    if (appInfoBytes.size() < kMinSize) {
        return std::nullopt;
    }

    // pisock on this system exposes only CategoryAppInfo_t /
    // unpack_CategoryAppInfo (no AppInfo_t / unpack_AppInfo wrapper).
    // Datebook AppInfo blocks start with the CategoryAppInfo header,
    // so unpacking just that prefix gives us the 16 category names.
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const int unpacked = unpack_CategoryAppInfo(
        &info,
        reinterpret_cast<const unsigned char *>(appInfoBytes.constData()),
        static_cast<size_t>(appInfoBytes.size()));
    if (unpacked < 0) {
        return std::nullopt;
    }

    CategoryNames out;
    for (int i = 0; i < 16; ++i) {
        // pisock zeros the buffer; trim at NUL.
        const char *raw = info.name[i];
        out.names[i] = QString::fromUtf8(raw,
            static_cast<int>(::strnlen(raw, sizeof(info.name[i]))));
    }
    if (out.names[0].isEmpty()) {
        out.names[0] = QStringLiteral("Unfiled");
    }
    return out;
}

bool populateFromAppInfo(CategoryMappingStore &store,
                         const QString &dbName,
                         const QByteArray &appInfoBytes)
{
    auto parsed = parseCategoryAppInfo(appInfoBytes);
    if (!parsed) return false;

    for (int slot = 1; slot < 16; ++slot) {
        const QString &name = parsed->names[slot];
        if (!name.isEmpty()) {
            store.setSlotName(dbName, slot, name);
        }
    }
    return true;
}

} // namespace WildPalms::PalmCalendar
