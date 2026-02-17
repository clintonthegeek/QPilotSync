#include "categorymanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTextStream>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Event>

CategoryManager::CategoryManager(const QString &dataType, QObject *parent)
    : QObject(parent)
    , m_dataType(dataType)
{
}

void CategoryManager::setBasePath(const QString &syncPath)
{
    m_basePath = syncPath;
}

QString CategoryManager::categoriesFilePath() const
{
    if (m_basePath.isEmpty()) {
        return QString();
    }
    return m_basePath + QStringLiteral("/.qpilotsync.state/categories-%1.json").arg(m_dataType);
}

void CategoryManager::load()
{
    m_categories.clear();

    QString filePath = categoriesFilePath();
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // File doesn't exist, try to discover categories from data files
        QString dataPath = m_basePath + QStringLiteral("/") + m_dataType;
        m_categories = discoverCategories(dataPath, m_dataType);

        // Always ensure "Unfiled" exists
        if (!m_categories.contains(unfiledCategoryName())) {
            m_categories.prepend(unfiledCategoryName());
        }
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray categoriesArray = root[QStringLiteral("categories")].toArray();

    for (const QJsonValue &value : categoriesArray) {
        QString name = value.toString();
        if (!name.isEmpty() && !m_categories.contains(name)) {
            m_categories.append(name);
        }
    }

    // Ensure "Unfiled" is always present
    if (!m_categories.contains(unfiledCategoryName())) {
        m_categories.prepend(unfiledCategoryName());
    }
}

void CategoryManager::save()
{
    QString filePath = categoriesFilePath();
    if (filePath.isEmpty()) {
        return;
    }

    // Ensure state directory exists
    QFileInfo fileInfo(filePath);
    QDir dir;
    if (!dir.mkpath(fileInfo.absolutePath())) {
        return;
    }

    QJsonObject root;
    QJsonArray categoriesArray;
    for (const QString &category : m_categories) {
        categoriesArray.append(category);
    }
    root[QStringLiteral("categories")] = categoriesArray;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

QStringList CategoryManager::categories() const
{
    return m_categories;
}

bool CategoryManager::addCategory(const QString &name)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_categories.contains(trimmed)) {
        return false;
    }

    m_categories.append(trimmed);
    save();
    Q_EMIT categoriesChanged();
    return true;
}

bool CategoryManager::renameCategory(const QString &oldName, const QString &newName)
{
    QString trimmedNew = newName.trimmed();
    if (trimmedNew.isEmpty() || oldName == unfiledCategoryName()) {
        return false;
    }

    int index = m_categories.indexOf(oldName);
    if (index == -1) {
        return false;
    }

    if (m_categories.contains(trimmedNew) && trimmedNew != oldName) {
        return false;
    }

    m_categories[index] = trimmedNew;
    save();
    Q_EMIT categoryRenamed(oldName, trimmedNew);
    Q_EMIT categoriesChanged();
    return true;
}

bool CategoryManager::deleteCategory(const QString &name)
{
    // Cannot delete "Unfiled"
    if (name == unfiledCategoryName()) {
        return false;
    }

    int index = m_categories.indexOf(name);
    if (index == -1) {
        return false;
    }

    m_categories.removeAt(index);
    save();
    Q_EMIT categoryDeleted(name);
    Q_EMIT categoriesChanged();
    return true;
}

bool CategoryManager::hasCategory(const QString &name) const
{
    return m_categories.contains(name);
}

QString CategoryManager::unfiledCategoryName()
{
    return QStringLiteral("Unfiled");
}

QString CategoryManager::allCategoriesText()
{
    return QStringLiteral("All");
}

QStringList CategoryManager::discoverCategories(const QString &dataPath, const QString &dataType)
{
    QStringList discovered;

    if (dataType == QStringLiteral("memos")) {
        // Scan markdown files for category in YAML frontmatter
        QDir dir(dataPath);
        if (!dir.exists()) {
            return discovered;
        }

        QStringList filters;
        filters << QStringLiteral("*.md") << QStringLiteral("*.txt");
        QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

        static QRegularExpression categoryRe(QStringLiteral("^category:\\s*(.+)$"),
                                              QRegularExpression::MultilineOption);

        for (const QFileInfo &fileInfo : files) {
            QFile file(fileInfo.filePath());
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QTextStream(&file).readAll();
                file.close();

                QRegularExpressionMatch match = categoryRe.match(content);
                if (match.hasMatch()) {
                    QString category = match.captured(1).trimmed();
                    if (!category.isEmpty() && !discovered.contains(category)) {
                        discovered.append(category);
                    }
                }
            }
        }
    } else if (dataType == QStringLiteral("todos")) {
        // Scan individual .ics files in the todos directory
        QDir dir(dataPath);
        if (dir.exists()) {
            QStringList filters;
            filters << QStringLiteral("*.ics");
            QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

            KCalendarCore::ICalFormat format;
            for (const QFileInfo &fileInfo : files) {
                KCalendarCore::MemoryCalendar::Ptr calendar(
                    new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

                if (format.load(calendar, fileInfo.filePath())) {
                    KCalendarCore::Todo::List todos = calendar->todos();
                    for (const KCalendarCore::Todo::Ptr &todo : todos) {
                        QStringList cats = todo->categories();
                        for (const QString &cat : cats) {
                            if (!cat.isEmpty() && !discovered.contains(cat)) {
                                discovered.append(cat);
                            }
                        }
                    }
                }
            }
        }
    } else if (dataType == QStringLiteral("contacts")) {
        // Scan individual .vcf files for CATEGORIES property
        QDir dir(dataPath);
        if (dir.exists()) {
            QStringList filters;
            filters << QStringLiteral("*.vcf");
            QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

            static QRegularExpression categoryRe(QStringLiteral("^CATEGORIES:(.+)$"),
                                                  QRegularExpression::MultilineOption);

            for (const QFileInfo &fileInfo : files) {
                QFile file(fileInfo.filePath());
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QString content = QTextStream(&file).readAll();
                    file.close();

                    // Unfold continuation lines
                    content.replace(QStringLiteral("\r\n "), QString());
                    content.replace(QStringLiteral("\r\n\t"), QString());
                    content.replace(QStringLiteral("\n "), QString());
                    content.replace(QStringLiteral("\n\t"), QString());

                    QRegularExpressionMatch match = categoryRe.match(content);
                    if (match.hasMatch()) {
                        QStringList cats = match.captured(1).split(QLatin1Char(','));
                        for (const QString &cat : cats) {
                            QString trimmed = cat.trimmed();
                            if (!trimmed.isEmpty() && !discovered.contains(trimmed)) {
                                discovered.append(trimmed);
                            }
                        }
                    }
                }
            }
        }
    } else if (dataType == QStringLiteral("calendar")) {
        // Scan individual .ics files in the calendar directory
        QDir dir(dataPath);
        if (dir.exists()) {
            QStringList filters;
            filters << QStringLiteral("*.ics");
            QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

            KCalendarCore::ICalFormat format;
            for (const QFileInfo &fileInfo : files) {
                KCalendarCore::MemoryCalendar::Ptr calendar(
                    new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));

                if (format.load(calendar, fileInfo.filePath())) {
                    KCalendarCore::Event::List events = calendar->events();
                    for (const KCalendarCore::Event::Ptr &event : events) {
                        QStringList cats = event->categories();
                        for (const QString &cat : cats) {
                            if (!cat.isEmpty() && !discovered.contains(cat)) {
                                discovered.append(cat);
                            }
                        }
                    }
                }
            }
        }
    }

    discovered.sort(Qt::CaseInsensitive);
    return discovered;
}
