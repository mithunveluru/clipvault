#include "platform/SourceLocation.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <limits>

using namespace clipvault::platform;

class TestSourceLocationAdversarial : public QObject {
    Q_OBJECT

private slots:
    void init();
    void terminalTitleCannotPointOutsideCwd();
    void terminalTitleNamesDangerousFile();
    void procAndDevTargets();
    void oddFileNames();
    void desktopSuffixCase();
    void hugeTitlesStayFast();
    void invalidUtf8Title();
    void bogusPid();

private:
    QString makeFile(const QString& relative, const QByteArray& body = "x");
    QTemporaryDir m_dir;
};

void TestSourceLocationAdversarial::init()
{
    QVERIFY(m_dir.isValid());
    QDir(m_dir.path()).mkpath(QStringLiteral("work"));
}

QString TestSourceLocationAdversarial::makeFile(const QString& relative, const QByteArray& body)
{
    const QString path = m_dir.filePath(relative);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return { };
    file.write(body);
    return path;
}

// any program printing an osc title escape controls a terminal title
void TestSourceLocationAdversarial::terminalTitleCannotPointOutsideCwd()
{
    const QString work = m_dir.filePath(QStringLiteral("work"));
    const QString outside = makeFile(QStringLiteral("secret.txt"));
    QCOMPARE(
        pathFromEditorTitle(QStringLiteral("../secret.txt - VIM"), EditorKind::FileFirst, work), QString());
    // vim shows absolute paths so they stay allowed outside the kernel trees
    QCOMPARE(pathFromEditorTitle(QStringLiteral("/etc/passwd - VIM"), EditorKind::FileFirst, work),
        QStringLiteral("/etc/passwd"));
    Q_UNUSED(outside)
}

void TestSourceLocationAdversarial::terminalTitleNamesDangerousFile()
{
    const QString jar = makeFile(QStringLiteral("work/evil.jar"), "PK\x03\x04");
    QProcess shell;
    shell.setWorkingDirectory(m_dir.filePath(QStringLiteral("work")));
    shell.start(QStringLiteral("sleep"), { QStringLiteral("30") });
    QVERIFY(shell.waitForStarted());
    const WindowInfo terminal { QStringLiteral("kitty"), QStringLiteral("kitty"),
        QStringLiteral("evil.jar - VIM"), QCoreApplication::applicationPid(), 1 };
    QTRY_VERIFY(!resolveSourcePath(terminal).isEmpty());
    const QString resolved = resolveSourcePath(terminal);
    // a file the title names must never be handed to its default handler directly
    QVERIFY2(openableLocation(resolved) != jar, qPrintable(QStringLiteral("would open the jar")));
    shell.kill();
    shell.waitForFinished();
}

void TestSourceLocationAdversarial::procAndDevTargets()
{
    QCOMPARE(
        pathFromEditorTitle(QStringLiteral("/proc/self/environ - VIM"), EditorKind::FileFirst, QString()),
        QString());
    QCOMPARE(
        pathFromEditorTitle(QStringLiteral("/dev/zero - VIM"), EditorKind::FileFirst, QString()), QString());
    const QString link = m_dir.filePath(QStringLiteral("work/zero.txt"));
    QVERIFY(QFile::link(QStringLiteral("/dev/zero"), link));
    QCOMPARE(openableLocation(link), QString());
    const QString environ = m_dir.filePath(QStringLiteral("work/env.txt"));
    QVERIFY(QFile::link(QStringLiteral("/proc/self/environ"), environ));
    QCOMPARE(openableLocation(environ), QString());
}

void TestSourceLocationAdversarial::oddFileNames()
{
    const QString dashed = makeFile(QStringLiteral("work/--help"));
    QCOMPARE(openableLocation(dashed), dashed);
    const QString newline = makeFile(QStringLiteral("work/a\nb.txt"));
    QCOMPARE(openableLocation(newline), newline);
    const QString query = makeFile(QStringLiteral("work/a?x=1#y"));
    QCOMPARE(QUrl::fromLocalFile(query).toLocalFile(), query);
    QCOMPARE(
        pathFromEditorTitle(QStringLiteral("file:///etc/passwd - VIM"), EditorKind::FileFirst, QString()),
        QString());
}

void TestSourceLocationAdversarial::desktopSuffixCase()
{
    const QString launcher = makeFile(QStringLiteral("work/evil.Desktop"), "[Desktop Entry]\nExec=true\n");
    QCOMPARE(openableLocation(launcher), m_dir.filePath(QStringLiteral("work")));
}

void TestSourceLocationAdversarial::hugeTitlesStayFast()
{
    const QStringList titles {
        QString(200000, QLatin1Char('(')) + QStringLiteral(" - VIM"),
        QStringLiteral("a") + QString(200000, QLatin1Char(' ')) + QStringLiteral("(x - VIM"),
        QStringLiteral("x") + QStringLiteral(" - x").repeated(50000),
    };
    for (const QString& title : titles) {
        QElapsedTimer timer;
        timer.start();
        Q_UNUSED(pathFromEditorTitle(title, EditorKind::FileFirst, m_dir.path()))
        Q_UNUSED(isVimTitle(title))
        QVERIFY2(timer.elapsed() < 200, qPrintable(QString::number(timer.elapsed())));
    }
}

void TestSourceLocationAdversarial::invalidUtf8Title()
{
    const QString title = QString::fromUtf8("\xff\xfe\xc0\xaf..\xc0\xafsecret - VIM");
    QCOMPARE(pathFromEditorTitle(title, EditorKind::FileFirst, m_dir.path()), QString());
}

void TestSourceLocationAdversarial::bogusPid()
{
    QCOMPARE(foregroundWorkingDirectory(4294967295LL), QString());
    QCOMPARE(processWorkingDirectory(std::numeric_limits<qint64>::max()), QString());
}

QTEST_GUILESS_MAIN(TestSourceLocationAdversarial)
#include "test_source_location_adversarial.moc"
