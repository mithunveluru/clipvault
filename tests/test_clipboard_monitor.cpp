#include "clipboard/ClipboardMonitor.h"

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QtTest>

using namespace clipvault;

// Covers copy back suppression and pause needs a display
class TestClipboardMonitor : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void captures_copied_text();
    void ignores_its_own_write_back();
    void a_later_external_copy_of_the_same_text_is_still_captured();
    void ignores_a_repeat_announcement_of_the_same_selection();
    void captures_the_same_text_again_once_the_repeat_window_passes();
    void ignores_changes_while_paused();
    void captures_again_after_resuming();

private:
    // Copies the way another application would
    static void externalCopy(const QString& text)
    {
        QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
        QTest::qWait(60);
    }
};

void TestClipboardMonitor::initTestCase()
{
    if (!QGuiApplication::clipboard())
        QSKIP("no system clipboard on this platform");
    QGuiApplication::clipboard()->clear(QClipboard::Clipboard);
    QTest::qWait(60);
    QSignalSpy probe(QGuiApplication::clipboard(), &QClipboard::dataChanged);
    QGuiApplication::clipboard()->setText(QStringLiteral("clipvault-probe"));
    // xcb emits dataChanged synchronously from setText
    if (probe.count() == 0 && !probe.wait(1000))
        QSKIP("clipboard change notifications are not delivered on this platform");
}

void TestClipboardMonitor::init()
{
    QGuiApplication::clipboard()->setText(QStringLiteral("reset-%1").arg(QRandomGenerator::global()->generate()));
    QTest::qWait(60);
}

void TestClipboardMonitor::captures_copied_text()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);

    externalCopy(QStringLiteral("hello world"));

    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.first().first().toString(), QStringLiteral("hello world"));
}

void TestClipboardMonitor::ignores_its_own_write_back()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);

    // Copy back must not become a new entry
    monitor.writeBack(QStringLiteral("an old history entry"));
    QTest::qWait(120);

    QCOMPARE(captured.count(), 0);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("an old history entry"));
}

void TestClipboardMonitor::a_later_external_copy_of_the_same_text_is_still_captured()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);

    monitor.writeBack(QStringLiteral("shared text"));
    QTest::qWait(120);
    QCOMPARE(captured.count(), 0);

    // Suppression covers one event not the string forever
    externalCopy(QStringLiteral("something else"));
    externalCopy(QStringLiteral("shared text"));

    QCOMPARE(captured.count(), 2);
    QCOMPARE(captured.last().first().toString(), QStringLiteral("shared text"));
}

void TestClipboardMonitor::ignores_a_repeat_announcement_of_the_same_selection()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);

    // Back to back identical text is one copy
    externalCopy(QStringLiteral("announced twice"));
    externalCopy(QStringLiteral("announced twice"));

    QCOMPARE(captured.count(), 1);
}

void TestClipboardMonitor::captures_the_same_text_again_once_the_repeat_window_passes()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);

    externalCopy(QStringLiteral("copied twice, deliberately"));
    QCOMPARE(captured.count(), 1);

    // A deliberate re-copy later must still register
    QTest::qWait(1100);
    externalCopy(QStringLiteral("copied twice, deliberately"));
    QCOMPARE(captured.count(), 2);
}

void TestClipboardMonitor::ignores_changes_while_paused()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);
    QSignalSpy pausedChanged(&monitor, &ClipboardMonitor::pausedChanged);

    monitor.setPaused(true);
    QCOMPARE(pausedChanged.count(), 1);
    QVERIFY(monitor.isPaused());

    externalCopy(QStringLiteral("secret while paused"));
    QCOMPARE(captured.count(), 0);
}

void TestClipboardMonitor::captures_again_after_resuming()
{
    ClipboardMonitor monitor;
    QSignalSpy captured(&monitor, &ClipboardMonitor::textCaptured);

    monitor.setPaused(true);
    externalCopy(QStringLiteral("not recorded"));
    QCOMPARE(captured.count(), 0);

    monitor.setPaused(false);
    externalCopy(QStringLiteral("recorded again"));

    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.first().first().toString(), QStringLiteral("recorded again"));
}

// Not QTEST_MAIN because the monitor needs the xcb plugin
int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    TestClipboardMonitor testObject;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&testObject, argc, argv);
}
#include "test_clipboard_monitor.moc"
