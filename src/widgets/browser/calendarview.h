#ifndef CALENDARVIEW_H
#define CALENDARVIEW_H

#include <QWidget>

class QCalendarWidget;
class QListWidget;
class QSplitter;

/**
 * @brief Calendar data browser view
 *
 * Displays calendar events from the sync folder using KCalendarCore.
 */
class CalendarView : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarView(QWidget *parent = nullptr);
    ~CalendarView() override = default;

    void loadFromPath(const QString &syncPath);
    void refresh();

private:
    void setupUI();
    void loadEvents();

    QSplitter *m_splitter;
    QCalendarWidget *m_calendar;
    QListWidget *m_eventList;

    QString m_syncPath;
};

#endif // CALENDARVIEW_H
