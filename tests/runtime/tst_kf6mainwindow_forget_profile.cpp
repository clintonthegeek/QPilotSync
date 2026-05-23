// tests/runtime/tst_kf6mainwindow_forget_profile.cpp
#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>

#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

#include <KSharedConfig>

using namespace WildPalms::Runtime;

class TestableForgetWindow : public KF6MainWindow {
public:
    using KF6MainWindow::KF6MainWindow;

    bool nextConfirmReturn = false;
    bool nextDeleteFiles   = false;
    int  confirmInvocations = 0;
    ProfileEntry lastEntry;

protected:
    bool confirmForgetProfile(const ProfileEntry &entry,
                               bool *outDeleteFiles) override {
        ++confirmInvocations;
        lastEntry = entry;
        if (outDeleteFiles) *outDeleteFiles = nextDeleteFiles;
        return nextConfirmReturn;
    }
};

class TstKf6MainWindowForgetProfile : public QObject
{
    Q_OBJECT
private slots:
    void forgetWithoutDeleteKeepsFiles();
    void forgetWithDeleteRemovesFiles();
    void forgetCancelDoesNothing();
    void forgetActiveProfileIsRejected();
    void forgetClearsSerialBinding();
};

void TstKf6MainWindowForgetProfile::forgetWithoutDeleteKeepsFiles()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    QVERIFY(entry.isValid());
    const QString path = entry.path;

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = false;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QVERIFY(QDir(path).exists());
    QVERIFY(!win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetWithDeleteRemovesFiles()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    QVERIFY(entry.isValid());
    const QString path = entry.path;

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = true;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QVERIFY(!QDir(path).exists());
    QVERIFY(!win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetCancelDoesNothing()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    const QString path = entry.path;

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = false;
    win.nextDeleteFiles   = true;  // ignored when cancel

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QVERIFY(QDir(path).exists());
    QVERIFY(win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetActiveProfileIsRejected()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.runLoadProfileForTest(entry.path);
    QCOMPARE(win.currentProfileIdForTest(), entry.id);

    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = true;
    win.confirmInvocations = 0;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QCOMPARE(win.confirmInvocations, 0);
    QVERIFY(win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetClearsSerialBinding()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    QVERIFY(entry.isValid());

    // F.1d: bind a serial before forgetting.
    const QString boundSerial = QStringLiteral("F1D-SN-TEST");
    QVERIFY(reg->bindSerial(entry.id, boundSerial));
    QVERIFY(reg->findBySerial(boundSerial).isValid());

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = false;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    // Forget cascades the serial binding — serial must be unbound.
    QVERIFY(!win.profileRegistryForTest()->findBySerial(boundSerial).isValid());
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowForgetProfile)
#include "tst_kf6mainwindow_forget_profile.moc"
