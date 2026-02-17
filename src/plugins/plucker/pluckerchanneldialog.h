#ifndef PLUCKERCHANNELDIALOG_H
#define PLUCKERCHANNELDIALOG_H

#include <QDialog>
#include "pluckerconfig.h"

class PluckerChannelDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluckerChannelDialog(QWidget *parent = nullptr);
    PluckerChannelDialog(const PluckerChannel &channel, QWidget *parent = nullptr);

    PluckerChannel channel() const;

private:
    PluckerChannel m_channel;
};

#endif // PLUCKERCHANNELDIALOG_H
