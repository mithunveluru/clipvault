#include "platform/FocusTracker.h"
#include "platform/X11FocusTracker.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <xcb/xcb.h>

#include <cstdlib>
#include <memory>

using clipvault::platform::WindowInfo;
using clipvault::platform::X11FocusTracker;

namespace {

struct FreeDeleter {
    void operator()(void* pointer) const { std::free(pointer); }
};

// xvfb runs no window manager so the test plays one and owns the root properties
class FakeWindowManager {
public:
    FakeWindowManager()
        : m_connection(xcb_connect(nullptr, nullptr))
    {
        if (xcb_connection_has_error(m_connection.get()) != 0)
            return;
        m_root = xcb_setup_roots_iterator(xcb_get_setup(m_connection.get())).data->root;
        m_active = atom("_NET_ACTIVE_WINDOW");
        m_name = atom("_NET_WM_NAME");
        m_pid = atom("_NET_WM_PID");
        m_utf8 = atom("UTF8_STRING");
        // a real window manager selects this to receive activation requests
        const std::uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
        xcb_change_window_attributes(m_connection.get(), m_root, XCB_CW_EVENT_MASK, &mask);
        xcb_flush(m_connection.get());
    }

    [[nodiscard]] bool ok() const { return m_root != 0; }

    xcb_window_t createWindow(const QByteArray& wmClass, const QByteArray& title, std::uint32_t pid)
    {
        xcb_connection_t* c = m_connection.get();
        const xcb_window_t window = xcb_generate_id(c);
        xcb_create_window(c, XCB_COPY_FROM_PARENT, window, m_root, 0, 0, 10, 10, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
            static_cast<std::uint32_t>(wmClass.size()), wmClass.constData());
        setTitle(window, title);
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, m_pid, XCB_ATOM_CARDINAL, 32, 1, &pid);
        xcb_flush(c);
        return window;
    }

    void setTitle(xcb_window_t window, const QByteArray& title)
    {
        xcb_change_property(m_connection.get(), XCB_PROP_MODE_REPLACE, window, m_name, m_utf8, 8,
            static_cast<std::uint32_t>(title.size()), title.constData());
        xcb_flush(m_connection.get());
    }

    void setActive(xcb_window_t window)
    {
        xcb_change_property(
            m_connection.get(), XCB_PROP_MODE_REPLACE, m_root, m_active, XCB_ATOM_WINDOW, 32, 1, &window);
        xcb_flush(m_connection.get());
    }

    // the window named by the next activation request or zero
    xcb_window_t takeActivationRequest()
    {
        while (const std::unique_ptr<xcb_generic_event_t, FreeDeleter> event {
            xcb_poll_for_event(m_connection.get()) }) {
            if ((event->response_type & ~0x80) != XCB_CLIENT_MESSAGE)
                continue;
            const auto* message = reinterpret_cast<const xcb_client_message_event_t*>(event.get());
            if (message->type == m_active)
                return message->window;
        }
        return 0;
    }

private:
    xcb_atom_t atom(const char* name)
    {
        const std::unique_ptr<xcb_intern_atom_reply_t, FreeDeleter> reply(
            xcb_intern_atom_reply(m_connection.get(),
                xcb_intern_atom(m_connection.get(), 0, static_cast<std::uint16_t>(qstrlen(name)), name),
                nullptr));
        return reply ? reply->atom : xcb_atom_t { XCB_ATOM_NONE };
    }

    X11FocusTracker::Connection m_connection;
    xcb_window_t m_root = 0;
    xcb_atom_t m_active = 0;
    xcb_atom_t m_name = 0;
    xcb_atom_t m_pid = 0;
    xcb_atom_t m_utf8 = 0;
};

} // namespace

class TestX11FocusTracker : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void followsActiveWindowAndTitle();
    void ownWindowIsNotForeign();
    void activateSendsEwmhRequest();
    void factoryPicksX11WithoutExtension();

private:
    std::unique_ptr<FakeWindowManager> m_wm;
};

void TestX11FocusTracker::initTestCase()
{
    m_wm = std::make_unique<FakeWindowManager>();
    if (!m_wm->ok())
        QSKIP("needs an x server such as xvfb");
}

void TestX11FocusTracker::followsActiveWindowAndTitle()
{
    const auto tracker = X11FocusTracker::create();
    QVERIFY(tracker);
    QSignalSpy spy(tracker.get(), &X11FocusTracker::focusChanged);

    const xcb_window_t window
        = m_wm->createWindow(QByteArrayLiteral("cvtest\0CvTest\0"), QByteArrayLiteral("first title"), 4242);
    m_wm->setActive(window);
    QTRY_COMPARE(tracker->current().windowId, quint64(window));
    const WindowInfo info = tracker->current();
    QCOMPARE(info.app, QStringLiteral("cvtest"));
    QCOMPARE(info.windowClass, QStringLiteral("CvTest"));
    QCOMPARE(info.title, QStringLiteral("first title"));
    QCOMPARE(info.pid, 4242);
    QCOMPARE(tracker->lastForeignWindow().windowId, quint64(window));
    QVERIFY(!spy.isEmpty());

    m_wm->setTitle(window, QByteArrayLiteral("second title"));
    QTRY_COMPARE(tracker->current().title, QStringLiteral("second title"));

    m_wm->setActive(0);
    QTRY_VERIFY(!tracker->current().isValid());
    QCOMPARE(tracker->lastForeignWindow().windowId, quint64(window));
}

void TestX11FocusTracker::ownWindowIsNotForeign()
{
    const auto tracker = X11FocusTracker::create();
    QVERIFY(tracker);
    const xcb_window_t other = m_wm->createWindow(QByteArrayLiteral("other\0Other\0"), "other", 4243);
    m_wm->setActive(other);
    QTRY_COMPARE(tracker->lastForeignWindow().windowId, quint64(other));

    const auto self = static_cast<std::uint32_t>(QCoreApplication::applicationPid());
    const xcb_window_t own
        = m_wm->createWindow(QByteArrayLiteral("clipvault\0clipvault\0"), "ClipVault", self);
    m_wm->setActive(own);
    QTRY_COMPARE(tracker->current().windowId, quint64(own));
    QCOMPARE(tracker->lastForeignWindow().windowId, quint64(other));
}

void TestX11FocusTracker::activateSendsEwmhRequest()
{
    const auto tracker = X11FocusTracker::create();
    QVERIFY(tracker);
    QVERIFY(!tracker->activate(WindowInfo { }));
    const xcb_window_t window = m_wm->createWindow(QByteArrayLiteral("target\0Target\0"), "target", 4244);
    WindowInfo target;
    target.windowClass = QStringLiteral("Target");
    target.windowId = window;
    QVERIFY(tracker->activate(target));
    xcb_window_t requested = 0;
    for (int i = 0; i < 100 && requested == 0; ++i) {
        QTest::qWait(20);
        requested = m_wm->takeActivationRequest();
    }
    QCOMPARE(requested, window);
    QVERIFY(tracker->mechanismDescription().contains(QLatin1String("X11")));
}

void TestX11FocusTracker::factoryPicksX11WithoutExtension()
{
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        QSKIP("the factory only uses x11 on the xcb platform");
    const auto tracker = clipvault::platform::createFocusTracker();
    QVERIFY(tracker->mechanismDescription().contains(QLatin1String("X11")));
}

int main(int argc, char** argv)
{
    // no session bus so the shell extension can never be found and the live bus is never touched
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    // the same platform choice main makes
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && qEnvironmentVariableIsSet("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    const QApplication app(argc, argv);
    TestX11FocusTracker test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_x11_focus_tracker.moc"
