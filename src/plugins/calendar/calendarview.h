#ifndef CALENDARVIEW_H
#define CALENDARVIEW_H

#include <QWidget>
#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QDate>

class QCalendarWidget;
class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QSplitter;
class QToolBar;
class CategoryManager;
class CategoryModel;
class CategoryFilterWidget;

/**
 * @brief Calendar data browser view
 *
 * Displays calendar events from individual .ics files in the sync folder
 * using KCalendarCore, with date highlighting, recurring event expansion,
 * and category filtering.
 */
class CalendarView : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarView(QWidget *parent = nullptr);
    ~CalendarView() override = default;

    void loadFromPath(const QString &syncPath);
    void refresh();

private Q_SLOTS:
    void onDateSelected();
    void onEventSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void onCategoryFilterChanged(const QString &category);
    void onManageCategories();
    void onCurrentPageChanged(int year, int month);

private:
    struct EventItem {
        QString filePath;
        QString uid;
        int recordId;
        QString summary;
        QString description;
        QDateTime dtStart;
        QDateTime dtEnd;
        bool isAllDay;
        bool hasAlarm;
        int alarmMinutesBefore;
        QString alarmDisplay;
        QString recurrenceDisplay;
        QStringList categories;
        QString category;  // Primary category for filtering
        bool isPrivate;
        bool isRecurring;
    };

    void setupUI();
    void loadEvents();
    void highlightDates();
    void updateEventList();
    QString buildAlarmDisplay(int minutesBefore) const;
    QString buildDetailHtml(const EventItem &event) const;

    QToolBar *m_toolbar;
    CategoryFilterWidget *m_categoryFilter;
    CategoryManager *m_categoryManager;
    CategoryModel *m_categoryModel;

    QSplitter *m_splitter;
    QSplitter *m_rightSplitter;
    QCalendarWidget *m_calendar;
    QListWidget *m_eventList;
    QTextEdit *m_detailsView;

    QString m_syncPath;
    QList<EventItem> m_events;
    // Map of date -> list of event indices (including recurring occurrences)
    QMap<QDate, QList<int>> m_dateToEvents;
    QHash<QListWidgetItem*, int> m_itemToIndex;
};

#endif // CALENDARVIEW_H
