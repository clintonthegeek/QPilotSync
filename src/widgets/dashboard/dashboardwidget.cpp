#include "dashboardwidget.h"
#include "syncstatusmodel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QLocale>
#include <QIcon>

#include <KLocalizedString>

DashboardWidget::DashboardWidget(QWidget *parent) : QWidget(parent)
{
    setupUI();
}

void DashboardWidget::setupUI()
{
    setFixedHeight(140);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);
    root->setSpacing(6);

    // ---- top tier ----
    auto *top = new QHBoxLayout;
    top->setSpacing(16);

    m_deviceIconLabel = new QLabel;
    m_deviceIconLabel->setFixedSize(48, 48);
    top->addWidget(m_deviceIconLabel);

    auto *deviceCol = new QVBoxLayout;
    deviceCol->setSpacing(2);
    m_deviceNameLabel = new QLabel(i18n("No device"));
    m_deviceNameLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_deviceStatusLabel = new QLabel(i18n("Disconnected"));
    m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_deviceDetailsLabel = new QLabel;
    m_deviceDetailsLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    deviceCol->addWidget(m_deviceNameLabel);
    deviceCol->addWidget(m_deviceStatusLabel);
    deviceCol->addWidget(m_deviceDetailsLabel);
    deviceCol->addStretch();
    top->addLayout(deviceCol);

    auto *sep1 = new QFrame; sep1->setFrameShape(QFrame::VLine); sep1->setFrameShadow(QFrame::Sunken);
    top->addWidget(sep1);

    auto *profileCol = new QVBoxLayout;
    profileCol->setSpacing(2);
    m_profileNameLabel = new QLabel(i18n("No profile"));
    m_profileNameLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_lastSyncLabel = new QLabel(i18n("Last sync: Never"));
    m_lastSyncLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_autoSyncLabel = new QLabel;
    m_autoSyncLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    profileCol->addWidget(m_profileNameLabel);
    profileCol->addWidget(m_lastSyncLabel);
    profileCol->addWidget(m_autoSyncLabel);
    profileCol->addStretch();
    top->addLayout(profileCol);

    auto *sep2 = new QFrame; sep2->setFrameShape(QFrame::VLine); sep2->setFrameShadow(QFrame::Sunken);
    top->addWidget(sep2);

    auto *nowCol = new QVBoxLayout;
    nowCol->setSpacing(2);
    m_headlineLabel = new QLabel;
    m_headlineLabel->setAlignment(Qt::AlignCenter);
    m_headlineLabel->setWordWrap(true);
    m_progressBar = new QProgressBar;
    m_progressBar->setTextVisible(true);
    m_progressBar->hide();
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_primaryButton = new QPushButton;
    m_primaryButton->hide();
    m_conflictButton = new QPushButton;
    m_conflictButton->setFlat(true);
    m_conflictButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_conflictButton->hide();
    btnRow->addWidget(m_primaryButton);
    btnRow->addWidget(m_conflictButton);
    btnRow->addStretch();
    nowCol->addWidget(m_headlineLabel);
    nowCol->addWidget(m_progressBar);
    nowCol->addLayout(btnRow);
    nowCol->addStretch();
    top->addLayout(nowCol, 1);

    root->addLayout(top);

    auto *sep3 = new QFrame; sep3->setFrameShape(QFrame::HLine); sep3->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep3);

    // ---- bottom tier: conduit chips ----
    m_conduitRow = new QHBoxLayout;
    m_conduitRow->setSpacing(14);
    m_conduitRow->addStretch();
    root->addLayout(m_conduitRow);

    m_relativeTimer = new QTimer(this);
    m_relativeTimer->setInterval(60 * 1000);
    connect(m_relativeTimer, &QTimer::timeout, this, &DashboardWidget::render);
    m_relativeTimer->start();
}

void DashboardWidget::setModel(SyncStatusModel *model)
{
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        connect(m_model, &SyncStatusModel::changed, this, &DashboardWidget::render);
        connect(m_primaryButton, &QPushButton::clicked,
                m_model, &SyncStatusModel::triggerPrimaryAction);
        connect(m_conflictButton, &QPushButton::clicked,
                m_model, &SyncStatusModel::triggerResolveConflicts);
    }
    render();
}

static QString relativeTime(const QDateTime &t)
{
    if (!t.isValid())
        return i18n("Last sync: Never");
    const qint64 secs = t.secsTo(QDateTime::currentDateTime());
    if (secs < 60)        return i18n("Synced just now");
    if (secs < 3600)      return i18n("Synced %1 min ago", secs / 60);
    if (secs < 86400)     return i18n("Synced %1 h ago", secs / 3600);
    return i18n("Last sync: %1", QLocale().toString(t, QLocale::ShortFormat));
}

void DashboardWidget::render()
{
    if (!m_model)
        return;
    using LS = SyncStatusModel::LinkState;
    const LS state = m_model->linkState();
    const bool connected = (state == LS::Connected || state == LS::Syncing);

    // device
    m_deviceNameLabel->setText(m_model->deviceName().isEmpty()
        ? i18n("No device") : m_model->deviceName());
    m_deviceDetailsLabel->setText(m_model->deviceDetails());
    m_deviceDetailsLabel->setVisible(!m_model->deviceDetails().isEmpty());
    if (connected) {
        m_deviceStatusLabel->setText(i18n("Connected"));
        m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        m_deviceIconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("network-connect")).pixmap(48, 48));
    } else {
        m_deviceStatusLabel->setText(state == LS::Disconnected ? i18n("Disconnected") : i18n("Listening"));
        m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_deviceIconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("phone")).pixmap(48, 48));
    }

    // profile
    m_profileNameLabel->setText(m_model->profileName().isEmpty()
        ? i18n("No profile") : m_model->profileName());
    m_lastSyncLabel->setText(relativeTime(m_model->lastSyncTime()));
    m_autoSyncLabel->setText(m_model->autoSyncPlan());
    m_autoSyncLabel->setVisible(!m_model->autoSyncPlan().isEmpty());

    // now zone
    m_headlineLabel->setText(m_model->headline());
    if (state == LS::Syncing && m_model->progressTotal() > 0) {
        m_progressBar->setRange(0, m_model->progressTotal());
        m_progressBar->setValue(m_model->progressCurrent());
        m_progressBar->show();
    } else {
        m_progressBar->hide();
    }
    const QString action = m_model->primaryActionLabel();
    m_primaryButton->setText(action);
    m_primaryButton->setVisible(!action.isEmpty());

    const int conflicts = m_model->conflictCount();
    m_conflictButton->setText(i18n("%1 conflicts", conflicts));
    m_conflictButton->setVisible(conflicts > 0);

    renderConduits();
}

void DashboardWidget::renderConduits()
{
    // Clear existing chip widgets (keep the trailing stretch).
    while (m_conduitRow->count() > 0) {
        QLayoutItem *item = m_conduitRow->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    using CS = SyncStatusModel::ChipState;
    for (const auto &c : m_model->conduits()) {
        QString glyph;
        QString color = QStringLiteral("gray");
        switch (c.state) {
        case CS::Pending:     glyph = QStringLiteral("·"); break;
        case CS::Active:      glyph = QStringLiteral("⟳ %1/%2").arg(c.current).arg(c.total); color = QStringLiteral("#1d6fb8"); break;
        case CS::Done:        glyph = QStringLiteral("✓ +%1 ~%2 −%3").arg(c.created).arg(c.modified).arg(c.deleted); color = QStringLiteral("green"); break;
        case CS::Error:       glyph = QStringLiteral("✗"); color = QStringLiteral("#c0392b"); break;
        case CS::Interrupted: glyph = QStringLiteral("⚠"); color = QStringLiteral("#c0392b"); break;
        }
        auto *chip = new QLabel(QStringLiteral("%1 %2").arg(c.label, glyph));
        chip->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(color));
        m_conduitRow->addWidget(chip);
    }
    m_conduitRow->addStretch();
}
