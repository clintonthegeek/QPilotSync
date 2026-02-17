#include "pluckerview.h"
#include "pluckerchanneldialog.h"
#include "pluckerconfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QMessageBox>

#include <KLocalizedString>

PluckerView::PluckerView(QWidget *parent)
    : QWidget(parent)
    , m_channelList(nullptr)
    , m_detailLabel(nullptr)
    , m_addBtn(nullptr)
    , m_editBtn(nullptr)
    , m_removeBtn(nullptr)
    , m_fetchBtn(nullptr)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Channel tree widget
    m_channelList = new QTreeWidget(this);
    m_channelList->setHeaderLabels({i18n("Name"), i18n("Last Fetched"), i18n("Status")});
    m_channelList->setRootIsDecorated(false);
    m_channelList->setAlternatingRowColors(true);
    m_channelList->header()->setStretchLastSection(false);
    m_channelList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_channelList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_channelList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_channelList, &QTreeWidget::itemSelectionChanged,
            this, &PluckerView::onSelectionChanged);
    mainLayout->addWidget(m_channelList, 1);

    // Button row
    auto *buttonLayout = new QHBoxLayout;

    m_addBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                               i18n("Add"), this);
    connect(m_addBtn, &QPushButton::clicked, this, &PluckerView::onAdd);
    buttonLayout->addWidget(m_addBtn);

    m_editBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")),
                                i18n("Edit"), this);
    m_editBtn->setEnabled(false);
    connect(m_editBtn, &QPushButton::clicked, this, &PluckerView::onEdit);
    buttonLayout->addWidget(m_editBtn);

    m_removeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                  i18n("Remove"), this);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &PluckerView::onRemove);
    buttonLayout->addWidget(m_removeBtn);

    buttonLayout->addStretch();

    m_fetchBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("download")),
                                 i18n("Fetch Now"), this);
    m_fetchBtn->setEnabled(false);
    connect(m_fetchBtn, &QPushButton::clicked, this, &PluckerView::onFetchNow);
    buttonLayout->addWidget(m_fetchBtn);

    mainLayout->addLayout(buttonLayout);

    // Detail panel
    m_detailLabel = new QLabel(this);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setTextFormat(Qt::RichText);
    m_detailLabel->setFrameShape(QFrame::StyledPanel);
    m_detailLabel->setMinimumHeight(80);
    m_detailLabel->setText(i18n("Select a channel to view details."));
    mainLayout->addWidget(m_detailLabel);
}

void PluckerView::loadFromPath(const QString &syncPath)
{
    m_syncPath = syncPath;
    m_config.load(syncPath);
    populateList();
}

void PluckerView::refresh()
{
    if (!m_syncPath.isEmpty()) {
        m_config.load(m_syncPath);
        populateList();
    }
}

void PluckerView::populateList()
{
    m_channelList->clear();
    m_detailLabel->setText(i18n("Select a channel to view details."));
    m_editBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_fetchBtn->setEnabled(false);

    const QList<PluckerChannel> channels = m_config.channels();
    for (const PluckerChannel &ch : channels) {
        auto *item = new QTreeWidgetItem(m_channelList);
        item->setText(0, ch.name);

        if (ch.lastFetched.isValid()) {
            item->setText(1, ch.lastFetched.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        } else {
            item->setText(1, i18n("Never"));
        }

        bool due = PluckerConfig::isDue(ch);
        if (due) {
            item->setText(2, i18n("Due"));
            item->setForeground(2, QBrush(Qt::red));
        } else {
            item->setText(2, i18n("OK"));
        }

        item->setData(0, Qt::UserRole, ch.id);
    }
}

void PluckerView::onAdd()
{
    PluckerChannelDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        PluckerChannel ch = dialog.channel();
        m_config.addChannel(ch);
        m_config.save(m_syncPath);
        populateList();
        Q_EMIT channelsModified();
    }
}

void PluckerView::onEdit()
{
    QTreeWidgetItem *item = m_channelList->currentItem();
    if (!item) {
        return;
    }

    QString id = item->data(0, Qt::UserRole).toString();
    PluckerChannel ch = m_config.channel(id);

    PluckerChannelDialog dialog(ch, this);
    if (dialog.exec() == QDialog::Accepted) {
        PluckerChannel updated = dialog.channel();
        m_config.updateChannel(updated);
        m_config.save(m_syncPath);
        populateList();
        Q_EMIT channelsModified();
    }
}

void PluckerView::onRemove()
{
    QTreeWidgetItem *item = m_channelList->currentItem();
    if (!item) {
        return;
    }

    QString name = item->text(0);
    int result = QMessageBox::question(this, i18n("Remove Channel"),
                                       i18n("Remove channel '%1'?", name),
                                       QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes) {
        return;
    }

    QString id = item->data(0, Qt::UserRole).toString();
    m_config.removeChannel(id);
    m_config.save(m_syncPath);
    populateList();
    Q_EMIT channelsModified();
}

void PluckerView::onFetchNow()
{
    QMessageBox::information(this, i18n("Fetch Now"),
                             i18n("Not implemented yet."));
}

void PluckerView::onSelectionChanged()
{
    QTreeWidgetItem *item = m_channelList->currentItem();
    if (!item) {
        m_editBtn->setEnabled(false);
        m_removeBtn->setEnabled(false);
        m_fetchBtn->setEnabled(false);
        m_detailLabel->setText(i18n("Select a channel to view details."));
        return;
    }

    m_editBtn->setEnabled(true);
    m_removeBtn->setEnabled(true);
    m_fetchBtn->setEnabled(true);

    QString id = item->data(0, Qt::UserRole).toString();
    PluckerChannel ch = m_config.channel(id);
    updateDetailPanel(ch);
}

void PluckerView::updateDetailPanel(const PluckerChannel &channel)
{
    QString schedule;
    if (channel.updateEnabled) {
        schedule = i18n("Every %1 %2", channel.updateFrequency, channel.updatePeriod);
    } else {
        schedule = i18n("Manual");
    }

    QString html = QStringLiteral(
        "<b>%1</b><br/>"
        "<b>URL:</b> %2<br/>"
        "<b>Depth:</b> %3<br/>"
        "<b>BPP:</b> %4<br/>"
        "<b>Compression:</b> %5<br/>"
        "<b>Schedule:</b> %6<br/>"
        "<b>Category:</b> %7"
    ).arg(
        channel.name.toHtmlEscaped(),
        channel.homeUrl.toHtmlEscaped(),
        QString::number(channel.maxDepth),
        QString::number(channel.bpp),
        channel.compression.toHtmlEscaped(),
        schedule.toHtmlEscaped(),
        channel.category.isEmpty() ? i18n("(none)") : channel.category.toHtmlEscaped()
    );

    m_detailLabel->setText(html);
}
