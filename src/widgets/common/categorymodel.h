#ifndef CATEGORYMODEL_H
#define CATEGORYMODEL_H

#include <QAbstractListModel>
#include <QStringList>

class CategoryManager;

/**
 * @brief Qt List model for categories
 *
 * Provides a model interface for displaying categories in combo boxes
 * and list views. Wraps a CategoryManager instance.
 */
class CategoryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        CategoryNameRole = Qt::UserRole + 1,
        RecordCountRole
    };

    explicit CategoryModel(QObject *parent = nullptr);
    ~CategoryModel() override = default;

    /**
     * @brief Set the CategoryManager to wrap
     * @param manager The manager (ownership not transferred)
     */
    void setManager(CategoryManager *manager);

    /**
     * @brief Get the wrapped CategoryManager
     */
    CategoryManager *manager() const;

    /**
     * @brief Load categories from the manager
     */
    void reload();

    /**
     * @brief Get all category names including "All"
     * @return List with "All" followed by all category names
     */
    QStringList categoryNames() const;

    /**
     * @brief Get the index for a category name
     * @param name Category name
     * @return Index in the model, or -1 if not found
     */
    int indexOfCategory(const QString &name) const;

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Static accessors for special values
    static QString allCategoriesText();
    static QString unfiledCategoryText();

Q_SIGNALS:
    /**
     * @brief Emitted when categories are reloaded
     */
    void categoriesChanged();

private Q_SLOTS:
    void onManagerCategoriesChanged();

private:
    CategoryManager *m_manager;
    QStringList m_categories;  // Cached category list with "All" prepended
};

#endif // CATEGORYMODEL_H
