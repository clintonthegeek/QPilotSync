#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"
#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/profile.h"

namespace {
DeviceFingerprint makeFp(const QString &serial,
                         quint32 userId = 0,
                         const QString &userName = {},
                         const QString &modelName = {}) {
    DeviceFingerprint fp;
    fp.usbSerialNumber = serial;
    fp.userId = userId;
    fp.userName = userName;
    fp.modelName = modelName;
    return fp;
}

// Subclass that captures dialog opens via the virtual seam.
struct CapturingMainWindow : public KF6MainWindow {
    int dialogOpens = 0;
protected:
    bool openMismatchDialogForTest(const DeviceFingerprint &,
                                    const DeviceFingerprint &) override {
        ++dialogOpens;
        return false; // simulate Disconnect
    }
};
} // namespace

class TstKf6MainWindowMismatchDialog : public QObject {
    Q_OBJECT
private slots:
    void matchDoesNotOpenDialog();
    void indeterminateDoesNotOpenDialog();
    void mismatchKnownPopulatesStructuredMessage();
};

void TstKf6MainWindowMismatchDialog::matchDoesNotOpenDialog()
{
    CapturingMainWindow win;
    auto a = makeFp(QStringLiteral("S1"));
    auto b = makeFp(QStringLiteral("S1"));
    QCOMPARE(win.runMismatchCheckForTest(a, b), true);
    QCOMPARE(win.dialogOpens, 0);
}

void TstKf6MainWindowMismatchDialog::indeterminateDoesNotOpenDialog()
{
    CapturingMainWindow win;
    auto a = makeFp(QStringLiteral("S1"));   // serial only
    auto b = makeFp(QString(), 0, QStringLiteral("user"));  // username only
    QCOMPARE(win.runMismatchCheckForTest(a, b), true);  // indeterminate → proceed
    QCOMPARE(win.dialogOpens, 0);
}

void TstKf6MainWindowMismatchDialog::mismatchKnownPopulatesStructuredMessage()
{
    CapturingMainWindow win;
    auto a = makeFp(QStringLiteral("S1"), 0, QStringLiteral("alice"));
    auto b = makeFp(QStringLiteral("S2"), 0, QStringLiteral("bob"));
    const QString msg = win.renderMismatchMessageForTest(a, b);
    QVERIFY(msg.contains(QStringLiteral("Serial"), Qt::CaseInsensitive));
    QVERIFY(msg.contains(QStringLiteral("User"),   Qt::CaseInsensitive));
    QVERIFY(msg.contains(QStringLiteral("S1")));
    QVERIFY(msg.contains(QStringLiteral("S2")));
    QVERIFY(msg.contains(QStringLiteral("alice")));
    QVERIFY(msg.contains(QStringLiteral("bob")));
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowMismatchDialog)
#include "tst_kf6mainwindow_mismatch_dialog.moc"
