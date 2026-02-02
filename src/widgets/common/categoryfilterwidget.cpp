#include "categoryfilterwidget.h"
#include "categorymodel.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

#include <KLocalizedString>

CategoryFilterWidget::CategoryFilterWidget(QWidget *parent)
    : QWidget(parent)
    , m_comboBox(nullptr)
    , m_manageButton(nullptr)
    , m_model(nullptr)
{
    setupUI();
}

void CategoryFilterWidget::setupUI()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    QLabel *label = new QLabel(i18n("Category:"), this);
    layout->addWidget(label);

    m_comboBox = new QComboBox(this);
    m_comboBox->setMinimumWidth(120);
    connect(m_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CategoryFilterWidget::onComboIndexChanged);
    layout->addWidget(m_comboBox);

    m_manageButton = new QPushButton(i18n("Manage..."), this);
    connect(m_manageButton, &QPushButton::clicked,
            this, &CategoryFilterWidget::onManageClicked);
    layout->addWidget(m_manageButton);
}

void CategoryFilterWidget::setModel(CategoryModel *model)
{
    if (m_model) {
        disconnect(m_model, &CategoryModel::categoriesChanged,
                   m_comboBox, nullptr);
    }

    m_model = model;
    m_comboBox->setModel(model);

    if (m_model) {
        // Select "All" by default
        m_comboBox->setCurrentIndex(0);
    }
}

QString CategoryFilterWidget::selectedCategory() const
{
    return m_comboBox->currentText();
}

void CategoryFilterWidget::setSelectedCategory(const QString &category)
{
    if (!m_model) {
        return;
    }

    int index = m_model->indexOfCategory(category);
    if (index >= 0) {
        m_comboBox->setCurrentIndex(index);
    }
}

bool CategoryFilterWidget::isAllSelected() const
{
    return m_comboBox->currentIndex() == 0 ||
           selectedCategory() == CategoryModel::allCategoriesText();
}

void CategoryFilterWidget::onComboIndexChanged(int index)
{
    Q_UNUSED(index)
    Q_EMIT categoryChanged(selectedCategory());
}

void CategoryFilterWidget::onManageClicked()
{
    Q_EMIT manageRequested();
}
