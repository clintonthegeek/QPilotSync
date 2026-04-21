// Phase D unit test — SyncConfigStore_WP round-trips through QSettings.

#include <QtTest/QtTest>
#include <QSettings>
#include <QTemporaryDir>

#include "syncconfigstore_wp.h"

using WildPalms::FullSync::SyncConfigStore_WP;
using Kalburator::Sync::LogicalCalendar;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::ConflictResolution;

namespace {

LogicalCalendar makeLogical(const QString &id, const QString &displayName)
{
    LogicalCalendar lc;
    lc.id = id;
    lc.displayName = displayName;
    lc.color = QColor(Qt::green);
    return lc;
}

SyncMapping makeMapping(const QString &id)
{
    SyncMapping m;
    m.id = id;
    m.sourceBackend = QStringLiteral("palm");
    m.sourceCalendar = QStringLiteral("datebook");
    m.targetBackend = QStringLiteral("caldav");
    m.targetCalendar = QStringLiteral("work");
    m.mode = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::AskUser;
    m.enabled = true;
    return m;
}

} // namespace

class TestFullSyncSyncConfigStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void logicalCalendarCrud();
    void backendConfigRoundTrip();
    void mappingsReportedViaHasSyncMappings();
    void saveCountIsMonotonic();
    void saveThenReloadPreservesState();

private:
    QTemporaryDir m_tempDir;
    QString settingsPath() const
    {
        return m_tempDir.path() + QLatin1String("/fullsync-test.ini");
    }
};

void TestFullSyncSyncConfigStore::init()
{
    // Fresh temp dir per test.
    QVERIFY(m_tempDir.isValid());
}

void TestFullSyncSyncConfigStore::logicalCalendarCrud()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);

    store.addLogicalCalendar(makeLogical(QStringLiteral("lc-1"), QStringLiteral("Work")));
    QCOMPARE(store.logicalCalendar(QStringLiteral("lc-1")).displayName, QStringLiteral("Work"));

    auto updated = makeLogical(QStringLiteral("lc-1"), QStringLiteral("Work (renamed)"));
    store.updateLogicalCalendar(updated);
    QCOMPARE(store.logicalCalendar(QStringLiteral("lc-1")).displayName, QStringLiteral("Work (renamed)"));

    store.removeLogicalCalendar(QStringLiteral("lc-1"));
    QVERIFY(store.logicalCalendar(QStringLiteral("lc-1")).id.isEmpty());
}

void TestFullSyncSyncConfigStore::backendConfigRoundTrip()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);

    QVariantMap config;
    config.insert(QStringLiteral("url"), QStringLiteral("https://caldav.example.com"));
    config.insert(QStringLiteral("username"), QStringLiteral("alice"));

    store.setBackendConfig(QStringLiteral("caldav"), config);
    QCOMPARE(store.backendConfig(QStringLiteral("caldav")), config);
    QVERIFY(store.backendConfig(QStringLiteral("missing")).isEmpty());
}

void TestFullSyncSyncConfigStore::mappingsReportedViaHasSyncMappings()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);

    QVERIFY(!store.hasSyncMappings());
    QVERIFY(store.syncMappings().isEmpty());

    store.setSyncMappings({makeMapping(QStringLiteral("m-1")), makeMapping(QStringLiteral("m-2"))});
    QVERIFY(store.hasSyncMappings());
    QCOMPARE(store.syncMappings().size(), 2);
}

void TestFullSyncSyncConfigStore::saveCountIsMonotonic()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    SyncConfigStore_WP store(&settings);

    QCOMPARE(store.saveCount(), 0);
    store.save();
    store.save();
    QCOMPARE(store.saveCount(), 2);
}

void TestFullSyncSyncConfigStore::saveThenReloadPreservesState()
{
    const QString path = settingsPath();

    {
        QSettings settings(path, QSettings::IniFormat);
        SyncConfigStore_WP store(&settings);
        store.addLogicalCalendar(makeLogical(QStringLiteral("lc-1"), QStringLiteral("Work")));
        store.setBackendConfig(QStringLiteral("caldav"),
                               {{QStringLiteral("url"), QStringLiteral("https://x")}});
        store.setSyncMappings({makeMapping(QStringLiteral("m-1"))});
        store.save();
    }

    QSettings settings(path, QSettings::IniFormat);
    SyncConfigStore_WP reloaded(&settings);
    QCOMPARE(reloaded.logicalCalendar(QStringLiteral("lc-1")).displayName, QStringLiteral("Work"));
    QCOMPARE(reloaded.backendConfig(QStringLiteral("caldav")).value(QStringLiteral("url")).toString(),
             QStringLiteral("https://x"));
    QVERIFY(reloaded.hasSyncMappings());
    QCOMPARE(reloaded.syncMappings().size(), 1);
    QCOMPARE(reloaded.syncMappings().first().id, QStringLiteral("m-1"));
}

QTEST_MAIN(TestFullSyncSyncConfigStore)
#include "test_fullsync_syncconfigstore.moc"
