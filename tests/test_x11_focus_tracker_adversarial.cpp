#include "platform/X11FocusTracker.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QTimer>

#include <xcb/xcb.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>

using clipvault::platform::WindowInfo;
using clipvault::platform::X11FocusTracker;

namespace {

struct FreeDeleter {
    void operator()(void* pointer) const { std::free(pointer); }
};

// plays the window manager and any hostile client on its own connection
class Client {
public:
    Client()
        : m_c(xcb_connect(nullptr, nullptr))
    {
        m_root = xcb_setup_roots_iterator(xcb_get_setup(m_c.get())).data->root;
        m_active = atom("_NET_ACTIVE_WINDOW");
        m_name = atom("_NET_WM_NAME");
        m_utf8 = atom("UTF8_STRING");
    }
    xcb_connection_t* c() const { return m_c.get(); }
    xcb_window_t root() const { return m_root; }

    xcb_window_t window(const QByteArray& wmClass)
    {
        const xcb_window_t w = xcb_generate_id(c());
        xcb_create_window(c(), XCB_COPY_FROM_PARENT, w, m_root, 0, 0, 10, 10, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
        xcb_change_property(c(), XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
            static_cast<std::uint32_t>(wmClass.size()), wmClass.constData());
        xcb_flush(c());
        return w;
    }
    void title(xcb_window_t w, const QByteArray& t)
    {
        xcb_change_property(c(), XCB_PROP_MODE_REPLACE, w, m_name, m_utf8, 8,
            static_cast<std::uint32_t>(t.size()), t.constData());
    }
    void active(xcb_window_t w)
    {
        xcb_change_property(c(), XCB_PROP_MODE_REPLACE, m_root, m_active, XCB_ATOM_WINDOW, 32, 1, &w);
        xcb_flush(c());
    }

private:
    xcb_atom_t atom(const char* name)
    {
        const std::unique_ptr<xcb_intern_atom_reply_t, FreeDeleter> r(xcb_intern_atom_reply(
            c(), xcb_intern_atom(c(), 0, static_cast<std::uint16_t>(qstrlen(name)), name), nullptr));
        return r ? r->atom : 0;
    }
    X11FocusTracker::Connection m_c;
    xcb_window_t m_root = 0;
    xcb_atom_t m_active = 0;
    xcb_atom_t m_name = 0;
    xcb_atom_t m_utf8 = 0;
};

} // namespace

class TestX11Adversarial : public QObject {
    Q_OBJECT

private slots:
    void bogusAndDestroyedWindows();
    void rootAsActiveWindowKeepsTracking();
    void titleSpamDoesNotStarveEventLoop();
};

void TestX11Adversarial::bogusAndDestroyedWindows()
{
    Client wm;
    const auto tracker = X11FocusTracker::create();
    QVERIFY(tracker);
    wm.active(0x0dead00);
    QTest::qWait(100);
    QVERIFY(!tracker->current().isValid());
    const xcb_window_t w = wm.window(QByteArrayLiteral("a\0A\0"));
    wm.active(w);
    QTRY_COMPARE(tracker->current().windowId, quint64(w));
    xcb_destroy_window(wm.c(), w);
    xcb_flush(wm.c());
    const xcb_window_t next = wm.window(QByteArrayLiteral("b\0B\0"));
    wm.active(next);
    QTRY_COMPARE(tracker->current().windowId, quint64(next));
}

// a window manager or any client may point the active window at the root
void TestX11Adversarial::rootAsActiveWindowKeepsTracking()
{
    Client wm;
    const auto tracker = X11FocusTracker::create();
    QVERIFY(tracker);
    wm.active(wm.root());
    QTest::qWait(100);
    const xcb_window_t a = wm.window(QByteArrayLiteral("a\0A\0"));
    wm.active(a);
    QTRY_COMPARE(tracker->current().windowId, quint64(a));
    const xcb_window_t b = wm.window(QByteArrayLiteral("b\0B\0"));
    wm.active(b);
    QTRY_COMPARE(tracker->current().windowId, quint64(b));
}

void TestX11Adversarial::titleSpamDoesNotStarveEventLoop()
{
    Client wm;
    const xcb_window_t w = wm.window(QByteArrayLiteral("spam\0Spam\0"));
    wm.active(w);
    const auto tracker = X11FocusTracker::create();
    QVERIFY(tracker);
    QTRY_COMPARE(tracker->current().windowId, quint64(w));

    const pid_t child = fork();
    if (child == 0) {
        Client spammer;
        for (std::uint64_t i = 0;; ++i) {
            spammer.title(w, QByteArray::number(i));
            if (i % 64 == 0)
                xcb_flush(spammer.c());
        }
    }
    int ticks = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&ticks] { ++ticks; });
    timer.start(10);
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    qInfo() << "timer ticks in two seconds under title spam" << ticks;
    QVERIFY(ticks > 50);
}

int main(int argc, char** argv)
{
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    const QApplication app(argc, argv);
    TestX11Adversarial test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_x11_focus_tracker_adversarial.moc"
