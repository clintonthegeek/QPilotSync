#include <QtTest/QtTest>
#include <QIcon>
#include <QWidget>
#include "core/ibackendplugin.h"
#include "conflictrecord.h"

// Minimal concrete IBackendPlugin that overrides only the required pure
// virtuals. Every optional hook falls through to the default
// implementation, which is the surface this test pins down.
class TrivialBackendPlugin : public WildPalms::IBackendPlugin {
public:
    QString pluginId()    const override { return QStringLiteral("trivial"); }
    QString displayName() const override { return QStringLiteral("Trivial"); }
    QIcon   icon()        const override { return {}; }
    QString description() const override { return {}; }
    QString version()     const override { return QStringLiteral("0.0"); }
    QStringList claimedDatabases() const override { return {}; }
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                    PalmDeviceConnection *) override { return {}; }
};

class TestIBackendPluginDefaults : public QObject {
    Q_OBJECT
private slots:
    void viewHooksDefaultToNoView();
    void conflictHooksDefaultToNoop();
};

void TestIBackendPluginDefaults::viewHooksDefaultToNoView()
{
    TrivialBackendPlugin p;
    QCOMPARE(p.hasMainView(), false);
    QWidget parent;
    QCOMPARE(p.createMainView(&parent), static_cast<QWidget *>(nullptr));
    QVERIFY(p.mainViewName().isEmpty());
    QVERIFY(p.mainViewIcon().isNull());
}

void TestIBackendPluginDefaults::conflictHooksDefaultToNoop()
{
    TrivialBackendPlugin p;
    Kalburator::Sync::QSyncCore::RecordSnapshot snap;
    snap.content = "hello";
    p.enrichConflictSnapshot(snap, true);        // must not crash
    QCOMPARE(snap.content, QByteArray("hello")); // default mutates nothing

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<pre>")));
    QVERIFY(html.contains(QStringLiteral("hello")));
}

QTEST_MAIN(TestIBackendPluginDefaults)
#include "tst_ibackendplugin_defaults.moc"
