#include "categorymappingstore.h"

#include <algorithm>

namespace WildPalms::PalmCalendar {

bool CategoryMappingStore::setSlotName(const QString &dbName, int slot,
                                       const QString &name)
{
    if (slot == UnfiledSlot) {
        return name == QStringLiteral("Unfiled");  // No-op accept for parity
    }
    if (slot < 1 || slot >= MaxSlots) {
        return false;
    }
    auto &dbSlots = m_slots[dbName];
    if (name.isEmpty()) {
        dbSlots.remove(slot);
        if (dbSlots.isEmpty()) {
            m_slots.remove(dbName);
        }
    } else {
        dbSlots.insert(slot, name);
    }
    return true;
}

QString CategoryMappingStore::slotName(const QString &dbName, int slot) const
{
    if (slot == UnfiledSlot) {
        return QStringLiteral("Unfiled");
    }
    const auto it = m_slots.constFind(dbName);
    if (it == m_slots.constEnd()) {
        return {};
    }
    return it.value().value(slot);
}

QList<int> CategoryMappingStore::populatedSlots(const QString &dbName) const
{
    const auto it = m_slots.constFind(dbName);
    if (it == m_slots.constEnd()) {
        return {};
    }
    QList<int> result = it.value().keys();
    std::sort(result.begin(), result.end());
    return result;
}

void CategoryMappingStore::clear(const QString &dbName)
{
    m_slots.remove(dbName);
}

} // namespace WildPalms::PalmCalendar
