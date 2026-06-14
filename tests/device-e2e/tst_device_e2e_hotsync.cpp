#include <QTest>
#include <QtGlobal>
#include "emulatorfixture.h"
#include "recontrolclient.h"
#include "../wildpalms_qtest_main.h"

using namespace WildPalms::DeviceE2E;

class TestDeviceE2EHotSync : public QObject
{
    Q_OBJECT
private slots:
    void emulatorLaunchesAndExposesPtyAndDatebook()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        EmulatorFixture emu;
        QVERIFY2(emu.launch(), qPrintable(emu.lastError()));
        QVERIFY(!emu.ptyPath().isEmpty());
        // "apps all" lists every database (rec + res); bare "apps" only lists sysFileTApplication entries
        // and would never contain data-record DBs like DatebookDB regardless of the baseline.
        const ReControlReply apps = emu.client()->commandMultiline(QStringLiteral("apps all"), 5000);
        QVERIFY2(apps.raw.contains(QStringLiteral("DatebookDB")),
                 "baseline psf has no DatebookDB; pick/create a baseline where Datebook exists");
    }

    void hubToPalm_calendar_firstHotSync()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        QVERIFY(true); // replaced in Task 6
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestDeviceE2EHotSync)
#include "tst_device_e2e_hotsync.moc"
