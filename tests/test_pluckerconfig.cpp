#include <QTest>
#include <QTemporaryDir>
#include <QSettings>
#include "../src/plugins/plucker/pluckerconfig.h"

using PluckerChannel = WildPalms::PluckerPlugin::PluckerChannel;

class TestPluckerConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaultChannel();
    void testAddAndRetrieveChannel();
    void testRemoveChannel();
    void testSaveAndLoad();
    void testIsDue_neverFetched();
    void testIsDue_notYetDue();
    void testIsDue_pastDue();
    void testIsDue_disabledChannel();
    void testChannelToCLIArgs();
};

void TestPluckerConfig::testDefaultChannel()
{
    PluckerChannel ch;
    QCOMPARE(ch.maxDepth, 2);
    QCOMPARE(ch.bpp, 8);
    QCOMPARE(ch.stayOnHost, false);
    QCOMPARE(ch.depthFirst, false);
    QCOMPARE(ch.compression, QStringLiteral("zlib"));
    QCOMPARE(ch.storageMode, QStringLiteral("ram"));
    QCOMPARE(ch.updateEnabled, true);
    QCOMPARE(ch.updateFrequency, 1);
    QCOMPARE(ch.updatePeriod, QStringLiteral("days"));
    QVERIFY(!ch.id.isEmpty());  // Auto-generated UUID
}

void TestPluckerConfig::testAddAndRetrieveChannel()
{
    PluckerConfig config;
    PluckerChannel ch;
    ch.name = "Test Channel";
    ch.homeUrl = "http://example.com";
    ch.maxDepth = 5;

    config.addChannel(ch);

    QCOMPARE(config.channels().size(), 1);
    QCOMPARE(config.channel(ch.id).name, QStringLiteral("Test Channel"));
    QCOMPARE(config.channel(ch.id).homeUrl, QStringLiteral("http://example.com"));
    QCOMPARE(config.channel(ch.id).maxDepth, 5);
}

void TestPluckerConfig::testRemoveChannel()
{
    PluckerConfig config;
    PluckerChannel ch;
    ch.name = "Doomed";
    config.addChannel(ch);
    QCOMPARE(config.channels().size(), 1);

    config.removeChannel(ch.id);
    QCOMPARE(config.channels().size(), 0);
}

void TestPluckerConfig::testSaveAndLoad()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString configPath = tmpDir.path();

    // Save
    {
        PluckerConfig config;
        PluckerChannel ch1;
        ch1.name = "BBC News";
        ch1.homeUrl = "http://bbc.co.uk";
        ch1.maxDepth = 3;
        ch1.bpp = 4;
        ch1.stayOnHost = true;
        ch1.category = "News";
        ch1.updateFrequency = 6;
        ch1.updatePeriod = "hours";
        config.addChannel(ch1);

        PluckerChannel ch2;
        ch2.name = "Slashdot";
        ch2.homeUrl = "http://slashdot.org";
        ch2.updateEnabled = false;
        config.addChannel(ch2);

        config.save(configPath);
    }

    // Load in a fresh instance
    {
        PluckerConfig config;
        config.load(configPath);

        QCOMPARE(config.channels().size(), 2);

        // Find BBC by name (order may vary)
        PluckerChannel bbc;
        for (const auto &ch : config.channels()) {
            if (ch.name == "BBC News") bbc = ch;
        }
        QCOMPARE(bbc.homeUrl, QStringLiteral("http://bbc.co.uk"));
        QCOMPARE(bbc.maxDepth, 3);
        QCOMPARE(bbc.bpp, 4);
        QCOMPARE(bbc.stayOnHost, true);
        QCOMPARE(bbc.category, QStringLiteral("News"));
        QCOMPARE(bbc.updateFrequency, 6);
        QCOMPARE(bbc.updatePeriod, QStringLiteral("hours"));
    }
}

void TestPluckerConfig::testIsDue_neverFetched()
{
    PluckerChannel ch;
    ch.updateEnabled = true;
    QVERIFY(PluckerConfig::isDue(ch));
}

void TestPluckerConfig::testIsDue_notYetDue()
{
    PluckerChannel ch;
    ch.updateEnabled = true;
    ch.updateFrequency = 1;
    ch.updatePeriod = "days";
    ch.lastFetched = QDateTime::currentDateTime().addSecs(-3600);  // 1 hour ago
    QVERIFY(!PluckerConfig::isDue(ch));
}

void TestPluckerConfig::testIsDue_pastDue()
{
    PluckerChannel ch;
    ch.updateEnabled = true;
    ch.updateFrequency = 1;
    ch.updatePeriod = "days";
    ch.lastFetched = QDateTime::currentDateTime().addDays(-2);  // 2 days ago
    QVERIFY(PluckerConfig::isDue(ch));
}

void TestPluckerConfig::testIsDue_disabledChannel()
{
    PluckerChannel ch;
    ch.updateEnabled = false;
    QVERIFY(!PluckerConfig::isDue(ch));
}

void TestPluckerConfig::testChannelToCLIArgs()
{
    PluckerChannel ch;
    ch.name = "My Site";
    ch.homeUrl = "http://example.com/page";
    ch.maxDepth = 5;
    ch.stayOnHost = true;
    ch.depthFirst = true;
    ch.bpp = 4;
    ch.maxWidth = 200;
    ch.maxHeight = 300;
    ch.compression = "zlib";
    ch.category = "Reference";
    ch.noImages = false;

    QStringList args = PluckerConfig::buildCLIArgs(ch, "/tmp/out");

    QVERIFY(args.contains("--home-url=http://example.com/page"));
    QVERIFY(args.contains("--doc-name=My Site"));
    QVERIFY(args.contains("--maxdepth=5"));
    QVERIFY(args.contains("--stayonhost"));
    QVERIFY(args.contains("--depth-first"));
    QVERIFY(args.contains("--bpp=4"));
    QVERIFY(args.contains("--maxwidth=200"));
    QVERIFY(args.contains("--maxheight=300"));
    QVERIFY(args.contains("--compression=zlib"));
    QVERIFY(args.contains("--category=Reference"));
    QVERIFY(args.contains(QStringLiteral("--pluckerdir=/tmp/out")));
    bool hasDocFile = false;
    for (const QString &arg : args) {
        if (arg.startsWith("--doc-file=")) hasDocFile = true;
    }
    QVERIFY(hasDocFile);
}

QTEST_GUILESS_MAIN(TestPluckerConfig)
#include "test_pluckerconfig.moc"
