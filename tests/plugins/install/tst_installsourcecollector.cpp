#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "runtime/installsourcecollector.h"

using WildPalms::InstallSourceCollector;

class TestInstallSourceCollector : public QObject
{
    Q_OBJECT

private slots:
    void collect_emptyFolderAndNullManager_returnsEmpty()
    {
        InstallSourceCollector c;
        const auto r = c.collect(QString(), nullptr);
        QVERIFY(r.files.isEmpty());
        QVERIFY(r.folderSourcedPaths.isEmpty());
    }

    void collect_folder_picksUpPrcAndPdb()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString folder = tmp.path();
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(folder).filePath(QStringLiteral("foo.prc")));
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.pdb"),
                    QDir(folder).filePath(QStringLiteral("bar.pdb")));
        QFile junk(QDir(folder).filePath(QStringLiteral("junk.txt")));
        junk.open(QIODevice::WriteOnly);
        junk.write("xx");
        junk.close();

        InstallSourceCollector c;
        const auto r = c.collect(folder, nullptr);

        QCOMPARE(r.files.size(), 2);
        QCOMPARE(r.folderSourcedPaths.size(), 2);
        QStringList names;
        for (const auto &f : r.files) names << f.displayName;
        std::sort(names.begin(), names.end());
        QCOMPARE(names, (QStringList{QStringLiteral("bar.pdb"),
                                       QStringLiteral("foo.prc")}));
    }

    void collect_folder_caseInsensitive()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(tmp.path()).filePath(QStringLiteral("UPPER.PRC")));

        InstallSourceCollector c;
        const auto r = c.collect(tmp.path(), nullptr);
        QCOMPARE(r.files.size(), 1);
        QCOMPARE(r.files[0].displayName, QStringLiteral("UPPER.PRC"));
    }

    void collect_nonexistentFolder_returnsEmpty()
    {
        InstallSourceCollector c;
        const auto r = c.collect(QStringLiteral("/no/such/dir"), nullptr);
        QVERIFY(r.files.isEmpty());
    }

    void moveSucceededToInstalled_movesOnlyMatchingPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString folder = tmp.path();
        const QString a = QDir(folder).filePath(QStringLiteral("a.prc"));
        const QString b = QDir(folder).filePath(QStringLiteral("b.pdb"));
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"), a);
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.pdb"), b);

        InstallSourceCollector c;
        const auto r = c.collect(folder, nullptr);
        QCOMPARE(r.files.size(), 2);

        c.moveSucceededToInstalled(r, QStringList{a});
        QVERIFY(!QFile::exists(a));
        QVERIFY(QFile::exists(QDir(folder).filePath(
            QStringLiteral("installed/a.prc"))));
        QVERIFY(QFile::exists(b));
    }
};

QTEST_MAIN(TestInstallSourceCollector)
#include "tst_installsourcecollector.moc"
