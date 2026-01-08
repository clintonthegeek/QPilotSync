#ifndef IMPORTHANDLER_H
#define IMPORTHANDLER_H

#include <QObject>
#include <QString>

class QWidget;
class KPilotDeviceLink;
class LogWidget;

/**
 * @brief Handles import operations from local files to Palm device
 *
 * Manages importing memos, contacts, calendar events, and todos
 * from various file formats to the connected Palm device.
 */
class ImportHandler : public QObject
{
    Q_OBJECT

public:
    explicit ImportHandler(QWidget *parent = nullptr);

    void setDeviceLink(KPilotDeviceLink *link) { m_deviceLink = link; }
    void setLogWidget(LogWidget *log) { m_logWidget = log; }

    // Import operations (with user dialogs)
    void importMemo();
    void importContact();
    void importEvent();
    void importTodo();

signals:
    void importComplete(const QString &type, int recordId);
    void importError(const QString &message);

private:
    QWidget *m_parentWidget;
    KPilotDeviceLink *m_deviceLink = nullptr;
    LogWidget *m_logWidget = nullptr;
};

#endif // IMPORTHANDLER_H
