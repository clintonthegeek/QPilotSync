#ifndef EXPORTHANDLER_H
#define EXPORTHANDLER_H

#include <QObject>
#include <QString>

class QWidget;
class KPilotDeviceLink;
class LogWidget;

/**
 * @brief Handles export operations from Palm device to local files
 *
 * Manages exporting memos, contacts, calendar events, and todos
 * from the connected Palm device to various file formats.
 */
class ExportHandler : public QObject
{
    Q_OBJECT

public:
    explicit ExportHandler(QWidget *parent = nullptr);

    void setDeviceLink(KPilotDeviceLink *link) { m_deviceLink = link; }
    void setLogWidget(LogWidget *log) { m_logWidget = log; }

    // Individual export operations (with user dialogs)
    void exportMemos();
    void exportContacts();
    void exportCalendar();
    void exportTodos();
    void exportAll();

    // Batch export helpers (no dialogs, for programmatic use)
    void exportMemosToDir(const QString &exportDir, int &exportedCount, int &skippedCount);
    void exportContactsToDir(const QString &exportDir, int &exportedCount, int &skippedCount);
    void exportCalendarToDir(const QString &exportDir, int &exportedCount, int &skippedCount);
    void exportTodosToDir(const QString &exportDir, int &exportedCount, int &skippedCount);

signals:
    void exportComplete(const QString &type, int exportedCount, int skippedCount);
    void exportError(const QString &message);

private:
    QWidget *m_parentWidget;
    KPilotDeviceLink *m_deviceLink = nullptr;
    LogWidget *m_logWidget = nullptr;
};

#endif // EXPORTHANDLER_H
