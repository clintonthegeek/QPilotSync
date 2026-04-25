#include <QtTest/QtTest>

#include "plugins/calendar/calendarbackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

// Complete-type includes for the libkalburator pointers returned by the
// plugin (delete on a forward-decl is UB, and IBlobBackend::backendId is
// virtual but still needs the full type for the call).
#include "iblobbackend.h"
#include "syncbackend.h"
#include "conflictpolicy.h"   // brings in Kalburator::Sync::QSyncCore::ConflictHandler

using WildPalms::CalendarPlugin::CalendarBackendPlugin;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestCalendarBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void identityFields();
    void claimsDatebookDB();
    void hasMainView();
    void createBackendsReturnsBothSlots();
    void createConflictHandlerNonNullAfterCreateBackends();
};

void TestCalendarBackendPlugin::identityFields()
{
    CalendarBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("calendar"));
    QVERIFY(!p.displayName().isEmpty());
    QVERIFY(!p.description().isEmpty());
    QCOMPARE(p.version(), QStringLiteral("2.0"));
}

void TestCalendarBackendPlugin::claimsDatebookDB()
{
    CalendarBackendPlugin p;
    QCOMPARE(p.claimedDatabases(), QStringList{QStringLiteral("DatebookDB")});
}

void TestCalendarBackendPlugin::hasMainView()
{
    CalendarBackendPlugin p;
    QVERIFY(p.hasMainView());
    QVERIFY(!p.mainViewName().isEmpty());
}

void TestCalendarBackendPlugin::createBackendsReturnsBothSlots()
{
    CalendarBackendPlugin p;
    MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);

    auto provided = p.createBackends(nullptr, &conn);
    QVERIFY(provided.blob != nullptr);
    QVERIFY(provided.calendar != nullptr);
    QCOMPARE(provided.blob->backendId(), QStringLiteral("calendar"));
    delete provided.blob;
    delete provided.calendar;
}

void TestCalendarBackendPlugin::createConflictHandlerNonNullAfterCreateBackends()
{
    CalendarBackendPlugin p;
    MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);
    auto provided = p.createBackends(nullptr, &conn);
    delete provided.blob;
    delete provided.calendar;

    auto *h = p.createConflictHandler();
    QVERIFY(h != nullptr);
    delete h;
}

QTEST_MAIN(TestCalendarBackendPlugin)
#include "tst_calendarbackendplugin.moc"
