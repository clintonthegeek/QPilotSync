#ifndef PLUCKERVIEW_H
#define PLUCKERVIEW_H

#include <QWidget>
#include "pluckerconfig.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

class PluckerView : public QWidget
{
    Q_OBJECT

public:
    explicit PluckerView(QWidget *parent = nullptr);

public Q_SLOTS:
    void loadFromPath(const QString &syncPath);
    void refresh();

Q_SIGNALS:
    void channelsModified();

private Q_SLOTS:
    void onAdd();
    void onEdit();
    void onRemove();
    void onFetchNow();
    void onSelectionChanged();

private:
    void populateList();
    void updateDetailPanel(const PluckerChannel &channel);

    QTreeWidget *m_channelList;
    QLabel *m_detailLabel;
    QPushButton *m_addBtn;
    QPushButton *m_editBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_fetchBtn;

    PluckerConfig m_config;
    QString m_syncPath;
};

#endif // PLUCKERVIEW_H
