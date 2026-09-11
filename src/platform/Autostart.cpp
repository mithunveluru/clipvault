#include "Autostart.h"

#include "../core/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace clipvault::platform {
namespace {

// XDG autostart works across desktops without privileges
QString autostartFilePath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty())
        configDir = QDir::homePath() + QStringLiteral("/.config");
    return configDir + QStringLiteral("/autostart/clipvault.desktop");
}

} // namespace

bool isAutostartEnabled()
{
    return QFile::exists(autostartFilePath());
}

bool setAutostartEnabled(bool enabled)
{
    const QString path = autostartFilePath();
    if (!enabled) {
        if (!QFile::exists(path))
            return true;
        return QFile::remove(path);
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(lcApp) << "could not create the autostart directory";
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qCWarning(lcApp) << "could not write the autostart entry:" << file.errorString();
        return false;
    }

    QTextStream out(&file);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=ClipVault\n"
        << "Comment=Local-first clipboard history\n"
        << "Exec=" << QCoreApplication::applicationFilePath() << " --background\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
    out.flush();
    return file.error() == QFileDevice::NoError;
}

} // namespace clipvault::platform
