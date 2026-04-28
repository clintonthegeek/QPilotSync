#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "pluckerchannel.h"
#include "pluckerfetcher.h"

using namespace WildPalms::PluckerPlugin;

class TestPluckerFetcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_fixtureDir = QStringLiteral(PLUCKER_FIXTURE_DIR);
    }

    void fetch_success_returnsPdbBytes()
    {
        PluckerChannel ch;
        ch.id      = QStringLiteral("c1");
        ch.name    = QStringLiteral("Channel One");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QDir(m_fixtureDir).filePath(
            QStringLiteral("spider_stub.py")));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/10000);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.errorMessage.isEmpty());
        QCOMPARE(result.pdbBytes, QByteArray("PLUCKER_TEST"));
        QCOMPARE(result.docFile, QStringLiteral("Channel_One"));
    }

    void fetch_nonZeroExit_reportsFailure()
    {
        PluckerChannel ch;
        ch.name    = QStringLiteral("Channel Two");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QDir(m_fixtureDir).filePath(
            QStringLiteral("spider_fail.py")));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/10000);
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(result.pdbBytes.isEmpty());
    }

    void fetch_timeout_reportsFailure()
    {
        PluckerChannel ch;
        ch.name    = QStringLiteral("Channel Hang");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QDir(m_fixtureDir).filePath(
            QStringLiteral("spider_hang.py")));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/500);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("timeout"),
                                              Qt::CaseInsensitive));
    }

    void fetch_missingScript_reportsFailure()
    {
        PluckerChannel ch;
        ch.name    = QStringLiteral("X");
        ch.homeUrl = QStringLiteral("https://example.com");

        PluckerFetcher fetcher;
        fetcher.setSpiderScriptPath(QStringLiteral("/no/such/path.py"));

        const auto result = fetcher.fetch(ch, /*timeoutMs=*/5000);
        QVERIFY(!result.success);
    }

private:
    QString m_fixtureDir;
};

QTEST_MAIN(TestPluckerFetcher)
#include "tst_pluckerfetcher.moc"
