#ifndef CATEGORYINFO_H
#define CATEGORYINFO_H

#include <QString>
#include <QStringList>
#include <pi-appinfo.h>

/**
 * @brief Helper class for parsing and modifying Palm category information
 *
 * Palm databases store up to 16 categories. Category 0 is typically "Unfiled".
 * This class parses and can modify the CategoryAppInfo structure from the
 * database AppInfo block.
 *
 * For bidirectional sync:
 * - Use addCategory() to create new categories from PC data
 * - Use pack() to serialize modified categories for writing back to Palm
 */
class CategoryInfo
{
public:
    static const int MAX_CATEGORIES = 16;
    static const int MAX_CATEGORY_NAME_LEN = 15;  // 15 chars + null

    CategoryInfo();

    /**
     * @brief Parse category information from raw AppInfo block data
     * @param appInfoData Raw AppInfo block data
     * @param size Size of the data
     * @return true if parsing succeeded
     */
    bool parse(const unsigned char *appInfoData, size_t size);

    /**
     * @brief Get category name by index
     * @param index Category index (0-15)
     * @return Category name, or empty string if invalid/empty
     */
    QString categoryName(int index) const;

    /**
     * @brief Get category index by name
     * @param name Category name (case-insensitive)
     * @return Category index (0-15), or -1 if not found
     */
    int categoryIndex(const QString &name) const;

    /**
     * @brief Get category index by name, creating if needed
     * @param name Category name
     * @return Category index (0-15), or 0 (Unfiled) if no slots available
     */
    int getOrCreateCategory(const QString &name);

    /**
     * @brief Get all category names
     * @return List of 16 category names (some may be empty)
     */
    QStringList allCategories() const;

    /**
     * @brief Get list of non-empty category names
     */
    QStringList usedCategories() const;

    /**
     * @brief Check if parsing was successful
     */
    bool isValid() const { return m_valid; }

    // ========== Modification Methods ==========

    /**
     * @brief Add a new category
     * @param name Category name (will be truncated to 15 chars)
     * @return Category index (1-15), or -1 if no slots available
     *
     * Note: Slot 0 is reserved for "Unfiled" and won't be used for new categories.
     */
    int addCategory(const QString &name);

    /**
     * @brief Set/rename a category at a specific index
     * @param index Category index (0-15)
     * @param name New category name (will be truncated to 15 chars)
     * @return true if successful
     */
    bool setCategory(int index, const QString &name);

    /**
     * @brief Find the first empty category slot
     * @return Index of empty slot (1-15), or -1 if all slots are full
     *
     * Note: Slot 0 is reserved for "Unfiled".
     */
    int findEmptySlot() const;

    /**
     * @brief Check if categories have been modified since parsing
     */
    bool isDirty() const { return m_dirty; }

    /**
     * @brief Clear the dirty flag
     */
    void clearDirty() { m_dirty = false; }

    // ========== Serialization ==========

    /**
     * @brief Get the size needed for pack()
     * @return Size in bytes
     */
    size_t packSize() const;

    /**
     * @brief Serialize category information to a buffer
     * @param buffer Output buffer (must be at least packSize() bytes)
     * @param bufferSize Size of the buffer
     * @return Number of bytes written, or -1 on error
     */
    int pack(unsigned char *buffer, size_t bufferSize) const;

    /**
     * @brief Get the raw CategoryAppInfo structure (for AppInfo blocks that
     *        contain more than just categories)
     */
    const CategoryAppInfo_t& rawCategories() const { return m_categories; }

private:
    CategoryAppInfo_t m_categories;
    bool m_valid;
    bool m_dirty;

    // Helper to encode text for Palm (Unicode to Windows-1252)
    static QByteArray encodePalmText(const QString &text);
};

#endif // CATEGORYINFO_H
