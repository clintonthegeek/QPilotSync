#include "logwidget.h"

LogWidget::LogWidget(QWidget *parent)
    : QTextEdit(parent)
{
    setReadOnly(true);
    document()->setMaximumBlockCount(1000); // Limit log size
}

void LogWidget::logInfo(const QString &message)
{
    append(QString("[INFO] %1").arg(message));
}

void LogWidget::logWarning(const QString &message)
{
    append(QString("[WARNING] %1").arg(message));
}

void LogWidget::logError(const QString &message)
{
    append(QString("[ERROR] %1").arg(message));
}

void LogWidget::clear()
{
    QTextEdit::clear();
}
