#ifndef CATEGORYMANAGER_H
#define CATEGORYMANAGER_H

#include <QObject>
#include <QStringList>

/**
 * @brief Manages categories for a specific data type (memos, todos, etc.)
 *
 * Categories are stored in a JSON file in the profile's .state directory.
 * This class handles CRUD operations for categories and tracks usage counts.
 */
class CategoryManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a CategoryManager for a specific data type
     * @param dataType The data type name (e.g., "memos", "todos")
     * @param parent Parent object
     */
    explicit CategoryManager(const QString &dataType, QObject *parent = nullptr);
    ~CategoryManager() override = default;

    /**
     * @brief Set the sync folder base path
     * @param syncPath Path to the sync folder (e.g., ~/PalmSync)
     */
    void setBasePath(const QString &syncPath);

    /**
     * @brief Load categories from the JSON file
     */
    void load();

    /**
     * @brief Save categories to the JSON file
     */
    void save();

    /**
     * @brief Get the path to the categories file
     * @return Full path to .state/categories-{datatype}.json
     */
    QString categoriesFilePath() const;

    // Category operations

    /**
     * @brief Get all category names
     * @return List of category names (includes "Unfiled" if present)
     */
    QStringList categories() const;

    /**
     * @brief Add a new category
     * @param name Category name
     * @return true if added, false if already exists
     */
    bool addCategory(const QString &name);

    /**
     * @brief Rename a category
     * @param oldName Current name
     * @param newName New name
     * @return true if renamed, false if oldName not found or newName exists
     */
    bool renameCategory(const QString &oldName, const QString &newName);

    /**
     * @brief Delete a category
     * @param name Category name to delete
     * @return true if deleted, false if not found or is "Unfiled"
     */
    bool deleteCategory(const QString &name);

    /**
     * @brief Check if a category exists
     * @param name Category name
     * @return true if exists
     */
    bool hasCategory(const QString &name) const;

    // Static utility methods

    /**
     * @brief Get the default "Unfiled" category name
     */
    static QString unfiledCategoryName();

    /**
     * @brief Get the "All" filter text (for UI)
     */
    static QString allCategoriesText();

    /**
     * @brief Discover categories from existing files
     * @param dataPath Path to data directory (e.g., syncPath/memos)
     * @param dataType Type of data ("memos", "todos", "contacts", "calendar")
     * @return List of discovered category names
     */
    static QStringList discoverCategories(const QString &dataPath, const QString &dataType);

Q_SIGNALS:
    /**
     * @brief Emitted when categories change (add/rename/delete)
     */
    void categoriesChanged();

    /**
     * @brief Emitted when a category is renamed
     * @param oldName Previous name
     * @param newName New name
     */
    void categoryRenamed(const QString &oldName, const QString &newName);

    /**
     * @brief Emitted when a category is deleted
     * @param name Deleted category name
     */
    void categoryDeleted(const QString &name);

private:
    QString m_dataType;
    QString m_basePath;
    QStringList m_categories;
};

#endif // CATEGORYMANAGER_H
