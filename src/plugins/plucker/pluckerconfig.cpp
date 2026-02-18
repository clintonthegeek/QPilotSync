#include "pluckerconfig.h"
#include <QSettings>
#include <QDir>
#include <QRegularExpression>

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

void PluckerConfig::load(const QString &syncPath)
{
    m_channels.clear();
    QString configFile = QDir(syncPath).filePath(QStringLiteral(".qpilotsync.conf"));
    QSettings settings(configFile, QSettings::IniFormat);

    settings.beginGroup(QStringLiteral("Plucker"));
    QStringList ids = settings.value(QStringLiteral("channelIds")).toStringList();
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
        ch.imageCompressionLimit = settings.value("imageCompressionLimit", 50).toInt();
        ch.compression = settings.value("compression", "zlib").toString();
        ch.category = settings.value("category").toString();
        ch.storageMode = settings.value("storageMode", "ram").toString();
        ch.cardDirectory = settings.value("cardDirectory").toString();
        ch.updateEnabled = settings.value("updateEnabled", true).toBool();
        ch.updateFrequency = settings.value("updateFrequency", 1).toInt();
        ch.updatePeriod = settings.value("updatePeriod", "days").toString();
        ch.lastFetched = settings.value("lastFetched").toDateTime();

        settings.endGroup();
        m_channels.append(ch);
    }
}

void PluckerConfig::save(const QString &syncPath)
{
    QString configFile = QDir(syncPath).filePath(QStringLiteral(".qpilotsync.conf"));
    QSettings settings(configFile, QSettings::IniFormat);

    QStringList ids;
    for (const auto &ch : m_channels) {
        ids.append(ch.id);
    }
    settings.beginGroup(QStringLiteral("Plucker"));
    settings.setValue(QStringLiteral("channelIds"), ids);
    settings.endGroup();

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
        settings.setValue("imageCompressionLimit", ch.imageCompressionLimit);
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

bool PluckerConfig::isDue(const PluckerChannel &channel)
{
    if (!channel.updateEnabled) return false;
    if (!channel.lastFetched.isValid()) return true;
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
        next = next.addDays(freq);
    }

    return next;
}

QString PluckerConfig::sanitizeDocFile(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
    if (safe.isEmpty()) safe = QStringLiteral("untitled");
    return safe;
}

QStringList PluckerConfig::buildCLIArgs(const PluckerChannel &channel, const QString &outputDir)
{
    QStringList args;

    args << QStringLiteral("--home-url=%1").arg(channel.homeUrl);
    args << QStringLiteral("--doc-name=%1").arg(channel.name);
    args << QStringLiteral("--doc-file=%1").arg(sanitizeDocFile(channel.name));
    args << QStringLiteral("--pluckerdir=%1").arg(outputDir);
    args << QStringLiteral("--maxdepth=%1").arg(channel.maxDepth);
    args << QStringLiteral("--bpp=%1").arg(channel.bpp);
    args << QStringLiteral("--maxwidth=%1").arg(channel.maxWidth);
    args << QStringLiteral("--maxheight=%1").arg(channel.maxHeight);
    args << QStringLiteral("--alt-maxwidth=%1").arg(channel.altMaxWidth);
    args << QStringLiteral("--alt-maxheight=%1").arg(channel.altMaxHeight);
    args << QStringLiteral("--compression=%1").arg(channel.compression);

    if (channel.stayOnHost) args << QStringLiteral("--stayonhost");
    if (channel.depthFirst) args << QStringLiteral("--depth-first");
    if (channel.noImages) args << QStringLiteral("--noimages");
    if (!channel.category.isEmpty()) args << QStringLiteral("--category=%1").arg(channel.category);
    if (!channel.userAgent.isEmpty()) args << QStringLiteral("--user-agent=%1").arg(channel.userAgent);
    if (!channel.urlPattern.isEmpty()) args << QStringLiteral("--staybelow=%1").arg(channel.urlPattern);

    args << QStringLiteral("--no-urlinfo");

    return args;
}
