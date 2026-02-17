#ifndef PLUCKERCONFIG_H
#define PLUCKERCONFIG_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QUuid>

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

class PluckerConfig
{
public:
    PluckerConfig() = default;

    void addChannel(const PluckerChannel &channel);
    void updateChannel(const PluckerChannel &channel);
    void removeChannel(const QString &id);
    PluckerChannel channel(const QString &id) const;
    QList<PluckerChannel> channels() const;

    void load(const QString &syncPath);
    void save(const QString &syncPath);

    static bool isDue(const PluckerChannel &channel);
    static QDateTime nextDueTime(const PluckerChannel &channel);
    static QStringList buildCLIArgs(const PluckerChannel &channel, const QString &outputDir);
    static QString sanitizeDocFile(const QString &name);

private:
    QList<PluckerChannel> m_channels;
};

#endif // PLUCKERCONFIG_H
