#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QTextEdit>

/**
 * @brief Simple logging widget for displaying application messages
 *
 * Provides formatted log output with INFO, WARNING, and ERROR levels.
 * Automatically limits the log size to prevent memory issues.
 */
class LogWidget : public QTextEdit
{
    Q_OBJECT

public:
    explicit LogWidget(QWidget *parent = nullptr);

public slots:
    void logInfo(const QString &message);
    void logWarning(const QString &message);
    void logError(const QString &message);
    void clear();
};

#endif // LOGWIDGET_H
