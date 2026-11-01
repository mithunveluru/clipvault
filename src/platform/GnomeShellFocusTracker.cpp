#include "GnomeShellFocusTracker.h"

#include "../core/Logging.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusServiceWatcher>

namespace clipvault::platform {
namespace {

QDBusMessage methodCall(const char* method)
{
    return QDBusMessage::createMethodCall(QLatin1String(GnomeShellFocusTracker::kService),
        QLatin1String(GnomeShellFocusTracker::kPath), QLatin1String(GnomeShellFocusTracker::kInterface),
        QLatin1String(method));
}

} // namespace

bool GnomeShellFocusTracker::isAvailable()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || bus.interface() == nullptr)
        return false;
    return bus.interface()->isServiceRegistered(QLatin1String(kService)).value();
}

GnomeShellFocusTracker::GnomeShellFocusTracker(QObject* parent)
    : FocusTracker(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
            QStringLiteral("FocusChanged"), this,
            SLOT(onFocusChanged(QString, QString, QString, qlonglong, qulonglong))))
        qCWarning(lcFocus) << "could not subscribe to focus changes from the shell extension";

    // the shell restarts or the extension is toggled and the name comes and goes with it
    m_watcher = std::make_unique<QDBusServiceWatcher>(QLatin1String(kService), bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher.get(), &QDBusServiceWatcher::serviceRegistered, this, &GnomeShellFocusTracker::query);
    connect(m_watcher.get(), &QDBusServiceWatcher::serviceUnregistered, this, [this] { update({ }); });
    query();
}

GnomeShellFocusTracker::~GnomeShellFocusTracker() = default;

void GnomeShellFocusTracker::query()
{
    QDBusConnection::sessionBus().callWithCallback(methodCall("GetFocus"), this,
        SLOT(onFocusChanged(QString, QString, QString, qlonglong, qulonglong)),
        SLOT(onCallFailed(QDBusError)));
}

void GnomeShellFocusTracker::onFocusChanged(
    const QString& appId, const QString& wmClass, const QString& title, qlonglong pid, qulonglong windowId)
{
    WindowInfo window;
    window.app = appId.isEmpty() ? wmClass : appId;
    window.windowClass = wmClass;
    window.title = title;
    window.pid = pid;
    window.windowId = windowId;
    update(window);
}

void GnomeShellFocusTracker::update(const WindowInfo& window)
{
    m_current = window;
    if (window.isValid() && window.pid != QCoreApplication::applicationPid())
        m_lastForeign = window;
    qCDebug(lcFocus) << "focus changed class length" << window.windowClass.size() << "title length"
                     << window.title.size();
    emit focusChanged(m_current);
}

bool GnomeShellFocusTracker::activate(const WindowInfo& window)
{
    if (window.windowId == 0)
        return false;
    QDBusMessage message = methodCall("Activate");
    message << QVariant::fromValue(static_cast<qulonglong>(window.windowId));
    // async so a stalled shell cannot freeze the window and the reply only feeds the log
    return QDBusConnection::sessionBus().callWithCallback(
        message, this, SLOT(onActivated(bool)), SLOT(onCallFailed(QDBusError)));
}

void GnomeShellFocusTracker::onActivated(bool activated)
{
    if (!activated)
        qCInfo(lcFocus) << "shell extension could not find the window to activate";
}

void GnomeShellFocusTracker::onCallFailed(const QDBusError& error)
{
    qCInfo(lcFocus) << "shell extension call failed" << error.name();
}

QString GnomeShellFocusTracker::mechanismDescription() const
{
    return tr("Focus comes from the ClipVault GNOME Shell extension and covers every window, "
              "including native Wayland apps.");
}

} // namespace clipvault::platform
