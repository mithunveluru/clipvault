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

// the same bound the x11 tracker puts on properties
constexpr qsizetype kMaxFieldLength = 4096;

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
            QStringLiteral("FocusChanged"), this, SLOT(onFocusMessage(QDBusMessage))))
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
    // the extension sends focus changes only to subscribers so this repeats whenever the name appears
    QDBusConnection::sessionBus().callWithCallback(
        methodCall("Subscribe"), this, SLOT(onSubscribed()), SLOT(onCallFailed(QDBusError)));
    QDBusConnection::sessionBus().callWithCallback(
        methodCall("GetFocus"), this, SLOT(onFocusMessage(QDBusMessage)), SLOT(onCallFailed(QDBusError)));
}

void GnomeShellFocusTracker::onFocusMessage(const QDBusMessage& message)
{
    // anything that owns the name can send this so the shape is checked before use
    if (message.signature() != QLatin1String("sssxt")) {
        qCInfo(lcFocus) << "ignored a focus message with signature length" << message.signature().size();
        return;
    }
    const QList<QVariant> args = message.arguments();
    WindowInfo window;
    window.windowClass = args.at(1).toString().left(kMaxFieldLength);
    window.app = args.at(0).toString().left(kMaxFieldLength);
    if (window.app.isEmpty())
        window.app = window.windowClass;
    window.title = args.at(2).toString().left(kMaxFieldLength);
    window.pid = args.at(3).toLongLong();
    window.windowId = args.at(4).toULongLong();
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

void GnomeShellFocusTracker::onSubscribed()
{
    qCDebug(lcFocus) << "subscribed to focus changes from the shell extension";
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
