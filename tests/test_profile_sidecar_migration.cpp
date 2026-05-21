/**
 * @file test_profile_sidecar_migration.cpp
 * @brief Tests for Profile one-shot .wildpalms.providers sidecar migration (K.8b T10)
 *
 * Verifies that Profile::load() migrates the legacy AccountController sidecar
 * into the [accounts] subgroup on first load, renames the sidecar, and is
 * idempotent (does not re-migrate when accounts are already populated).
 */

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "profile.h"
#include "backendconfiguration.h"

#include <KConfig>
#include <KConfigGroup>

using Kalburator::Sync::BackendConfiguration;

// Helper: write a .wildpalms.providers sidecar in the same format
// AccountController::persist() produces (KConfig, [Providers]/<id> with kind +
// displayName + connection params).
static void writeSidecar(const QString &path,
                         const QList<BackendConfiguration> &providers)
{
    KConfig cfg(path, KConfig::SimpleConfig);
    KConfigGroup root = cfg.group(QStringLiteral("Providers"));
    for (const auto &p : providers) {
        KConfigGroup sub = root.group(p.id);
        sub.writeEntry("kind", p.type);
        sub.writeEntry("displayName", p.displayName);
        for (auto it = p.connectionParams.constBegin();
             it != p.connectionParams.constEnd(); ++it) {
            sub.writeEntry(it.key(), it.value().toString());
        }
    }
    cfg.sync();
}

class TestProfileSidecarMigration : public QObject
{
    Q_OBJECT

private slots:
    void happyPath();
    void idempotenceWhenAccountsAlreadyPopulated();
    void emptyProvidersSidecarNotMigrated();
};

// --- Happy path -----------------------------------------------------------
// Seeds a .wildpalms.providers file, creates a fresh Profile, calls load(),
// asserts the migrated provider appears in accounts(), and that the sidecar
// has been renamed.

void TestProfileSidecarMigration::happyPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");
    QDir().mkpath(profilePath);

    // Write a minimal .wildpalms.conf so Profile::load() proceeds
    // (it returns false if the config file is absent).
    {
        Profile p(profilePath);
        QVERIFY(p.initialize());
        // initialize() calls save() which writes .wildpalms.conf
    }

    // Seed sidecar
    const QString sidecarPath =
        profilePath + QStringLiteral("/.wildpalms.providers");
    BackendConfiguration provider;
    provider.id = QStringLiteral("caldav-primary");
    provider.type = QStringLiteral("caldav");
    provider.displayName = QStringLiteral("Primary CalDAV");
    provider.connectionParams[QStringLiteral("url")] =
        QStringLiteral("https://example.com/dav");
    provider.connectionParams[QStringLiteral("username")] =
        QStringLiteral("alice");
    writeSidecar(sidecarPath, {provider});
    QVERIFY(QFile::exists(sidecarPath));

    // Load fresh Profile — should trigger migration
    Profile p2(profilePath);
    QVERIFY(p2.load());

    // Sidecar provider should now be in accounts
    const auto accts = p2.accounts();
    QCOMPARE(accts.size(), 1);
    QCOMPARE(accts.first().id, QStringLiteral("caldav-primary"));
    QCOMPARE(accts.first().type, QStringLiteral("caldav"));
    QCOMPARE(accts.first().displayName, QStringLiteral("Primary CalDAV"));
    QCOMPARE(accts.first().connectionParams.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.com/dav"));
    QCOMPARE(accts.first().connectionParams.value(QStringLiteral("username")).toString(),
             QStringLiteral("alice"));

    // Sidecar must be renamed (no longer at original path)
    QVERIFY(!QFile::exists(sidecarPath));

    // A .migrated.* file should exist
    const QStringList entries =
        QDir(profilePath).entryList(QStringList{QStringLiteral(".wildpalms.providers.migrated.*")},
                                    QDir::Files | QDir::Hidden);
    QVERIFY(!entries.isEmpty());
}

// --- Idempotence ----------------------------------------------------------
// If accounts are already in the conf, the sidecar must NOT be consumed,
// even if it exists.

void TestProfileSidecarMigration::idempotenceWhenAccountsAlreadyPopulated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");
    QDir().mkpath(profilePath);

    // Write a profile with an existing account
    {
        Profile p(profilePath);
        QVERIFY(p.initialize());
        BackendConfiguration existing;
        existing.id = QStringLiteral("already-there");
        existing.type = QStringLiteral("local");
        p.saveAccount(existing);
        QVERIFY(p.save());
    }

    // Seed sidecar with a DIFFERENT provider
    const QString sidecarPath =
        profilePath + QStringLiteral("/.wildpalms.providers");
    BackendConfiguration legacyProvider;
    legacyProvider.id = QStringLiteral("legacy-provider");
    legacyProvider.type = QStringLiteral("caldav");
    writeSidecar(sidecarPath, {legacyProvider});
    QVERIFY(QFile::exists(sidecarPath));

    // Load — should NOT migrate since accounts already populated
    Profile p2(profilePath);
    QVERIFY(p2.load());

    const auto accts = p2.accounts();
    QCOMPARE(accts.size(), 1);
    QCOMPARE(accts.first().id, QStringLiteral("already-there"));

    // Sidecar must still exist (not consumed)
    QVERIFY(QFile::exists(sidecarPath));
}

// --- Empty sidecar -------------------------------------------------------
// If the sidecar exists but has no providers (zero-length or header only),
// we skip migration — no accounts are populated, sidecar is not renamed.

void TestProfileSidecarMigration::emptyProvidersSidecarNotMigrated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");
    QDir().mkpath(profilePath);

    {
        Profile p(profilePath);
        QVERIFY(p.initialize());
    }

    // Seed an empty sidecar by touching the file directly (KConfig won't
    // create a file for an empty config, so we use QFile).
    const QString sidecarPath =
        profilePath + QStringLiteral("/.wildpalms.providers");
    {
        QFile f(sidecarPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // Write a valid-but-empty KConfig [Providers] section with no sub-groups
        f.write("[Providers]\n");
        f.close();
    }
    QVERIFY(QFile::exists(sidecarPath));

    Profile p2(profilePath);
    QVERIFY(p2.load());

    // No accounts migrated (sidecar had no provider sub-groups)
    QCOMPARE(p2.accounts().size(), 0);

    // Sidecar remains (nothing to archive — save() not called)
    QVERIFY(QFile::exists(sidecarPath));
}

QTEST_GUILESS_MAIN(TestProfileSidecarMigration)
#include "test_profile_sidecar_migration.moc"
