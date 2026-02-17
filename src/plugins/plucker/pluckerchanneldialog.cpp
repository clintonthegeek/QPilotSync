#include "pluckerchanneldialog.h"

PluckerChannelDialog::PluckerChannelDialog(QWidget *parent)
    : QDialog(parent)
{
}

PluckerChannelDialog::PluckerChannelDialog(const PluckerChannel &channel,
                                             QWidget *parent)
    : QDialog(parent), m_channel(channel)
{
}

PluckerChannel PluckerChannelDialog::channel() const
{
    return m_channel;
}
