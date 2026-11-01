#include "platform/GnomeShellFocusTracker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

using clipvault::platform::GnomeShellFocusTracker;
using clipvault::platform::WindowInfo;

namespace {

// a hostile owner of the extension name that misbehaves on purpose
class HostileShell : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.mithunveluru.ClipVault.Focus")
public:
    QString title = QStringLiteral("t");

public slots:
    // never answers so the caller has to wait for the bus timeout
    void Activate(qulonglong, const QDBusMessage& message) { message.setDelayedReply(true); }
    QString GetFocus(QString& outClass, QString& outTitle, qlonglong& outPid, qulonglong& outId)
    {
        outClass = QStringLiteral("kitty");
        outTitle = title;
        outPid = 1;
        outId = 5;
        return QStringLiteral("kitty");
    }
};

} // namespace

class TestFocusTrackerAdversarial : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void wrongSignatureSignalsAreIgnored();
    void signalsFromNonOwnersAreIgnored();
    void hugeTitleDoesNotStall();
    void silentActivateDoesNotBlock();

private:
    void send(QDBusConnection& bus, const QVariantList& args);

    QProcess m_daemon;
    QByteArray m_address;
    QDBusConnection m_shellBus { QString() };
    QDBusConnection m_rogueBus { QString() };
    HostileShell m_shell;
};

void TestFocusTrackerAdversarial::initTestCase()
{
    m_daemon.start(QStringLiteral("dbus-daemon"),
        { QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1") });
    if (!m_daemon.waitForStarted())
        QSKIP("dbus-daemon is not installed");
    QVERIFY(m_daemon.waitForReadyRead(5000));
    m_address = m_daemon.readLine().trimmed();
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_address);
    m_shellBus = QDBusConnection::connectToBus(QString::fromUtf8(m_address), QStringLiteral("hostile"));
    m_rogueBus = QDBusConnection::connectToBus(QString::fromUtf8(m_address), QStringLiteral("rogue"));
    QVERIFY(m_shellBus.registerObject(
        QLatin1String(GnomeShellFocusTracker::kPath), &m_shell, QDBusConnection::ExportAllSlots));
    QVERIFY(m_shellBus.registerService(QLatin1String(GnomeShellFocusTracker::kService)));
}

void TestFocusTrackerAdversarial::cleanupTestCase()
{
    QDBusConnection::disconnectFromBus(QStringLiteral("hostile"));
    QDBusConnection::disconnectFromBus(QStringLiteral("rogue"));
    m_daemon.kill();
    m_daemon.waitForFinished();
}

void TestFocusTrackerAdversarial::send(QDBusConnection& bus, const QVariantList& args)
{
    QDBusMessage signal = QDBusMessage::createSignal(QLatin1String(GnomeShellFocusTracker::kPath),
        QLatin1String(GnomeShellFocusTracker::kInterface), QStringLiteral("FocusChanged"));
    signal.setArguments(args);
    QVERIFY(bus.send(signal));
}

void TestFocusTrackerAdversarial::wrongSignatureSignalsAreIgnored()
{
    GnomeShellFocusTracker tracker;
    QTRY_COMPARE(tracker.current().windowId, 5ULL);
    send(m_shellBus, { QStringLiteral("a") });
    send(m_shellBus,
        { QStringLiteral("a"), QStringLiteral("b"), 3, QStringLiteral("c"),
            QVariant::fromValue(qulonglong(9)) });
    send(m_shellBus,
        { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QVariant::fromValue(qlonglong(1)),
            QVariant::fromValue(qulonglong(10)), QStringLiteral("extra") });
    QTest::qWait(200);
    QCOMPARE(tracker.current().windowId, 5ULL);
}

void TestFocusTrackerAdversarial::signalsFromNonOwnersAreIgnored()
{
    GnomeShellFocusTracker tracker;
    QTRY_COMPARE(tracker.current().windowId, 5ULL);
    send(m_rogueBus,
        { QStringLiteral("evil"), QStringLiteral("evil"), QStringLiteral("evil"),
            QVariant::fromValue(qlonglong(1)), QVariant::fromValue(qulonglong(666)) });
    QTest::qWait(200);
    QCOMPARE(tracker.current().windowId, 5ULL);
}

void TestFocusTrackerAdversarial::hugeTitleDoesNotStall()
{
    GnomeShellFocusTracker tracker;
    QTRY_COMPARE(tracker.current().windowId, 5ULL);
    QElapsedTimer timer;
    timer.start();
    send(m_shellBus,
        { QStringLiteral("kitty"), QStringLiteral("kitty"), QString(32 * 1024 * 1024, QLatin1Char('(')),
            QVariant::fromValue(qlonglong(1)), QVariant::fromValue(qulonglong(6)) });
    QTRY_COMPARE_WITH_TIMEOUT(tracker.current().windowId, 6ULL, 20000);
    qInfo() << "huge title delivered in ms" << timer.elapsed() << "title length kept"
            << tracker.current().title.size();
}

void TestFocusTrackerAdversarial::silentActivateDoesNotBlock()
{
    GnomeShellFocusTracker tracker;
    WindowInfo target;
    target.windowClass = QStringLiteral("kitty");
    target.windowId = 5;
    QElapsedTimer timer;
    timer.start();
    const bool sent = tracker.activate(target);
    QVERIFY(timer.elapsed() < 100);
    // true here only means the call left and says nothing about the window being raised
    QVERIFY(sent);
}

QTEST_GUILESS_MAIN(TestFocusTrackerAdversarial)
#include "test_focus_tracker_adversarial.moc"
