// Substrate A2: everything-is-a-provider — local folders as a credential-less
// contribution in the same registry as DAV/Akonadi.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "../wildpalms_qtest_main.h"

#include "runtime/localfolderprovider.h"
#include "runtime/localfoldercontribution.h"
#include <backendconfiguration.h>
#include <collectioninfo.h>

using namespace WildPalms::Runtime;
using Kalburator::Sync::BackendConfiguration;

class TstLocalFolderProvider : public QObject {
    Q_OBJECT
private slots:
    void contributionCreatesProvider();
    void connectListsConfiguredFolders();
    void createBackendDispatchesPerDomain();
    void missingFolderFailsConnect();
};

static BackendConfiguration cfgWith(const QList<QPair<QString,QString>> &entries)
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("lf-1");
    cfg.type = QStringLiteral("local-folder");
    cfg.displayName = QStringLiteral("My folders");
    QVariantList list;
    for (const auto &e : entries) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), e.first);
        m.insert(QStringLiteral("domain"), e.second);
        list.append(m);
    }
    cfg.connectionParams.insert(QStringLiteral("entries"), list);
    return cfg;
}

void TstLocalFolderProvider::contributionCreatesProvider()
{
    LocalFolderContribution contrib;
    QCOMPARE(contrib.backendType(), QStringLiteral("local-folder"));
    auto provider = contrib.createProvider(nullptr);
    QVERIFY(provider);
    QCOMPARE(provider->kind(), QStringLiteral("local-folder"));
}

void TstLocalFolderProvider::connectListsConfiguredFolders()
{
    QTemporaryDir d1, d2;
    LocalFolderProvider p;
    p.load(cfgWith({ { d1.path(), QStringLiteral("note") },
                     { d2.path(), QStringLiteral("calendar") } }));
    auto f = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
    QVERIFY(f.resultAt(0));
    QVERIFY(p.isConnected());
    const auto cols = p.collections();
    QCOMPARE(cols.size(), 2);
    QCOMPARE(cols[0].type, QStringLiteral("note"));      // memos match note domain
    QCOMPARE(cols[1].type, QStringLiteral("calendar"));
    QVERIFY(!cols[0].readOnly);
}

void TstLocalFolderProvider::createBackendDispatchesPerDomain()
{
    QTemporaryDir d1, d2;
    LocalFolderProvider p;
    p.load(cfgWith({ { d1.path(), QStringLiteral("note") },
                     { d2.path(), QStringLiteral("todo") } }));
    auto f = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
    const auto cols = p.collections();
    auto specs = p.createBackends();
    QCOMPARE(specs.size(), 2);
    // Specs come back in entry order; each hosts exactly one collection.
    QCOMPARE(specs[0].domainId, cols[0].id);   // MarkdownFilesBackend
    QCOMPARE(specs[1].domainId, cols[1].id);   // RawFilesBackend fallback
    QVERIFY(specs[0].backend);
    QVERIFY(specs[1].backend);
}

void TstLocalFolderProvider::missingFolderFailsConnect()
{
    LocalFolderProvider p;
    p.load(cfgWith({ { QStringLiteral("/nonexistent/path/xyz"),
                       QStringLiteral("note") } }));
    auto f = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
    QVERIFY(!f.resultAt(0));
    QVERIFY(!p.isConnected());
    QVERIFY(!p.lastError().isEmpty());
}

WILDPALMS_QTEST_MAIN(TstLocalFolderProvider)
#include "tst_localfolder_provider.moc"
