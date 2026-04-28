#include <QJsonObject>
#include <QTest>

#include "pluckerchannel.h"
#include "pluckerchannelserializer.h"

using namespace WildPalms::PluckerPlugin;

class TestPluckerChannel : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip_allFields()
    {
        PluckerChannel original;
        original.id                    = QStringLiteral("abc-123");
        original.name                  = QStringLiteral("BBC News");
        original.homeUrl               = QStringLiteral("https://www.bbc.co.uk/news");
        original.maxDepth              = 4;
        original.stayOnHost            = true;
        original.depthFirst            = true;
        original.userAgent             = QStringLiteral("WildPalms/1.0");
        original.urlPattern            = QStringLiteral("https://www.bbc.co.uk");
        original.bpp                   = 4;
        original.maxWidth              = 160;
        original.maxHeight             = 240;
        original.altMaxWidth           = 600;
        original.altMaxHeight          = 900;
        original.noImages              = true;
        original.imageCompressionLimit = 75;
        original.compression           = QStringLiteral("none");
        original.category              = QStringLiteral("News");
        original.storageMode           = QStringLiteral("vfs");
        original.cardDirectory         = QStringLiteral("/Documents");
        original.updateEnabled         = false;
        original.updateFrequency       = 3;
        original.updatePeriod          = QStringLiteral("weeks");
        original.lastFetched           = QDateTime::fromString(
            QStringLiteral("2026-04-26T10:30:00"), Qt::ISODate);

        const QJsonObject json = pluckerChannelToJson(original);
        const PluckerChannel restored = pluckerChannelFromJson(json);

        QCOMPARE(restored.id,                    original.id);
        QCOMPARE(restored.name,                  original.name);
        QCOMPARE(restored.homeUrl,               original.homeUrl);
        QCOMPARE(restored.maxDepth,              original.maxDepth);
        QCOMPARE(restored.stayOnHost,            original.stayOnHost);
        QCOMPARE(restored.depthFirst,            original.depthFirst);
        QCOMPARE(restored.userAgent,             original.userAgent);
        QCOMPARE(restored.urlPattern,            original.urlPattern);
        QCOMPARE(restored.bpp,                   original.bpp);
        QCOMPARE(restored.maxWidth,              original.maxWidth);
        QCOMPARE(restored.maxHeight,             original.maxHeight);
        QCOMPARE(restored.altMaxWidth,           original.altMaxWidth);
        QCOMPARE(restored.altMaxHeight,          original.altMaxHeight);
        QCOMPARE(restored.noImages,              original.noImages);
        QCOMPARE(restored.imageCompressionLimit, original.imageCompressionLimit);
        QCOMPARE(restored.compression,           original.compression);
        QCOMPARE(restored.category,              original.category);
        QCOMPARE(restored.storageMode,           original.storageMode);
        QCOMPARE(restored.cardDirectory,         original.cardDirectory);
        QCOMPARE(restored.updateEnabled,         original.updateEnabled);
        QCOMPARE(restored.updateFrequency,       original.updateFrequency);
        QCOMPARE(restored.updatePeriod,          original.updatePeriod);
        QCOMPARE(restored.lastFetched,           original.lastFetched);
    }

    void fromJson_emptyObject_yieldsDefaults()
    {
        const PluckerChannel ch = pluckerChannelFromJson(QJsonObject{});
        QCOMPARE(ch.maxDepth, 2);
        QCOMPARE(ch.bpp, 8);
        QCOMPARE(ch.compression, QStringLiteral("zlib"));
        QCOMPARE(ch.storageMode, QStringLiteral("ram"));
        QCOMPARE(ch.updateEnabled, true);
        QCOMPARE(ch.updateFrequency, 1);
        QCOMPARE(ch.updatePeriod, QStringLiteral("days"));
        QVERIFY(!ch.lastFetched.isValid());
    }

    void isDue_neverFetched_isTrue()
    {
        PluckerChannel ch;
        ch.updateEnabled = true;
        QVERIFY(pluckerIsDue(ch));
    }

    void isDue_disabled_isFalse()
    {
        PluckerChannel ch;
        ch.updateEnabled = false;
        QVERIFY(!pluckerIsDue(ch));
    }

    void isDue_recentDailyFetch_isFalse()
    {
        PluckerChannel ch;
        ch.updateEnabled   = true;
        ch.updateFrequency = 1;
        ch.updatePeriod    = QStringLiteral("days");
        ch.lastFetched     = QDateTime::currentDateTime().addSecs(-3600);
        QVERIFY(!pluckerIsDue(ch));
    }

    void sanitizeDocFile_specialChars_becomeUnderscores()
    {
        QCOMPARE(pluckerSanitizeDocFile(QStringLiteral("BBC News!")),
                 QStringLiteral("BBC_News_"));
        QCOMPARE(pluckerSanitizeDocFile(QStringLiteral("")),
                 QStringLiteral("untitled"));
    }
};

QTEST_MAIN(TestPluckerChannel)
#include "tst_pluckerchannel.moc"
