#ifndef CATEGORYFILTERWIDGET_H
#define CATEGORYFILTERWIDGET_H

#include <QWidget>

class QComboBox;
class QPushButton;
class CategoryModel;
class CategoryManager;

/**
 * @brief Reusable category filter widget for toolbars
 *
 * Provides a combo box for category filtering and a "Manage..." button
 * to open the category editor dialog.
 */
class CategoryFilterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CategoryFilterWidget(QWidget *parent = nullptr);
    ~CategoryFilterWidget() override = default;

    /**
     * @brief Set the category model to use
     * @param model The model (ownership not transferred)
     */
    void setModel(CategoryModel *model);

    /**
     * @brief Get the currently selected category
     * @return Category name, or "All" if no filter
     */
    QString selectedCategory() const;

    /**
     * @brief Set the selected category
     * @param category Category name to select
     */
    void setSelectedCategory(const QString &category);

    /**
     * @brief Check if "All" is selected (no filtering)
     */
    bool isAllSelected() const;

Q_SIGNALS:
    /**
     * @brief Emitted when the selected category changes
     * @param category The new category, or "All" for no filter
     */
    void categoryChanged(const QString &category);

    /**
     * @brief Emitted when "Manage..." button is clicked
     */
    void manageRequested();

private Q_SLOTS:
    void onComboIndexChanged(int index);
    void onManageClicked();

private:
    void setupUI();

    QComboBox *m_comboBox;
    QPushButton *m_manageButton;
    CategoryModel *m_model;
};

#endif // CATEGORYFILTERWIDGET_H
