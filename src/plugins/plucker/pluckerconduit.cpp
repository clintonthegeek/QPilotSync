#include "pluckerconduit.h"
#include "pluckerview.h"
#include "sync/conduit.h"
#include "palm/kpilotdevicelink.h"

#include <QIcon>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QDebug>

#include <KPluginFactory>

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
    QDir appDir(QCoreApplication::applicationDirPath());

    // Development: build/bin -> src/plugins/plucker/parser
    QString devPath = appDir.absoluteFilePath(
        QStringLiteral("../../src/plugins/plucker/parser/PyPlucker/Spider.py"));
    if (QFile::exists(devPath)) return QFileInfo(devPath).canonicalFilePath();

    return QString();
}

QString PluckerConduit::viewerPath() const
{
    QDir appDir(QCoreApplication::applicationDirPath());

    // Development path: build/bin -> src/plugins/plucker/viewer
    QString devPath = appDir.absoluteFilePath(
        QStringLiteral("../../src/plugins/plucker/viewer"));
    if (QDir(devPath).exists()) return QFileInfo(devPath).canonicalFilePath();

    // Installed path (alongside plugin .so)
    // TODO: determine from plugin metadata
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
    for (const auto &ch : m_config.channels()) {
        if (PluckerConfig::isDue(ch)) return true;
    }
    return false;
}

bool PluckerConduit::prepareExecution(Sync::SyncContext *context)
{
    if (context && !context->syncFolderPath.isEmpty()) {
        m_config.load(context->syncFolderPath);
    }

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
            PluckerChannel updated = ch;
            updated.lastFetched = QDateTime::currentDateTime();
            m_config.updateChannel(updated);
        } else {
            failCount++;
            Q_EMIT logMessage(QStringLiteral("Plucker: failed to fetch %1")
                              .arg(ch.name));
        }
    }

    if (context && !context->syncFolderPath.isEmpty()) {
        m_config.save(context->syncFolderPath);
    }

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

    // Check if Palm viewer needs installing
    if (context->deviceLink) {
        KPilotDeviceLink *link = context->deviceLink;
        if (!link->findDatabase(QStringLiteral("PlkrMain"))) {
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

QWidget *PluckerConduit::createView(QWidget *parent)
{
    return new PluckerView(parent);
}

K_PLUGIN_FACTORY_WITH_JSON(PluckerConduitFactory, "plucker-conduit.json",
                           registerPlugin<PluckerConduit>();)

#include "pluckerconduit.moc"
