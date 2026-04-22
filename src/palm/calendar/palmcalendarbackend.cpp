#include "palmcalendarbackend.h"

#include <QRegularExpression>

#include "syncoperation.h"

#include "categorymappingstore.h"
#include "datadomain.h"
#include "ipalmdatabaseaccess.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "datebookcodec.h"
#include "palmrecord.h"

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

// ========== Operation API (Task 5) ==========
FetchOperation *PalmCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);

    const int slot = slotFromCalendarId(calendarId);
    if (slot < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    op->setState(SyncOperation::Running);

    const auto records = m_device->readAllRecords(QLatin1String(DatabaseName));
    emit fetchStarted(calendarId, records.size());

    QList<KCalendarCore::Incidence::Ptr> items;
    int skipped = 0;
    for (const auto &rec : records) {
        if (static_cast<int>(rec.category) != slot) {
            continue;
        }
        const auto decoded = DatebookCodec::decode(rec);
        if (!decoded.isValid()) {
            ++skipped;
            continue;
        }
        items.append(decoded.event);
        emit itemFetched(calendarId, decoded.event);
    }

    op->setFetchedItems(items);
    op->complete();
    emit fetchFinished(calendarId, true);
    return op;
}

PushOperation *PalmCalendarBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);

    const int slot = slotFromCalendarId(calendarId);
    if (slot < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        emit writeFinished(calendarId, false, err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        emit writeFinished(calendarId, false, err);
        return op;
    }

    // Ensure the database exists. A real device may surface a missing
    // DatebookDB as a hard error; the mock creates lazily.
    m_device->createDatabase(QLatin1String(DatabaseName));

    op->setState(SyncOperation::Running);
    emit writeStarted(calendarId, items.size());

    for (const auto &incidence : items) {
        if (!incidence || incidence->type() != KCalendarCore::IncidenceBase::TypeEvent) {
            op->addFailedUid(incidence ? incidence->uid() : QString());
            continue;
        }
        const auto event = incidence.staticCast<KCalendarCore::Event>();
        auto rec = DatebookCodec::encode(event, slot);
        if (rec.data.isEmpty()) {
            op->addFailedUid(event->uid());
            continue;
        }

        if (rec.recordId == 0) {
            // New record.
            const auto newId = m_device->createRecord(
                QLatin1String(DatabaseName), rec);
            if (newId == 0) {
                op->addFailedUid(event->uid());
                continue;
            }
            // Stash the assigned record ID on the event so callers
            // carrying the Incidence onward see the server-side ID.
            event->setCustomProperty(
                "KCalendarCore",
                QByteArray(DatebookCodec::RecordIdProperty),
                QString::number(newId));
            op->addSucceededUid(event->uid());
        } else {
            if (m_device->updateRecord(QLatin1String(DatabaseName), rec)) {
                op->addSucceededUid(event->uid());
            } else {
                op->addFailedUid(event->uid());
            }
        }
    }

    op->complete();
    emit writeFinished(calendarId, true);
    return op;
}

DeleteOperation *PalmCalendarBackend::deleteItems(
    const QString &calendarId, const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);

    if (slotFromCalendarId(calendarId) < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        return op;
    }

    op->setState(SyncOperation::Running);

    // UIDs from DatebookCodec have the form "palm-datebook-<recordId>".
    static const QRegularExpression kUidRe(
        QStringLiteral("^palm-datebook-(\\d+)$"));

    for (const auto &uid : uids) {
        const auto m = kUidRe.match(uid);
        if (!m.hasMatch()) {
            op->addFailedUid(uid);
            continue;
        }
        bool ok = false;
        const auto recordId = m.captured(1).toUInt(&ok);
        if (!ok) {
            op->addFailedUid(uid);
            continue;
        }
        if (m_device->deleteRecord(QLatin1String(DatabaseName), recordId)) {
            op->addSucceededUid(uid);
        } else {
            op->addFailedUid(uid);
        }
    }

    op->complete();
    return op;
}

} // namespace WildPalms::PalmCalendar
