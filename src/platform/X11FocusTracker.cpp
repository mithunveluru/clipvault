#include "X11FocusTracker.h"

#include "../core/Logging.h"

#include <QCoreApplication>
#include <QScopedPointer>
#include <QSocketNotifier>
#include <QTimer>

#include <xcb/xcb.h>

#include <array>
#include <string_view>

namespace clipvault::platform {
namespace {

// xcb hands out replies from malloc
struct FreeDeleter {
    void operator()(void* pointer) const { QScopedPointerPodDeleter::cleanup(pointer); }
};
template <typename T> using Reply = std::unique_ptr<T, FreeDeleter>;

// four kilobytes is far beyond any real title or class
constexpr std::uint32_t kMaxPropertyWords = 1024;
// bounds the work done per wakeup of the event loop
constexpr int kMaxEventsPerDrain = 256;
constexpr int kMaxRefreshesPerWakeup = 4;
// the top bit marks events sent by another client
constexpr std::uint8_t kEventTypeMask = 0x7f;
// ewmh source indication for a pager which window managers honour without focus stealing checks
constexpr std::uint32_t kSourcePager = 2;

QByteArray readProperty(xcb_connection_t* connection, xcb_window_t window, xcb_atom_t atom, xcb_atom_t type)
{
    const Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(
        connection, xcb_get_property(connection, 0, window, atom, type, 0, kMaxPropertyWords), nullptr));
    if (!reply || reply->format != 8)
        return { };
    return { static_cast<const char*>(xcb_get_property_value(reply.get())),
        xcb_get_property_value_length(reply.get()) };
}

std::uint32_t cardinal(xcb_connection_t* connection, xcb_window_t window, xcb_atom_t atom, xcb_atom_t type)
{
    const Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(
        connection, xcb_get_property(connection, 0, window, atom, type, 0, 1), nullptr));
    if (!reply || reply->format != 32 || xcb_get_property_value_length(reply.get()) < 4)
        return 0;
    return *static_cast<const std::uint32_t*>(xcb_get_property_value(reply.get()));
}

bool sameWindow(const WindowInfo& a, const WindowInfo& b)
{
    return a.windowId == b.windowId && a.pid == b.pid && a.app == b.app && a.windowClass == b.windowClass
        && a.title == b.title;
}

} // namespace

void X11FocusTracker::ConnectionDeleter::operator()(xcb_connection_t* connection) const
{
    xcb_disconnect(connection);
}

std::unique_ptr<X11FocusTracker> X11FocusTracker::create(QObject* parent)
{
    int screen = 0;
    Connection connection(xcb_connect(nullptr, &screen));
    if (xcb_connection_has_error(connection.get()) != 0) {
        qCInfo(lcFocus) << "no x server reachable for focus tracking";
        return nullptr;
    }
    return std::make_unique<X11FocusTracker>(std::move(connection), screen, parent);
}

X11FocusTracker::X11FocusTracker(Connection connection, int screen, QObject* parent)
    : FocusTracker(parent)
    , m_connection(std::move(connection))
{
    xcb_connection_t* c = m_connection.get();
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (int i = 0; i < screen && it.rem > 0; ++i)
        xcb_screen_next(&it);
    m_root = it.data->root;

    constexpr std::array<std::string_view, 4> names { "_NET_ACTIVE_WINDOW", "_NET_WM_NAME", "_NET_WM_PID",
        "UTF8_STRING" };
    std::array<xcb_intern_atom_cookie_t, names.size()> cookies { };
    for (std::size_t i = 0; i < names.size(); ++i)
        cookies.at(i)
            = xcb_intern_atom(c, 0, static_cast<std::uint16_t>(names.at(i).size()), names.at(i).data());
    std::array<std::uint32_t*, names.size()> targets { &m_netActiveWindow, &m_netWmName, &m_netWmPid,
        &m_utf8String };
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Reply<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(c, cookies.at(i), nullptr));
        *targets.at(i) = reply ? reply->atom : std::uint32_t { XCB_ATOM_NONE };
    }

    const std::uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(c, m_root, XCB_CW_EVENT_MASK, &mask);

    m_notifier = std::make_unique<QSocketNotifier>(xcb_get_file_descriptor(c), QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, [this] {
        readEvents();
        if (xcb_connection_has_error(m_connection.get()) != 0) {
            qCWarning(lcFocus) << "x connection for focus tracking was lost";
            m_notifier->setEnabled(false);
        }
    });

    refresh();
    xcb_flush(c);
}

X11FocusTracker::~X11FocusTracker() = default;

X11FocusTracker::Drain X11FocusTracker::drainEvents()
{
    Drain drain;
    for (int i = 0; i < kMaxEventsPerDrain; ++i) {
        const Reply<xcb_generic_event_t> event { xcb_poll_for_event(m_connection.get()) };
        if (!event)
            return drain;
        // errors for windows that vanished arrive here as type zero and are ignored
        if ((event->response_type & kEventTypeMask) != XCB_PROPERTY_NOTIFY)
            continue;
        const auto* notify = reinterpret_cast<const xcb_property_notify_event_t*>(event.get());
        const bool activeMoved = notify->window == m_root && notify->atom == m_netActiveWindow;
        const bool watchedRenamed = notify->window == m_watched
            && (notify->atom == m_netWmName || notify->atom == XCB_ATOM_WM_NAME
                || notify->atom == XCB_ATOM_WM_CLASS);
        drain.changed = drain.changed || activeMoved || watchedRenamed;
    }
    drain.more = true;
    return drain;
}

void X11FocusTracker::readEvents()
{
    m_drainQueued = false;
    // replies read during refresh can queue further events that the notifier never reports
    for (int round = 0; round < kMaxRefreshesPerWakeup; ++round) {
        const Drain drain = drainEvents();
        // one refresh covers every change in the batch
        if (drain.changed)
            refresh();
        else if (!drain.more)
            return;
    }
    // a client spamming properties must not starve the event loop so the rest waits for the next pass
    scheduleDrain();
}

void X11FocusTracker::scheduleDrain()
{
    if (m_drainQueued)
        return;
    m_drainQueued = true;
    QTimer::singleShot(0, this, &X11FocusTracker::readEvents);
}

std::uint32_t X11FocusTracker::activeWindow() const
{
    return cardinal(m_connection.get(), m_root, m_netActiveWindow, XCB_ATOM_WINDOW);
}

WindowInfo X11FocusTracker::describe(std::uint32_t window) const
{
    xcb_connection_t* c = m_connection.get();
    WindowInfo info;
    info.windowId = window;

    // instance and class come as two nul terminated strings
    const QList<QByteArray> wmClass = readProperty(c, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING).split('\0');
    info.windowClass = QString::fromLatin1(wmClass.value(1));
    info.app = QString::fromLatin1(wmClass.value(0));
    if (info.app.isEmpty())
        info.app = info.windowClass;

    info.title = QString::fromUtf8(readProperty(c, window, m_netWmName, m_utf8String));
    if (info.title.isEmpty())
        info.title = QString::fromLatin1(readProperty(c, window, XCB_ATOM_WM_NAME, XCB_ATOM_ANY));
    info.pid = cardinal(c, window, m_netWmPid, XCB_ATOM_CARDINAL);
    return info;
}

void X11FocusTracker::watch(std::uint32_t window)
{
    // the root mask belongs to the constructor and must survive any active window value
    if (window == m_root)
        window = 0;
    if (window == m_watched)
        return;
    xcb_connection_t* c = m_connection.get();
    if (m_watched != 0) {
        const std::uint32_t none = XCB_EVENT_MASK_NO_EVENT;
        xcb_change_window_attributes(c, m_watched, XCB_CW_EVENT_MASK, &none);
    }
    if (window != 0) {
        const std::uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        xcb_change_window_attributes(c, window, XCB_CW_EVENT_MASK, &mask);
    }
    m_watched = window;
}

void X11FocusTracker::refresh()
{
    const std::uint32_t window = activeWindow();
    watch(window);
    xcb_flush(m_connection.get());
    const WindowInfo info = window != 0 && window != m_root ? describe(window) : WindowInfo { };
    if (sameWindow(info, m_current))
        return;
    m_current = info;
    if (info.isValid() && info.pid != QCoreApplication::applicationPid())
        m_lastForeign = info;
    qCDebug(lcFocus) << "focus changed class length" << info.windowClass.size() << "title length"
                     << info.title.size();
    emit focusChanged(m_current);
}

bool X11FocusTracker::activate(const WindowInfo& window)
{
    if (window.windowId == 0 || window.windowId > UINT32_MAX)
        return false;
    xcb_client_message_event_t event { };
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = static_cast<xcb_window_t>(window.windowId);
    event.type = m_netActiveWindow;
    event.data.data32[0] = kSourcePager;
    event.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(m_connection.get(), 0, m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<const char*>(&event));
    return xcb_flush(m_connection.get()) > 0;
}

QString X11FocusTracker::mechanismDescription() const
{
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return tr("Focus comes from X11, which under Wayland sees only XWayland windows. Clips from "
                  "native Wayland apps are recorded without a source. The ClipVault GNOME Shell "
                  "extension sees every window.");
    return tr("Focus comes from the X11 window manager and covers every window.");
}

} // namespace clipvault::platform
