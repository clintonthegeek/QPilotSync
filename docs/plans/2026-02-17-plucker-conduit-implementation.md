# Plucker Conduit Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build the Plucker conduit plugin — first IToolConduit implementation — that spiders web content via bundled PyPlucker, produces .pdb files, and installs them on Palm devices during sync.

**Architecture:** PluckerConduit implements IToolConduit (extends IConduit). At sync time, it checks which channels are due, spawns `python3 Spider.py` per channel via QProcess, collects `.pdb` output, and appends paths to `SyncContext::installQueue`. SyncEngine processes the queue after all conduits finish via new `KPilotDeviceLink::installFile()` method.

**Tech Stack:** Qt6, KDE Frameworks 6 (KPluginFactory, KConfig, KLocalizedString), QProcess, pilot-link (pi_file_install), Python 3 (PyPlucker engine)

**Design Doc:** `docs/plans/2026-02-17-plucker-conduit-design.md`

---

## Task 1: Vendor PyPlucker Engine and Viewer PRCs

**Files:**
- Create: `src/plugins/plucker/parser/PyPlucker/` (copy from Wine)
- Create: `src/plugins/plucker/viewer/` (copy PRCs from Wine)

**Step 1: Create plugin directory structure**

```bash
mkdir -p src/plugins/plucker/parser src/plugins/plucker/viewer
```

**Step 2: Copy PyPlucker engine from Wine installation**

```bash
cp -r "$HOME/.wine/drive_c/Program Files (x86)/Plucker/parser/python/PyPlucker" \
      src/plugins/plucker/parser/
```

This copies Spider.py, PluckerDocs.py, TextParser.py, ImageParser.py, and all
other PyPlucker modules. Do NOT copy the `vm/` directory (Windows Python runtime).

**Step 3: Copy viewer PRCs from Wine installation**

```bash
cp "$HOME/.wine/drive_c/Program Files (x86)/Plucker/viewer/palmos/viewer_en.prc" \
   "$HOME/.wine/drive_c/Program Files (x86)/Plucker/viewer/palmos/viewer_hires_en.prc" \
   "$HOME/.wine/drive_c/Program Files (x86)/Plucker/viewer/palmos/SysZLib.prc" \
   "$HOME/.wine/drive_c/Program Files (x86)/Plucker/viewer/palmos/SysZLib_hires.prc" \
   src/plugins/plucker/viewer/
```

**Step 4: Verify Python can import the engine**

```bash
cd src/plugins/plucker && python3 -c "import sys; sys.path.insert(0,'parser'); from PyPlucker import Spider; print('OK')"
```

Expected: `OK` (or import errors we'll need to fix — PyPlucker may need
minor patches for Python 3.10+ compatibility)

**Step 5: Commit**

```bash
git add src/plugins/plucker/parser/ src/plugins/plucker/viewer/
git commit -m "vendor: bundle PyPlucker engine and Palm viewer PRCs"
```

---

## Task 2: PluckerConfig — Data Model and Persistence (TDD)

**Files:**
- Create: `src/plugins/plucker/pluckerconfig.h`
- Create: `src/plugins/plucker/pluckerconfig.cpp`
- Create: `tests/test_pluckerconfig.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the test file**

Create `tests/test_pluckerconfig.cpp`:

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QSettings>
#include "../src/plugins/plucker/pluckerconfig.h"

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
    // lastFetched is null/invalid — never fetched
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
    // Even if never fetched, disabled channels are not due
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
    // doc-file should be a sanitized version of the name
    bool hasDocFile = false;
    for (const QString &arg : args) {
        if (arg.startsWith("--doc-file=")) hasDocFile = true;
    }
    QVERIFY(hasDocFile);
}

QTEST_GUILESS_MAIN(TestPluckerConfig)
#include "test_pluckerconfig.moc"
```

**Step 2: Add test to CMakeLists.txt**

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(test_pluckerconfig test_pluckerconfig.cpp
    ../src/plugins/plucker/pluckerconfig.cpp)
target_link_libraries(test_pluckerconfig Qt::Test Qt::Core)
target_include_directories(test_pluckerconfig PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_pluckerconfig COMMAND test_pluckerconfig)
```

**Step 3: Run tests — verify they fail**

```bash
cmake --build build && ctest --test-dir build -R test_pluckerconfig -v
```

Expected: Build fails (pluckerconfig.h doesn't exist yet)

**Step 4: Write pluckerconfig.h**

Create `src/plugins/plucker/pluckerconfig.h`:

```cpp
#ifndef PLUCKERCONFIG_H
#define PLUCKERCONFIG_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QUuid>

/**
 * @brief Configuration for a single Plucker channel (web content source)
 *
 * Maps 1:1 to a section in the profile config and to PyPlucker CLI flags.
 */
struct PluckerChannel {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    QString homeUrl;

    // Spidering
    int maxDepth = 2;
    bool stayOnHost = false;
    bool depthFirst = false;
    QString userAgent;
    QString urlPattern;

    // Images
    int bpp = 8;
    int maxWidth = 150;
    int maxHeight = 250;
    int altMaxWidth = 450;
    int altMaxHeight = 800;
    bool noImages = false;
    int imageCompressionLimit = 50;

    // Output
    QString compression = QStringLiteral("zlib");
    QString category;

    // Destination
    QString storageMode = QStringLiteral("ram");
    QString cardDirectory;

    // Scheduling
    bool updateEnabled = true;
    int updateFrequency = 1;
    QString updatePeriod = QStringLiteral("days");
    QDateTime lastFetched;
};

/**
 * @brief Manages a list of Plucker channels with profile-scoped persistence
 *
 * Channels are stored in .qpilotsync.conf under [Plucker] and
 * [Plucker-<channelId>] groups using QSettings INI format.
 */
class PluckerConfig
{
public:
    PluckerConfig() = default;

    // Channel management
    void addChannel(const PluckerChannel &channel);
    void updateChannel(const PluckerChannel &channel);
    void removeChannel(const QString &id);
    PluckerChannel channel(const QString &id) const;
    QList<PluckerChannel> channels() const;

    // Persistence (syncPath = profile root folder)
    void load(const QString &syncPath);
    void save(const QString &syncPath);

    // Scheduling
    static bool isDue(const PluckerChannel &channel);
    static QDateTime nextDueTime(const PluckerChannel &channel);

    // CLI arg builder for PyPlucker Spider.py
    static QStringList buildCLIArgs(const PluckerChannel &channel,
                                     const QString &outputDir);

    // Sanitize channel name to a valid filename stem
    static QString sanitizeDocFile(const QString &name);

private:
    QList<PluckerChannel> m_channels;
};

#endif // PLUCKERCONFIG_H
```

**Step 5: Write pluckerconfig.cpp**

Create `src/plugins/plucker/pluckerconfig.cpp`:

```cpp
#include "pluckerconfig.h"
#include <QSettings>
#include <QDir>
#include <QRegularExpression>

// ========== Channel Management ==========

void PluckerConfig::addChannel(const PluckerChannel &channel)
{
    m_channels.append(channel);
}

void PluckerConfig::updateChannel(const PluckerChannel &channel)
{
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].id == channel.id) {
            m_channels[i] = channel;
            return;
        }
    }
}

void PluckerConfig::removeChannel(const QString &id)
{
    m_channels.removeIf([&id](const PluckerChannel &ch) {
        return ch.id == id;
    });
}

PluckerChannel PluckerConfig::channel(const QString &id) const
{
    for (const auto &ch : m_channels) {
        if (ch.id == id) return ch;
    }
    return PluckerChannel();
}

QList<PluckerChannel> PluckerConfig::channels() const
{
    return m_channels;
}

// ========== Persistence ==========

void PluckerConfig::load(const QString &syncPath)
{
    m_channels.clear();
    QString configFile = QDir(syncPath).filePath(
        QStringLiteral(".qpilotsync.conf"));
    QSettings settings(configFile, QSettings::IniFormat);

    settings.beginGroup(QStringLiteral("Plucker"));
    QStringList ids = settings.value(
        QStringLiteral("channelIds")).toStringList();
    settings.endGroup();

    for (const QString &id : ids) {
        settings.beginGroup(QStringLiteral("Plucker-%1").arg(id));

        PluckerChannel ch;
        ch.id = id;
        ch.name = settings.value("name").toString();
        ch.homeUrl = settings.value("homeUrl").toString();
        ch.maxDepth = settings.value("maxDepth", 2).toInt();
        ch.stayOnHost = settings.value("stayOnHost", false).toBool();
        ch.depthFirst = settings.value("depthFirst", false).toBool();
        ch.userAgent = settings.value("userAgent").toString();
        ch.urlPattern = settings.value("urlPattern").toString();
        ch.bpp = settings.value("bpp", 8).toInt();
        ch.maxWidth = settings.value("maxWidth", 150).toInt();
        ch.maxHeight = settings.value("maxHeight", 250).toInt();
        ch.altMaxWidth = settings.value("altMaxWidth", 450).toInt();
        ch.altMaxHeight = settings.value("altMaxHeight", 800).toInt();
        ch.noImages = settings.value("noImages", false).toBool();
        ch.imageCompressionLimit = settings.value(
            "imageCompressionLimit", 50).toInt();
        ch.compression = settings.value(
            "compression", "zlib").toString();
        ch.category = settings.value("category").toString();
        ch.storageMode = settings.value(
            "storageMode", "ram").toString();
        ch.cardDirectory = settings.value("cardDirectory").toString();
        ch.updateEnabled = settings.value(
            "updateEnabled", true).toBool();
        ch.updateFrequency = settings.value(
            "updateFrequency", 1).toInt();
        ch.updatePeriod = settings.value(
            "updatePeriod", "days").toString();
        ch.lastFetched = settings.value("lastFetched").toDateTime();

        settings.endGroup();
        m_channels.append(ch);
    }
}

void PluckerConfig::save(const QString &syncPath)
{
    QString configFile = QDir(syncPath).filePath(
        QStringLiteral(".qpilotsync.conf"));
    QSettings settings(configFile, QSettings::IniFormat);

    // Write channel ID list
    QStringList ids;
    for (const auto &ch : m_channels) {
        ids.append(ch.id);
    }
    settings.beginGroup(QStringLiteral("Plucker"));
    settings.setValue(QStringLiteral("channelIds"), ids);
    settings.endGroup();

    // Write each channel
    for (const auto &ch : m_channels) {
        settings.beginGroup(QStringLiteral("Plucker-%1").arg(ch.id));
        settings.setValue("name", ch.name);
        settings.setValue("homeUrl", ch.homeUrl);
        settings.setValue("maxDepth", ch.maxDepth);
        settings.setValue("stayOnHost", ch.stayOnHost);
        settings.setValue("depthFirst", ch.depthFirst);
        settings.setValue("userAgent", ch.userAgent);
        settings.setValue("urlPattern", ch.urlPattern);
        settings.setValue("bpp", ch.bpp);
        settings.setValue("maxWidth", ch.maxWidth);
        settings.setValue("maxHeight", ch.maxHeight);
        settings.setValue("altMaxWidth", ch.altMaxWidth);
        settings.setValue("altMaxHeight", ch.altMaxHeight);
        settings.setValue("noImages", ch.noImages);
        settings.setValue("imageCompressionLimit",
                          ch.imageCompressionLimit);
        settings.setValue("compression", ch.compression);
        settings.setValue("category", ch.category);
        settings.setValue("storageMode", ch.storageMode);
        settings.setValue("cardDirectory", ch.cardDirectory);
        settings.setValue("updateEnabled", ch.updateEnabled);
        settings.setValue("updateFrequency", ch.updateFrequency);
        settings.setValue("updatePeriod", ch.updatePeriod);
        settings.setValue("lastFetched", ch.lastFetched);
        settings.endGroup();
    }

    settings.sync();
}

// ========== Scheduling ==========

bool PluckerConfig::isDue(const PluckerChannel &channel)
{
    if (!channel.updateEnabled) return false;
    if (!channel.lastFetched.isValid()) return true;  // Never fetched
    return QDateTime::currentDateTime() >= nextDueTime(channel);
}

QDateTime PluckerConfig::nextDueTime(const PluckerChannel &channel)
{
    if (!channel.lastFetched.isValid()) return QDateTime();

    QDateTime next = channel.lastFetched;
    int freq = qMax(1, channel.updateFrequency);

    if (channel.updatePeriod == "hours") {
        next = next.addSecs(freq * 3600);
    } else if (channel.updatePeriod == "days") {
        next = next.addDays(freq);
    } else if (channel.updatePeriod == "weeks") {
        next = next.addDays(freq * 7);
    } else if (channel.updatePeriod == "months") {
        next = next.addMonths(freq);
    } else {
        next = next.addDays(freq);  // Default to days
    }

    return next;
}

// ========== CLI Args ==========

QString PluckerConfig::sanitizeDocFile(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                 QStringLiteral("_"));
    if (safe.isEmpty()) safe = QStringLiteral("untitled");
    return safe;
}

QStringList PluckerConfig::buildCLIArgs(const PluckerChannel &channel,
                                         const QString &outputDir)
{
    QStringList args;

    args << QStringLiteral("--home-url=%1").arg(channel.homeUrl);
    args << QStringLiteral("--doc-name=%1").arg(channel.name);
    args << QStringLiteral("--doc-file=%1").arg(
                sanitizeDocFile(channel.name));
    args << QStringLiteral("--pluckerdir=%1").arg(outputDir);
    args << QStringLiteral("--maxdepth=%1").arg(channel.maxDepth);
    args << QStringLiteral("--bpp=%1").arg(channel.bpp);
    args << QStringLiteral("--maxwidth=%1").arg(channel.maxWidth);
    args << QStringLiteral("--maxheight=%1").arg(channel.maxHeight);
    args << QStringLiteral("--alt_maxwidth=%1").arg(channel.altMaxWidth);
    args << QStringLiteral("--alt_maxheight=%1").arg(channel.altMaxHeight);
    args << QStringLiteral("--compression=%1").arg(channel.compression);

    if (channel.stayOnHost) {
        args << QStringLiteral("--stayonhost");
    }
    if (channel.depthFirst) {
        args << QStringLiteral("--depth-first");
    }
    if (channel.noImages) {
        args << QStringLiteral("--noimages");
    }
    if (!channel.category.isEmpty()) {
        args << QStringLiteral("--category=%1").arg(channel.category);
    }
    if (!channel.userAgent.isEmpty()) {
        args << QStringLiteral("--user-agent=%1").arg(channel.userAgent);
    }
    if (!channel.urlPattern.isEmpty()) {
        args << QStringLiteral("--staybelow=%1").arg(channel.urlPattern);
    }

    args << QStringLiteral("--no-urlinfo");

    return args;
}
```

**Step 6: Build and run tests**

```bash
cmake --build build && ctest --test-dir build -R test_pluckerconfig -v
```

Expected: All 8 tests PASS

**Step 7: Commit**

```bash
git add src/plugins/plucker/pluckerconfig.h src/plugins/plucker/pluckerconfig.cpp \
        tests/test_pluckerconfig.cpp
# Also add the CMakeLists.txt change
git commit -m "feat(plucker): add PluckerConfig data model with persistence and scheduling"
```

---

## Task 3: KPilotDeviceLink — installFile() and findDatabase()

**Files:**
- Modify: `src/palm/kpilotdevicelink.h`
- Modify: `src/palm/kpilotdevicelink.cpp`

**Step 1: Add method declarations to header**

In `src/palm/kpilotdevicelink.h`, add after `resetSyncFlags()`:

```cpp
    /**
     * @brief Install a .pdb/.prc file onto the Palm device
     *
     * Wraps pilot-link pi_file_install(). Installs to internal storage (card 0).
     *
     * @param filePath Absolute path to the .pdb or .prc file
     * @return true on success
     */
    bool installFile(const QString &filePath);

    /**
     * @brief Check if a database exists on the Palm device
     *
     * Wraps dlp_FindDBInfo(). Used to check for viewer apps, etc.
     *
     * @param dbName Palm database name (e.g. "PlkrMain")
     * @return true if the database exists on the device
     */
    bool findDatabase(const QString &dbName);
```

**Step 2: Implement in .cpp**

In `src/palm/kpilotdevicelink.cpp`, add:

```cpp
bool KPilotDeviceLink::installFile(const QString &filePath)
{
    if (!m_isConnected || m_socket < 0) {
        qWarning() << "[DeviceLink] Cannot install file — not connected";
        return false;
    }

    pi_file_t *pf = pi_file_open(filePath.toLocal8Bit().constData());
    if (!pf) {
        qWarning() << "[DeviceLink] Failed to open file for install:" << filePath;
        return false;
    }

    struct DBInfo dbInfo;
    pi_file_get_info(pf, &dbInfo);
    qDebug() << "[DeviceLink] Installing:" << dbInfo.name << "from" << filePath;

    int rc = pi_file_install(pf, m_socket, 0, nullptr);
    pi_file_close(pf);

    if (rc < 0) {
        qWarning() << "[DeviceLink] pi_file_install failed:" << rc;
        return false;
    }

    qDebug() << "[DeviceLink] Installed successfully:" << dbInfo.name;
    return true;
}

bool KPilotDeviceLink::findDatabase(const QString &dbName)
{
    if (!m_isConnected || m_socket < 0) {
        return false;
    }

    struct DBInfo info;
    int index = 0;
    // dlp_FindDBInfo searches by name when index is used with findFlags
    int rc = dlp_FindDBInfo(m_socket, 0, 0,
                             dbName.toLocal8Bit().constData(),
                             0, 0, &info);
    return (rc >= 0);
}
```

Note: Check the exact pilot-link headers for `pi_file_install()`, `pi_file_open()`,
`pi_file_close()`, `pi_file_get_info()`, and `dlp_FindDBInfo()` signatures.
The existing `InstallConduit` at `src/sync/conduits/installconduit.cpp` uses
exactly these calls — follow that pattern.

**Step 3: Build**

```bash
cmake --build build
```

Expected: Compiles clean. No new tests (requires real hardware).

**Step 4: Commit**

```bash
git add src/palm/kpilotdevicelink.h src/palm/kpilotdevicelink.cpp
git commit -m "feat(palm): add installFile() and findDatabase() to KPilotDeviceLink"
```

---

## Task 4: SyncEngine Post-Conduit Install Phase

**Files:**
- Modify: `src/sync/syncengine.cpp` (syncAllOrdered method)
- Modify: `src/sync/syncengine.h` (if needed for helper method)

**Step 1: Add install phase after the conduit loop**

In `src/sync/syncengine.cpp`, in `syncAllOrdered()`, find the section after the
`for (const QString &id : orderedIds)` loop ends and before `totalResult.endTime`.
Insert the install queue processing:

```cpp
    // Post-conduit install phase: process any files queued by tool conduits
    if (!m_context.installQueue.isEmpty() && m_deviceLink && m_deviceLink->isConnected()) {
        emit logMessage(QString("Installing %1 queued file(s)...")
                        .arg(m_context.installQueue.size()));

        int installed = 0;
        int failed = 0;
        for (const QString &filePath : m_context.installQueue) {
            if (m_cancelled) break;

            QFileInfo fi(filePath);
            emit logMessage(QString("  Installing %1...").arg(fi.fileName()));

            if (m_deviceLink->installFile(filePath)) {
                installed++;
            } else {
                failed++;
                emit logMessage(QString("  Failed to install %1").arg(fi.fileName()));
            }
        }

        emit logMessage(QString("Install phase: %1 installed, %2 failed")
                        .arg(installed).arg(failed));
        m_context.installQueue.clear();
    }
```

Note: The `SyncContext` used in `syncAllOrdered` may be a local variable or member.
Read the actual code to determine how to access `installQueue`. The `SyncContext`
is built in `syncConduit()` as a local — so for installQueue to accumulate across
conduits, it must be passed by pointer and persist. Check if `syncConduit()` takes
a `SyncContext*` or builds its own. If it builds its own, you'll need to refactor
to use a shared context or accumulate installQueue externally.

Most likely approach: add a `QStringList m_pendingInstalls` member to SyncEngine,
and after each `syncConduit()` call, move any entries from the returned context's
installQueue into `m_pendingInstalls`. Then process `m_pendingInstalls` at the end.

**Step 2: Build and verify existing tests still pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All existing tests pass (install phase is a no-op when installQueue is empty)

**Step 3: Commit**

```bash
git add src/sync/syncengine.cpp src/sync/syncengine.h
git commit -m "feat(sync): add post-conduit install phase for installQueue processing"
```

---

## Task 5: PluckerConduit Plugin Skeleton

**Files:**
- Create: `src/plugins/plucker/plucker-conduit.json`
- Create: `src/plugins/plucker/pluckerconduit.h`
- Create: `src/plugins/plucker/pluckerconduit.cpp`
- Create: `src/plugins/plucker/CMakeLists.txt`
- Modify: `src/plugins/CMakeLists.txt` (add plucker subdirectory)

**Step 1: Create plugin JSON metadata**

Create `src/plugins/plucker/plucker-conduit.json`:

```json
{
    "KPlugin": {
        "Name": "Plucker Conduit",
        "Description": "Fetches web content and installs as Plucker documents on Palm",
        "Icon": "text-html",
        "Authors": [{ "Name": "QPilotSync contributors" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-QPilotSync-ConduitId": "plucker",
    "X-QPilotSync-ConduitType": "tool",
    "X-QPilotSync-PalmCreatorId": "Plkr",
    "X-QPilotSync-PalmDatabase": "",
    "X-QPilotSync-RequiresDevice": true,
    "X-QPilotSync-RunBefore": [],
    "X-QPilotSync-RunAfter": ["memos", "contacts", "calendar", "todos"],
    "X-QPilotSync-DefaultEnabled": false,
    "X-QPilotSync-SortOrder": 50
}
```

**Step 2: Create conduit header**

Create `src/plugins/plucker/pluckerconduit.h`:

```cpp
#ifndef PLUCKERCONDUIT_H
#define PLUCKERCONDUIT_H

#include <QObject>
#include <QIcon>
#include "../../core/itoolconduit.h"
#include "pluckerconfig.h"

namespace Sync {
class SyncContext;
struct SyncResult;
}

class PluckerConduit : public QObject, public IToolConduit
{
    Q_OBJECT
    Q_INTERFACES(IConduit IToolConduit)

public:
    explicit PluckerConduit(QObject *parent = nullptr);
    ~PluckerConduit() override = default;

    // ========== IConduit Identity ==========
    QString conduitId() const override { return QStringLiteral("plucker"); }
    QString displayName() const override { return QStringLiteral("Plucker"); }
    QIcon icon() const override;
    QString description() const override;
    QString version() const override { return QStringLiteral("1.0.0"); }

    // ========== IConduit Capabilities ==========
    bool requiresDevice() const override { return true; }

    // ========== IConduit Sync ==========
    Sync::SyncResult sync(Sync::SyncContext *context) override;
    bool canSync(const Sync::SyncContext *context) const override;
    bool shouldRun(const Sync::SyncContext *context) const override;

    // ========== IConduit UI ==========
    bool hasView() const override { return true; }
    QWidget *createView(QWidget *parent) override;
    QString viewName() const override { return QStringLiteral("Plucker"); }
    QIcon viewIcon() const override;

    // ========== IToolConduit ==========
    QString toolPath() const override;
    bool prepareExecution(Sync::SyncContext *context) override;
    bool installResults(Sync::SyncContext *context) override;

Q_SIGNALS:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);

private:
    bool spiderChannel(const PluckerChannel &channel,
                       const QString &outputDir);
    QString findPython() const;
    QString parserPath() const;

    PluckerConfig m_config;
    QString m_outputDir;
    QStringList m_producedFiles;
};

#endif // PLUCKERCONDUIT_H
```

**Step 3: Create conduit .cpp with stubs**

Create `src/plugins/plucker/pluckerconduit.cpp`:

```cpp
#include "pluckerconduit.h"
#include "pluckerview.h"
#include "../../sync/conduit.h"

#include <QIcon>
#include <QProcess>
#include <QDir>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QDebug>

PluckerConduit::PluckerConduit(QObject *parent)
    : QObject(parent)
{
}

QIcon PluckerConduit::icon() const
{
    return QIcon::fromTheme(QStringLiteral("text-html"));
}

QString PluckerConduit::description() const
{
    return QStringLiteral("Fetches web content and installs as Plucker documents on Palm");
}

QIcon PluckerConduit::viewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("text-html"));
}

QString PluckerConduit::toolPath() const
{
    return findPython();
}

QString PluckerConduit::findPython() const
{
    // Try python3 first, then python
    for (const auto &name : {QStringLiteral("python3"), QStringLiteral("python")}) {
        QProcess probe;
        probe.start(name, {QStringLiteral("--version")});
        if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
            return name;
        }
    }
    return QStringLiteral("python3");
}

QString PluckerConduit::parserPath() const
{
    // The PyPlucker package is bundled alongside the plugin .so
    // Plugin .so lives in <prefix>/lib/qpilotsync/conduits/
    // Parser lives in <prefix>/lib/qpilotsync/conduits/plucker-parser/PyPlucker/
    // At dev time, look relative to the executable
    QDir appDir(QCoreApplication::applicationDirPath());

    // Development: build/bin -> src/plugins/plucker/parser
    QString devPath = appDir.absoluteFilePath(
        QStringLiteral("../../src/plugins/plucker/parser/PyPlucker/Spider.py"));
    if (QFile::exists(devPath)) return devPath;

    // Installed: look alongside the plugin
    // TODO: Determine installed parser path from plugin metadata
    return QString();
}

bool PluckerConduit::canSync(const Sync::SyncContext *context) const
{
    Q_UNUSED(context);
    return !m_config.channels().isEmpty() && !parserPath().isEmpty();
}

bool PluckerConduit::shouldRun(const Sync::SyncContext *context) const
{
    Q_UNUSED(context);
    // Check if any channel is due
    for (const auto &ch : m_config.channels()) {
        if (PluckerConfig::isDue(ch)) return true;
    }
    return false;
}

bool PluckerConduit::prepareExecution(Sync::SyncContext *context)
{
    // Load config from profile
    if (context && !context->syncFolderPath.isEmpty()) {
        m_config.load(context->syncFolderPath);
    }

    // Create temp output directory
    m_outputDir = QDir::tempPath() + QStringLiteral("/qpilotsync-plucker-")
                  + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(m_outputDir);
    m_producedFiles.clear();

    return true;
}

Sync::SyncResult PluckerConduit::sync(Sync::SyncContext *context)
{
    Sync::SyncResult result;
    result.startTime = QDateTime::currentDateTime();
    result.success = true;

    if (!prepareExecution(context)) {
        result.success = false;
        result.errorMessage = QStringLiteral("Failed to prepare Plucker execution");
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    // Spider each due channel
    const auto allChannels = m_config.channels();
    int dueCount = 0;
    int successCount = 0;
    int failCount = 0;

    for (const auto &ch : allChannels) {
        if (!PluckerConfig::isDue(ch)) continue;
        dueCount++;

        Q_EMIT progressUpdated(successCount + failCount, dueCount,
            QStringLiteral("Fetching %1...").arg(ch.name));
        Q_EMIT logMessage(QStringLiteral("Plucker: spidering %1 (%2)")
                          .arg(ch.name, ch.homeUrl));

        if (spiderChannel(ch, m_outputDir)) {
            successCount++;
            // Update lastFetched
            PluckerChannel updated = ch;
            updated.lastFetched = QDateTime::currentDateTime();
            m_config.updateChannel(updated);
        } else {
            failCount++;
            Q_EMIT logMessage(QStringLiteral("Plucker: failed to fetch %1")
                              .arg(ch.name));
        }
    }

    // Save updated config (lastFetched times)
    if (context && !context->syncFolderPath.isEmpty()) {
        m_config.save(context->syncFolderPath);
    }

    // Collect results
    installResults(context);

    if (dueCount == 0) {
        Q_EMIT logMessage(QStringLiteral("Plucker: no channels due"));
    } else {
        Q_EMIT logMessage(QStringLiteral("Plucker: %1/%2 channels fetched, %3 file(s) queued")
                          .arg(successCount).arg(dueCount).arg(m_producedFiles.size()));
    }

    result.endTime = QDateTime::currentDateTime();
    return result;
}

bool PluckerConduit::spiderChannel(const PluckerChannel &channel,
                                    const QString &outputDir)
{
    QString python = findPython();
    QString spider = parserPath();

    if (spider.isEmpty()) {
        Q_EMIT errorOccurred(QStringLiteral("PyPlucker Spider.py not found"));
        return false;
    }

    QStringList args;
    args << spider;
    args << PluckerConfig::buildCLIArgs(channel, outputDir);

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    // Set PYTHONPATH so Spider.py can find the PyPlucker package
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QFileInfo spiderInfo(spider);
    env.insert(QStringLiteral("PYTHONPATH"),
               spiderInfo.absolutePath() + QStringLiteral("/.."));
    process.setProcessEnvironment(env);

    process.start(python, args);

    if (!process.waitForFinished(300000)) {  // 5 minute timeout
        process.kill();
        Q_EMIT errorOccurred(QStringLiteral("Plucker timeout for %1").arg(channel.name));
        return false;
    }

    if (process.exitCode() != 0) {
        QString output = QString::fromUtf8(process.readAll());
        Q_EMIT errorOccurred(QStringLiteral("Plucker error for %1: %2")
                             .arg(channel.name, output.left(500)));
        return false;
    }

    // Check for output .pdb file
    QString expectedPdb = QDir(outputDir).filePath(
        PluckerConfig::sanitizeDocFile(channel.name) + QStringLiteral(".pdb"));
    if (QFile::exists(expectedPdb)) {
        m_producedFiles.append(expectedPdb);
        return true;
    }

    Q_EMIT logMessage(QStringLiteral("Plucker: no .pdb produced for %1").arg(channel.name));
    return false;
}

bool PluckerConduit::installResults(Sync::SyncContext *context)
{
    if (!context) return false;

    // Check if viewer needs installing
    if (context->deviceLink) {
        // Cast to check for findDatabase — see Task 9 for viewer install
    }

    for (const QString &pdbPath : m_producedFiles) {
        context->installQueue.append(pdbPath);
    }

    return !m_producedFiles.isEmpty();
}

QWidget *PluckerConduit::createView(QWidget *parent)
{
    return new PluckerView(parent);
}

// Plugin factory registration
#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(PluckerConduitFactory, "plucker-conduit.json",
                           registerPlugin<PluckerConduit>();)

#include "pluckerconduit.moc"
```

**Step 4: Create CMakeLists.txt**

Create `src/plugins/plucker/CMakeLists.txt`:

```cmake
kcoreaddons_add_plugin(qpilotsync_plucker
    SOURCES
        pluckerconduit.cpp
        pluckerconduit.h
        pluckerview.cpp
        pluckerview.h
        pluckerchanneldialog.cpp
        pluckerchanneldialog.h
        pluckerconfig.cpp
        pluckerconfig.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_plucker
    QPilotCore
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
    Qt::Widgets
)
```

**Step 5: Add to parent CMakeLists.txt**

In `src/plugins/CMakeLists.txt`, add:

```cmake
add_subdirectory(plucker)
```

**Step 6: Create PluckerView stub** (needed for build)

Create `src/plugins/plucker/pluckerview.h`:

```cpp
#ifndef PLUCKERVIEW_H
#define PLUCKERVIEW_H

#include <QWidget>

class PluckerView : public QWidget
{
    Q_OBJECT
public:
    explicit PluckerView(QWidget *parent = nullptr);

public Q_SLOTS:
    void loadFromPath(const QString &syncPath);
    void refresh();
};

#endif // PLUCKERVIEW_H
```

Create `src/plugins/plucker/pluckerview.cpp`:

```cpp
#include "pluckerview.h"

PluckerView::PluckerView(QWidget *parent)
    : QWidget(parent)
{
}

void PluckerView::loadFromPath(const QString &syncPath)
{
    Q_UNUSED(syncPath);
    // TODO: Task 7
}

void PluckerView::refresh()
{
    // TODO: Task 7
}
```

**Step 7: Create PluckerChannelDialog stub** (needed for build)

Create `src/plugins/plucker/pluckerchanneldialog.h`:

```cpp
#ifndef PLUCKERCHANNELDIALOG_H
#define PLUCKERCHANNELDIALOG_H

#include <QDialog>
#include "pluckerconfig.h"

class PluckerChannelDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluckerChannelDialog(QWidget *parent = nullptr);
    PluckerChannelDialog(const PluckerChannel &channel, QWidget *parent = nullptr);

    PluckerChannel channel() const;

private:
    PluckerChannel m_channel;
};

#endif // PLUCKERCHANNELDIALOG_H
```

Create `src/plugins/plucker/pluckerchanneldialog.cpp`:

```cpp
#include "pluckerchanneldialog.h"

PluckerChannelDialog::PluckerChannelDialog(QWidget *parent)
    : QDialog(parent)
{
}

PluckerChannelDialog::PluckerChannelDialog(const PluckerChannel &channel,
                                             QWidget *parent)
    : QDialog(parent), m_channel(channel)
{
}

PluckerChannel PluckerChannelDialog::channel() const
{
    return m_channel;
}
```

**Step 8: Build**

```bash
cmake --build build
```

Expected: Compiles. Plugin .so is built to `build/lib/qpilotsync/conduits/`.

**Step 9: Verify plugin is discoverable**

Run the app briefly or check with:

```bash
ls -la build/lib/qpilotsync/conduits/qpilotsync_plucker*
```

Expected: `.so` file exists

**Step 10: Commit**

```bash
git add src/plugins/plucker/ src/plugins/CMakeLists.txt
git commit -m "feat(plucker): add PluckerConduit plugin skeleton with IToolConduit implementation"
```

---

## Task 6: SyncEngine Signal Wiring for Tool Conduits

**Files:**
- Modify: `src/sync/syncengine.cpp` (connectConduitSignals)

**Step 1: Handle PluckerConduit signals**

In `SyncEngine::connectConduitSignals()`, after the existing `SyncConduitBase*`
dynamic_cast block, add a fallback for tool conduits that define their own signals:

```cpp
    // For tool conduits (IToolConduit), check for signals by QObject metadata
    // PluckerConduit defines logMessage, errorOccurred, progressUpdated
    auto *obj = dynamic_cast<QObject*>(conduit);
    if (obj && !syncBase) {
        // Connect by name — works for any QObject with matching signals
        if (obj->metaObject()->indexOfSignal("logMessage(QString)") >= 0) {
            connect(obj, SIGNAL(logMessage(QString)),
                    this, SIGNAL(logMessage(QString)));
        }
        if (obj->metaObject()->indexOfSignal("errorOccurred(QString)") >= 0) {
            connect(obj, SIGNAL(errorOccurred(QString)),
                    this, SIGNAL(errorOccurred(QString)));
        }
        if (obj->metaObject()->indexOfSignal("progressUpdated(int,int,QString)") >= 0) {
            connect(obj, SIGNAL(progressUpdated(int,int,QString)),
                    this, SIGNAL(progressUpdated(int,int,QString)));
        }
    }
```

**Step 2: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All tests pass

**Step 3: Commit**

```bash
git add src/sync/syncengine.cpp
git commit -m "feat(sync): wire tool conduit signals (logMessage, progress) through SyncEngine"
```

---

## Task 7: PluckerView — Full Channel List UI

**Files:**
- Modify: `src/plugins/plucker/pluckerview.h`
- Modify: `src/plugins/plucker/pluckerview.cpp`

**Step 1: Write the full PluckerView header**

Replace `src/plugins/plucker/pluckerview.h`:

```cpp
#ifndef PLUCKERVIEW_H
#define PLUCKERVIEW_H

#include <QWidget>
#include "pluckerconfig.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

/**
 * @brief Channel list view for the Plucker conduit
 *
 * Displayed as a sidebar page in the main window. Shows configured
 * Plucker channels with their status, and provides add/edit/remove
 * and manual fetch functionality.
 */
class PluckerView : public QWidget
{
    Q_OBJECT

public:
    explicit PluckerView(QWidget *parent = nullptr);

public Q_SLOTS:
    void loadFromPath(const QString &syncPath);
    void refresh();

Q_SIGNALS:
    void channelsModified();

private Q_SLOTS:
    void onAdd();
    void onEdit();
    void onRemove();
    void onFetchNow();
    void onSelectionChanged();

private:
    void populateList();
    void updateDetailPanel(const PluckerChannel &channel);

    QTreeWidget *m_channelList;
    QLabel *m_detailLabel;
    QPushButton *m_addBtn;
    QPushButton *m_editBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_fetchBtn;

    PluckerConfig m_config;
    QString m_syncPath;
};

#endif // PLUCKERVIEW_H
```

**Step 2: Write the full PluckerView implementation**

Replace `src/plugins/plucker/pluckerview.cpp` with:

- Constructor builds layout: QTreeWidget on top, button row, detail label at bottom
- QTreeWidget columns: Enabled (checkbox), Name, Last Fetched, Status (due indicator)
- `loadFromPath()` stores syncPath, calls `m_config.load()`, calls `populateList()`
- `populateList()` clears tree, iterates channels, creates items with checkboxes
- Due channels show a "Due" text or icon in the status column
- `onAdd()` opens `PluckerChannelDialog()`, on accept adds channel, saves, refreshes
- `onEdit()` opens dialog pre-filled, on accept updates channel, saves, refreshes
- `onRemove()` confirms, removes channel, saves, refreshes
- `onFetchNow()` spawns QProcess for the selected channel with a progress dialog
- `onSelectionChanged()` updates the detail panel
- Detail panel shows: name, URL, depth, bpp, compression, schedule, category

Full implementation — reference MemoView pattern for layout and button wiring.
Use `KLocalizedString` (`i18n()`) for all user-visible strings.
Use `QIcon::fromTheme()` for button icons.

**Step 3: Build**

```bash
cmake --build build
```

**Step 4: Commit**

```bash
git add src/plugins/plucker/pluckerview.h src/plugins/plucker/pluckerview.cpp
git commit -m "feat(plucker): implement PluckerView channel list with add/edit/remove/fetch"
```

---

## Task 8: PluckerChannelDialog — 5-Tab Config

**Files:**
- Modify: `src/plugins/plucker/pluckerchanneldialog.h`
- Modify: `src/plugins/plucker/pluckerchanneldialog.cpp`

**Step 1: Write the full header**

Replace `src/plugins/plucker/pluckerchanneldialog.h` with full declaration:

- Private members for all widgets across 5 tabs
- `applyToChannel()` — reads widgets into m_channel
- `loadFromChannel()` — populates widgets from m_channel

**Step 2: Write the full implementation**

Replace `src/plugins/plucker/pluckerchanneldialog.cpp`:

- Constructor: create QTabWidget with 5 tabs
- Set minimum size ~550x450
- Standard OK/Cancel buttons via QDialogButtonBox
- On accept: call applyToChannel()

**Tab 1 — Starting Page:**
- QLineEdit for URL + Browse button (QFileDialog for local files)
- QLineEdit for doc name
- QComboBox for category (editable)

**Tab 2 — Spidering:**
- QSpinBox for max depth (range 1-999)
- QCheckBox for stay on host
- QRadioButton pair: breadth-first (default) / depth-first
- QLineEdit for URL include pattern
- QLineEdit for custom user-agent

**Tab 3 — Images:**
- QComboBox for bpp: "No images" (0), "1-bit" (1), "2-bit" (2), "4-bit" (4), "8-bit" (8), "16-bit" (16)
- QSpinBox pair for max thumbnail width/height
- QSpinBox pair for alt (full-size) max width/height
- QSpinBox for image compression limit (0-100%)

**Tab 4 — Destination:**
- QRadioButton group: Internal RAM, SD Card, Memory Stick, CompactFlash
- QLineEdit for card directory path (enabled only for card modes)
- QComboBox for compression: zlib, DOC

**Tab 5 — Scheduling:**
- QCheckBox for auto-update enabled
- QSpinBox for frequency (1-999999)
- QComboBox for period: hours, days, weeks, months
- QLabel showing computed next-due date/time

Use `KLocalizedString` (`i18n()`) throughout. Use `QFormLayout` within each tab
for clean label-field alignment.

**Step 3: Build**

```bash
cmake --build build
```

**Step 4: Commit**

```bash
git add src/plugins/plucker/pluckerchanneldialog.h src/plugins/plucker/pluckerchanneldialog.cpp
git commit -m "feat(plucker): implement PluckerChannelDialog with 5-tab config"
```

---

## Task 9: Viewer Auto-Install

**Files:**
- Modify: `src/plugins/plucker/pluckerconduit.cpp` (installResults method)

**Step 1: Implement viewer detection and queuing**

In `PluckerConduit::installResults()`, before queuing channel .pdb files:

```cpp
bool PluckerConduit::installResults(Sync::SyncContext *context)
{
    if (!context) return false;

    // Check if Palm viewer needs installing
    if (context->deviceLink) {
        auto *link = dynamic_cast<KPilotDeviceLink*>(
            context->deviceLink);
        if (link && !link->findDatabase(QStringLiteral("PlkrMain"))) {
            Q_EMIT logMessage(QStringLiteral("Plucker viewer not found on device — queuing install"));

            // Find bundled viewer PRCs
            QString viewerDir = viewerPath();
            if (!viewerDir.isEmpty()) {
                QDir dir(viewerDir);
                // Install SysZLib first (dependency)
                QString syszlib = dir.filePath(QStringLiteral("SysZLib.prc"));
                if (QFile::exists(syszlib)) {
                    context->installQueue.append(syszlib);
                }
                // Then the viewer
                QString viewer = dir.filePath(QStringLiteral("viewer_en.prc"));
                if (QFile::exists(viewer)) {
                    context->installQueue.append(viewer);
                }
            }
        }
    }

    // Queue produced .pdb files
    for (const QString &pdbPath : m_producedFiles) {
        context->installQueue.append(pdbPath);
    }

    return !m_producedFiles.isEmpty();
}
```

**Step 2: Add viewerPath() helper**

In `pluckerconduit.h`, add private method:

```cpp
    QString viewerPath() const;
```

In `pluckerconduit.cpp`:

```cpp
QString PluckerConduit::viewerPath() const
{
    QDir appDir(QCoreApplication::applicationDirPath());

    // Development path
    QString devPath = appDir.absoluteFilePath(
        QStringLiteral("../../src/plugins/plucker/viewer"));
    if (QDir(devPath).exists()) return QFileInfo(devPath).canonicalFilePath();

    // Installed path (alongside plugin .so)
    // TODO: determine from plugin metadata
    return QString();
}
```

**Step 3: Add KPilotDeviceLink include**

At top of `pluckerconduit.cpp`:

```cpp
#include "../../palm/kpilotdevicelink.h"
```

**Step 4: Build**

```bash
cmake --build build
```

**Step 5: Commit**

```bash
git add src/plugins/plucker/pluckerconduit.h src/plugins/plucker/pluckerconduit.cpp
git commit -m "feat(plucker): auto-detect and install Palm viewer PRCs on first sync"
```

---

## Task 10: Integration Wiring and Final Build Verification

**Files:**
- Possibly modify: `src/kf6/kf6mainwindow.cpp` (if conduit signals need connection)

**Step 1: Full build**

```bash
cmake --build build
```

**Step 2: Run all tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: All tests pass (existing 10 + new test_pluckerconfig)

**Step 3: Verify plugin loads**

Run the app. Check debug output for:

```
[ConduitManager] Discovered conduit: plucker creatorId: Plkr sortOrder: 50 defaultEnabled: false
```

The Plucker conduit should appear in Settings > Conduits under "Plucker" group.
Enable it via the checkbox.

**Step 4: Verify PluckerView appears in sidebar**

After enabling Plucker in settings, restart app. The sidebar should show a
"Plucker" page with an empty channel list and Add/Edit/Remove/Fetch buttons.

**Step 5: Final commit**

```bash
git add -A
git commit -m "feat(plucker): complete Plucker conduit plugin integration"
```

---

## Summary of All Tasks

| Task | What | Tests |
|------|------|-------|
| 1 | Vendor PyPlucker + viewer PRCs | Manual (python import check) |
| 2 | PluckerConfig data model + persistence | test_pluckerconfig (8 tests) |
| 3 | KPilotDeviceLink::installFile/findDatabase | Manual (needs hardware) |
| 4 | SyncEngine post-conduit install phase | Existing tests pass (no-op path) |
| 5 | PluckerConduit plugin skeleton | Build + discovery |
| 6 | SyncEngine signal wiring for tool conduits | Build + existing tests |
| 7 | PluckerView channel list UI | Manual |
| 8 | PluckerChannelDialog 5-tab config | Manual |
| 9 | Viewer auto-install | Manual (needs hardware) |
| 10 | Integration wiring + final verification | All tests + manual |
