#ifndef CATEGORYEDITORDIALOG_H
#define CATEGORYEDITORDIALOG_H

#include <QDialog>

class QListWidget;
class QPushButton;
class CategoryManager;

/**
 * @brief Dialog for managing categories
 *
 * Allows the user to add, rename, and delete categories for a data type.
 */
class CategoryEditorDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct the dialog
     * @param manager The category manager to modify
     * @param parent Parent widget
     */
    explicit CategoryEditorDialog(CategoryManager *manager, QWidget *parent = nullptr);
    ~CategoryEditorDialog() override = default;

private Q_SLOTS:
    void onAddCategory();
    void onRenameCategory();
    void onDeleteCategory();
    void onSelectionChanged();

private:
    void setupUI();
    void refreshList();

    QListWidget *m_categoryList;
    QPushButton *m_addButton;
    QPushButton *m_renameButton;
    QPushButton *m_deleteButton;
    CategoryManager *m_manager;
};

#endif // CATEGORYEDITORDIALOG_H
