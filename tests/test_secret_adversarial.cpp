#include "TestSupport.h"
#include "clipboard/ClipboardMonitor.h"
#include "core/Logging.h"
#include "core/SecretDetector.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QtTest>

using namespace clipvault;
using namespace clipvault::test;
using namespace std::chrono_literals;

namespace {

const QString kSecret = QStringLiteral("gh"
                                       "p_AdversarialSyntheticToken000000000000D4");

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return file.readAll();
}

} // namespace

class TestSecretAdversarial : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_logDir.isValid());
        installLogHandler();
        QLoggingCategory::setFilterRules(QStringLiteral("clipvault.*.debug=true"));
        enableLogFile(logPath());
    }

    // the requirement is that not even a hash of a secret reaches the log
    void captureAndWriteBackLogNoSecretReference()
    {
        if (!QGuiApplication::clipboard())
            QSKIP("no clipboard");
        Fixture fixture;
        ClipboardMonitor monitor;
        connect(&monitor, &ClipboardMonitor::textCaptured, &fixture.history(),
            [&fixture](const QString& text) { fixture.history().add(text); });
        QGuiApplication::clipboard()->setText(kSecret, QClipboard::Clipboard);
        QTest::qWait(100);
        QCOMPARE(fixture.history().ephemeral().count(), 1);
        monitor.writeBack(kSecret);
        QTest::qWait(100);
        const QByteArray log = readAll(logPath());
        QVERIFY2(!log.contains(redact(kSecret).toUtf8()), log.constData());
    }

    // the store only zeroes a buffer nobody else shares so a live window must let go first
    void expiryWipesAnUnsharedBufferWhileTheWindowIsOpen()
    {
        Fixture fixture;
        MainWindow window(fixture.history(), fixture.search(), fixture.settings());
        window.show();
        bool sawWipe = false;
        bool detached = false;
        fixture.history().ephemeral().setWiper([&](QString& text) {
            sawWipe = true;
            detached = text.isDetached();
            text = QString();
        });
        fixture.history().ephemeral().add(
            kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 80ms);
        QTRY_VERIFY_WITH_TIMEOUT(sawWipe, 2000);
        QVERIFY(detached);
    }

    void worstCaseInputsStayFast()
    {
        const QList<QPair<const char*, QString>> cases = {
            { "eyJ repeated 4k", QStringLiteral("eyJ").repeated(1365) },
            { "eyJ dot chain 4k", QStringLiteral("eyJaaaaaaaaa.").repeated(315) },
            { "db scheme no at 4k", QStringLiteral("postgres://a:") + QString(4080, QLatin1Char(':')) },
            { "db scheme repeated 4k", QStringLiteral("redis://a:b").repeated(372) },
            { "password eq 4k", QStringLiteral("password=").repeated(455) },
            { "bearer repeated 4k", QStringLiteral("bearer ").repeated(585) },
            { "sk dash 4k", QStringLiteral("sk-").repeated(1365) },
            { "single token 64k", QString(65536, QLatin1Char('A')) },
            { "AKIA run 4k", QStringLiteral("AKIA").repeated(1024) },
            { "slack hook 4k", QStringLiteral("https://hooks.slack.com/services/").repeated(124) },
            { "underscores 4k", QString(4096, QLatin1Char('_')) },
        };
        for (const auto& [name, text] : cases) {
            QElapsedTimer timer;
            timer.start();
            for (int i = 0; i < 10; ++i)
                (void)detectSecret(text);
            const qint64 perCall = timer.elapsed() / 10;
            qInfo("%s %lld ms", name, static_cast<long long>(perCall));
            QVERIFY2(perCall < 50, name);
        }
    }

    void boundaries()
    {
        const QString pad4k = QString(kSecretEmbeddedScanChars - kSecret.size() - 1, QLatin1Char('x'));
        QVERIFY(detectSecret(pad4k + QLatin1Char(' ') + kSecret).isSecret());
        QVERIFY(!detectSecret(pad4k + QStringLiteral("  ") + kSecret).isSecret());
        QVERIFY(detectSecret(kSecret + QStringLiteral("\r\n")).isSecret());
        QVERIFY(detectSecret(QStringLiteral("\u00e9\u00e9 ") + kSecret).isSecret());
        QVERIFY(detectSecret(kSecret + QString(kSecretMaxScanChars - kSecret.size(), QLatin1Char(' ')))
                .isSecret());
        QVERIFY(!detectSecret(kSecret + QString(kSecretMaxScanChars - kSecret.size() + 1, QLatin1Char(' ')))
                .isSecret());
    }

    void pinnedSecretTextDoesNotMatchSearch()
    {
        Fixture fixture;
        const auto outcome = fixture.history().add(kSecret);
        QVERIFY(outcome.id < -1);
        QVERIFY(fixture.history().setPinned(outcome.id, true));
        QVERIFY(fixture.search().search(QStringLiteral("AdversarialSynthetic"), 10).isEmpty());
    }

private:
    QString logPath() const { return m_logDir.filePath(QStringLiteral("clipvault.log")); }
    QTemporaryDir m_logDir;
};

QTEST_MAIN(TestSecretAdversarial)
#include "test_secret_adversarial.moc"
