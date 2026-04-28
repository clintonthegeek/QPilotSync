#include <QTest>

#include "palm/sync/mockpalmfileinstaller.h"

using namespace WildPalms::PalmSync;

class TestPalmFileInstaller : public QObject
{
    Q_OBJECT

private slots:
    void mock_recordsCallsByDefaultSucceeds()
    {
        MockPalmFileInstaller m;
        QString err;
        QVERIFY(m.installFile(QStringLiteral("/tmp/foo.prc"), &err));
        QVERIFY(err.isEmpty());
        QCOMPARE(m.installedPaths(),
                 (QStringList{QStringLiteral("/tmp/foo.prc")}));
    }

    void mock_setNextResult_appliesOnce()
    {
        MockPalmFileInstaller m;
        m.setNextResult(false, QStringLiteral("simulated"));

        QString err;
        QVERIFY(!m.installFile(QStringLiteral("/tmp/a.prc"), &err));
        QCOMPARE(err, QStringLiteral("simulated"));

        QVERIFY(m.installFile(QStringLiteral("/tmp/b.prc"), &err));
    }

    void mock_setAllowAll_isSticky()
    {
        MockPalmFileInstaller m;
        m.setAllowAll(false);
        QVERIFY(!m.installFile(QStringLiteral("/tmp/a.prc")));
        QVERIFY(!m.installFile(QStringLiteral("/tmp/b.prc")));
        m.setAllowAll(true);
        QVERIFY(m.installFile(QStringLiteral("/tmp/c.prc")));
    }

    void mock_recordsAllPaths()
    {
        MockPalmFileInstaller m;
        m.installFile(QStringLiteral("/tmp/a.prc"));
        m.installFile(QStringLiteral("/tmp/b.prc"));
        m.installFile(QStringLiteral("/tmp/c.prc"));
        QCOMPARE(m.installedPaths().size(), 3);
        QCOMPARE(m.installedPaths()[1], QStringLiteral("/tmp/b.prc"));
    }
};

QTEST_MAIN(TestPalmFileInstaller)
#include "tst_palmfileinstaller.moc"
