#ifndef INSTALLVIEW_H
#define INSTALLVIEW_H

#include <QWidget>

class QListWidget;
class QPushButton;

class InstallView : public QWidget
{
    Q_OBJECT

public:
    explicit InstallView(QWidget *parent = nullptr);

    void setInstallFolder(const QString &path);
    void refresh();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onClearInstalled();

private:
    void populatePendingList();
    void populateInstalledList();

    QString m_installFolder;

    QListWidget *m_pendingList;
    QListWidget *m_installedList;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_clearInstalledButton;
    QPushButton *m_refreshButton;
};

#endif // INSTALLVIEW_H
