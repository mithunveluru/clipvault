// Measurement harness for the figures quoted in the README

#include "services/HistoryService.h"
#include "services/SearchService.h"
#include "services/SettingsService.h"
#include "storage/ClipboardRepository.h"
#include "storage/Database.h"
#include "storage/SettingsRepository.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTextStream>

using namespace clipvault;

namespace {

QString rssKb()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("n/a");
    // procfs reports size zero so atEnd is true immediately
    for (;;) {
        const QByteArray raw = status.readLine();
        if (raw.isEmpty())
            break;
        const QString line = QString::fromLatin1(raw);
        if (line.startsWith(QLatin1String("VmRSS:")))
            return line.mid(6).trimmed();
    }
    return QStringLiteral("n/a");
}

QString sampleContent(int index)
{
    switch (index % 4) {
    case 0: return QStringLiteral("https://github.com/example/project/pull/%1").arg(index);
    case 1: return QStringLiteral("SELECT * FROM employees WHERE id = %1;").arg(index);
    case 2: return QStringLiteral("contact.person%1@example.com").arg(index);
    default: return QStringLiteral("A plain note number %1 with some searchable words").arg(index);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    // per insert debug lines skew the timings
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.info=false"));
    const int total = argc > 1 ? QString::fromLatin1(argv[1]).toInt() : 10000;

    QTextStream out(stdout);
    QTemporaryDir dir;
    QElapsedTimer timer;

    timer.start();
    Database database(dir.filePath(QStringLiteral("bench.db")), QStringLiteral("bench"));
    QString error;
    if (!database.open(&error)) {
        out << "database open failed: " << error << '\n';
        return 1;
    }
    const qint64 initNs = timer.nsecsElapsed();

    ClipboardRepository repository(database.handle());
    SettingsRepository settingsRepository(database.handle());
    SettingsService settings(settingsRepository);
    settings.setMaxItems(100000);
    settings.setRetentionSeconds(0);
    HistoryService history(repository, settings);
    SearchService search(repository);

    timer.restart();
    for (int i = 0; i < total; ++i)
        history.add(sampleContent(i));
    const qint64 insertNs = timer.nsecsElapsed();

    out << "entries stored          " << repository.count() << '\n';
    out << "database init           " << QString::number(initNs / 1e6, 'f', 2) << " ms\n";
    out << "insert, mean            " << QString::number(insertNs / 1e6 / total, 'f', 3)
        << " ms/entry\n";

    const QStringList queries = { QStringLiteral(""), QStringLiteral("github"),
                                  QStringLiteral("employees"), QStringLiteral("zzz-no-match"),
                                  QStringLiteral("searchable") };
    for (const QString& query : queries) {
        // Warm then average since one query is below timer resolution
        search.search(query);
        timer.restart();
        constexpr int kRuns = 20;
        int hits = 0;
        for (int i = 0; i < kRuns; ++i)
            hits = static_cast<int>(search.search(query).size());
        const double meanMs = timer.nsecsElapsed() / 1e6 / kRuns;
        out << "search " << (query.isEmpty() ? QStringLiteral("<recent>") : query).leftJustified(16)
            << QString::number(meanMs, 'f', 3) << " ms (" << hits << " rows)\n";
    }

    out << "resident memory         " << rssKb() << '\n';
    database.close();
    return 0;
}
