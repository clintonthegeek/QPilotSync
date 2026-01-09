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
    void testFingerprintRegistryKey();
    void testFingerprintFromRegistryKey();

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

    // ========== Conduit Settings Tests ==========
    void testConduitEnabledDefault();
    void testSetConduitEnabled();
    void testConduitSettings();

    // ========== Persistence Tests ==========
    void testInitialize();
    void testSaveAndLoad();

    // ========== Validity Tests ==========
    void testIsValidWithValidPath();
    void testIsValidWithInvalidPath();

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

void TestProfile::testFingerprintRegistryKey()
{
    DeviceFingerprint fp;
    fp.userId = 12345;
    fp.userName = "TestUser";

    QString key = fp.registryKey();
    QVERIFY(key.contains("12345"));
    QVERIFY(key.contains("TestUser"));
}

void TestProfile::testFingerprintFromRegistryKey()
{
    DeviceFingerprint original;
    original.userId = 12345;
    original.userName = "TestUser";

    QString key = original.registryKey();
    DeviceFingerprint parsed = DeviceFingerprint::fromRegistryKey(key);

    QCOMPARE(parsed.userId, original.userId);
    QCOMPARE(parsed.userName, original.userName);
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
    Profile profile(m_tempDir->path());
    QString configPath = profile.configFilePath();

    QVERIFY(configPath.contains(m_tempDir->path()));
    QVERIFY(configPath.endsWith(".qpilotsync.conf"));
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

// ========== Conduit Settings Tests ==========

void TestProfile::testConduitEnabledDefault()
{
    Profile profile(m_tempDir->path());
    // Default conduits should be enabled
    QVERIFY(profile.conduitEnabled("memos"));
    QVERIFY(profile.conduitEnabled("contacts"));
}

void TestProfile::testSetConduitEnabled()
{
    Profile profile(m_tempDir->path());

    profile.setConduitEnabled("memos", false);
    QVERIFY(!profile.conduitEnabled("memos"));

    profile.setConduitEnabled("memos", true);
    QVERIFY(profile.conduitEnabled("memos"));
}

void TestProfile::testConduitSettings()
{
    Profile profile(m_tempDir->path());

    QJsonObject settings;
    settings["option1"] = true;
    settings["option2"] = "value";

    profile.setConduitSettings("memos", settings);

    QJsonObject retrieved = profile.conduitSettings("memos");
    QCOMPARE(retrieved["option1"].toBool(), true);
    QCOMPARE(retrieved["option2"].toString(), QString("value"));
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
        profile.setConduitEnabled("memos", false);
        profile.save();
    }

    // Load into new profile object
    {
        Profile profile(profilePath);
        profile.load();

        QCOMPARE(profile.name(), QString("Test Profile"));
        QCOMPARE(profile.devicePath(), QString("/dev/ttyUSB1"));
        QCOMPARE(profile.baudRate(), QString("57600"));
        QVERIFY(!profile.conduitEnabled("memos"));
    }
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

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
