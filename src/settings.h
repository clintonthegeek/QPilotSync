#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>
#include <QSettings>

/**
 * @brief Application settings manager using QSettings
 *
 * Persists user preferences for device connection, export paths, etc.
 */
class Settings
{
public:
    static Settings& instance();

    // Device settings
    QString devicePath() const;
    void setDevicePath(const QString &path);

    QString baudRate() const;
    void setBaudRate(const QString &rate);

    // Export settings
    QString lastExportPath() const;
    void setLastExportPath(const QString &path);

    // Window geometry
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    // Sync to disk
    void sync();

private:
    Settings();
    ~Settings() = default;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    QSettings m_settings;
};

#endif // SETTINGS_H
