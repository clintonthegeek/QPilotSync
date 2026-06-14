#include <QTest>
#include <QtGlobal>
#include "../wildpalms_qtest_main.h"

namespace {
bool harnessConfigured()
{
    return !qEnvironmentVariableIsEmpty("WILDPALMS_POSE64_BIN")
        && !qEnvironmentVariableIsEmpty("WILDPALMS_PALM_BASELINE_PSF");
}
} // namespace

class TestDeviceE2EHotSync : public QObject
{
    Q_OBJECT
private slots:
    void hubToPalm_calendar_firstHotSync()
    {
        if (!harnessConfigured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        QVERIFY(true); // replaced in Task 6
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestDeviceE2EHotSync)
#include "tst_device_e2e_hotsync.moc"
