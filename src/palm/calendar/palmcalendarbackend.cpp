#include "palmcalendarbackend.h"

#include <QRegularExpression>

#include "syncoperation.h"

#include "categorymappingstore.h"
#include "datadomain.h"
#include "ipalmdatabaseaccess.h"

namespace WildPalms::PalmCalendar {

using Kalburator::Sync::DataDomain;
using Kalburator::Sync::DeleteOperation;
using Kalburator::Sync::FetchOperation;
using Kalburator::Sync::PushOperation;
using Kalburator::Sync::SyncOperation;
using WildPalms::PalmSync::IPalmDatabaseAccess;

PalmCalendarBackend::PalmCalendarBackend(IPalmDatabaseAccess *device,
                                         CategoryMappingStore *categoryStore,
                                         QObject *parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_device(device)
    , m_categoryStore(categoryStore)
{
}

PalmCalendarBackend::~PalmCalendarBackend() = default;

QString PalmCalendarBackend::backendType() const
{
    return QStringLiteral("palm-calendar");
}

DataDomain PalmCalendarBackend::dataDomain() const
{
    return DataDomain::Calendar;
}

void PalmCalendarBackend::loadCalendars(const QString &collectionId)
{
    if (collectionId != QLatin1String(CollectionId)) {
        emit loadCalendarsFinished(
            collectionId, false,
            QStringLiteral("not a Palm calendar collection"));
        return;
    }

    // Slot 0 always.
    emit calendarDiscovered(collectionId, calendarIdForSlot(0));

    // Slots 1..15 from the store.
    if (m_categoryStore) {
        for (int slot : m_categoryStore->populatedSlots(
                 QLatin1String(DatabaseName))) {
            emit calendarDiscovered(collectionId, calendarIdForSlot(slot));
        }
    }

    emit loadCalendarsFinished(collectionId, true);
}

int PalmCalendarBackend::slotFromCalendarId(const QString &calendarId)
{
    static const QRegularExpression kRe(QStringLiteral("^palm:calendar/(\\d+)$"));
    const auto m = kRe.match(calendarId);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    if (!ok || n < 0 || n > 15) return -1;
    return n;
}

QString PalmCalendarBackend::calendarIdForSlot(int slot)
{
    return QStringLiteral("%1%2")
        .arg(QLatin1String(CalendarIdPrefix))
        .arg(slot);
}

// ========== Legacy pure-virtual stubs (Task 6 fills these) ==========
void PalmCalendarBackend::loadItems(KCalendarCore::MemoryCalendar *,
                                     bool) {}
void PalmCalendarBackend::storeCalendars(
    const QString &, const QList<KCalendarCore::MemoryCalendar *> &) {}
void PalmCalendarBackend::storeItems(
    KCalendarCore::MemoryCalendar *,
    const QList<KCalendarCore::Incidence::Ptr> &) {}
void PalmCalendarBackend::updateItem(
    KCalendarCore::MemoryCalendar *, const KCalendarCore::Incidence::Ptr &,
    const QString &) {}
void PalmCalendarBackend::startSync(
    const QString &, KCalendarCore::MemoryCalendar *,
    const QList<KCalendarCore::Incidence::Ptr> &,
    const QList<KCalendarCore::Incidence::Ptr> &,
    const QMap<QString, QString> &) {}
void PalmCalendarBackend::removeItem(const QString &, const QString &) {}

// ========== Operation API stubs (Task 5 fills these) ==========
FetchOperation *PalmCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    op->fail(QStringLiteral("fetchItems: not yet implemented (Task 5)"));
    return op;
}

PushOperation *PalmCalendarBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    op->fail(QStringLiteral("pushItems: not yet implemented (Task 5)"));
    return op;
}

DeleteOperation *PalmCalendarBackend::deleteItems(
    const QString &calendarId, const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
    op->fail(QStringLiteral("deleteItems: not yet implemented (Task 5)"));
    return op;
}

} // namespace WildPalms::PalmCalendar
