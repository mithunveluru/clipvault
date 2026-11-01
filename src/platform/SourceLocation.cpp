#include "SourceLocation.h"

#include "../core/Logging.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUrl>

#include <filesystem>
#include <utility>

namespace clipvault::platform {
namespace {

// deep enough for terminal then shell then a tool it started
constexpr int kMaxProcessDepth = 16;

bool matchesAny(const WindowInfo& window, std::initializer_list<QLatin1String> names)
{
    const QString app = window.app.toLower();
    const QString cls = window.windowClass.toLower();
    for (const QLatin1String name : names) {
        if (app == name || cls == name)
            return true;
    }
    return false;
}

QString expandHome(const QString& path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

bool looksLikeDirectory(const QString& hint)
{
    return hint.startsWith(QLatin1Char('/')) || hint == QLatin1String("~")
        || hint.startsWith(QLatin1String("~/"));
}

struct Segment {
    QString name;
    QString hint;
};

// splits a trailing parenthesised or bracketed hint off a title segment
Segment splitSegment(QString text)
{
    static const QRegularExpression dirty(QStringLiteral("^[\\x{25CF}\\x{2022}*]+\\s*"));
    static const QRegularExpression trailingHint(QStringLiteral("^(.*?)\\s*[(\\[]([^()\\[\\]]*)[)\\]]$"));
    static const QRegularExpression modified(QStringLiteral("\\s+[+*]$"));
    text.remove(dirty);
    Segment segment;
    const QRegularExpressionMatch match = trailingHint.match(text);
    if (match.hasMatch()) {
        segment.name = match.captured(1);
        segment.hint = match.captured(2).trimmed();
    } else {
        segment.name = std::move(text);
    }
    segment.name.remove(modified);
    segment.name = segment.name.trimmed();
    return segment;
}

QList<qint64> childrenOf(qint64 pid)
{
    QList<qint64> children;
    const QDir tasks(QStringLiteral("/proc/%1/task").arg(pid));
    for (const QString& task : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(tasks.filePath(task + QStringLiteral("/children")));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        for (const QByteArray& token : file.readAll().split(' ')) {
            bool ok = false;
            const qint64 child = token.trimmed().toLongLong(&ok);
            if (ok && child > 0)
                children << child;
        }
    }
    return children;
}

// field 22 of proc stat counted after the command name which may itself hold spaces
qint64 startTime(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray stat = file.readAll();
    const qsizetype close = stat.lastIndexOf(')');
    if (close < 0)
        return -1;
    const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
    // the slice starts at field 3 so field 22 sits at index 19
    constexpr qsizetype kStartTimeIndex = 19;
    if (fields.size() <= kStartTimeIndex)
        return -1;
    return fields.at(kStartTimeIndex).toLongLong();
}

qint64 newestChild(qint64 pid)
{
    qint64 newest = 0;
    qint64 newestStart = -1;
    for (const qint64 child : childrenOf(pid)) {
        const qint64 start = startTime(child);
        if (start > newestStart || (start == newestStart && child > newest)) {
            newest = child;
            newestStart = start;
        }
    }
    return newest;
}

bool isKernelPath(const QString& path)
{
    for (const QLatin1String root :
        { QLatin1String("/proc"), QLatin1String("/sys"), QLatin1String("/dev") }) {
        if (path == root || path.startsWith(root + QLatin1Char('/')))
            return true;
    }
    return false;
}

// only plain text goes straight to its handler and anything else shows its folder
bool opensAsText(const QFileInfo& file)
{
    static const QStringList blocked { QStringLiteral("desktop"), QStringLiteral("jar"),
        QStringLiteral("flatpakref"), QStringLiteral("flatpakrepo"), QStringLiteral("exe"),
        QStringLiteral("msi") };
    if (file.isExecutable() || blocked.contains(file.suffix(), Qt::CaseInsensitive))
        return false;
    const QMimeType type = QMimeDatabase().mimeTypeForFile(file, QMimeDatabase::MatchDefault);
    // launchers and scripts count as text yet their handler may run them
    if (type.inherits(QStringLiteral("application/x-desktop"))
        || type.inherits(QStringLiteral("application/x-shellscript")))
        return false;
    return type.inherits(QStringLiteral("text/plain"));
}

} // namespace

bool isTerminal(const WindowInfo& window)
{
    return matchesAny(window,
        { QLatin1String("gnome-terminal"), QLatin1String("gnome-terminal-server"),
            QLatin1String("org.gnome.terminal"), QLatin1String("kgx"), QLatin1String("org.gnome.console"),
            QLatin1String("konsole"), QLatin1String("org.kde.konsole"), QLatin1String("alacritty"),
            QLatin1String("kitty"), QLatin1String("foot"), QLatin1String("footclient"),
            QLatin1String("wezterm"), QLatin1String("org.wezfurlong.wezterm"), QLatin1String("xterm"),
            QLatin1String("uxterm"), QLatin1String("tilix"), QLatin1String("com.gexperts.tilix"),
            QLatin1String("terminator") });
}

EditorKind editorKind(const WindowInfo& window)
{
    if (window.app.contains(QLatin1String("jetbrains"), Qt::CaseInsensitive)
        || window.windowClass.contains(QLatin1String("jetbrains"), Qt::CaseInsensitive))
        return EditorKind::ProjectFirst;
    if (matchesAny(window,
            { QLatin1String("code"), QLatin1String("code-oss"), QLatin1String("code - oss"),
                QLatin1String("codium"), QLatin1String("vscodium"), QLatin1String("com.visualstudio.code"),
                QLatin1String("com.vscodium.codium"), QLatin1String("gedit"),
                QLatin1String("org.gnome.gedit"), QLatin1String("gnome-text-editor"),
                QLatin1String("org.gnome.texteditor"), QLatin1String("kate"), QLatin1String("org.kde.kate"),
                QLatin1String("sublime_text"), QLatin1String("sublime-text"),
                QLatin1String("com.sublimetext.three"), QLatin1String("gvim"), QLatin1String("nvim-qt"),
                QLatin1String("neovide") }))
        return EditorKind::FileFirst;
    return EditorKind::None;
}

bool isVimTitle(const QString& title)
{
    static const QRegularExpression vim(
        QStringLiteral("\\s-\\s(n|g)?vim$"), QRegularExpression::CaseInsensitiveOption);
    return vim.match(title.trimmed()).hasMatch();
}

QString pathFromEditorTitle(const QString& title, EditorKind kind, const QString& baseDir)
{
    if (kind == EditorKind::None || title.contains(QLatin1String("://")))
        return { };
    static const QRegularExpression separator(QStringLiteral("\\s+[-\\x{2013}\\x{2014}]\\s+"));
    const QStringList parts = title.trimmed().split(separator, Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return { };

    Segment file;
    Segment project;
    if (kind == EditorKind::ProjectFirst) {
        if (parts.size() < 2)
            return { };
        project = splitSegment(parts.at(0));
        file = splitSegment(parts.at(1));
    } else {
        file = splitSegment(parts.at(0));
        if (parts.size() >= 3)
            project = splitSegment(parts.at(1));
    }

    // a hint like ssh or wsl or dev container means the file is not on this machine
    for (const Segment* segment : { &file, &project }) {
        if (!looksLikeDirectory(segment->hint) && segment->hint.contains(QLatin1Char(':')))
            return { };
    }

    QString directory;
    if (looksLikeDirectory(file.hint))
        directory = expandHome(file.hint);
    else if (looksLikeDirectory(project.hint))
        directory = expandHome(project.hint);
    QString projectName = project.name;
    if (projectName.isEmpty() && !file.hint.isEmpty() && !looksLikeDirectory(file.hint))
        projectName = file.hint;

    const QString name = expandHome(file.name);
    if (name.isEmpty() || name.startsWith(QStringLiteral("…")) || name.startsWith(QLatin1String("...")))
        return { };

    QString candidate;
    if (QDir::isAbsolutePath(name))
        candidate = name;
    else if (!directory.isEmpty())
        candidate = directory + QLatin1Char('/') + name;
    else if (!baseDir.isEmpty() && (projectName.isEmpty() || QFileInfo(baseDir).fileName() == projectName))
        candidate = baseDir + QLatin1Char('/') + name;
    if (candidate.isEmpty() || !QDir::isAbsolutePath(candidate))
        return { };
    candidate = QDir::cleanPath(candidate);
    return QFileInfo(candidate).isFile() ? candidate : QString();
}

QString processWorkingDirectory(qint64 pid)
{
    if (pid <= 0)
        return { };
    std::error_code error;
    const std::filesystem::path cwd
        = std::filesystem::read_symlink(QStringLiteral("/proc/%1/cwd").arg(pid).toStdString(), error);
    if (error)
        return { };
    const QString path = QString::fromStdString(cwd.string());
    // a removed directory reads back with a deleted suffix and fails this check
    return QFileInfo(path).isDir() ? path : QString();
}

QString foregroundWorkingDirectory(qint64 pid)
{
    if (pid <= 0)
        return { };
    qint64 current = pid;
    for (int depth = 0; depth < kMaxProcessDepth; ++depth) {
        const qint64 child = newestChild(current);
        if (child == 0)
            break;
        current = child;
    }
    // the terminal process itself says nothing about where the user is
    return current == pid ? QString() : processWorkingDirectory(current);
}

QString resolveSourcePath(const WindowInfo& window)
{
    if (isTerminal(window)) {
        QString cwd = foregroundWorkingDirectory(window.pid);
        if (isVimTitle(window.title)) {
            const QString file = pathFromEditorTitle(window.title, EditorKind::FileFirst, cwd);
            if (!file.isEmpty())
                return file;
        }
        return cwd;
    }
    const EditorKind kind = editorKind(window);
    if (kind == EditorKind::None)
        return { };
    return pathFromEditorTitle(window.title, kind, processWorkingDirectory(window.pid));
}

QString openableLocation(const QString& location)
{
    if (location.isEmpty() || !QDir::isAbsolutePath(location))
        return { };
    const QFileInfo given(location);
    if (given.isSymLink() && isKernelPath(QDir::cleanPath(given.symLinkTarget())))
        return { };
    QString canonical = given.canonicalFilePath();
    if (canonical.isEmpty() || isKernelPath(canonical))
        return { };
    const QFileInfo target(canonical);
    if (target.isDir())
        return canonical;
    if (!target.isFile())
        return { };
    return opensAsText(target) ? canonical : target.absolutePath();
}

bool openSourceLocation(const QString& location)
{
    const QString target = openableLocation(location);
    if (target.isEmpty()) {
        qCInfo(lcFocus) << "refused to open a source location of length" << location.size();
        return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

} // namespace clipvault::platform
