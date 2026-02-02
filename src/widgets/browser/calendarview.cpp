#include "calendarview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QCalendarWidget>
#include <QListWidget>
#include <QLabel>
#include <QDir>
#include <QFile>

#include <KLocalizedString>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

CalendarView::CalendarView(QWidget *parent)
    : QWidget(parent)
    , m_splitter(nullptr)
    , m_calendar(nullptr)
    , m_eventList(nullptr)
{
    setupUI();
}

void CalendarView::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *titleLabel = new QLabel(i18n("<h3>Calendar Events</h3>"));
    layout->addWidget(titleLabel);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Calendar widget
    m_calendar = new QCalendarWidget(this);
    m_calendar->setGridVisible(true);
    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &CalendarView::loadEvents);
    m_splitter->addWidget(m_calendar);

    // Event list
    m_eventList = new QListWidget(this);
    m_splitter->addWidget(m_eventList);

    m_splitter->setSizes({400, 400});

    layout->addWidget(m_splitter);
}

void CalendarView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;
    loadEvents();
}

void CalendarView::refresh()
{
    loadEvents();
}

void CalendarView::loadEvents()
{
    m_eventList->clear();

    if (m_syncPath.isEmpty()) {
        m_eventList->addItem(i18n("No sync folder selected"));
        return;
    }

    QString calendarPath = m_syncPath + QStringLiteral("/calendar.ics");
    QFile file(calendarPath);

    if (!file.exists()) {
        m_eventList->addItem(i18n("No calendar data found"));
        return;
    }

    // Load calendar using KCalendarCore
    KCalendarCore::MemoryCalendar::Ptr calendar(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    KCalendarCore::ICalFormat format;

    if (!format.load(calendar, calendarPath)) {
        m_eventList->addItem(i18n("Failed to load calendar"));
        return;
    }

    // Get events for selected date
    QDate selectedDate = m_calendar->selectedDate();
    KCalendarCore::Event::List events = calendar->events(selectedDate);

    if (events.isEmpty()) {
        m_eventList->addItem(i18n("No events on %1", selectedDate.toString()));
        return;
    }

    for (const KCalendarCore::Event::Ptr &event : events) {
        QString time = event->dtStart().time().toString(QStringLiteral("hh:mm"));
        QString text = QStringLiteral("%1  %2").arg(time, event->summary());
        m_eventList->addItem(text);
    }
}
