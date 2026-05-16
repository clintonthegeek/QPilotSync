// Layer B: Palm backends must report failure via loadRecordsOrError when
// the underlying pilot-link transport has dropped. Without this override,
// the silent default in IBlobBackend treats empty as success — the
// silent-success-becomes-data-loss bug from testpalm5.

#include <QtTest>

#include "palm/memo/palmmemobackend.h"
#include "palm/todo/palmtodobackend.h"
#include "palm/contacts/palmcontactsbackend.h"
#include "palm/calendar/palmcalendarbackend.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

#include "../wildpalms_qtest_main.h"

using namespace WildPalms::PalmSync;
using namespace WildPalms::PalmMemo;
using namespace WildPalms::PalmToDo;
using namespace WildPalms::PalmContacts;
using namespace WildPalms::PalmCalendar;

class TstPalmBackendDisconnect : public QObject
{
    Q_OBJECT
private slots:
    void memo_reportsFailureWhenDisconnected();
    void todo_reportsFailureWhenDisconnected();
    void contacts_reportsFailureWhenDisconnected();
    void calendar_reportsFailureWhenDisconnected();
    void memo_succeedsWhenConnected();
};

void TstPalmBackendDisconnect::memo_reportsFailureWhenDisconnected()
{
    MockPalmDatabaseAccess dev;
    dev.setConnected(false);
    PalmMemoBackend backend(&dev, QStringLiteral("device-1"));

    QList<Kalburator::Sync::BackendRecord> records;
    QString err;
    const bool ok = backend.loadRecordsOrError(
        QLatin1String(PalmMemoBackend::CollectionId), records, err);

    QVERIFY2(!ok, "loadRecordsOrError returned true despite disconnected device");
    QVERIFY2(!err.isEmpty(), "errorMessage was empty");
    QVERIFY2(records.isEmpty(), "records list should be empty on failure");
}

void TstPalmBackendDisconnect::todo_reportsFailureWhenDisconnected()
{
    MockPalmDatabaseAccess dev;
    dev.setConnected(false);
    PalmToDoBackend backend(&dev, QStringLiteral("device-1"));

    QList<Kalburator::Sync::BackendRecord> records;
    QString err;
    const bool ok = backend.loadRecordsOrError(
        QLatin1String(PalmToDoBackend::CollectionId), records, err);

    QVERIFY(!ok);
    QVERIFY(!err.isEmpty());
}

void TstPalmBackendDisconnect::contacts_reportsFailureWhenDisconnected()
{
    MockPalmDatabaseAccess dev;
    dev.setConnected(false);
    PalmContactsBackend backend(&dev, QStringLiteral("device-1"));

    QList<Kalburator::Sync::BackendRecord> records;
    QString err;
    const bool ok = backend.loadRecordsOrError(
        QLatin1String(PalmContactsBackend::CollectionId), records, err);

    QVERIFY(!ok);
    QVERIFY(!err.isEmpty());
}

void TstPalmBackendDisconnect::calendar_reportsFailureWhenDisconnected()
{
    MockPalmDatabaseAccess dev;
    dev.setConnected(false);
    CategoryMappingStore categoryStore;
    PalmCalendarBackend backend(&dev, &categoryStore);

    QList<Kalburator::Sync::BackendRecord> records;
    QString err;
    const bool ok = backend.loadRecordsOrError(
        QLatin1String(PalmCalendarBackend::CollectionId), records, err);

    QVERIFY(!ok);
    QVERIFY(!err.isEmpty());
}

void TstPalmBackendDisconnect::memo_succeedsWhenConnected()
{
    MockPalmDatabaseAccess dev;
    dev.setConnected(true);
    PalmMemoBackend backend(&dev, QStringLiteral("device-1"));

    QList<Kalburator::Sync::BackendRecord> records;
    QString err;
    const bool ok = backend.loadRecordsOrError(
        QLatin1String(PalmMemoBackend::CollectionId), records, err);

    QVERIFY(ok);
    QVERIFY(err.isEmpty());
}

WILDPALMS_QTEST_GUILESS_MAIN(TstPalmBackendDisconnect)
#include "tst_palm_backend_disconnect.moc"
