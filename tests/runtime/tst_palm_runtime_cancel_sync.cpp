// K.8b T16: Verify PalmRuntime::cancelSync() — mid-sync cancel restoration.
//
// Full E2E cancel is not tested here because it requires constructing a
// mock backend + mapping that runs long enough to be cancelled mid-flight
// (covered by libkalburator's own cancel tests which pin the SyncEngine
// cancellation channel). This test covers:
//   1. cancelSync() is a no-op (no crash) when no sync is running.
//   2. cancelSync() is a no-op when called after a sync has completed.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include "mockblobbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "synctypes.h"
#include "pluginmanager.h"
#include "backendregistry.h"
#include "stock_plugins.h"
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

class TstPalmRuntimeCancelSync : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Seed DomainRegistry with stock plugins (same as tst_palm_runtime_hotsync).
        // Phase Q.1: PluginManager ctor now requires a BackendRegistry*.
        Kalburator::Sync::BackendRegistry registry;
        Kalburator::PluginManager pm(&registry);
        Kalburator::registerStockPlugins(pm);
    }

    void cancelSync_noSync_nocrash()
    {
        // cancelSync() must be a safe no-op when nothing is running.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());
        QCOMPARE(runtime.isRunning(), false);

        // Must not crash.
        runtime.cancelSync();

        QCOMPARE(runtime.isRunning(), false);
    }

    void cancelSync_afterSync_nocrash()
    {
        // cancelSync() called after a sync completes must be a safe no-op:
        // the watcher cleans itself up on finished, so calling cancel on it
        // after the fact should not crash.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        // Minimal backend setup for a real (fast) hotSync.
        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm:calendar/0");
            ci.name = QStringLiteral("Unfiled");
            palmBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("palm-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm-calendar")));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-calendar/0");
            ci.name = QStringLiteral("PC Calendar");
            pcBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("pc-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc-calendar")));

        {
            SyncMapping m;
            m.id             = QStringLiteral("cancel-test-mapping");
            m.sourceBackend  = QStringLiteral("palm-calendar");
            m.targetBackend  = QStringLiteral("pc-calendar");
            m.sourceCalendar = QStringLiteral("palm:calendar/0");
            m.targetCalendar = QStringLiteral("pc-calendar/0");
            m.mode           = SyncMode::TwoWay;
            m.enabled        = true;
            runtime.setMappingsForTest({m});
        }

        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);

        // Sync is done; watcher has been cleaned up.  cancelSync must not crash.
        QCOMPARE(runtime.isRunning(), false);
        runtime.cancelSync();
    }
};

QTEST_GUILESS_MAIN(TstPalmRuntimeCancelSync)
#include "tst_palm_runtime_cancel_sync.moc"
