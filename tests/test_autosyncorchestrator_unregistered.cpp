/**
 * @file test_autosyncorchestrator_unregistered.cpp
 * @brief Tests for AutoSyncOrchestrator's new unregistered-device confirmation path
 *
 * Verifies that detecting an unrecognised Palm device does NOT silently
 * create a profile; instead it emits unregisteredDeviceDetected so that
 * the UI can prompt the user.
 */

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include "kf6/autosyncorchestrator.h"
#include "profile.h"
#include "runtime/profileregistry.h"
#include <KSharedConfig>

class TestAutoSyncOrchestratorUnregistered : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void testUnregisteredEmitsSignalAndDoesNotCreate();
    void testCreateProfileForDeviceCreatesProfile();

private:
    QTemporaryDir *m_tempDir = nullptr;
};

void TestAutoSyncOrchestratorUnregistered::initTestCase()
{
    // Isolate config and home so we don't pollute the real config.
    // The test executable's CMakeLists sets XDG_CONFIG_HOME + HOME.
    qDebug() << "HOME=" << qgetenv("HOME");

    // Wipe any PalmSync dirs left over from a prior run of this test binary
    // so that testUnregisteredEmitsSignalAndDoesNotCreate starts with a clean slate.
    QDir palmSyncDir(QDir::homePath() + QStringLiteral("/PalmSync"));
    if (palmSyncDir.exists()) {
        palmSyncDir.removeRecursively();
    }
}

void TestAutoSyncOrchestratorUnregistered::cleanup()
{
    delete m_tempDir;
    m_tempDir = nullptr;

    // Remove any PalmSync dirs created in the isolated HOME so tests don't
    // bleed into each other across runs.
    QDir palmSyncDir(QDir::homePath() + QStringLiteral("/PalmSync"));
    if (palmSyncDir.exists()) {
        palmSyncDir.removeRecursively();
    }
}

void TestAutoSyncOrchestratorUnregistered::testUnregisteredEmitsSignalAndDoesNotCreate()
{
    AutoSyncOrchestrator orch;

    QSignalSpy spy(&orch, &AutoSyncOrchestrator::unregisteredDeviceDetected);
    QVERIFY(spy.isValid());

    // Drive the slot directly (no PalmDeviceMonitor needed for this assertion).
    QMetaObject::invokeMethod(&orch, "onPalmDetected", Qt::DirectConnection,
                              Q_ARG(QStringList, QStringList{ "/dev/ttyUSB0" }),
                              Q_ARG(QString, QStringLiteral("UNKNOWN-SN-12345")));

    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("UNKNOWN-SN-12345"));

    // And: no PalmSync/* subdir was created in $HOME.
    QDir palmSyncDir(QDir::homePath() + QStringLiteral("/PalmSync"));
    if (palmSyncDir.exists()) {
        QStringList entries = palmSyncDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY2(entries.isEmpty(),
                 qPrintable(QStringLiteral("Unexpected profile dirs created: ") + entries.join(", ")));
    }
}

void TestAutoSyncOrchestratorUnregistered::testCreateProfileForDeviceCreatesProfile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    auto cfg = KSharedConfig::openConfig(tempDir.path() + QStringLiteral("/wprc"));
    WildPalms::Runtime::ProfileRegistry registry(cfg);
    registry.setDefaultRoot(tempDir.path() + QStringLiteral("/root"));

    AutoSyncOrchestrator orch;
    orch.setProfileRegistry(&registry);

    QSignalSpy created(&orch, &AutoSyncOrchestrator::profileCreated);
    QVERIFY(created.isValid());

    Profile *p = orch.createProfileForDevice(
        QStringLiteral("CONFIRMED-SN-99999"),
        QStringLiteral("TestUser"),
        42u);

    QVERIFY(p != nullptr);
    QCOMPARE(created.count(), 1);
    QVERIFY(QDir(p->syncFolderPath()).exists());
    delete p;
}

QTEST_MAIN(TestAutoSyncOrchestratorUnregistered)
#include "test_autosyncorchestrator_unregistered.moc"
