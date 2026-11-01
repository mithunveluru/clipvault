#include "Logging.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QTextStream>

#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcApp, "clipvault.app")
Q_LOGGING_CATEGORY(lcDb, "clipvault.db")
Q_LOGGING_CATEGORY(lcClip, "clipvault.clipboard")
Q_LOGGING_CATEGORY(lcHotkey, "clipvault.hotkey")
Q_LOGGING_CATEGORY(lcFocus, "clipvault.focus")
Q_LOGGING_CATEGORY(lcSecret, "clipvault.secret")

namespace clipvault {

QString redact(const QString& content)
{
    if (content.isEmpty())
        return QStringLiteral("<empty>");

    // Per run salt blocks guessing short secrets
    static const QByteArray salt = [] {
        QByteArray bytes(32, Qt::Uninitialized);
        QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
        return bytes;
    }();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(salt);
    hash.addData(content.toUtf8());
    return QStringLiteral("<%1 chars, ref:%2>")
        .arg(content.size())
        .arg(QString::fromLatin1(hash.result().toHex().left(8)));
}

namespace {

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

QMutex g_sinkMutex;
QFile* g_logFile = nullptr;

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString line = QStringLiteral("%1 %2 %3  %4\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                  QString::fromLatin1(levelName(type)),
                                  QString::fromLatin1(context.category ? context.category : "default"),
                                  message);

    // Serialised because worker threads also log
    QMutexLocker locker(&g_sinkMutex);

    QTextStream err(stderr);
    err << line;
    err.flush();

    if (g_logFile) {
        g_logFile->write(line.toUtf8());
        g_logFile->flush();
    }
}

} // namespace

void installLogHandler()
{
    // Background utility stays quiet unless debugging is requested
    QLoggingCategory::setFilterRules(qEnvironmentVariableIsSet("CLIPVAULT_DEBUG")
                                         ? QStringLiteral("clipvault.*.debug=true")
                                         : QStringLiteral("clipvault.*.debug=false"));
    qInstallMessageHandler(handler);
}

void enableLogFile(const QString& path)
{
    constexpr qint64 kMaxBytes = 1024 * 1024;

    QMutexLocker locker(&g_sinkMutex);
    if (g_logFile)
        return;

    // Single rollover keeps the on disk footprint bounded
    if (QFileInfo(path).size() > kMaxBytes) {
        const QString previous = path + QStringLiteral(".1");
        QFile::remove(previous);
        QFile::rename(path, previous);
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning("could not open the log file");
        return;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    g_logFile = file.release();
}

} // namespace clipvault
