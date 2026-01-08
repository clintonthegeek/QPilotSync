#ifndef WEBCALENDARCONDUIT_H
#define WEBCALENDARCONDUIT_H

#include "../conduit.h"
#include <QUrl>
#include <QDateTime>
#include <QJsonArray>

class QNetworkAccessManager;
class QNetworkReply;

namespace Sync {

/**
 * @brief Feed configuration for web calendar subscriptions
 */
struct WebCalendarFeed
{
    QString name;           ///< Display name (e.g., "Work Calendar")
    QUrl url;               ///< iCalendar feed URL
    QString category;       ///< Palm category to assign events to
    bool enabled = true;    ///< Whether this feed is active

    QJsonObject toJson() const;
    static WebCalendarFeed fromJson(const QJsonObject &obj);
};

/**
 * @brief Fetch interval for web calendar updates
 */
enum class FetchInterval
{
    EverySync,      ///< Fetch on every HotSync
    Daily,          ///< Once per day
    Weekly,         ///< Once per week
    Monthly         ///< Once per month
};

/**
 * @brief Conduit for fetching remote iCalendar feeds
 *
 * Downloads .ics files from URLs and writes them to the calendar
 * subscriptions folder. The CalendarConduit then picks up these
 * files during sync.
 *
 * Features:
 *   - Multiple feed URLs with individual names/categories
 *   - Configurable fetch interval (skip if recently fetched)
 *   - Date filtering (future events only, next 90 days, etc.)
 *   - Offline-tolerant (warns but continues if fetch fails)
 *
 * This conduit does NOT require a Palm device connection.
 * It runs BEFORE CalendarConduit to provide fresh data.
 */
class WebCalendarConduit : public Conduit
{
    Q_OBJECT

public:
    explicit WebCalendarConduit(QObject *parent = nullptr);
    ~WebCalendarConduit() override;

    // ========== Conduit Identity ==========

    QString conduitId() const override { return "webcalendar"; }
    QString displayName() const override { return "Web Calendar Subscriptions"; }
    QString palmDatabaseName() const override { return QString(); }  // No Palm DB
    QString fileExtension() const override { return ".ics"; }

    // ========== Conduit Metadata ==========

    QString description() const override {
        return "Subscribe to remote iCalendar feeds (read-only)";
    }
    QString version() const override { return "1.0.0"; }

    // ========== Capabilities ==========

    bool requiresDevice() const override { return false; }
    bool canSyncToPalm() const override { return false; }  // Read-only from web
    bool canSyncFromPalm() const override { return false; }

    // ========== Dependency Ordering ==========

    QStringList runBefore() const override { return {"calendar"}; }

    // ========== Settings ==========

    bool hasSettings() const override { return true; }
    QWidget* createSettingsWidget(QWidget *parent) override;
    void loadSettings(const QJsonObject &settings) override;
    QJsonObject saveSettings() const override;

    // ========== Pre-Sync Check ==========

    bool shouldRun(SyncContext *context) const override;

    // ========== Sync Operation ==========

    SyncResult sync(SyncContext *context) override;

    // ========== Feed Management ==========

    QList<WebCalendarFeed> feeds() const { return m_feeds; }
    void setFeeds(const QList<WebCalendarFeed> &feeds) { m_feeds = feeds; }
    void addFeed(const WebCalendarFeed &feed);
    void removeFeed(int index);

    FetchInterval fetchInterval() const { return m_fetchInterval; }
    void setFetchInterval(FetchInterval interval) { m_fetchInterval = interval; }

    /**
     * @brief Date filtering options for imported events
     */
    enum class DateFilter {
        All,              ///< Import all events
        RecurringAndFuture, ///< Keep recurring events (even if started in past) + future events
        FutureOnly        ///< Only events with DTSTART in the future
    };

    DateFilter dateFilter() const { return m_dateFilter; }
    void setDateFilter(DateFilter filter) { m_dateFilter = filter; }

    // ========== Record Conversion (not used) ==========

    BackendRecord* palmToBackend(PilotRecord *palmRecord,
                                  SyncContext *context) override {
        Q_UNUSED(palmRecord); Q_UNUSED(context);
        return nullptr;
    }

    PilotRecord* backendToPalm(BackendRecord *backendRecord,
                                SyncContext *context) override {
        Q_UNUSED(backendRecord); Q_UNUSED(context);
        return nullptr;
    }

    bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override {
        Q_UNUSED(palm); Q_UNUSED(backend);
        return false;
    }

    QString palmRecordDescription(PilotRecord *record) const override {
        Q_UNUSED(record);
        return QString();
    }

private:
    /**
     * @brief Fetch a single calendar feed
     * @return true if fetch succeeded
     */
    bool fetchFeed(const WebCalendarFeed &feed, const QString &outputDir,
                   SyncResult &result);

    /**
     * @brief Filter events by date range
     * @param icsContent Raw .ics file content
     * @return Filtered .ics content
     */
    QByteArray filterEventsByDate(const QByteArray &icsContent) const;

    /**
     * @brief Check if enough time has passed since last fetch
     */
    bool shouldFetchNow() const;

    QList<WebCalendarFeed> m_feeds;
    FetchInterval m_fetchInterval = FetchInterval::Weekly;
    QDateTime m_lastFetchTime;

    DateFilter m_dateFilter = DateFilter::RecurringAndFuture;

    QNetworkAccessManager *m_networkManager = nullptr;
};

} // namespace Sync

#endif // WEBCALENDARCONDUIT_H
