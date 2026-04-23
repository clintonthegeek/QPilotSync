#ifndef WILDPALMS_ADAPTERS_PALMMEMOSADAPTER_H
#define WILDPALMS_ADAPTERS_PALMMEMOSADAPTER_H

// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase. Prefer binding via the
// codec headers + PalmBackend directly if you need long-term ABI
// stability.

#include <cstdint>
#include <optional>

#include <QList>
#include <QString>

#include "memocodec.h"

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Palm::Adapters {

struct MemoRow {
    std::uint32_t       id = 0;            ///< PalmRecord::recordId
    int                 categorySlot = 0;  ///< 0..15
    QString             categoryName;      ///< resolved via CategoryMappingStore
    WildPalms::PalmCodecs::Memo content;
};

QList<MemoRow>
readAllMemos(WildPalms::PalmSync::PalmBackend *pb,
             const WildPalms::PalmCalendar::CategoryMappingStore *cats);

std::optional<MemoRow>
readMemo(WildPalms::PalmSync::PalmBackend *pb,
         const WildPalms::PalmCalendar::CategoryMappingStore *cats,
         std::uint32_t id);

std::uint32_t
writeMemo(WildPalms::PalmSync::PalmBackend *pb,
          int categorySlot,
          const WildPalms::PalmCodecs::Memo &m);

void
deleteMemo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id);

} // namespace WildPalms::Palm::Adapters

#endif // WILDPALMS_ADAPTERS_PALMMEMOSADAPTER_H
