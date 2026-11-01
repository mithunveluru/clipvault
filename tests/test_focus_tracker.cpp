#include "platform/FocusTracker.h"
#include "platform/GnomeShellFocusTracker.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

using clipvault::platform::GnomeShellFocusTracker;
using clipvault::platform::WindowInfo;

namespace {

// stands in for the shell extension with the same interface on a second connection
class FakeShell : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.mithunveluru.ClipVault.Focus")
public:
    QString appId = QStringLiteral("org.gnome.TextEditor");
    QString wmClass = QStringLiteral("gnome-text-editor");
    QString title = QStringLiteral("notes.txt");
    // a fixed pid can equal our own on a fresh machine and then reads as our window
    qlonglong pid = QCoreApplication::applicationPid() + 1;
    qulonglong windowId = 77;
    QList<qulonglong> activated;
    QStringList subscribers;
    QDBusConnection* bus = nullptr;

    // like the extension this goes only to subscribers and never as a broadcast
    void announce()
    {
        for (const QString& name : subscribers) {
            QDBusMessage signal
                = QDBusMessage::createTargetedSignal(name, QLatin1String(GnomeShellFocusTracker::kPath),
                    QLatin1String(GnomeShellFocusTracker::kInterface), QStringLiteral("FocusChanged"));
            signal << appId << wmClass << title << pid << windowId;
            bus->send(signal);
        }
    }

public slots:
    // the real extension also checks the caller binary which a fake cannot
    void Subscribe(const QDBusMessage& message)
    {
        if (!subscribers.contains(message.service()))
            subscribers << message.service();
    }
    QString GetFocus(QString& outClass, QString& outTitle, qlonglong& outPid, qulonglong& outId)
    {
        outClass = wmClass;
        outTitle = title;
        outPid = pid;
        outId = windowId;
        return appId;
    }
    bool Activate(qulonglong id)
    {
        activated << id;
        return id == windowId;
    }

signals:
    void FocusChanged(const QString& appId, const QString& wmClass, const QString& title, qlonglong pid,
        qulonglong windowId);
};

} // namespace

class TestFocusTracker : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void factoryFallsBackToNull();
    void readsInitialFocusAndChanges();
    void ownWindowIsNotForeign();
    void activateCallsTheExtension();
    void factoryPrefersTheExtension();
    void extensionLeavingClearsFocus();
    void subscribesAgainWhenTheNameReturns();

private:
    bool startShell();
    void stopShell();

    QProcess m_daemon;
    QDBusConnection m_shellBus { QString() };
    FakeShell m_shell;
};

void TestFocusTracker::initTestCase()
{
    // a private bus daemon so the live session bus is never touched whoever runs the test
    m_daemon.start(QStringLiteral("dbus-daemon"),
        { QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1") });
    if (!m_daemon.waitForStarted())
        QSKIP("dbus-daemon is not installed");
    QVERIFY(m_daemon.waitForReadyRead(5000));
    const QByteArray address = m_daemon.readLine().trimmed();
    QVERIFY(!address.isEmpty());
    qputenv("DBUS_SESSION_BUS_ADDRESS", address);
    QVERIFY(QDBusConnection::sessionBus().isConnected());

    m_shellBus = QDBusConnection::connectToBus(QString::fromUtf8(address), QStringLiteral("fake-shell"));
    QVERIFY(m_shellBus.isConnected());
    m_shell.bus = &m_shellBus;
    QVERIFY(m_shellBus.registerObject(QLatin1String(GnomeShellFocusTracker::kPath), &m_shell,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
}

void TestFocusTracker::cleanupTestCase()
{
    QDBusConnection::disconnectFromBus(QStringLiteral("fake-shell"));
    m_daemon.kill();
    m_daemon.waitForFinished();
}

bool TestFocusTracker::startShell()
{
    return m_shellBus.registerService(QLatin1String(GnomeShellFocusTracker::kService));
}

void TestFocusTracker::stopShell()
{
    m_shellBus.unregisterService(QLatin1String(GnomeShellFocusTracker::kService));
}

void TestFocusTracker::factoryFallsBackToNull()
{
    QVERIFY(!GnomeShellFocusTracker::isAvailable());
    // no gui application here so x11 is skipped as well
    const auto tracker = clipvault::platform::createFocusTracker();
    QVERIFY(tracker);
    QVERIFY(!tracker->current().isValid());
    QVERIFY(!tracker->activate(WindowInfo { QStringLiteral("a"), QStringLiteral("b"), QString(), 1, 1 }));
    QVERIFY(tracker->mechanismDescription().contains(QLatin1String("does not tell")));
}

void TestFocusTracker::readsInitialFocusAndChanges()
{
    QVERIFY(startShell());
    GnomeShellFocusTracker tracker;
    QSignalSpy spy(&tracker, &GnomeShellFocusTracker::focusChanged);
    QTRY_COMPARE(tracker.current().windowId, 77ULL);
    QCOMPARE(tracker.current().app, QStringLiteral("org.gnome.TextEditor"));
    QCOMPARE(tracker.current().windowClass, QStringLiteral("gnome-text-editor"));
    QCOMPARE(tracker.current().title, QStringLiteral("notes.txt"));
    QCOMPARE(tracker.current().pid, QCoreApplication::applicationPid() + 1);
    QCOMPARE(tracker.lastForeignWindow().windowId, 77ULL);

    m_shell.appId.clear();
    m_shell.wmClass = QStringLiteral("kitty");
    m_shell.title = QStringLiteral("shell");
    m_shell.windowId = 78;
    m_shell.announce();
    QTRY_COMPARE(tracker.current().windowId, 78ULL);
    // an empty app id falls back to the wm class
    QCOMPARE(tracker.current().app, QStringLiteral("kitty"));
    QVERIFY(spy.count() >= 2);
    stopShell();
}

void TestFocusTracker::ownWindowIsNotForeign()
{
    QVERIFY(startShell());
    m_shell.windowId = 90;
    m_shell.pid = QCoreApplication::applicationPid() + 1;
    GnomeShellFocusTracker tracker;
    QTRY_COMPARE(tracker.lastForeignWindow().windowId, 90ULL);

    m_shell.windowId = 91;
    m_shell.pid = QCoreApplication::applicationPid();
    m_shell.announce();
    QTRY_COMPARE(tracker.current().windowId, 91ULL);
    QCOMPARE(tracker.lastForeignWindow().windowId, 90ULL);
    m_shell.pid = QCoreApplication::applicationPid() + 1;
    stopShell();
}

void TestFocusTracker::activateCallsTheExtension()
{
    QVERIFY(startShell());
    m_shell.activated.clear();
    m_shell.windowId = 55;
    GnomeShellFocusTracker tracker;
    QVERIFY(!tracker.activate(WindowInfo { }));
    WindowInfo target;
    target.windowClass = QStringLiteral("kitty");
    target.windowId = 55;
    QVERIFY(tracker.activate(target));
    QTRY_COMPARE(m_shell.activated.size(), 1);
    QCOMPARE(m_shell.activated.first(), 55ULL);
    stopShell();
}

void TestFocusTracker::factoryPrefersTheExtension()
{
    QVERIFY(startShell());
    QVERIFY(GnomeShellFocusTracker::isAvailable());
    const auto tracker = clipvault::platform::createFocusTracker();
    QVERIFY(tracker->mechanismDescription().contains(QLatin1String("GNOME Shell extension")));
    QTRY_VERIFY(tracker->current().isValid());
    stopShell();
}

void TestFocusTracker::extensionLeavingClearsFocus()
{
    QVERIFY(startShell());
    GnomeShellFocusTracker tracker;
    QTRY_VERIFY(tracker.current().isValid());
    stopShell();
    QTRY_VERIFY(!tracker.current().isValid());
    // the last foreign window survives so paste back still knows where to go
    QVERIFY(tracker.lastForeignWindow().isValid());
}

void TestFocusTracker::subscribesAgainWhenTheNameReturns()
{
    QVERIFY(startShell());
    m_shell.subscribers.clear();
    GnomeShellFocusTracker tracker;
    const QString self = QDBusConnection::sessionBus().baseService();
    QTRY_VERIFY(m_shell.subscribers.contains(self));
    stopShell();
    m_shell.subscribers.clear();
    QVERIFY(startShell());
    QTRY_VERIFY(m_shell.subscribers.contains(self));
    m_shell.windowId = 99;
    m_shell.announce();
    QTRY_COMPARE(tracker.current().windowId, 99ULL);
    stopShell();
}

QTEST_GUILESS_MAIN(TestFocusTracker)
#include "test_focus_tracker.moc"
