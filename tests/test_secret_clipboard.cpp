#include "clipboard/ClipboardMonitor.h"
#include "services/EphemeralStore.h"

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QtTest>

using namespace clipvault;
using namespace std::chrono_literals;

namespace {

const QString kSecret = QStringLiteral("gh"
                                       "p_ClipboardSyntheticToken00000000000000C3");

void externalCopy(const QString& text)
{
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
    QTest::qWait(60);
}

QString clipboardText()
{
    return QGuiApplication::clipboard()->text(QClipboard::Clipboard);
}

} // namespace

// expiry clearing needs a real clipboard so this runs under a display
class TestSecretClipboard : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!QGuiApplication::clipboard())
            QSKIP("no system clipboard on this platform");
        externalCopy(QStringLiteral("clipvault-probe"));
        if (clipboardText() != QStringLiteral("clipvault-probe"))
            QSKIP("clipboard does not round trip on this platform");
    }

    void clearsOnlyWhenTheTextStillMatches()
    {
        ClipboardMonitor monitor;
        externalCopy(kSecret);
        monitor.clearIfHolds(QStringLiteral("something else"));
        QCOMPARE(clipboardText(), kSecret);
        monitor.clearIfHolds(kSecret);
        QTest::qWait(60);
        QVERIFY(clipboardText().isEmpty());
    }

    void writeBackThenExpiryClearsTheClipboard()
    {
        ClipboardMonitor monitor;
        EphemeralStore store;
        store.setClipboardClearer([&monitor](const QString& text) { monitor.clearIfHolds(text); });
        store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 150ms);
        monitor.writeBack(kSecret);
        QTest::qWait(60);
        QCOMPARE(clipboardText(), kSecret);
        QTRY_COMPARE_WITH_TIMEOUT(store.count(), 0, 2000);
        QTest::qWait(60);
        QVERIFY(clipboardText().isEmpty());
    }

    void newerClipboardTextSurvivesExpiry()
    {
        ClipboardMonitor monitor;
        EphemeralStore store;
        store.setClipboardClearer([&monitor](const QString& text) { monitor.clearIfHolds(text); });
        store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 150ms);
        externalCopy(kSecret);
        externalCopy(QStringLiteral("copied later"));
        QTRY_COMPARE_WITH_TIMEOUT(store.count(), 0, 2000);
        QCOMPARE(clipboardText(), QStringLiteral("copied later"));
    }
};

// no test main macro because the monitor needs the xcb plugin
int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    const QApplication app(argc, argv);
    TestSecretClipboard testObject;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&testObject, argc, argv);
}
#include "test_secret_clipboard.moc"
