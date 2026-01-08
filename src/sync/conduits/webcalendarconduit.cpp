#include "webcalendarconduit.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>

namespace Sync {

// ========== WebCalendarFeed ==========

QJsonObject WebCalendarFeed::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["url"] = url.toString();
    obj["category"] = category;
    obj["enabled"] = enabled;
    return obj;
}

WebCalendarFeed WebCalendarFeed::fromJson(const QJsonObject &obj)
{
    WebCalendarFeed feed;
    feed.name = obj["name"].toString();
    feed.url = QUrl(obj["url"].toString());
    feed.category = obj["category"].toString();
    feed.enabled = obj["enabled"].toBool(true);
    return feed;
}

// ========== WebCalendarConduit ==========

WebCalendarConduit::WebCalendarConduit(QObject *parent)
    : Conduit(parent)
{
    // Note: QNetworkAccessManager is created lazily in sync() to ensure
    // it's created on the worker thread where it will be used
}

WebCalendarConduit::~WebCalendarConduit()
{
    // m_networkManager is deleted after each sync, but clean up if needed
    delete m_networkManager;
    m_networkManager = nullptr;
}

// ========== Settings ==========

void WebCalendarConduit::loadSettings(const QJsonObject &settings)
{
    m_feeds.clear();

    // Load feeds
    QJsonArray feedsArray = settings["feeds"].toArray();
    for (const QJsonValue &val : feedsArray) {
        m_feeds.append(WebCalendarFeed::fromJson(val.toObject()));
    }

    // Load fetch interval
    QString intervalStr = settings["fetch_interval"].toString("weekly");
    if (intervalStr == "every_sync") {
        m_fetchInterval = FetchInterval::EverySync;
    } else if (intervalStr == "daily") {
        m_fetchInterval = FetchInterval::Daily;
    } else if (intervalStr == "weekly") {
        m_fetchInterval = FetchInterval::Weekly;
    } else if (intervalStr == "monthly") {
        m_fetchInterval = FetchInterval::Monthly;
    }

    // Load date filter
    QString filterStr = settings["date_filter"].toString("recurring_and_future");
    if (filterStr == "all") {
        m_dateFilter = DateFilter::All;
    } else if (filterStr == "recurring_and_future") {
        m_dateFilter = DateFilter::RecurringAndFuture;
    } else if (filterStr == "future") {
        m_dateFilter = DateFilter::FutureOnly;
    }

    // Load last fetch time
    QString lastFetchStr = settings["last_fetch"].toString();
    if (!lastFetchStr.isEmpty()) {
        m_lastFetchTime = QDateTime::fromString(lastFetchStr, Qt::ISODate);
    }
}

QJsonObject WebCalendarConduit::saveSettings() const
{
    QJsonObject settings;

    // Save feeds
    QJsonArray feedsArray;
    for (const WebCalendarFeed &feed : m_feeds) {
        feedsArray.append(feed.toJson());
    }
    settings["feeds"] = feedsArray;

    // Save fetch interval
    QString intervalStr;
    switch (m_fetchInterval) {
    case FetchInterval::EverySync: intervalStr = "every_sync"; break;
    case FetchInterval::Daily: intervalStr = "daily"; break;
    case FetchInterval::Weekly: intervalStr = "weekly"; break;
    case FetchInterval::Monthly: intervalStr = "monthly"; break;
    }
    settings["fetch_interval"] = intervalStr;

    // Save date filter
    QString filterStr;
    switch (m_dateFilter) {
    case DateFilter::All: filterStr = "all"; break;
    case DateFilter::RecurringAndFuture: filterStr = "recurring_and_future"; break;
    case DateFilter::FutureOnly: filterStr = "future"; break;
    }
    settings["date_filter"] = filterStr;

    // Save last fetch time
    if (m_lastFetchTime.isValid()) {
        settings["last_fetch"] = m_lastFetchTime.toString(Qt::ISODate);
    }

    return settings;
}

QWidget* WebCalendarConduit::createSettingsWidget(QWidget *parent)
{
    // TODO: Implement settings widget
    // For now, return nullptr - settings can be edited via JSON
    Q_UNUSED(parent);
    return nullptr;
}

// ========== Feed Management ==========

void WebCalendarConduit::addFeed(const WebCalendarFeed &feed)
{
    m_feeds.append(feed);
}

void WebCalendarConduit::removeFeed(int index)
{
    if (index >= 0 && index < m_feeds.size()) {
        m_feeds.removeAt(index);
    }
}

// ========== Pre-Sync Check ==========

bool WebCalendarConduit::shouldRun(SyncContext *context) const
{
    Q_UNUSED(context);

    // No feeds configured? Skip
    if (m_feeds.isEmpty()) {
        return false;
    }

    return shouldFetchNow();
}

bool WebCalendarConduit::shouldFetchNow() const
{
    if (!m_lastFetchTime.isValid()) {
        return true;  // Never fetched before
    }

    QDateTime now = QDateTime::currentDateTime();
    qint64 hoursSinceLastFetch = m_lastFetchTime.secsTo(now) / 3600;

    switch (m_fetchInterval) {
    case FetchInterval::EverySync:
        return true;
    case FetchInterval::Daily:
        return hoursSinceLastFetch >= 24;
    case FetchInterval::Weekly:
        return hoursSinceLastFetch >= 24 * 7;
    case FetchInterval::Monthly:
        return hoursSinceLastFetch >= 24 * 30;
    }

    return true;
}

// ========== Sync Operation ==========

SyncResult WebCalendarConduit::sync(SyncContext *context)
{
    SyncResult result;
    result.startTime = QDateTime::currentDateTime();
    result.success = true;

    if (m_feeds.isEmpty()) {
        emit logMessage("No calendar feeds configured");
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    // Create network manager on the current thread (worker thread)
    // This avoids "Cannot create children for a parent in a different thread" errors
    delete m_networkManager;
    m_networkManager = new QNetworkAccessManager();  // No parent - we manage lifetime

    emit logMessage(QString("Fetching %1 calendar feed(s)...").arg(m_feeds.size()));

    // Determine base output directory: profile_path/calendar/
    // Each feed gets its own subdirectory for organization
    QString baseOutputDir;
    if (context->backend) {
        CollectionInfo calInfo = context->backend->collectionInfo("calendar");
        baseOutputDir = calInfo.path;
    } else {
        emit errorOccurred("No backend configured");
        result.success = false;
        result.errorMessage = "No backend configured";
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    // Ensure base directory exists
    QDir dir;
    if (!dir.mkpath(baseOutputDir)) {
        emit errorOccurred(QString("Failed to create directory: %1").arg(baseOutputDir));
        result.success = false;
        result.errorMessage = "Failed to create calendar directory";
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    int successCount = 0;
    int failCount = 0;

    for (const WebCalendarFeed &feed : m_feeds) {
        if (isCancelled()) {
            emit logMessage("Fetch cancelled");
            break;
        }

        if (!feed.enabled) {
            continue;
        }

        emit progressUpdated(successCount + failCount, m_feeds.size(),
            QString("Fetching %1...").arg(feed.name));

        if (fetchFeed(feed, baseOutputDir, result)) {
            successCount++;
        } else {
            failCount++;
        }
    }

    // Update last fetch time on success
    if (successCount > 0) {
        // Cast away const-ness to update last fetch time
        const_cast<WebCalendarConduit*>(this)->m_lastFetchTime = QDateTime::currentDateTime();
    }

    // Report results
    if (failCount > 0) {
        // Add a proper DataLossWarning for the fetch failures
        DataLossWarning warning;
        warning.severity = WarningSeverity::Warning;
        warning.category = WarningCategory::Unsupported;
        warning.field = "feed";
        warning.message = QString("%1 of %2 feeds failed to fetch")
            .arg(failCount).arg(m_feeds.size());
        result.warnings.append(warning);
        emit logMessage(QString("Warning: %1 feed(s) failed to fetch").arg(failCount));
    }

    emit logMessage(QString("Fetched %1 calendar feed(s)").arg(successCount));

    // Clean up network manager
    delete m_networkManager;
    m_networkManager = nullptr;

    result.endTime = QDateTime::currentDateTime();
    return result;
}

bool WebCalendarConduit::fetchFeed(const WebCalendarFeed &feed, const QString &outputDir,
                                    SyncResult &result)
{
    if (!feed.url.isValid()) {
        emit logMessage(QString("Invalid URL for feed '%1'").arg(feed.name));
        return false;
    }

    emit logMessage(QString("Fetching: %1 (%2)").arg(feed.name).arg(feed.url.toString()));

    // Synchronous HTTP fetch using event loop
    QNetworkRequest request(feed.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "QPilotSync/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    qDebug() << "[WebCalendarConduit] Starting fetch for URL:" << feed.url.toString();

    QNetworkReply *reply = m_networkManager->get(request);

    // Wait for completion with timeout
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    qDebug() << "[WebCalendarConduit] Waiting for response...";
    timeout.start(30000);  // 30 second timeout
    loop.exec();
    qDebug() << "[WebCalendarConduit] Event loop finished, reply finished:" << reply->isFinished();

    if (timeout.isActive()) {
        timeout.stop();
    } else {
        // Timeout occurred
        reply->abort();
        reply->deleteLater();
        emit logMessage(QString("Timeout fetching '%1'").arg(feed.name));
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage(QString("Failed to fetch '%1': %2")
            .arg(feed.name).arg(reply->errorString()));
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    qDebug() << "[WebCalendarConduit] Response: HTTP" << httpStatus
             << "Content-Type:" << contentType
             << "Size:" << data.size() << "bytes";
    if (data.size() > 0 && data.size() < 500) {
        qDebug() << "[WebCalendarConduit] Full response:" << data;
    } else if (data.size() >= 500) {
        qDebug() << "[WebCalendarConduit] First 500 bytes:" << data.left(500);
    }
    reply->deleteLater();

    if (data.isEmpty()) {
        emit logMessage(QString("Empty response for '%1'").arg(feed.name));
        return false;
    }

    // Validate it looks like iCalendar data
    if (!data.contains("BEGIN:VCALENDAR")) {
        emit logMessage(QString("Invalid iCalendar data from '%1' (got %2 bytes, Content-Type: %3)")
            .arg(feed.name).arg(data.size()).arg(contentType));
        return false;
    }

    // Filter events by date if configured
    if (m_dateFilter != DateFilter::All) {
        data = filterEventsByDate(data);
    }

    // Parse the iCalendar and split into individual event files
    // This is necessary because CalendarConduit expects one event per .ics file
    QString content = QString::fromUtf8(data);

    // Extract the VCALENDAR header (PRODID, VERSION, CALSCALE, VTIMEZONE, etc.)
    int firstEvent = content.indexOf("BEGIN:VEVENT");
    if (firstEvent < 0) {
        emit logMessage(QString("No events found in '%1'").arg(feed.name));
        return true;  // Not an error, just no events
    }

    QString calHeader = content.left(firstEvent);

    // Create subdirectory for this feed
    QString feedDirName = feed.name;
    feedDirName.replace(QRegularExpression("[^a-zA-Z0-9_ -]"), "_");
    if (feedDirName.isEmpty()) {
        feedDirName = "webcal";
    }

    QString feedOutputDir = outputDir + "/" + feedDirName;
    QDir dir;
    if (!dir.mkpath(feedOutputDir)) {
        emit logMessage(QString("Failed to create directory: %1").arg(feedOutputDir));
        return false;
    }

    // Extract each VEVENT and write as individual file
    QRegularExpression eventRe("BEGIN:VEVENT.*?END:VEVENT",
                               QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator eventIter = eventRe.globalMatch(content);

    int eventCount = 0;
    int written = 0;

    while (eventIter.hasNext()) {
        QRegularExpressionMatch match = eventIter.next();
        QString event = match.captured();
        eventCount++;

        // Extract UID for filename (or use index if no UID)
        QString uid;
        QRegularExpression uidRe("UID:([^\r\n]+)");
        QRegularExpressionMatch uidMatch = uidRe.match(event);
        if (uidMatch.hasMatch()) {
            uid = uidMatch.captured(1).trimmed();
            // Make UID safe for filename
            uid.replace(QRegularExpression("[^a-zA-Z0-9_@.-]"), "_");
        }

        // Extract SUMMARY for a human-readable filename
        QString summary;
        QRegularExpression summaryRe("SUMMARY:([^\r\n]+)");
        QRegularExpressionMatch summaryMatch = summaryRe.match(event);
        if (summaryMatch.hasMatch()) {
            summary = summaryMatch.captured(1).trimmed();
            summary.replace(QRegularExpression("[^a-zA-Z0-9_ -]"), "_");
            summary = summary.left(40);  // Limit length
        }

        // Extract DTSTART for filename
        QString dateStr;
        QRegularExpression dtstartRe("DTSTART[^:]*:([0-9T]+)");
        QRegularExpressionMatch dtstartMatch = dtstartRe.match(event);
        if (dtstartMatch.hasMatch()) {
            dateStr = dtstartMatch.captured(1).left(8);  // YYYYMMDD
            if (dateStr.length() == 8) {
                dateStr = dateStr.left(4) + "-" + dateStr.mid(4, 2) + "-" + dateStr.mid(6, 2);
            }
        }

        // Generate filename: "Summary YYYY-MM-DD.ics" or "UID.ics"
        QString fileName;
        if (!summary.isEmpty() && !dateStr.isEmpty()) {
            fileName = QString("%1 %2.ics").arg(summary).arg(dateStr);
        } else if (!uid.isEmpty()) {
            fileName = QString("%1.ics").arg(uid);
        } else {
            fileName = QString("event_%1.ics").arg(eventCount);
        }

        QString filePath = feedOutputDir + "/" + fileName;

        // Build complete iCalendar for this single event
        QString singleEventIcs = calHeader + event + "\r\nEND:VCALENDAR\r\n";

        // Write to file
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(singleEventIcs.toUtf8());
            file.close();
            written++;
        } else {
            qDebug() << "[WebCalendarConduit] Failed to write:" << filePath << file.errorString();
        }
    }

    emit logMessage(QString("  → %1 events written as individual .ics files").arg(written));
    result.pcStats.created += written;
    return true;
}

QByteArray WebCalendarConduit::filterEventsByDate(const QByteArray &icsContent) const
{
    // Filter events based on date and recurrence rules
    // This is a basic implementation - for production use KCalendarCore

    QByteArray result;
    QDateTime now = QDateTime::currentDateTime();

    if (m_dateFilter == DateFilter::All) {
        return icsContent;  // No filtering
    }

    // Split by events and filter
    // This is a simplistic approach - proper parsing would use KCalendarCore
    QString content = QString::fromUtf8(icsContent);

    // Find header (before first VEVENT)
    int firstEvent = content.indexOf("BEGIN:VEVENT");
    if (firstEvent < 0) {
        return icsContent;  // No events, return as-is
    }

    QString header = content.left(firstEvent);
    result = header.toUtf8();

    // Extract and filter each event
    QRegularExpression eventRe("BEGIN:VEVENT.*?END:VEVENT",
                               QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator i = eventRe.globalMatch(content);

    int kept = 0;
    int filtered = 0;

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString event = match.captured();

        bool keep = false;

        // Check if event has RRULE (recurring event)
        bool hasRrule = event.contains("RRULE:");
        bool rruleHasUntil = false;
        QDateTime rruleUntil;

        if (hasRrule) {
            // Check for UNTIL in RRULE
            QRegularExpression untilRe("RRULE:.*UNTIL=([0-9T]+)");
            QRegularExpressionMatch untilMatch = untilRe.match(event);
            if (untilMatch.hasMatch()) {
                rruleHasUntil = true;
                QString untilStr = untilMatch.captured(1);
                if (untilStr.contains('T')) {
                    rruleUntil = QDateTime::fromString(untilStr.left(15), "yyyyMMdd'T'HHmmss");
                } else {
                    rruleUntil = QDateTime::fromString(untilStr.left(8), "yyyyMMdd");
                }
            }
        }

        // Extract DTSTART
        QRegularExpression dtstartRe("DTSTART[^:]*:([0-9T]+)");
        QRegularExpressionMatch dtstartMatch = dtstartRe.match(event);

        QDateTime eventDate;
        if (dtstartMatch.hasMatch()) {
            QString dateStr = dtstartMatch.captured(1);
            if (dateStr.length() >= 8) {
                if (dateStr.contains('T')) {
                    eventDate = QDateTime::fromString(dateStr.left(15), "yyyyMMdd'T'HHmmss");
                } else {
                    eventDate = QDateTime::fromString(dateStr.left(8), "yyyyMMdd");
                }
            }
        }

        // Apply filter logic
        switch (m_dateFilter) {
        case DateFilter::All:
            keep = true;
            break;

        case DateFilter::RecurringAndFuture:
            // Keep if:
            // 1. Event is in the future, OR
            // 2. Event has RRULE without UNTIL (infinite recurrence), OR
            // 3. Event has RRULE with UNTIL that's in the future
            if (eventDate.isValid() && eventDate >= now) {
                keep = true;  // Future event
            } else if (hasRrule) {
                if (!rruleHasUntil) {
                    keep = true;  // Infinite recurrence
                } else if (rruleUntil.isValid() && rruleUntil >= now) {
                    keep = true;  // Recurrence extends into future
                }
            }
            break;

        case DateFilter::FutureOnly:
            // Strict: only keep events with DTSTART in the future
            keep = eventDate.isValid() && eventDate >= now;
            break;
        }

        if (keep) {
            result += event.toUtf8();
            result += "\r\n";
            kept++;
        } else {
            filtered++;
        }
    }

    // Add footer
    result += "END:VCALENDAR\r\n";

    if (filtered > 0) {
        qDebug() << "Filtered" << filtered << "past events, kept" << kept;
    }

    return result;
}

} // namespace Sync
