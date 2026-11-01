#include "platform/SourceLocation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace clipvault::platform;

class TestSourceLocation : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void detectsTerminalsAndEditors();
    void detectsVimTitles();
    void editorTitles_data();
    void editorTitles();
    void baseDirNeedsMatchingProject();
    void neverReturnsMissingFilesOrUrls();
    void readsNewestDescendantWorkingDirectory();
    void terminalWithoutChildrenGivesNothing();
    void resolvesTerminalAndEditorWindows();
    void openableLocationRules();

private:
    QString makeFile(const QString& relative);

    QTemporaryDir m_dir;
    QByteArray m_oldHome;
};

void TestSourceLocation::init()
{
    QVERIFY(m_dir.isValid());
    m_oldHome = qgetenv("HOME");
    // tilde in titles expands to this throwaway home
    qputenv("HOME", m_dir.path().toUtf8());
    QDir(m_dir.path()).mkpath(QStringLiteral("src/clipvault/src"));
    makeFile(QStringLiteral("src/clipvault/main.cpp"));
    makeFile(QStringLiteral("src/clipvault/src/app.cpp"));
}

void TestSourceLocation::cleanup()
{
    qputenv("HOME", m_oldHome);
}

QString TestSourceLocation::makeFile(const QString& relative)
{
    const QString path = m_dir.filePath(relative);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return { };
    file.write("x");
    return path;
}

void TestSourceLocation::detectsTerminalsAndEditors()
{
    QVERIFY(isTerminal({ QStringLiteral("org.gnome.Console"), QString(), QString(), 0, 0 }));
    QVERIFY(isTerminal({ QString(), QStringLiteral("kitty"), QString(), 0, 0 }));
    QVERIFY(isTerminal(
        { QStringLiteral("gnome-terminal-server"), QStringLiteral("Gnome-terminal"), QString(), 0, 0 }));
    QVERIFY(!isTerminal({ QStringLiteral("firefox"), QStringLiteral("firefox"), QString(), 0, 0 }));

    QCOMPARE(editorKind({ QStringLiteral("code"), QStringLiteral("Code"), QString(), 0, 0 }),
        EditorKind::FileFirst);
    QCOMPARE(editorKind({ QStringLiteral("org.gnome.TextEditor"), QString(), QString(), 0, 0 }),
        EditorKind::FileFirst);
    QCOMPARE(
        editorKind({ QStringLiteral("jetbrains-clion"), QStringLiteral("jetbrains-clion"), QString(), 0, 0 }),
        EditorKind::ProjectFirst);
    QCOMPARE(editorKind({ QStringLiteral("firefox"), QStringLiteral("firefox"), QString(), 0, 0 }),
        EditorKind::None);
}

void TestSourceLocation::detectsVimTitles()
{
    QVERIFY(isVimTitle(QStringLiteral("main.cpp (~/src) - VIM")));
    QVERIFY(isVimTitle(QStringLiteral("main.cpp + (~/src) - NVIM")));
    QVERIFY(!isVimTitle(QStringLiteral("user@host: ~/src")));
    QVERIFY(!isVimTitle(QStringLiteral("vim tips - Firefox")));
}

void TestSourceLocation::editorTitles_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("expected");

    const auto file = EditorKind::FileFirst;
    const auto project = EditorKind::ProjectFirst;
    const QString main = QStringLiteral("src/clipvault/main.cpp");
    const QString app = QStringLiteral("src/clipvault/src/app.cpp");

    QTest::newRow("vim with dir") << QStringLiteral("main.cpp (~/src/clipvault) - VIM") << int(file) << main;
    QTest::newRow("nvim modified") << QStringLiteral("main.cpp + (~/src/clipvault) - NVIM") << int(file)
                                   << main;
    QTest::newRow("gedit dirty") << QStringLiteral("*main.cpp (~/src/clipvault) - gedit") << int(file)
                                 << main;
    QTest::newRow("sublime full path")
        << QStringLiteral("~/src/clipvault/main.cpp (clipvault) - Sublime Text") << int(file) << main;
    QTest::newRow("jetbrains with dir")
        << QStringLiteral("clipvault [~/src/clipvault] – src/app.cpp") << int(project) << app;
    QTest::newRow("vscode remote") << QStringLiteral("main.cpp - clipvault [SSH: box] - Visual Studio Code")
                                   << int(file) << QString();
    QTest::newRow("elided path") << QStringLiteral("clipvault – …/src/app.cpp") << int(project) << QString();
    QTest::newRow("no file") << QStringLiteral("Welcome - Visual Studio Code") << int(file) << QString();
    QTest::newRow("unknown editor") << QStringLiteral("main.cpp (~/src/clipvault) - VIM")
                                    << int(EditorKind::None) << QString();
}

void TestSourceLocation::editorTitles()
{
    QFETCH(QString, title);
    QFETCH(int, kind);
    QFETCH(QString, expected);
    const QString path = pathFromEditorTitle(title, static_cast<EditorKind>(kind), QString());
    QCOMPARE(path, expected.isEmpty() ? QString() : m_dir.filePath(expected));
}

void TestSourceLocation::baseDirNeedsMatchingProject()
{
    const QString base = m_dir.filePath(QStringLiteral("src/clipvault"));
    const QString main = m_dir.filePath(QStringLiteral("src/clipvault/main.cpp"));
    QCOMPARE(pathFromEditorTitle(
                 QStringLiteral("● main.cpp - clipvault - Visual Studio Code"), EditorKind::FileFirst, base),
        main);
    QCOMPARE(
        pathFromEditorTitle(QStringLiteral("main.cpp - Visual Studio Code"), EditorKind::FileFirst, base),
        main);
    QCOMPARE(pathFromEditorTitle(
                 QStringLiteral("main.cpp - other - Visual Studio Code"), EditorKind::FileFirst, base),
        QString());
    QCOMPARE(pathFromEditorTitle(QStringLiteral("clipvault – src/app.cpp"), EditorKind::ProjectFirst, base),
        m_dir.filePath(QStringLiteral("src/clipvault/src/app.cpp")));
}

void TestSourceLocation::neverReturnsMissingFilesOrUrls()
{
    QCOMPARE(pathFromEditorTitle(
                 QStringLiteral("gone.cpp (~/src/clipvault) - gedit"), EditorKind::FileFirst, QString()),
        QString());
    // a directory is not a file named by an editor
    QCOMPARE(
        pathFromEditorTitle(QStringLiteral("clipvault (~/src) - gedit"), EditorKind::FileFirst, QString()),
        QString());
    QCOMPARE(pathFromEditorTitle(QStringLiteral("https://example.org/main.cpp - Visual Studio Code"),
                 EditorKind::FileFirst, m_dir.path()),
        QString());
    QCOMPARE(pathFromEditorTitle(QStringLiteral("main.cpp (~/src/clipvault/../clipvault) - gedit"),
                 EditorKind::FileFirst, QString()),
        m_dir.filePath(QStringLiteral("src/clipvault/main.cpp")));
}

void TestSourceLocation::readsNewestDescendantWorkingDirectory()
{
    const QString outer = m_dir.filePath(QStringLiteral("src"));
    const QString inner = m_dir.filePath(QStringLiteral("src/clipvault"));

    QProcess older;
    older.setWorkingDirectory(m_dir.path());
    older.start(QStringLiteral("sleep"), { QStringLiteral("30") });
    QVERIFY(older.waitForStarted());
    QTest::qWait(20);

    // a shell that stays alive with a child in another directory stands in for a terminal tab
    QProcess shell;
    shell.setWorkingDirectory(outer);
    shell.start(
        QStringLiteral("sh"), { QStringLiteral("-c"), QStringLiteral("cd clipvault && sleep 30; true") });
    QVERIFY(shell.waitForStarted());

    const qint64 self = QCoreApplication::applicationPid();
    QTRY_COMPARE(foregroundWorkingDirectory(self), inner);
    QCOMPARE(processWorkingDirectory(older.processId()), m_dir.path());

    shell.kill();
    older.kill();
    shell.waitForFinished();
    older.waitForFinished();
}

void TestSourceLocation::terminalWithoutChildrenGivesNothing()
{
    QProcess lone;
    lone.start(QStringLiteral("sleep"), { QStringLiteral("30") });
    QVERIFY(lone.waitForStarted());
    QCOMPARE(foregroundWorkingDirectory(lone.processId()), QString());
    QCOMPARE(foregroundWorkingDirectory(0), QString());
    QCOMPARE(processWorkingDirectory(-1), QString());
    lone.kill();
    lone.waitForFinished();
}

void TestSourceLocation::resolvesTerminalAndEditorWindows()
{
    const QString inner = m_dir.filePath(QStringLiteral("src/clipvault"));
    QProcess shell;
    shell.setWorkingDirectory(inner);
    shell.start(QStringLiteral("sleep"), { QStringLiteral("30") });
    QVERIFY(shell.waitForStarted());
    const qint64 self = QCoreApplication::applicationPid();

    WindowInfo terminal { QStringLiteral("kitty"), QStringLiteral("kitty"), QStringLiteral("user@host"), self,
        1 };
    QTRY_COMPARE(resolveSourcePath(terminal), inner);
    terminal.title = QStringLiteral("main.cpp - VIM");
    QCOMPARE(resolveSourcePath(terminal), m_dir.filePath(QStringLiteral("src/clipvault/main.cpp")));

    const WindowInfo editor { QStringLiteral("gedit"), QStringLiteral("Gedit"),
        QStringLiteral("app.cpp (~/src/clipvault/src) - gedit"), 0, 1 };
    QCOMPARE(resolveSourcePath(editor), m_dir.filePath(QStringLiteral("src/clipvault/src/app.cpp")));

    const WindowInfo browser { QStringLiteral("firefox"), QStringLiteral("firefox"),
        QStringLiteral("main.cpp (~/src/clipvault) - VIM"), self, 1 };
    QCOMPARE(resolveSourcePath(browser), QString());

    shell.kill();
    shell.waitForFinished();
}

void TestSourceLocation::openableLocationRules()
{
    const QString file = m_dir.filePath(QStringLiteral("src/clipvault/main.cpp"));
    QCOMPARE(openableLocation(file), file);
    QCOMPARE(openableLocation(m_dir.path()), QDir(m_dir.path()).canonicalPath());

    const QString script = makeFile(QStringLiteral("src/run.sh"));
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QCOMPARE(openableLocation(script), m_dir.filePath(QStringLiteral("src")));
    const QString launcher = makeFile(QStringLiteral("src/evil.desktop"));
    QCOMPARE(openableLocation(launcher), m_dir.filePath(QStringLiteral("src")));

    QCOMPARE(openableLocation(QString()), QString());
    QCOMPARE(openableLocation(QStringLiteral("relative/main.cpp")), QString());
    QCOMPARE(openableLocation(QStringLiteral("https://example.org")), QString());
    QCOMPARE(openableLocation(QStringLiteral("file:///etc/passwd")), QString());
    QCOMPARE(openableLocation(m_dir.filePath(QStringLiteral("missing"))), QString());
    QCOMPARE(openableLocation(QStringLiteral("/dev/null")), QString());
    QVERIFY(!openSourceLocation(QStringLiteral("https://example.org")));
}

QTEST_GUILESS_MAIN(TestSourceLocation)
#include "test_source_location.moc"
