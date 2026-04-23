#include "palmtodosadapter.h"

#include <QLoggingCategory>

#include "categorymappingstore.h"
#include "palmbackend.h"
#include "palmrecord.h"

Q_LOGGING_CATEGORY(lcTodoAdapter, "wildpalms.palm.adapter.todo")

namespace WildPalms::Palm::Adapters {

namespace {

constexpr const char *kDbName = "ToDoDB";

TodoRow rowFromPalmRecord(const WildPalms::PalmSync::PalmRecord &pr,
                          const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    TodoRow row;
    row.id           = pr.recordId;
    row.categorySlot = static_cast<int>(pr.category);
    row.categoryName = cats
        ? cats->slotName(QLatin1String(kDbName), row.categorySlot)
        : QString();
    auto decoded = WildPalms::PalmCodecs::decodeTodo(pr.data);
    if (!decoded) {
        qCWarning(lcTodoAdapter) << "decodeTodo failed for recordId" << pr.recordId;
        return row;
    }
    row.content = *decoded;
    return row;
}

} // namespace

QList<TodoRow> readAllTodos(WildPalms::PalmSync::PalmBackend *pb,
                             const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    QList<TodoRow> out;
    for (const auto &pr : pb->loadPalmRecords(QLatin1String(kDbName))) {
        if (!pr.isDeleted()) {
            out.append(rowFromPalmRecord(pr, cats));
        }
    }
    return out;
}

std::optional<TodoRow> readTodo(WildPalms::PalmSync::PalmBackend *pb,
                                const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                                std::uint32_t id)
{
    const auto pr = pb->loadPalmRecord(QLatin1String(kDbName), id);
    if (!pr) return std::nullopt;
    if (pr->isDeleted()) return std::nullopt;
    return rowFromPalmRecord(*pr, cats);
}

std::uint32_t writeTodo(WildPalms::PalmSync::PalmBackend *pb,
                        int categorySlot,
                        const WildPalms::PalmCodecs::Todo &t)
{
    // Ensure the collection exists before inserting.
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QLatin1String(kDbName));
    if (pb->collectionInfo(colId).id.isEmpty()) {
        Kalburator::Sync::CollectionInfo info;
        info.id   = colId;
        info.name = QLatin1String(kDbName);
        info.type = QStringLiteral("todos");
        pb->createCollection(info);
    }
    WildPalms::PalmSync::PalmRecord pr;
    pr.category = static_cast<std::uint8_t>(categorySlot & 0x0F);
    pr.data     = WildPalms::PalmCodecs::encodeTodo(t);
    return pb->createPalmRecord(QLatin1String(kDbName), pr);
}

void deleteTodo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QLatin1String(kDbName), id);
    pb->deleteRecord(recId);
}

} // namespace WildPalms::Palm::Adapters
