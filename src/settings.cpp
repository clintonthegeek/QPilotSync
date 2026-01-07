#include "settings.h"
#include <QDir>

Settings& Settings::instance()
{
    static Settings instance;
    return instance;
}

Settings::Settings()
    : m_settings("QPilotSync", "QPilotSync")
{
}

QString Settings::devicePath() const
{
    return m_settings.value("device/path", "/dev/ttyUSB0").toString();
}

void Settings::setDevicePath(const QString &path)
{
    m_settings.setValue("device/path", path);
}

QString Settings::baudRate() const
{
    return m_settings.value("device/baudRate", "115200").toString();
}

void Settings::setBaudRate(const QString &rate)
{
    m_settings.setValue("device/baudRate", rate);
}

QString Settings::lastExportPath() const
{
    return m_settings.value("export/lastPath", QDir::homePath()).toString();
}

void Settings::setLastExportPath(const QString &path)
{
    m_settings.setValue("export/lastPath", path);
}

QByteArray Settings::windowGeometry() const
{
    return m_settings.value("window/geometry").toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &geometry)
{
    m_settings.setValue("window/geometry", geometry);
}

void Settings::sync()
{
    m_settings.sync();
}
