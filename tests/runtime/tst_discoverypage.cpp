// tests/runtime/tst_discoverypage.cpp
#include <QtTest/QtTest>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QPromise>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/discoverypage.h"
#include "app/wizard/discoveryrow.h"
#include "app/wizard/wizardstate.h"

#include <backendregistry.h>
#include <backendcontribution.h>
#include <iprovider.h>
#include <collectioninfo.h>

using WildPalms::Wizard::DiscoveryPage;
using WildPalms::Wizard::DiscoveryRow;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::WizardAccount;
using WildPalms::Wizard::TargetKind;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendContribution;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::CollectionInfo;

// ---- Stubs ----

namespace {

class StubProvider : public IProvider {
    Q_OBJECT
public:
    explicit StubProvider(QObject *parent = nullptr)
        : IProvider(parent), m_connectResult(true) {}

    void setConnectResult(bool ok) { m_connectResult = ok; }
    void setCollections(const QList<CollectionInfo> &c) { m_collections = c; }

    QString id() const override { return QStringLiteral("stub-id"); }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; p.start();
        p.addResult(m_connectResult); p.finish();
        m_connected = m_connectResult;
        if (m_connectResult) emit collectionsChanged();
        return p.future();
    }
    void disconnect() override { m_connected = false; }
    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return m_collections; }
    std::unique_ptr<Kalburator::Sync::IBlobBackend> createBackend(const QString &) override {
        return nullptr;
    }

private:
    bool m_connectResult = true;
    bool m_connected = false;
    QList<CollectionInfo> m_collections;
};

class StubContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent = nullptr) const override {
        auto p = std::make_unique<StubProvider>(parent);
        p->setConnectResult(s_nextConnectResult);
        p->setCollections(s_nextCollections);
        return p;
    }

    static bool                   s_nextConnectResult;
    static QList<CollectionInfo>  s_nextCollections;
};
bool                  StubContribution::s_nextConnectResult = true;
QList<CollectionInfo> StubContribution::s_nextCollections;

WizardState makeStateWithOneRemote() {
    WizardState s;
    WizardAccount acc;
    acc.id   = QStringLiteral("acc-1");
    acc.kind = QStringLiteral("stub");
    acc.config.id   = QStringLiteral("acc-1");
    acc.config.type = QStringLiteral("stub");
    acc.config.displayName = QStringLiteral("Stub Account");
    s.accounts.append(acc);

    MappingSpec m;
    m.pluginId   = QStringLiteral("calendar");
    m.kind       = TargetKind::Account;
    m.accountRef = acc.id;
    s.mappings.append(m);
    return s;
}

} // namespace

class TstDiscoveryPage : public QObject {
    Q_OBJECT
private slots:
    void init();
    void successPopulatesPicker();
    void emptyResultBlocksFinishWithRetry();
    void failureBlocksFinishWithRetry();
    void chosenCollectionWritesToState();

private:
    BackendRegistry m_registry;
};

void TstDiscoveryPage::init()
{
    // Re-register a fresh stub contribution each test (registry persists
    // across slots within the class instance).
    m_registry.unregisterContribution(QStringLiteral("stub"));
    m_registry.registerContribution(std::make_shared<StubContribution>());
    StubContribution::s_nextConnectResult = true;
    StubContribution::s_nextCollections.clear();
}

void TstDiscoveryPage::successPopulatesPicker()
{
    CollectionInfo ci;
    ci.id   = QStringLiteral("personal");
    ci.name = QStringLiteral("Personal");
    StubContribution::s_nextCollections = { ci };

    auto s = makeStateWithOneRemote();
    DiscoveryPage page(&m_registry, &s);
    page.initializePage();
    QTest::qWait(50);   // let the QFutureWatcher resolve

    auto rows = page.findChildren<DiscoveryRow*>();
    QCOMPARE(rows.size(), 1);
    auto *list = rows.first()->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), QStringLiteral("Personal"));
}

void TstDiscoveryPage::emptyResultBlocksFinishWithRetry()
{
    StubContribution::s_nextCollections.clear();
    auto s = makeStateWithOneRemote();
    DiscoveryPage page(&m_registry, &s);
    page.initializePage();
    QTest::qWait(50);

    QVERIFY(!page.isComplete());
    auto rows = page.findChildren<DiscoveryRow*>();
    QCOMPARE(rows.size(), 1);
    auto *retry = rows.first()->findChild<QPushButton*>(QStringLiteral("retry"));
    QVERIFY(retry);
    QVERIFY(!retry->isHidden());   // visible flag set; offscreen platform never realizes the widget
}

void TstDiscoveryPage::failureBlocksFinishWithRetry()
{
    StubContribution::s_nextConnectResult = false;
    auto s = makeStateWithOneRemote();
    DiscoveryPage page(&m_registry, &s);
    page.initializePage();
    QTest::qWait(50);

    QVERIFY(!page.isComplete());
    auto rows = page.findChildren<DiscoveryRow*>();
    auto *retry = rows.first()->findChild<QPushButton*>(QStringLiteral("retry"));
    QVERIFY(retry);
    QVERIFY(!retry->isHidden());   // visible flag set; offscreen platform never realizes the widget
}

void TstDiscoveryPage::chosenCollectionWritesToState()
{
    CollectionInfo ci;
    ci.id   = QStringLiteral("personal");
    ci.name = QStringLiteral("Personal");
    StubContribution::s_nextCollections = { ci };

    auto s = makeStateWithOneRemote();
    DiscoveryPage page(&m_registry, &s);
    page.initializePage();
    QTest::qWait(50);

    auto rows = page.findChildren<DiscoveryRow*>();
    auto *list = rows.first()->findChild<QListWidget*>();
    list->setCurrentRow(0);
    QVERIFY(page.isComplete());
    QVERIFY(page.validatePage());
    QCOMPARE(s.mappings[0].collectionId, QStringLiteral("personal"));
}

WILDPALMS_QTEST_MAIN(TstDiscoveryPage)
#include "tst_discoverypage.moc"
