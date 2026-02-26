#include "installview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDropEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QLocale>

#include <KLocalizedString>

InstallView::InstallView(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Pending files section ---
    auto *pendingGroup = new QGroupBox(i18n("Pending Installation"), this);
    auto *pendingLayout = new QVBoxLayout(pendingGroup);

    m_pendingList = new QListWidget(pendingGroup);
    m_pendingList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pendingLayout->addWidget(m_pendingList);

    auto *pendingButtons = new QHBoxLayout;
    m_addButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                  i18n("Add Files..."), pendingGroup);
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                     i18n("Remove"), pendingGroup);
    pendingButtons->addWidget(m_addButton);
    pendingButtons->addWidget(m_removeButton);
    pendingButtons->addStretch();
    pendingLayout->addLayout(pendingButtons);

    mainLayout->addWidget(pendingGroup);

    // --- Installed history section ---
    auto *installedGroup = new QGroupBox(i18n("Installed History"), this);
    auto *installedLayout = new QVBoxLayout(installedGroup);

    m_installedList = new QListWidget(installedGroup);
    m_installedList->setSelectionMode(QAbstractItemView::NoSelection);
    installedLayout->addWidget(m_installedList);

    auto *installedButtons = new QHBoxLayout;
    m_clearInstalledButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")),
                                             i18n("Clear History"), installedGroup);
    installedButtons->addWidget(m_clearInstalledButton);
    installedButtons->addStretch();
    installedLayout->addLayout(installedButtons);

    mainLayout->addWidget(installedGroup);

    // --- Refresh button ---
    auto *bottomBar = new QHBoxLayout;
    m_refreshButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                      i18n("Refresh"), this);
    bottomBar->addStretch();
    bottomBar->addWidget(m_refreshButton);
    mainLayout->addLayout(bottomBar);

    // Connections
    connect(m_addButton, &QPushButton::clicked, this, &InstallView::onAddFiles);
    connect(m_removeButton, &QPushButton::clicked, this, &InstallView::onRemoveSelected);
    connect(m_clearInstalledButton, &QPushButton::clicked, this, &InstallView::onClearInstalled);
    connect(m_refreshButton, &QPushButton::clicked, this, &InstallView::refresh);

    // Enable drag-and-drop
    setAcceptDrops(true);
}

void InstallView::setInstallFolder(const QString &path)
{
    m_installFolder = path;
    refresh();
}

void InstallView::refresh()
{
    populatePendingList();
    populateInstalledList();
}

void InstallView::loadFromPath(const QString &syncPath)
{
    if (syncPath.isEmpty()) {
        m_installFolder.clear();
        refresh();
        return;
    }
    setInstallFolder(QDir(syncPath).filePath(QStringLiteral("install")));
}

void InstallView::populatePendingList()
{
    m_pendingList->clear();
    if (m_installFolder.isEmpty()) return;

    QDir dir(m_installFolder);
    if (!dir.exists()) return;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable,
                                                     QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : entries) {
        auto *item = new QListWidgetItem(info.fileName());
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(i18n("Size: %1 bytes", info.size()));
        m_pendingList->addItem(item);
    }
}

void InstallView::populateInstalledList()
{
    m_installedList->clear();
    if (m_installFolder.isEmpty()) return;

    QDir dir(QDir(m_installFolder).filePath(QStringLiteral("installed")));
    if (!dir.exists()) return;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable,
                                                     QDir::Time | QDir::Reversed);
    for (const QFileInfo &info : entries) {
        auto *item = new QListWidgetItem(info.fileName());
        item->setToolTip(i18n("Installed: %1", QLocale().toString(info.lastModified(), QLocale::ShortFormat)));
        m_installedList->addItem(item);
    }
}

void InstallView::onAddFiles()
{
    if (m_installFolder.isEmpty()) return;

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        i18n("Select Palm Files to Install"),
        QString(),
        i18n("Palm Files (*.prc *.pdb *.PRC *.PDB);;All Files (*)"));

    if (files.isEmpty()) return;

    QDir installDir(m_installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        QFile::copy(filePath, destPath);
    }

    refresh();
}

void InstallView::onRemoveSelected()
{
    const QList<QListWidgetItem *> selected = m_pendingList->selectedItems();
    if (selected.isEmpty()) return;

    for (QListWidgetItem *item : selected) {
        QString filePath = item->data(Qt::UserRole).toString();
        QFile::remove(filePath);
    }

    refresh();
}

void InstallView::onClearInstalled()
{
    if (m_installFolder.isEmpty()) return;

    QDir installedDir(QDir(m_installFolder).filePath(QStringLiteral("installed")));
    if (!installedDir.exists()) return;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = installedDir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo &info : entries) {
        QFile::remove(info.absoluteFilePath());
    }

    refresh();
}

void InstallView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (path.endsWith(QLatin1String(".prc"), Qt::CaseInsensitive) ||
                path.endsWith(QLatin1String(".pdb"), Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void InstallView::dropEvent(QDropEvent *event)
{
    if (m_installFolder.isEmpty()) return;

    QDir installDir(m_installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    for (const QUrl &url : event->mimeData()->urls()) {
        QString filePath = url.toLocalFile();
        if (filePath.isEmpty()) continue;

        if (!filePath.endsWith(QLatin1String(".prc"), Qt::CaseInsensitive) &&
            !filePath.endsWith(QLatin1String(".pdb"), Qt::CaseInsensitive)) {
            continue;
        }

        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        QFile::copy(filePath, destPath);
    }

    refresh();
}
