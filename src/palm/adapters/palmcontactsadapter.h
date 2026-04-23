#ifndef WILDPALMS_ADAPTERS_PALMCONTACTSADAPTER_H
#define WILDPALMS_ADAPTERS_PALMCONTACTSADAPTER_H

// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase.

#include <cstdint>
#include <optional>

#include <QList>
#include <QString>

#include "contactcodec.h"

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Palm::Adapters {

struct ContactRow {
    std::uint32_t                 id = 0;
    int                           categorySlot = 0;
    QString                       categoryName;
    WildPalms::PalmCodecs::Contact content;
};

QList<ContactRow>
readAllContacts(WildPalms::PalmSync::PalmBackend *pb,
                const WildPalms::PalmCalendar::CategoryMappingStore *cats);

std::optional<ContactRow>
readContact(WildPalms::PalmSync::PalmBackend *pb,
            const WildPalms::PalmCalendar::CategoryMappingStore *cats,
            std::uint32_t id);

std::uint32_t
writeContact(WildPalms::PalmSync::PalmBackend *pb,
             int categorySlot,
             const WildPalms::PalmCodecs::Contact &c);

void
deleteContact(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id);

} // namespace WildPalms::Palm::Adapters

#endif // WILDPALMS_ADAPTERS_PALMCONTACTSADAPTER_H
