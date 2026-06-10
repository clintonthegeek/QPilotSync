// tests/runtime/tst_kf6mainwindow_newprofile.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "../wildpalms_qtest_main.h"

#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/app/wizard/wizardstate.h"
#include "../../src/app/wizard/newprofilewizard.h"
#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"

#include <KSharedConfig>

class TstKf6MainWindowNewProfile : public QObject {
    Q_OBJECT
private slots:
    void cancelledWizardCreatesNothing();
    void allLocalWizardCreatesProfileWithNoMappingRows();
    void remoteWizardWritesOneWildcardRowAndOneAccount();
};

namespace {
std::unique_ptr<WildPalms::Runtime::ProfileRegistry>
makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/wprc"));
    auto r = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path() + QStringLiteral("/wp-root"));
    return r;
}
} // namespace

void TstKf6MainWindowNewProfile::cancelledWizardCreatesNothing()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto reg = makeRegistry(tmp);

    KF6MainWindow win;
    win.setProfileRegistryForTest(std::move(reg));

    // Stub returns empty-name Result (cancelled).
    win.setRunProfileWizardForTest([]() {
        return WildPalms::Wizard::Result{};
    });

    const auto *p = win.profileRegistryForTest();
    QCOMPARE(p->entries().size(), 0);
    win.runNewProfileForTest();
    QCOMPARE(p->entries().size(), 0);
}

void TstKf6MainWindowNewProfile::allLocalWizardCreatesProfileWithNoMappingRows()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto reg = makeRegistry(tmp);

    KF6MainWindow win;
    win.setProfileRegistryForTest(std::move(reg));

    // Stub returns an all-local Result.
    win.setRunProfileWizardForTest([]() {
        WildPalms::Wizard::Result r;
        r.state.profileName = QStringLiteral("Local");
        for (const auto &pid : { QStringLiteral("calendar"),
                                  QStringLiteral("contacts"),
                                  QStringLiteral("memo"),
                                  QStringLiteral("todo") }) {
            WildPalms::Wizard::MappingSpec m;
            m.pluginId = pid;
            m.kind     = WildPalms::Wizard::TargetKind::RawFiles;
            r.state.mappings.append(m);
        }
        return r;
    });

    win.runNewProfileForTest();

    const auto *p = win.profileRegistryForTest();
    QCOMPARE(p->entries().size(), 1);
    const auto entry = p->entries().first();
    QCOMPARE(entry.name, QStringLiteral("Local"));

    // The profile.conf should exist with no mapping rows (all RawFiles =>
    // no persisted rows; finishConnect auto-generates them at first sync).
    Profile prof(entry.path);
    QVERIFY(prof.load());
    QCOMPARE(prof.name(), QStringLiteral("Local"));
    QCOMPARE(prof.accounts().size(), 0);
}

void TstKf6MainWindowNewProfile::remoteWizardWritesOneWildcardRowAndOneAccount()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto reg = makeRegistry(tmp);

    KF6MainWindow win;
    win.setProfileRegistryForTest(std::move(reg));

    win.setRunProfileWizardForTest([]() {
        WildPalms::Wizard::Result r;
        r.state.profileName = QStringLiteral("Remote");

        WildPalms::Wizard::WizardAccount acc;
        acc.id   = QStringLiteral("acc-uuid");
        acc.kind = QStringLiteral("caldav");
        acc.config.displayName = QStringLiteral("Fastmail");
        acc.config.connectionParams.insert(QStringLiteral("url"),
            QStringLiteral("https://caldav.fastmail.com/"));
        r.state.accounts.append(acc);

        WildPalms::Wizard::MappingSpec cal;
        cal.pluginId     = QStringLiteral("calendar");
        cal.kind         = WildPalms::Wizard::TargetKind::Account;
        cal.accountRef   = acc.id;
        cal.collectionId = QStringLiteral("Personal");
        r.state.mappings.append(cal);

        for (const auto &pid : { QStringLiteral("contacts"),
                                  QStringLiteral("memo"),
                                  QStringLiteral("todo") }) {
            WildPalms::Wizard::MappingSpec m;
            m.pluginId = pid;
            m.kind     = WildPalms::Wizard::TargetKind::RawFiles;
            r.state.mappings.append(m);
        }
        return r;
    });

    win.runNewProfileForTest();

    const auto entry = win.profileRegistryForTest()->entries().first();
    Profile prof(entry.path);
    QVERIFY(prof.load());
    QCOMPARE(prof.name(), QStringLiteral("Remote"));

    // One account written.
    QCOMPARE(prof.accounts().size(), 1);
    QCOMPARE(prof.accounts().first().id, QStringLiteral("acc-uuid"));
    QCOMPARE(prof.accounts().first().displayName, QStringLiteral("Fastmail"));

    // One wildcard mapping row.
    const auto mappingsJson = prof.syncMappingsJson();
    QCOMPARE(mappingsJson.size(), 1);
    const auto row = mappingsJson.first().toObject();
    QCOMPARE(row[QStringLiteral("sourceBackend")].toString(),
             QStringLiteral("calendar"));
    QCOMPARE(row[QStringLiteral("sourceCalendar")].toString(),
             QString());                                     // wildcard
    QCOMPARE(row[QStringLiteral("targetBackend")].toString(),
             QStringLiteral("acc-uuid"));
    QCOMPARE(row[QStringLiteral("targetCalendar")].toString(),
             QStringLiteral("Personal"));
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowNewProfile)
#include "tst_kf6mainwindow_newprofile.moc"
