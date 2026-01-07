#ifndef CATEGORYINFO_H
#define CATEGORYINFO_H

#include <QString>
#include <QStringList>
#include <pi-appinfo.h>

/**
 * @brief Helper class for parsing Palm category information
 *
 * Palm databases store up to 16 categories. Category 0 is typically "Unfiled".
 * This class parses the CategoryAppInfo structure from the database AppInfo block.
 */
class CategoryInfo
{
public:
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
     * @brief Get all category names
     * @return List of 16 category names (some may be empty)
     */
    QStringList allCategories() const;

    /**
     * @brief Check if parsing was successful
     */
    bool isValid() const { return m_valid; }

private:
    CategoryAppInfo_t m_categories;
    bool m_valid;
};

#endif // CATEGORYINFO_H
