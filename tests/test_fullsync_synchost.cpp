// Phase D unit test — SyncHost_WP and the conflict stubs.

#include <QtTest/QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "calendarcollection_wp.h"
#include "conflictpresenter_wp.h"
#include "conflictresolver_wp.h"
#include "synchost_wp.h"
#include "syncconfigstore_wp.h"

using WildPalms::FullSync::CalendarCollection_WP;
using WildPalms::FullSync::ConflictPresenter_WP;
using WildPalms::FullSync::ConflictResolver_WP;
using WildPalms::FullSync::SyncConfigStore_WP;
using WildPalms::FullSync::SyncHost_WP;
using Kalburator::Sync::ConflictInfo;
using Kalburator::Sync::ConflictResolution;

class TestFullSyncSyncHost : public QObject
{
    Q_OBJECT

private slots:
    void collectionAndConfigStoreAreExposed();
    void incidenceSourceAndRegistryAreNullForPhaseD();
    void applyIncidenceCountersTick();
    void unloadAndRegenerateMappingsCount();
    void backendRegistrationLookup();
    void conflictResolverAutoAccepts();
    void conflictPresenterCountsRefreshes();

private:
    QTemporaryDir m_tempDir;
};

void TestFullSyncSyncHost::collectionAndConfigStoreAreExposed()
{
    CalendarCollection_WP coll(QStringLiteral("profile-A"));
    QSettings settings(m_tempDir.path() + QLatin1String("/a.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&coll, &store);

    QCOMPARE(host.collection(), static_cast<Kalburator::Sync::ICalendarCollection*>(&coll));
    QCOMPARE(host.configStore(), static_cast<Kalburator::Sync::ISyncConfigStore*>(&store));
}

void TestFullSyncSyncHost::incidenceSourceAndRegistryAreNullForPhaseD()
{
    CalendarCollection_WP coll(QStringLiteral("profile-A"));
    QSettings settings(m_tempDir.path() + QLatin1String("/b.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&coll, &store);

    QVERIFY(host.incidenceSource() == nullptr);
    QVERIFY(host.incidenceRegistry() == nullptr);
}

void TestFullSyncSyncHost::applyIncidenceCountersTick()
{
    CalendarCollection_WP coll(QStringLiteral("profile-A"));
    QSettings settings(m_tempDir.path() + QLatin1String("/c.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&coll, &store);

    KCalendarCore::Incidence::Ptr inc(new KCalendarCore::Event);
    inc->setUid(QStringLiteral("uid-1"));

    QCOMPARE(host.applyAdditionCount(), 0);
    QVERIFY(host.applyIncidenceAddition(QStringLiteral("cal-1"), inc));
    QCOMPARE(host.applyAdditionCount(), 1);

    QVERIFY(host.applyIncidenceUpdate(QStringLiteral("cal-1"), inc));
    QCOMPARE(host.applyUpdateCount(), 1);

    QVERIFY(host.applyIncidenceRemoval(QStringLiteral("cal-1"), QStringLiteral("uid-1")));
    QCOMPARE(host.applyRemovalCount(), 1);
}

void TestFullSyncSyncHost::unloadAndRegenerateMappingsCount()
{
    CalendarCollection_WP coll(QStringLiteral("profile-A"));
    QSettings settings(m_tempDir.path() + QLatin1String("/d.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&coll, &store);

    host.unloadCalendar(QStringLiteral("cal-1"));
    host.generateSyncMappingsFromLogicalCalendars();
    QCOMPARE(host.unloadCount(), 1);
    QCOMPARE(host.regenerateMappingsCount(), 1);
}

void TestFullSyncSyncHost::backendRegistrationLookup()
{
    CalendarCollection_WP coll(QStringLiteral("profile-A"));
    QSettings settings(m_tempDir.path() + QLatin1String("/e.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&coll, &store);

    QVERIFY(host.backendById(QStringLiteral("palm")) == nullptr);
    QVERIFY(host.backends().isEmpty());

    host.registerBackend(QString(), reinterpret_cast<Kalburator::Sync::SyncBackend*>(0x1));
    QVERIFY(host.backends().isEmpty());

    host.registerBackend(QStringLiteral("palm"), nullptr);
    QVERIFY(host.backends().isEmpty());
}

void TestFullSyncSyncHost::conflictResolverAutoAccepts()
{
    ConflictResolver_WP resolver;
    ConflictInfo info;
    info.conflictId = QStringLiteral("c-1");
    QCOMPARE(resolver.resolveConflict(info, nullptr), ConflictResolution::SourceWins);
    QCOMPARE(resolver.resolveCount(), 1);
}

void TestFullSyncSyncHost::conflictPresenterCountsRefreshes()
{
    ConflictPresenter_WP presenter;
    QCOMPARE(presenter.refreshCount(), 0);
    presenter.refreshConflicts();
    presenter.refreshConflicts();
    QCOMPARE(presenter.refreshCount(), 2);
}

QTEST_MAIN(TestFullSyncSyncHost)
#include "test_fullsync_synchost.moc"
