#include <QtTest/QtTest>

#include "runtime/backendpluginmanager.h"

using WildPalms::BackendPluginManager;

class TestBackendPluginManager : public QObject
{
    Q_OBJECT
private slots:
    void discoverWithEmptyPathYieldsEmptyCatalogue();
    // Populated in later steps / later task.
};

void TestBackendPluginManager::discoverWithEmptyPathYieldsEmptyCatalogue()
{
    BackendPluginManager mgr(nullptr, nullptr, nullptr);
    // Point at a subdir that definitely holds no plugins.
    mgr.setPluginSubdir(QStringLiteral("wildpalms_e8_nonexistent"));
    mgr.discoverPlugins();

    QCOMPARE(mgr.catalogue().size(), 0);
    QCOMPARE(mgr.plugins().size(), 0);
}

QTEST_MAIN(TestBackendPluginManager)
#include "tst_backendpluginmanager.moc"
