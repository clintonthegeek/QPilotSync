#include "palmmemosadapter.h"

#include <QLoggingCategory>

#include "backendrecord.h"
#include "categorymappingstore.h"
#include "collectioninfo.h"
#include "palmbackend.h"
#include "palmrecord.h"

Q_LOGGING_CATEGORY(lcMemoAdapter, "wildpalms.palm.adapter.memo")

namespace WildPalms::Palm::Adapters {

namespace {

constexpr const char *kDbName = "MemoDB";

MemoRow rowFromPalmRecord(const WildPalms::PalmSync::PalmRecord &pr,
                          const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    MemoRow row;
    row.id           = pr.recordId;
    row.categorySlot = static_cast<int>(pr.category);
    row.categoryName = cats
        ? cats->slotName(QLatin1String(kDbName), row.categorySlot)
        : QString();
    auto decoded = WildPalms::PalmCodecs::decodeMemo(pr.data);
    if (!decoded) {
        qCWarning(lcMemoAdapter) << "decodeMemo failed for recordId" << pr.recordId;
        return row;
    }
    row.content = *decoded;
    return row;
}

} // namespace

QList<MemoRow> readAllMemos(WildPalms::PalmSync::PalmBackend *pb,
                             const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    QList<MemoRow> out;
    for (const auto &pr : pb->loadPalmRecords(QLatin1String(kDbName))) {
        if (!pr.isDeleted()) {
            out.append(rowFromPalmRecord(pr, cats));
        }
    }
    return out;
}

std::optional<MemoRow> readMemo(WildPalms::PalmSync::PalmBackend *pb,
                                 const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                                 std::uint32_t id)
{
    const auto pr = pb->loadPalmRecord(QLatin1String(kDbName), id);
    if (!pr) return std::nullopt;
    if (pr->isDeleted()) return std::nullopt;
    return rowFromPalmRecord(*pr, cats);
}

std::uint32_t writeMemo(WildPalms::PalmSync::PalmBackend *pb,
                         int categorySlot,
                         const WildPalms::PalmCodecs::Memo &m)
{
    // Ensure the collection exists before inserting.
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QLatin1String(kDbName));
    if (pb->collectionInfo(colId).id.isEmpty()) {
        Kalburator::Sync::CollectionInfo info;
        info.id   = colId;
        info.name = QLatin1String(kDbName);
        info.type = QStringLiteral("memos");
        pb->createCollection(info);
    }
    WildPalms::PalmSync::PalmRecord pr;
    pr.category = static_cast<std::uint8_t>(categorySlot & 0x0F);
    pr.data     = WildPalms::PalmCodecs::encodeMemo(m);
    return pb->createPalmRecord(QLatin1String(kDbName), pr);
}

void deleteMemo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QLatin1String(kDbName), id);
    pb->deleteRecord(recId);
}

} // namespace WildPalms::Palm::Adapters
