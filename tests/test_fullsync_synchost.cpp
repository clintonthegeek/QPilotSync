// G.9.a unit test — SyncHost_WP narrowed interface.

#include <QtTest/QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>

#include "conflictpresenter_wp.h"
#include "conflictresolver_wp.h"
#include "synchost_wp.h"
#include "syncconfigstore_wp.h"

using WildPalms::FullSync::ConflictPresenter_WP;
using WildPalms::FullSync::ConflictResolver_WP;
using WildPalms::FullSync::SyncConfigStore_WP;
using WildPalms::FullSync::SyncHost_WP;
using Kalburator::Sync::ConflictInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ISyncHost;

class TestFullSyncSyncHost : public QObject
{
    Q_OBJECT

private slots:
    void configStoreIsExposed();
    void recordChangedCounterTicks();
    void backendRegistrationLookup();
    void conflictResolverAutoAccepts();
    void conflictPresenterCountsRefreshes();

private:
    QTemporaryDir m_tempDir;
};

void TestFullSyncSyncHost::configStoreIsExposed()
{
    QSettings settings(m_tempDir.path() + QLatin1String("/a.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&store);

    QCOMPARE(host.configStore(), static_cast<Kalburator::Sync::ISyncConfigStore*>(&store));
}

void TestFullSyncSyncHost::recordChangedCounterTicks()
{
    QSettings settings(m_tempDir.path() + QLatin1String("/b.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&store);

    QCOMPARE(host.recordChangedCount(), 0);

    host.recordChanged(QStringLiteral("mapping-1"), QStringLiteral("uid-1"), ISyncHost::ChangeKind::Created);
    QCOMPARE(host.recordChangedCount(), 1);

    host.recordChanged(QStringLiteral("mapping-1"), QStringLiteral("uid-1"), ISyncHost::ChangeKind::Updated);
    QCOMPARE(host.recordChangedCount(), 2);

    host.recordChanged(QStringLiteral("mapping-1"), QStringLiteral("uid-1"), ISyncHost::ChangeKind::Deleted);
    QCOMPARE(host.recordChangedCount(), 3);
}

void TestFullSyncSyncHost::backendRegistrationLookup()
{
    QSettings settings(m_tempDir.path() + QLatin1String("/c.ini"), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);
    SyncHost_WP host(&store);

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
