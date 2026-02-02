#include "categorymodel.h"
#include "categorymanager.h"

CategoryModel::CategoryModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(nullptr)
{
}

void CategoryModel::setManager(CategoryManager *manager)
{
    if (m_manager) {
        disconnect(m_manager, &CategoryManager::categoriesChanged,
                   this, &CategoryModel::onManagerCategoriesChanged);
    }

    m_manager = manager;

    if (m_manager) {
        connect(m_manager, &CategoryManager::categoriesChanged,
                this, &CategoryModel::onManagerCategoriesChanged);
    }

    reload();
}

CategoryManager *CategoryModel::manager() const
{
    return m_manager;
}

void CategoryModel::reload()
{
    beginResetModel();

    m_categories.clear();
    m_categories.append(allCategoriesText());

    if (m_manager) {
        m_categories.append(m_manager->categories());
    }

    endResetModel();

    Q_EMIT categoriesChanged();
}

QStringList CategoryModel::categoryNames() const
{
    return m_categories;
}

int CategoryModel::indexOfCategory(const QString &name) const
{
    return m_categories.indexOf(name);
}

int CategoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_categories.count();
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_categories.count()) {
        return QVariant();
    }

    const QString &category = m_categories.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case CategoryNameRole:
        return category;

    case RecordCountRole:
        // TODO: Could be enhanced to return actual counts
        return 0;

    default:
        return QVariant();
    }
}

QString CategoryModel::allCategoriesText()
{
    return CategoryManager::allCategoriesText();
}

QString CategoryModel::unfiledCategoryText()
{
    return CategoryManager::unfiledCategoryName();
}

void CategoryModel::onManagerCategoriesChanged()
{
    reload();
}
