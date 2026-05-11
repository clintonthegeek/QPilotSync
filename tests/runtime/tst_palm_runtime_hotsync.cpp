#include <QTest>
#include <QFuture>

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include "mockblobbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "synctypes.h"
#include "pluginmanager.h"
#include "stock_plugins.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

class TestPalmRuntimeHotSync : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // K.7: seed DomainRegistry with stock plugins so dispatchSync
        // finds the blob domain definition (BlobPlugin).
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
    }

    void hotSync_emptyTarget_recordPropagates() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        // Palm source: one record in collection "palm:calendar/0"
        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm:calendar/0");
            ci.name = QStringLiteral("Unfiled");
            palmBlob->createCollection(ci);

            BackendRecord rec;
            rec.id   = QStringLiteral("event-001");
            rec.data = QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
                                  "UID:event-001@palm\r\nSUMMARY:Test Event\r\n"
                                  "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                                  "END:VEVENT\r\nEND:VCALENDAR\r\n");
            palmBlob->createRecord(QStringLiteral("palm:calendar/0"), rec);
        }
        runtime.registerBlobBackendForTest(QStringLiteral("palm-calendar"), std::move(palmBlob));

        // PC target: empty — keep raw pointer for post-sync assertion
        auto pcBlobOwned = std::make_unique<MockBlobBackend>();
        MockBlobBackend *pcBlob = pcBlobOwned.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-calendar/0");
            ci.name = QStringLiteral("PC Calendar");
            pcBlob->createCollection(ci);
        }
        runtime.registerBlobBackendForTest(QStringLiteral("pc-calendar"), std::move(pcBlobOwned));

        // Mapping: palm → pc, TwoWay
        {
            SyncMapping m;
            m.id             = QStringLiteral("test-mapping-1");
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
        const auto result = future.resultAt(0);

        QVERIFY(result.success);

        // Verify the record was actually written to the target backend.
        // dispatchBlobSync does not populate targetStats.created, so we
        // verify the real behavioral outcome: the record exists in pcBlob.
        const auto written = pcBlob->recordsIn(QStringLiteral("pc-calendar/0"));
        QCOMPARE(written.size(), 1);
        QVERIFY(written.contains(QStringLiteral("event-001")));
    }
};

QTEST_GUILESS_MAIN(TestPalmRuntimeHotSync)
#include "tst_palm_runtime_hotsync.moc"
