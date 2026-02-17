#include "pluckerchanneldialog.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLocale>

#include <KLocalizedString>

PluckerChannelDialog::PluckerChannelDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    loadFromChannel();
}

PluckerChannelDialog::PluckerChannelDialog(const PluckerChannel &channel,
                                           QWidget *parent)
    : QDialog(parent), m_channel(channel)
{
    setupUI();
    loadFromChannel();
}

PluckerChannel PluckerChannelDialog::channel() const
{
    return m_channel;
}

void PluckerChannelDialog::setupUI()
{
    setWindowTitle(i18n("Channel Settings"));
    setMinimumSize(550, 450);

    auto *mainLayout = new QVBoxLayout(this);
    auto *tabWidget = new QTabWidget;

    // ── Tab 1: Starting Page ────────────────────────────────────────
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);

        m_urlEdit = new QLineEdit;
        m_urlEdit->setPlaceholderText(QStringLiteral("http://example.com"));
        form->addRow(i18n("URL:"), m_urlEdit);

        m_nameEdit = new QLineEdit;
        form->addRow(i18n("Document Name:"), m_nameEdit);

        m_categoryCombo = new QComboBox;
        m_categoryCombo->setEditable(true);
        m_categoryCombo->addItems({
            QString(),
            QStringLiteral("News"),
            QStringLiteral("Reference"),
            QStringLiteral("Tech"),
            QStringLiteral("Entertainment")
        });
        form->addRow(i18n("Category:"), m_categoryCombo);

        tabWidget->addTab(page, i18n("Starting Page"));
    }

    // ── Tab 2: Spidering ────────────────────────────────────────────
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);

        m_maxDepthSpin = new QSpinBox;
        m_maxDepthSpin->setRange(1, 999);
        m_maxDepthSpin->setValue(2);
        form->addRow(i18n("Max Depth:"), m_maxDepthSpin);

        m_stayOnHostCheck = new QCheckBox(i18n("Stay on same host"));
        form->addRow(QString(), m_stayOnHostCheck);

        // Retrieval order group
        auto *orderGroup = new QGroupBox(i18n("Retrieval Order"));
        auto *orderLayout = new QVBoxLayout(orderGroup);
        auto *orderBtnGroup = new QButtonGroup(this);

        m_breadthFirstRadio = new QRadioButton(i18n("Breadth-first"));
        m_depthFirstRadio = new QRadioButton(i18n("Depth-first"));
        m_breadthFirstRadio->setChecked(true);

        orderBtnGroup->addButton(m_breadthFirstRadio);
        orderBtnGroup->addButton(m_depthFirstRadio);
        orderLayout->addWidget(m_breadthFirstRadio);
        orderLayout->addWidget(m_depthFirstRadio);

        form->addRow(orderGroup);

        m_urlPatternEdit = new QLineEdit;
        m_urlPatternEdit->setPlaceholderText(i18n("Only follow URLs matching this pattern"));
        form->addRow(i18n("URL Pattern:"), m_urlPatternEdit);

        m_userAgentEdit = new QLineEdit;
        form->addRow(i18n("User Agent:"), m_userAgentEdit);

        tabWidget->addTab(page, i18n("Spidering"));
    }

    // ── Tab 3: Images ───────────────────────────────────────────────
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);

        m_bppCombo = new QComboBox;
        m_bppCombo->addItem(i18n("No images"), 0);
        m_bppCombo->addItem(i18n("1-bit"),  1);
        m_bppCombo->addItem(i18n("2-bit"),  2);
        m_bppCombo->addItem(i18n("4-bit"),  4);
        m_bppCombo->addItem(i18n("8-bit"),  8);
        m_bppCombo->addItem(i18n("16-bit"), 16);
        form->addRow(i18n("Color Depth:"), m_bppCombo);

        m_maxWidthSpin = new QSpinBox;
        m_maxWidthSpin->setRange(1, 9999);
        form->addRow(i18n("Thumbnail Max Width:"), m_maxWidthSpin);

        m_maxHeightSpin = new QSpinBox;
        m_maxHeightSpin->setRange(1, 9999);
        form->addRow(i18n("Thumbnail Max Height:"), m_maxHeightSpin);

        m_altMaxWidthSpin = new QSpinBox;
        m_altMaxWidthSpin->setRange(1, 9999);
        form->addRow(i18n("Full-size Max Width:"), m_altMaxWidthSpin);

        m_altMaxHeightSpin = new QSpinBox;
        m_altMaxHeightSpin->setRange(1, 9999);
        form->addRow(i18n("Full-size Max Height:"), m_altMaxHeightSpin);

        m_imageCompLimitSpin = new QSpinBox;
        m_imageCompLimitSpin->setRange(0, 100);
        m_imageCompLimitSpin->setSuffix(QStringLiteral("%"));
        form->addRow(i18n("Compression Limit:"), m_imageCompLimitSpin);

        tabWidget->addTab(page, i18n("Images"));
    }

    // ── Tab 4: Destination ──────────────────────────────────────────
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);

        // Storage group
        auto *storageGroup = new QGroupBox(i18n("Storage"));
        auto *storageLayout = new QVBoxLayout(storageGroup);
        auto *storageBtnGroup = new QButtonGroup(this);

        m_ramRadio = new QRadioButton(i18n("Internal RAM"));
        m_sdRadio  = new QRadioButton(i18n("SD Card"));
        m_msRadio  = new QRadioButton(i18n("Memory Stick"));
        m_cfRadio  = new QRadioButton(i18n("CompactFlash"));
        m_ramRadio->setChecked(true);

        storageBtnGroup->addButton(m_ramRadio);
        storageBtnGroup->addButton(m_sdRadio);
        storageBtnGroup->addButton(m_msRadio);
        storageBtnGroup->addButton(m_cfRadio);
        storageLayout->addWidget(m_ramRadio);
        storageLayout->addWidget(m_sdRadio);
        storageLayout->addWidget(m_msRadio);
        storageLayout->addWidget(m_cfRadio);

        form->addRow(storageGroup);

        m_cardDirEdit = new QLineEdit;
        m_cardDirEdit->setEnabled(false);
        form->addRow(i18n("Card Directory:"), m_cardDirEdit);

        m_compressionCombo = new QComboBox;
        m_compressionCombo->addItems({QStringLiteral("zlib"), QStringLiteral("DOC")});
        form->addRow(i18n("Compression:"), m_compressionCombo);

        // Enable card directory only when a card storage mode is selected
        auto updateCardDir = [this]() {
            m_cardDirEdit->setEnabled(!m_ramRadio->isChecked());
        };
        connect(m_ramRadio, &QRadioButton::toggled, this, updateCardDir);
        connect(m_sdRadio,  &QRadioButton::toggled, this, updateCardDir);
        connect(m_msRadio,  &QRadioButton::toggled, this, updateCardDir);
        connect(m_cfRadio,  &QRadioButton::toggled, this, updateCardDir);

        tabWidget->addTab(page, i18n("Destination"));
    }

    // ── Tab 5: Scheduling ───────────────────────────────────────────
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);

        m_autoUpdateCheck = new QCheckBox(i18n("Enable automatic updates"));
        m_autoUpdateCheck->setChecked(true);
        form->addRow(QString(), m_autoUpdateCheck);

        m_frequencySpin = new QSpinBox;
        m_frequencySpin->setRange(1, 999);
        form->addRow(i18n("Frequency:"), m_frequencySpin);

        m_periodCombo = new QComboBox;
        m_periodCombo->addItems({
            i18n("hours"),
            i18n("days"),
            i18n("weeks"),
            i18n("months")
        });
        form->addRow(i18n("Period:"), m_periodCombo);

        m_nextDueLabel = new QLabel;
        form->addRow(i18n("Next Due:"), m_nextDueLabel);

        // Enable/disable frequency and period based on auto-update
        auto updateScheduleWidgets = [this](bool checked) {
            m_frequencySpin->setEnabled(checked);
            m_periodCombo->setEnabled(checked);
        };
        connect(m_autoUpdateCheck, &QCheckBox::toggled, this, updateScheduleWidgets);

        tabWidget->addTab(page, i18n("Scheduling"));
    }

    mainLayout->addWidget(tabWidget);

    // ── Button box ──────────────────────────────────────────────────
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        applyToChannel();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PluckerChannelDialog::loadFromChannel()
{
    // Tab 1: Starting Page
    m_urlEdit->setText(m_channel.homeUrl);
    m_nameEdit->setText(m_channel.name);
    m_categoryCombo->setCurrentText(m_channel.category);

    // Tab 2: Spidering
    m_maxDepthSpin->setValue(m_channel.maxDepth);
    m_stayOnHostCheck->setChecked(m_channel.stayOnHost);
    if (m_channel.depthFirst) {
        m_depthFirstRadio->setChecked(true);
    } else {
        m_breadthFirstRadio->setChecked(true);
    }
    m_urlPatternEdit->setText(m_channel.urlPattern);
    m_userAgentEdit->setText(m_channel.userAgent);

    // Tab 3: Images
    // Map bpp value to combo index via stored user data
    if (m_channel.noImages) {
        m_bppCombo->setCurrentIndex(0); // "No images"
    } else {
        for (int i = 0; i < m_bppCombo->count(); ++i) {
            if (m_bppCombo->itemData(i).toInt() == m_channel.bpp) {
                m_bppCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    m_maxWidthSpin->setValue(m_channel.maxWidth);
    m_maxHeightSpin->setValue(m_channel.maxHeight);
    m_altMaxWidthSpin->setValue(m_channel.altMaxWidth);
    m_altMaxHeightSpin->setValue(m_channel.altMaxHeight);
    m_imageCompLimitSpin->setValue(m_channel.imageCompressionLimit);

    // Tab 4: Destination
    const QString &mode = m_channel.storageMode;
    if (mode == QLatin1String("sd")) {
        m_sdRadio->setChecked(true);
    } else if (mode == QLatin1String("ms")) {
        m_msRadio->setChecked(true);
    } else if (mode == QLatin1String("cf")) {
        m_cfRadio->setChecked(true);
    } else {
        m_ramRadio->setChecked(true);
    }
    m_cardDirEdit->setText(m_channel.cardDirectory);

    if (m_channel.compression == QLatin1String("DOC")) {
        m_compressionCombo->setCurrentIndex(1);
    } else {
        m_compressionCombo->setCurrentIndex(0);
    }

    // Tab 5: Scheduling
    m_autoUpdateCheck->setChecked(m_channel.updateEnabled);
    m_frequencySpin->setValue(m_channel.updateFrequency);
    m_frequencySpin->setEnabled(m_channel.updateEnabled);
    m_periodCombo->setEnabled(m_channel.updateEnabled);

    const QString &period = m_channel.updatePeriod;
    if (period == QLatin1String("hours")) {
        m_periodCombo->setCurrentIndex(0);
    } else if (period == QLatin1String("days")) {
        m_periodCombo->setCurrentIndex(1);
    } else if (period == QLatin1String("weeks")) {
        m_periodCombo->setCurrentIndex(2);
    } else if (period == QLatin1String("months")) {
        m_periodCombo->setCurrentIndex(3);
    }

    // Next due display
    if (m_channel.lastFetched.isValid()) {
        QDateTime nextDue = PluckerConfig::nextDueTime(m_channel);
        m_nextDueLabel->setText(QLocale().toString(nextDue, QLocale::LongFormat));
    } else {
        m_nextDueLabel->setText(i18n("Never fetched"));
    }
}

void PluckerChannelDialog::applyToChannel()
{
    // Tab 1: Starting Page
    m_channel.homeUrl  = m_urlEdit->text().trimmed();
    m_channel.name     = m_nameEdit->text().trimmed();
    m_channel.category = m_categoryCombo->currentText().trimmed();

    // Tab 2: Spidering
    m_channel.maxDepth   = m_maxDepthSpin->value();
    m_channel.stayOnHost = m_stayOnHostCheck->isChecked();
    m_channel.depthFirst = m_depthFirstRadio->isChecked();
    m_channel.urlPattern = m_urlPatternEdit->text().trimmed();
    m_channel.userAgent  = m_userAgentEdit->text().trimmed();

    // Tab 3: Images
    int bppValue = m_bppCombo->currentData().toInt();
    m_channel.noImages = (bppValue == 0);
    m_channel.bpp = (bppValue == 0) ? 1 : bppValue;
    m_channel.maxWidth              = m_maxWidthSpin->value();
    m_channel.maxHeight             = m_maxHeightSpin->value();
    m_channel.altMaxWidth           = m_altMaxWidthSpin->value();
    m_channel.altMaxHeight          = m_altMaxHeightSpin->value();
    m_channel.imageCompressionLimit = m_imageCompLimitSpin->value();

    // Tab 4: Destination
    if (m_sdRadio->isChecked()) {
        m_channel.storageMode = QStringLiteral("sd");
    } else if (m_msRadio->isChecked()) {
        m_channel.storageMode = QStringLiteral("ms");
    } else if (m_cfRadio->isChecked()) {
        m_channel.storageMode = QStringLiteral("cf");
    } else {
        m_channel.storageMode = QStringLiteral("ram");
    }
    m_channel.cardDirectory = m_cardDirEdit->text().trimmed();
    m_channel.compression   = m_compressionCombo->currentText();

    // Tab 5: Scheduling
    m_channel.updateEnabled   = m_autoUpdateCheck->isChecked();
    m_channel.updateFrequency = m_frequencySpin->value();

    // Map displayed (possibly localized) period back to internal string
    int periodIdx = m_periodCombo->currentIndex();
    static const char *periods[] = {"hours", "days", "weeks", "months"};
    if (periodIdx >= 0 && periodIdx < 4) {
        m_channel.updatePeriod = QString::fromLatin1(periods[periodIdx]);
    }
}
