#include "calendarview.h"
#include "../common/categorymanager.h"
#include "../common/categorymodel.h"
#include "../common/categoryfilterwidget.h"
#include "../dialogs/categoryeditordialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QCalendarWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QToolBar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCharFormat>
#include <QBitArray>
#include <QLocale>
#include <QSet>

#include <KLocalizedString>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Event>
#include <KCalendarCore/Alarm>
#include <KCalendarCore/Recurrence>

CalendarView::CalendarView(QWidget *parent)
    : QWidget(parent)
    , m_toolbar(nullptr)
    , m_categoryFilter(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryModel(nullptr)
    , m_splitter(nullptr)
    , m_rightSplitter(nullptr)
    , m_calendar(nullptr)
    , m_eventList(nullptr)
    , m_detailsView(nullptr)
{
    setupUI();
}

void CalendarView::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Toolbar
    m_toolbar = new QToolBar(this);

    m_toolbar->addSeparator();

    // Category filter
    m_categoryManager = new CategoryManager(QStringLiteral("calendar"), this);
    m_categoryModel = new CategoryModel(this);
    m_categoryModel->setManager(m_categoryManager);

    m_categoryFilter = new CategoryFilterWidget(this);
    m_categoryFilter->setModel(m_categoryModel);
    connect(m_categoryFilter, &CategoryFilterWidget::categoryChanged,
            this, &CalendarView::onCategoryFilterChanged);
    connect(m_categoryFilter, &CategoryFilterWidget::manageRequested,
            this, &CalendarView::onManageCategories);
    m_toolbar->addWidget(m_categoryFilter);

    layout->addWidget(m_toolbar);

    // Main splitter: calendar on left, event list + detail on right
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Calendar widget
    m_calendar = new QCalendarWidget(this);
    m_calendar->setGridVisible(true);
    connect(m_calendar, &QCalendarWidget::selectionChanged,
            this, &CalendarView::onDateSelected);
    connect(m_calendar, &QCalendarWidget::currentPageChanged,
            this, &CalendarView::onCurrentPageChanged);
    m_splitter->addWidget(m_calendar);

    // Right side: event list + detail panel
    m_rightSplitter = new QSplitter(Qt::Horizontal, this);

    m_eventList = new QListWidget(this);
    connect(m_eventList, &QListWidget::currentItemChanged,
            this, &CalendarView::onEventSelected);
    m_rightSplitter->addWidget(m_eventList);

    m_detailsView = new QTextEdit(this);
    m_detailsView->setReadOnly(true);
    m_detailsView->setPlaceholderText(i18n("Select an event to view details..."));
    m_rightSplitter->addWidget(m_detailsView);

    m_rightSplitter->setSizes({200, 300});
    m_splitter->addWidget(m_rightSplitter);

    m_splitter->setSizes({300, 500});
    layout->addWidget(m_splitter, 1);
}

void CalendarView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;

    // Initialize category manager
    m_categoryManager->setBasePath(syncPath);
    m_categoryManager->load();
    m_categoryModel->reload();

    loadEvents();
}

void CalendarView::refresh()
{
    loadEvents();
}

void CalendarView::loadEvents()
{
    m_events.clear();
    m_dateToEvents.clear();
    m_eventList->clear();
    m_itemToIndex.clear();
    m_detailsView->clear();

    if (m_syncPath.isEmpty()) {
        m_eventList->addItem(i18n("No sync folder selected"));
        return;
    }

    QString calendarPath = m_syncPath + QStringLiteral("/calendar");
    QDir calendarDir(calendarPath);

    if (!calendarDir.exists()) {
        m_eventList->addItem(i18n("No calendar data found"));
        return;
    }

    // Load all .ics files from calendar directory
    QStringList filters;
    filters << QStringLiteral("*.ics");
    QFileInfoList files = calendarDir.entryInfoList(filters, QDir::Files, QDir::Name);

    KCalendarCore::ICalFormat format;

    for (const QFileInfo &fileInfo : files) {
        KCalendarCore::MemoryCalendar::Ptr calendar(
            new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

        if (!format.load(calendar, fileInfo.filePath())) {
            continue;
        }

        KCalendarCore::Event::List kcalEvents = calendar->events();
        for (const KCalendarCore::Event::Ptr &kcalEvent : kcalEvents) {
            EventItem event;
            event.filePath = fileInfo.filePath();
            event.uid = kcalEvent->uid();
            event.recordId = 0;
            event.summary = kcalEvent->summary();
            event.description = kcalEvent->description();
            event.dtStart = kcalEvent->dtStart();
            event.dtEnd = kcalEvent->dtEnd();
            event.isAllDay = kcalEvent->allDay();
            event.isPrivate = (kcalEvent->secrecy() == KCalendarCore::Incidence::SecrecyPrivate);
            event.isRecurring = kcalEvent->recurs();

            // Parse record ID from custom property
            QString recordIdStr = kcalEvent->customProperty("QPILOTSYNC", "RECORD_ID");
            if (!recordIdStr.isEmpty()) {
                event.recordId = recordIdStr.toInt();
            }

            // Categories
            event.categories = kcalEvent->categories();
            event.category = event.categories.isEmpty()
                                 ? CategoryManager::unfiledCategoryName()
                                 : event.categories.first();

            // Alarm
            KCalendarCore::Alarm::List alarms = kcalEvent->alarms();
            if (!alarms.isEmpty()) {
                KCalendarCore::Alarm::Ptr alarm = alarms.first();
                event.hasAlarm = alarm->enabled();
                if (event.hasAlarm) {
                    KCalendarCore::Duration offset = alarm->startOffset();
                    // Offset is negative (before event start)
                    event.alarmMinutesBefore = -offset.asSeconds() / 60;
                    event.alarmDisplay = buildAlarmDisplay(event.alarmMinutesBefore);
                }
            } else {
                event.hasAlarm = false;
                event.alarmMinutesBefore = 0;
            }

            // Recurrence display
            if (event.isRecurring) {
                KCalendarCore::Recurrence *recurrence = kcalEvent->recurrence();
                switch (recurrence->recurrenceType()) {
                    case KCalendarCore::Recurrence::rDaily:
                        if (recurrence->frequency() == 1) {
                            event.recurrenceDisplay = i18n("Every day");
                        } else {
                            event.recurrenceDisplay = i18n("Every %1 days", recurrence->frequency());
                        }
                        break;
                    case KCalendarCore::Recurrence::rWeekly: {
                        QStringList dayNames;
                        QBitArray days = recurrence->days();
                        const QStringList shortDays = {
                            i18n("Mon"), i18n("Tue"), i18n("Wed"),
                            i18n("Thu"), i18n("Fri"), i18n("Sat"), i18n("Sun")
                        };
                        for (int i = 0; i < 7; i++) {
                            if (days.testBit(i)) {
                                dayNames << shortDays[i];
                            }
                        }
                        if (recurrence->frequency() == 1) {
                            event.recurrenceDisplay = i18n("Every week on %1", dayNames.join(QStringLiteral(", ")));
                        } else {
                            event.recurrenceDisplay = i18n("Every %1 weeks on %2",
                                                           recurrence->frequency(),
                                                           dayNames.join(QStringLiteral(", ")));
                        }
                        break;
                    }
                    case KCalendarCore::Recurrence::rMonthlyDay:
                        if (recurrence->frequency() == 1) {
                            QList<int> monthDays = recurrence->monthDays();
                            if (!monthDays.isEmpty()) {
                                event.recurrenceDisplay = i18n("Every month on the %1", monthDays.first());
                            } else {
                                event.recurrenceDisplay = i18n("Every month");
                            }
                        } else {
                            event.recurrenceDisplay = i18n("Every %1 months", recurrence->frequency());
                        }
                        break;
                    case KCalendarCore::Recurrence::rMonthlyPos:
                        event.recurrenceDisplay = i18n("Monthly");
                        break;
                    case KCalendarCore::Recurrence::rYearlyDay:
                    case KCalendarCore::Recurrence::rYearlyMonth:
                    case KCalendarCore::Recurrence::rYearlyPos:
                        if (recurrence->frequency() == 1) {
                            event.recurrenceDisplay = i18n("Every year on %1",
                                event.dtStart.date().toString(QStringLiteral("MMM d")));
                        } else {
                            event.recurrenceDisplay = i18n("Every %1 years", recurrence->frequency());
                        }
                        break;
                    default:
                        event.recurrenceDisplay = i18n("Repeating");
                        break;
                }
            }

            m_events.append(event);
        }
    }

    highlightDates();
    updateEventList();
}

void CalendarView::highlightDates()
{
    m_dateToEvents.clear();

    // Reset all date formats to default
    QTextCharFormat defaultFormat;
    m_calendar->setDateTextFormat(QDate(), defaultFormat);

    // Determine the visible month range
    int year = m_calendar->yearShown();
    int month = m_calendar->monthShown();
    QDate firstOfMonth(year, month, 1);
    QDate lastOfMonth(year, month, firstOfMonth.daysInMonth());

    // Expand range to include previous/next month visible days
    QDate rangeStart = firstOfMonth.addDays(-7);
    QDate rangeEnd = lastOfMonth.addDays(7);

    QString categoryFilter = m_categoryFilter->selectedCategory();
    bool showAll = m_categoryFilter->isAllSelected();

    for (int i = 0; i < m_events.size(); ++i) {
        const EventItem &event = m_events[i];

        // Category filter
        if (!showAll && event.category != categoryFilter) {
            continue;
        }

        if (event.isRecurring) {
            // Load the file again to use KCalendarCore recurrence expansion
            KCalendarCore::MemoryCalendar::Ptr calendar(
                new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
            KCalendarCore::ICalFormat format;

            if (format.load(calendar, event.filePath)) {
                KCalendarCore::Event::List kcalEvents = calendar->events();
                for (const KCalendarCore::Event::Ptr &kcalEvent : kcalEvents) {
                    if (kcalEvent->uid() == event.uid) {
                        QDateTime start(rangeStart, QTime(0, 0), QTimeZone::systemTimeZone());
                        QDateTime end(rangeEnd, QTime(23, 59, 59), QTimeZone::systemTimeZone());
                        KCalendarCore::DateTimeList occurrences = kcalEvent->recurrence()->timesInInterval(start, end);
                        for (const QDateTime &occ : occurrences) {
                            m_dateToEvents[occ.date()].append(i);
                        }
                        break;
                    }
                }
            }
        } else {
            // Non-recurring event: just add its date
            QDate eventDate = event.dtStart.date();
            if (eventDate >= rangeStart && eventDate <= rangeEnd) {
                m_dateToEvents[eventDate].append(i);
            }

            // For multi-day events, add all dates in range
            if (event.dtEnd.isValid() && event.dtEnd.date() > eventDate) {
                QDate d = eventDate.addDays(1);
                while (d <= event.dtEnd.date() && d <= rangeEnd) {
                    m_dateToEvents[d].append(i);
                    d = d.addDays(1);
                }
            }
        }
    }

    // Apply highlight format to dates with events
    QTextCharFormat highlightFormat;
    highlightFormat.setFontWeight(QFont::Bold);
    highlightFormat.setBackground(QColor(200, 220, 255));

    for (auto it = m_dateToEvents.constBegin(); it != m_dateToEvents.constEnd(); ++it) {
        m_calendar->setDateTextFormat(it.key(), highlightFormat);
    }
}

void CalendarView::updateEventList()
{
    m_eventList->clear();
    m_itemToIndex.clear();
    m_detailsView->clear();

    QDate selectedDate = m_calendar->selectedDate();

    QString categoryFilter = m_categoryFilter->selectedCategory();
    bool showAll = m_categoryFilter->isAllSelected();

    // Collect events for the selected date
    struct EventEntry {
        int eventIndex;
        QTime startTime;
        bool isAllDay;
        QString displayText;
    };
    QList<EventEntry> entries;

    if (m_dateToEvents.contains(selectedDate)) {
        const QList<int> &indices = m_dateToEvents[selectedDate];
        for (int idx : indices) {
            const EventItem &event = m_events[idx];

            if (!showAll && event.category != categoryFilter) {
                continue;
            }

            EventEntry entry;
            entry.eventIndex = idx;
            entry.isAllDay = event.isAllDay;
            entry.startTime = event.dtStart.time();

            if (event.isAllDay) {
                entry.displayText = i18n("All day: %1", event.summary);
            } else {
                entry.displayText = QStringLiteral("%1  %2")
                    .arg(event.dtStart.time().toString(QStringLiteral("HH:mm")),
                         event.summary);
            }

            entries.append(entry);
        }
    } else {
        // Also check non-recurring events directly (fallback)
        for (int i = 0; i < m_events.size(); ++i) {
            const EventItem &event = m_events[i];

            if (!showAll && event.category != categoryFilter) {
                continue;
            }

            if (event.dtStart.date() == selectedDate ||
                (event.dtEnd.isValid() && event.dtStart.date() <= selectedDate && event.dtEnd.date() >= selectedDate)) {

                EventEntry entry;
                entry.eventIndex = i;
                entry.isAllDay = event.isAllDay;
                entry.startTime = event.dtStart.time();

                if (event.isAllDay) {
                    entry.displayText = i18n("All day: %1", event.summary);
                } else {
                    entry.displayText = QStringLiteral("%1  %2")
                        .arg(event.dtStart.time().toString(QStringLiteral("HH:mm")),
                             event.summary);
                }

                entries.append(entry);
            }
        }
    }

    // Sort: all-day events first, then by start time
    std::sort(entries.begin(), entries.end(),
              [](const EventEntry &a, const EventEntry &b) {
                  if (a.isAllDay != b.isAllDay) return a.isAllDay;
                  return a.startTime < b.startTime;
              });

    // Remove duplicates (same event index)
    QSet<int> seen;
    QList<EventEntry> unique;
    for (const EventEntry &entry : entries) {
        if (!seen.contains(entry.eventIndex)) {
            seen.insert(entry.eventIndex);
            unique.append(entry);
        }
    }

    for (const EventEntry &entry : unique) {
        QListWidgetItem *item = new QListWidgetItem(entry.displayText, m_eventList);
        m_itemToIndex[item] = entry.eventIndex;
    }

    if (m_eventList->count() == 0) {
        m_eventList->addItem(i18n("No events on %1",
                                  QLocale().toString(selectedDate, QLocale::LongFormat)));
    }
}

QString CalendarView::buildAlarmDisplay(int minutesBefore) const
{
    if (minutesBefore <= 0) {
        return i18n("At time of event");
    }
    if (minutesBefore < 60) {
        return i18np("%1 minute before", "%1 minutes before", minutesBefore);
    }
    if (minutesBefore < 1440) {
        int hours = minutesBefore / 60;
        return i18np("%1 hour before", "%1 hours before", hours);
    }
    int days = minutesBefore / 1440;
    return i18np("%1 day before", "%1 days before", days);
}

QString CalendarView::buildDetailHtml(const EventItem &event) const
{
    QString html = QStringLiteral("<html><body style='font-family: sans-serif;'>");

    // Summary header
    QString summary = event.summary.isEmpty() ? i18n("(No title)") : event.summary;
    html += QStringLiteral("<h2>%1</h2>").arg(summary.toHtmlEscaped());

    html += QStringLiteral("<hr>");

    // Date and time
    html += QStringLiteral("<h3>%1</h3>").arg(i18n("When"));
    if (event.isAllDay) {
        if (event.dtEnd.isValid() && event.dtEnd.date() > event.dtStart.date()) {
            html += QStringLiteral("<p>%1 - %2 (%3)</p>")
                .arg(QLocale().toString(event.dtStart.date(), QLocale::LongFormat),
                     QLocale().toString(event.dtEnd.date(), QLocale::LongFormat),
                     i18n("All day"));
        } else {
            html += QStringLiteral("<p>%1 (%2)</p>")
                .arg(QLocale().toString(event.dtStart.date(), QLocale::LongFormat),
                     i18n("All day"));
        }
    } else {
        QString startStr = event.dtStart.toString(QStringLiteral("ddd, MMM d yyyy  HH:mm"));
        if (event.dtEnd.isValid() && event.dtEnd != event.dtStart) {
            if (event.dtEnd.date() == event.dtStart.date()) {
                // Same day
                html += QStringLiteral("<p>%1 - %2</p>")
                    .arg(startStr,
                         event.dtEnd.time().toString(QStringLiteral("HH:mm")));
            } else {
                // Multi-day
                html += QStringLiteral("<p>%1 - %2</p>")
                    .arg(startStr,
                         event.dtEnd.toString(QStringLiteral("ddd, MMM d yyyy  HH:mm")));
            }
        } else {
            html += QStringLiteral("<p>%1</p>").arg(startStr);
        }
    }

    // Recurrence
    if (!event.recurrenceDisplay.isEmpty()) {
        html += QStringLiteral("<p><b>%1:</b> %2</p>")
            .arg(i18n("Repeats"), event.recurrenceDisplay.toHtmlEscaped());
    }

    // Alarm
    if (event.hasAlarm) {
        html += QStringLiteral("<p><b>%1:</b> %2</p>")
            .arg(i18n("Alarm"), event.alarmDisplay.toHtmlEscaped());
    }

    // Category
    if (!event.categories.isEmpty()) {
        html += QStringLiteral("<p><b>%1:</b> %2</p>")
            .arg(i18n("Category"), event.categories.join(QStringLiteral(", ")).toHtmlEscaped());
    }

    // Description/Notes
    if (!event.description.isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(i18n("Notes"));
        html += QStringLiteral("<p>%1</p>")
            .arg(event.description.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    }

    html += QStringLiteral("</body></html>");
    return html;
}

void CalendarView::onDateSelected()
{
    updateEventList();
}

void CalendarView::onEventSelected(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)

    if (!current || !m_itemToIndex.contains(current)) {
        m_detailsView->clear();
        return;
    }

    int index = m_itemToIndex[current];
    const EventItem &event = m_events[index];
    m_detailsView->setHtml(buildDetailHtml(event));
}

void CalendarView::onCategoryFilterChanged(const QString &category)
{
    Q_UNUSED(category)
    highlightDates();
    updateEventList();
}

void CalendarView::onManageCategories()
{
    CategoryEditorDialog dialog(m_categoryManager, this);
    dialog.exec();

    m_categoryModel->reload();
}

void CalendarView::onCurrentPageChanged(int year, int month)
{
    Q_UNUSED(year)
    Q_UNUSED(month)
    highlightDates();
}
