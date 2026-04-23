#ifndef WILDPALMS_ADAPTERS_PALMTODOSADAPTER_H
#define WILDPALMS_ADAPTERS_PALMTODOSADAPTER_H

// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase.

#include <cstdint>
#include <optional>

#include <QList>
#include <QString>

#include "todocodec.h"

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Palm::Adapters {

struct TodoRow {
    std::uint32_t             id = 0;
    int                       categorySlot = 0;
    QString                   categoryName;
    WildPalms::PalmCodecs::Todo content;
};

QList<TodoRow>
readAllTodos(WildPalms::PalmSync::PalmBackend *pb,
             const WildPalms::PalmCalendar::CategoryMappingStore *cats);

std::optional<TodoRow>
readTodo(WildPalms::PalmSync::PalmBackend *pb,
         const WildPalms::PalmCalendar::CategoryMappingStore *cats,
         std::uint32_t id);

std::uint32_t
writeTodo(WildPalms::PalmSync::PalmBackend *pb,
          int categorySlot,
          const WildPalms::PalmCodecs::Todo &t);

void
deleteTodo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id);

} // namespace WildPalms::Palm::Adapters

#endif // WILDPALMS_ADAPTERS_PALMTODOSADAPTER_H
