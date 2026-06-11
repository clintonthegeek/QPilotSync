/**
 * @file test_profile.cpp
 * @brief Unit tests for Profile class
 *
 * Tests the sync profile settings management.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include <QTemporaryDir>
#include <QFile>
#include "profile.h"

class TestProfile : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== DeviceFingerprint Tests ==========
    void testFingerprintEmpty();
    void testFingerprintValid();
    void testFingerprintMatches();
    void testFingerprintMatchesByUserId();
    void testFingerprintMatchesByUsername();
    void testFingerprintDisplayString();

    // ========== Profile Construction Tests ==========
    void testDefaultConstruction();
    void testConstructionWithPath();

    // ========== Profile Path Tests ==========
    void testSetSyncFolderPath();
    void testConfigFilePath();
    void testStateDirectoryPath();
    void testInstallFolderPath();

    // ========== Device Settings Tests ==========
    void testDevicePathDefault();
    void testSetDevicePath();
    void testBaudRateDefault();
    void testSetBaudRate();
    void testSetDeviceFingerprint();
    void testHasRegisteredDevice();
    void testConnectionModeDefault();
    void testSetConnectionMode();
    void testAutoSyncOnConnectDefault();
    void testSetAutoSyncOnConnect();
    void testDefaultSyncTypeDefault();
    void testSetDefaultSyncType();

    // ========== Persistence Tests ==========
    void testInitialize();
    void testSaveAndLoad();
    void testSaveCreatesThreeFiles();
    void testRoundTripBasic();

    // ========== Validity Tests ==========
    void testIsValidWithValidPath();
    void testIsValidWithInvalidPath();

    // ========== F.1a T8: id / defaultPathForId / schemaVersion ==========
    void testDefaultPathForId();
    void testIdFromBasename();

    // ========== Substrate A3/A4: desired categories + initial-sync flag ==========
    void desiredCategoryNames_roundTrip();
    void desiredCategoryNames_capsAtFifteen();
    void initialSyncPending_roundTrip();

private:
    QTemporaryDir *m_tempDir;
};

void TestProfile::initTestCase()
{
    qDebug() << "Starting Profile tests";
}

void TestProfile::cleanupTestCase()
{
    qDebug() << "Profile tests complete";
}

void TestProfile::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestProfile::cleanup()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

// ========== DeviceFingerprint Tests ==========

void TestProfile::testFingerprintEmpty()
{
    DeviceFingerprint fp;
    QVERIFY(fp.isEmpty());
    QVERIFY(!fp.isValid());
}

void TestProfile::testFingerprintValid()
{
    DeviceFingerprint fp;
    fp.userId = 12345;
    fp.userName = "TestUser";

    QVERIFY(!fp.isEmpty());
    QVERIFY(fp.isValid());
}

void TestProfile::testFingerprintMatches()
{
    DeviceFingerprint fp1;
    fp1.userId = 12345;
    fp1.userName = "TestUser";

    DeviceFingerprint fp2;
    fp2.userId = 12345;
    fp2.userName = "TestUser";

    QVERIFY(fp1.matches(fp2));
}

void TestProfile::testFingerprintMatchesByUserId()
{
    DeviceFingerprint fp1;
    fp1.userId = 12345;
    fp1.userName = "User1";

    DeviceFingerprint fp2;
    fp2.userId = 12345;
    fp2.userName = "User2";  // Different username

    // Should match by userId even if username differs
    QVERIFY(fp1.matches(fp2));
}

void TestProfile::testFingerprintMatchesByUsername()
{
    DeviceFingerprint fp1;
    fp1.userId = 0;  // No userId
    fp1.userName = "TestUser";

    DeviceFingerprint fp2;
    fp2.userId = 0;  // No userId
    fp2.userName = "TestUser";

    // Should match by username when no userId
    QVERIFY(fp1.matches(fp2));
}

void TestProfile::testFingerprintDisplayString()
{
    DeviceFingerprint fp1;
    fp1.userId = 12345;
    fp1.userName = "TestUser";

    QString display = fp1.displayString();
    QVERIFY(display.contains("TestUser"));
    QVERIFY(display.contains("12345"));

    // Empty fingerprint
    DeviceFingerprint fp2;
    QVERIFY(fp2.displayString().isEmpty());
}

// ========== Profile Construction Tests ==========

void TestProfile::testDefaultConstruction()
{
    Profile profile;
    QVERIFY(profile.syncFolderPath().isEmpty());
}

void TestProfile::testConstructionWithPath()
{
    Profile profile(m_tempDir->path());
    QCOMPARE(profile.syncFolderPath(), m_tempDir->path());
}

// ========== Profile Path Tests ==========

void TestProfile::testSetSyncFolderPath()
{
    Profile profile;
    profile.setSyncFolderPath(m_tempDir->path());
    QCOMPARE(profile.syncFolderPath(), m_tempDir->path());
}

void TestProfile::testConfigFilePath()
{
    // profile.conf is the primary config file in the sync folder.
    // After initialize(), it must exist at <path>/profile.conf.
    const QString profilePath = m_tempDir->path() + QStringLiteral("/cp-test");
    Profile profile(profilePath);
    QVERIFY(profile.initialize());
    QVERIFY(QFile::exists(profilePath + QStringLiteral("/profile.conf")));
}

void TestProfile::testStateDirectoryPath()
{
    Profile profile(m_tempDir->path());
    QString statePath = profile.stateDirectoryPath();

    QVERIFY(statePath.contains(m_tempDir->path()));
}

void TestProfile::testInstallFolderPath()
{
    Profile profile(m_tempDir->path());
    QString installPath = profile.installFolderPath();

    QVERIFY(installPath.contains(m_tempDir->path()));
}

// ========== Device Settings Tests ==========

void TestProfile::testDevicePathDefault()
{
    Profile profile(m_tempDir->path());
    // Default should not be empty
    QVERIFY(!profile.devicePath().isEmpty());
}

void TestProfile::testSetDevicePath()
{
    Profile profile(m_tempDir->path());
    profile.setDevicePath("/dev/ttyUSB1");
    QCOMPARE(profile.devicePath(), QString("/dev/ttyUSB1"));
}

void TestProfile::testBaudRateDefault()
{
    Profile profile(m_tempDir->path());
    // Default should not be empty
    QVERIFY(!profile.baudRate().isEmpty());
}

void TestProfile::testSetBaudRate()
{
    Profile profile(m_tempDir->path());
    profile.setBaudRate("57600");
    QCOMPARE(profile.baudRate(), QString("57600"));
}

void TestProfile::testSetDeviceFingerprint()
{
    Profile profile(m_tempDir->path());

    DeviceFingerprint fp;
    fp.userId = 98765;
    fp.userName = "MyPalm";

    profile.setDeviceFingerprint(fp);

    DeviceFingerprint retrieved = profile.deviceFingerprint();
    QCOMPARE(retrieved.userId, fp.userId);
    QCOMPARE(retrieved.userName, fp.userName);
}

void TestProfile::testHasRegisteredDevice()
{
    Profile profile(m_tempDir->path());

    // Initially no registered device
    QVERIFY(!profile.hasRegisteredDevice());

    // After setting fingerprint
    DeviceFingerprint fp;
    fp.userId = 12345;
    profile.setDeviceFingerprint(fp);

    QVERIFY(profile.hasRegisteredDevice());
}

void TestProfile::testConnectionModeDefault()
{
    Profile profile(m_tempDir->path());
    QCOMPARE(profile.connectionMode(), ConnectionMode::KeepAlive);
}

void TestProfile::testSetConnectionMode()
{
    Profile profile(m_tempDir->path());
    profile.setConnectionMode(ConnectionMode::DisconnectAfterSync);
    QCOMPARE(profile.connectionMode(), ConnectionMode::DisconnectAfterSync);
}

void TestProfile::testAutoSyncOnConnectDefault()
{
    Profile profile(m_tempDir->path());
    QVERIFY(!profile.autoSyncOnConnect());
}

void TestProfile::testSetAutoSyncOnConnect()
{
    Profile profile(m_tempDir->path());
    profile.setAutoSyncOnConnect(true);
    QVERIFY(profile.autoSyncOnConnect());
}

void TestProfile::testDefaultSyncTypeDefault()
{
    Profile profile(m_tempDir->path());
    QCOMPARE(profile.defaultSyncType(), QString("hotsync"));
}

void TestProfile::testSetDefaultSyncType()
{
    Profile profile(m_tempDir->path());
    profile.setDefaultSyncType("fullsync");
    QCOMPARE(profile.defaultSyncType(), QString("fullsync"));
}

// ========== Persistence Tests ==========

void TestProfile::testInitialize()
{
    Profile profile(m_tempDir->path() + "/newprofile");
    bool result = profile.initialize();

    QVERIFY(result);
    QVERIFY(profile.exists());
}

void TestProfile::testSaveAndLoad()
{
    QString profilePath = m_tempDir->path() + "/saveload";

    // Create and configure profile
    {
        Profile profile(profilePath);
        profile.initialize();
        profile.setName("Test Profile");
        profile.setDevicePath("/dev/ttyUSB1");
        profile.setBaudRate("57600");
        profile.save();
    }

    // Load into new profile object
    {
        Profile profile(profilePath);
        profile.load();

        QCOMPARE(profile.name(), QString("Test Profile"));
        QCOMPARE(profile.devicePath(), QString("/dev/ttyUSB1"));
        QCOMPARE(profile.baudRate(), QString("57600"));
    }
}

void TestProfile::testSaveCreatesThreeFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Profile p;
    p.setSyncFolderPath(tmp.path() + QStringLiteral("/profile1"));
    p.setName(QStringLiteral("Test"));
    QVERIFY(p.save());

    QVERIFY(QFile::exists(p.syncFolderPath() + QStringLiteral("/profile.conf")));
    QVERIFY(QFile::exists(p.syncFolderPath() + QStringLiteral("/accounts.conf")));
    QVERIFY(QFile::exists(p.syncFolderPath() + QStringLiteral("/mappings.conf")));
}

void TestProfile::testRoundTripBasic()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/profile1");

    Profile p;
    p.setSyncFolderPath(dir);
    p.setName(QStringLiteral("RoundTrip"));
    p.setBaudRate(QStringLiteral("57600"));
    QVERIFY(p.save());

    Profile p2;
    p2.setSyncFolderPath(dir);
    QVERIFY(p2.load());
    QCOMPARE(p2.name(), QStringLiteral("RoundTrip"));
    QCOMPARE(p2.baudRate(), QStringLiteral("57600"));
}

// ========== Validity Tests ==========

void TestProfile::testIsValidWithValidPath()
{
    Profile profile(m_tempDir->path());
    QVERIFY(profile.isValid());
}

void TestProfile::testIsValidWithInvalidPath()
{
    Profile profile("/nonexistent/path/that/does/not/exist");
    QVERIFY(!profile.isValid());
}

// ========== F.1a T8: id / defaultPathForId / schemaVersion ==========

void TestProfile::testDefaultPathForId()
{
    const QString p = Profile::defaultPathForId(QStringLiteral("profile5"));
    QVERIFY(p.endsWith(QStringLiteral("/.wildpalms/profile5")));
}

void TestProfile::testIdFromBasename()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/myprofile");
    QVERIFY(QDir().mkpath(dir));

    Profile p;
    p.setSyncFolderPath(dir);
    // Before load(), id is empty.
    QCOMPARE(p.id(), QString());
}

void TestProfile::desiredCategoryNames_roundTrip()
{
    QTemporaryDir dir;
    Profile p(dir.path());
    p.initialize();
    QCOMPARE(p.desiredCategoryNames(QStringLiteral("DatebookDB")), QStringList{});

    const QStringList names{ QStringLiteral("Work"), QStringLiteral("Personal") };
    p.setDesiredCategoryNames(QStringLiteral("DatebookDB"), names);

    Profile reloaded(dir.path());
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.desiredCategoryNames(QStringLiteral("DatebookDB")), names);
}

void TestProfile::desiredCategoryNames_capsAtFifteen()
{
    QTemporaryDir dir;
    Profile p(dir.path());
    p.initialize();
    QStringList sixteen;
    for (int i = 0; i < 16; ++i) sixteen << QStringLiteral("C%1").arg(i);
    p.setDesiredCategoryNames(QStringLiteral("DatebookDB"), sixteen);
    QCOMPARE(p.desiredCategoryNames(QStringLiteral("DatebookDB")).size(), 15);
}

void TestProfile::initialSyncPending_roundTrip()
{
    QTemporaryDir dir;
    Profile p(dir.path());
    p.initialize();
    QVERIFY(!p.initialSyncPending());           // default false
    p.setInitialSyncPending(true);
    Profile reloaded(dir.path());
    QVERIFY(reloaded.load());
    QVERIFY(reloaded.initialSyncPending());
}

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
