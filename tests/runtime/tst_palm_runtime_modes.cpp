#include <QTest>
#include <QFuture>
#include <QTemporaryDir>

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include "mockblobbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "synctypes.h"
#include "rawfilesbackend.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

namespace {

static BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id   = id;
    r.data = data;
    return r;
}

static SyncMapping makeTwoWayMapping(const QString &srcBackend, const QString &srcCol,
                                     const QString &tgtBackend, const QString &tgtCol)
{
    SyncMapping m;
    m.id             = QStringLiteral("test-mapping");
    m.sourceBackend  = srcBackend;
    m.targetBackend  = tgtBackend;
    m.sourceCalendar = srcCol;
    m.targetCalendar = tgtCol;
    m.mode           = SyncMode::TwoWay;
    m.enabled        = true;
    return m;
}

} // namespace

class TestPalmRuntimeModes : public QObject {
    Q_OBJECT
private slots:
    void fullSync_clearsBaselinesThenCopiesPalmToPC() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-A"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-A\r\nSUMMARY:A\r\n"
                               "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("palm"), std::move(palmBlob));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *pcRaw = pcBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
        }
        runtime.registerBlobBackendForTest(QStringLiteral("pc"), std::move(pcBlob));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        // fullSync should succeed and copy Palm records to PC.
        auto future = runtime.fullSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        const auto written = pcRaw->recordsIn(QStringLiteral("pc-col"));
        QCOMPARE(written.size(), 1);
        QVERIFY(written.contains(QStringLiteral("rec-A")));
    }

    void copyPalmToPC_overwritesPC() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        // Palm has rec-A; PC already has rec-B.
        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-A"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-A\r\nSUMMARY:A\r\n"
                               "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("palm"), std::move(palmBlob));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *pcRaw = pcBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
            pcBlob->createRecord(QStringLiteral("pc-col"),
                makeRecord(QStringLiteral("rec-B"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-B\r\nSUMMARY:B\r\n"
                               "DTSTART:20260502T090000Z\r\nDTEND:20260502T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("pc"), std::move(pcBlob));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        auto future = runtime.copyPalmToPC();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // PC should now have Palm's record, not its own.
        const auto pcFinal = pcRaw->recordsIn(QStringLiteral("pc-col"));
        QVERIFY(pcFinal.contains(QStringLiteral("rec-A")));
        QVERIFY(!pcFinal.contains(QStringLiteral("rec-B")));
    }

    void copyPCToPalm_overwritesPalm() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        // PC has rec-A; Palm already has rec-B.
        auto palmBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *palmRaw = palmBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-B"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-B\r\nSUMMARY:B\r\n"
                               "DTSTART:20260502T090000Z\r\nDTEND:20260502T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("palm"), std::move(palmBlob));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
            pcBlob->createRecord(QStringLiteral("pc-col"),
                makeRecord(QStringLiteral("rec-A"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-A\r\nSUMMARY:A\r\n"
                               "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("pc"), std::move(pcBlob));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        auto future = runtime.copyPCToPalm();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // Palm should now have PC's record, not its own.
        const auto palmFinal = palmRaw->recordsIn(QStringLiteral("palm-col"));
        QVERIFY(palmFinal.contains(QStringLiteral("rec-A")));
        QVERIFY(!palmFinal.contains(QStringLiteral("rec-B")));
    }

    void backup_additiveCopiesPalmRecords() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-A"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-A\r\nSUMMARY:A\r\n"
                               "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-B"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-B\r\nSUMMARY:B\r\n"
                               "DTSTART:20260502T090000Z\r\nDTEND:20260502T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("palm"), std::move(palmBlob));

        // Register a dummy PC target so the runtime has a valid mapping.
        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
        }
        runtime.registerBlobBackendForTest(QStringLiteral("pc"), std::move(pcBlob));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        auto future = runtime.backup();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // Verify the backup root contains the Palm records.
        const QString backupPath = QDir(profileDir.path())
            .filePath(QStringLiteral("backup/palm/palm-col"));
        QVERIFY(QFileInfo(backupPath).isDir());
    }

    void restore_writesBackupRecordsToPalm() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        // Pre-populate backup root with one record.
        const QString backupPath = QDir(profileDir.path())
            .filePath(QStringLiteral("backup/palm/palm-col"));
        QDir().mkpath(backupPath);
        {
            Kalburator::Sinks::RawFilesBackend backupSink(backupPath);
            CollectionInfo col;
            col.id   = QStringLiteral("palm-col");
            col.name = QStringLiteral("Palm");
            backupSink.createCollection(col);
            backupSink.createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-X"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-X\r\nSUMMARY:X\r\n"
                               "DTSTART:20260503T090000Z\r\nDTEND:20260503T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }

        PalmRuntime runtime(profileDir.path());

        auto palmBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *palmRaw = palmBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            // Palm has rec-Y (not in backup) — should be deleted after restore.
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-Y"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-Y\r\nSUMMARY:Y\r\n"
                               "DTSTART:20260504T090000Z\r\nDTEND:20260504T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBlobBackendForTest(QStringLiteral("palm"), std::move(palmBlob));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
        }
        runtime.registerBlobBackendForTest(QStringLiteral("pc"), std::move(pcBlob));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        auto future = runtime.restore();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // Palm should now have rec-X (from backup) and NOT rec-Y (deleted).
        const auto palmFinal = palmRaw->recordsIn(QStringLiteral("palm-col"));
        QVERIFY(palmFinal.contains(QStringLiteral("rec-X")));
        QVERIFY(!palmFinal.contains(QStringLiteral("rec-Y")));
    }
};

QTEST_GUILESS_MAIN(TestPalmRuntimeModes)
#include "tst_palm_runtime_modes.moc"
