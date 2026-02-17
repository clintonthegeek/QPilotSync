#ifndef PLUCKERVIEW_H
#define PLUCKERVIEW_H

#include <QWidget>

class PluckerView : public QWidget
{
    Q_OBJECT
public:
    explicit PluckerView(QWidget *parent = nullptr);

public Q_SLOTS:
    void loadFromPath(const QString &syncPath);
    void refresh();
};

#endif // PLUCKERVIEW_H
