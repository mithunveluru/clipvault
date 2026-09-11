#include "app/ApplicationController.h"
#include "core/Logging.h"
#include "platform/SingleInstance.h"
#include "app/TrayIcon.h"
#include "storage/Database.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMessageBox>

#include <unistd.h>

namespace {

QString instanceKey()
{
    // Per user socket isolates concurrent logins
    return QStringLiteral("clipvault-%1").arg(getuid());
}

} // namespace

int main(int argc, char** argv)
{
    // Background capture needs X11 selection events
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && qEnvironmentVariableIsSet("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ClipVault"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("clipvault"));
    clipvault::installLogHandler();

    if (QGuiApplication::platformName() != QLatin1String("xcb")) {
        qCWarning(lcApp) << "platform" << QGuiApplication::platformName()
                         << "limits capture to times when ClipVault has focus";
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ClipVault - local-first clipboard history."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption showOption(
        QStringLiteral("show"), QStringLiteral("Bring up the ClipVault quick-pick window."));
    const QCommandLineOption backgroundOption(
        QStringLiteral("background"), QStringLiteral("Start without showing the window."));
    const QCommandLineOption databaseOption(
        QStringLiteral("db"), QStringLiteral("Use an alternative database file."),
        QStringLiteral("path"));
    parser.addOption(showOption);
    parser.addOption(backgroundOption);
    parser.addOption(databaseOption);
    parser.process(app);

    clipvault::platform::SingleInstance instance(instanceKey());
    if (!instance.tryBecomePrimary()) {
        // Hand over to the owning instance and exit
        const QString command = parser.isSet(showOption) ? QStringLiteral("show")
                                                         : QStringLiteral("open");
        if (!instance.sendToPrimary(command)) {
            qCCritical(lcApp) << "ClipVault is already running but did not respond";
            return 1;
        }
        return 0;
    }

    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(clipvault::makeTrayIcon(false));

    const QString databasePath = parser.isSet(databaseOption) ? parser.value(databaseOption)
                                                              : clipvault::Database::defaultPath();

    clipvault::ApplicationController controller(instance);
    QString error;
    if (!controller.initialize(databasePath, &error)) {
        qCCritical(lcApp) << "startup aborted:" << error;
        QMessageBox::critical(nullptr, QObject::tr("ClipVault"),
                              QObject::tr("ClipVault could not start.\n\n%1").arg(error));
        return 1;
    }

    clipvault::enableLogFile(QFileInfo(databasePath).absolutePath()
                             + QStringLiteral("/clipvault.log"));

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                     &clipvault::ApplicationController::shutdown);

    controller.start(parser.isSet(backgroundOption));
    if (parser.isSet(showOption))
        controller.showPopup();

    return app.exec();
}
