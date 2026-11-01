// measures the semantic numbers quoted in docs semantic search md and needs a model folder

#include "services/HistoryService.h"
#include "services/OnnxEmbeddingProvider.h"
#include "services/SemanticIndex.h"
#include "services/SettingsService.h"
#include "storage/ClipboardRepository.h"
#include "storage/Database.h"
#include "storage/EmbeddingRepository.h"
#include "storage/SettingsRepository.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTextStream>

using namespace clipvault;

namespace {

long rssKb()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    for (;;) {
        const QByteArray line = status.readLine();
        if (line.isEmpty())
            break;
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').front().toLong();
    }
    return -1;
}

QString sampleContent(int index)
{
    switch (index % 6) {
    case 0: return QStringLiteral("https://github.com/example/project/pull/%1").arg(index);
    case 1: return QStringLiteral("SELECT * FROM employees WHERE id = %1;").arg(index);
    case 2: return QStringLiteral("Meeting %1 moved to Thursday afternoon in the small room").arg(index);
    case 3: return QStringLiteral("kubectl logs deployment/api-%1 --since=1h | grep error").arg(index);
    case 4:
        return QStringLiteral("Remember to call the plumber about the leaking kitchen tap, ticket %1")
            .arg(index);
    default:
        return QStringLiteral("Paragraph %1. ").arg(index)
            + QStringLiteral(
                "The quarterly report shows steady growth in every region while costs stayed flat. ")
                  .repeated(8);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.info=false"));
    QTextStream out(stdout);
    if (argc < 4) {
        out << "usage: clipvault_semantic_bench model-dir model-sha256 vocab-sha256 [clips]\n";
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const OnnxEmbeddingProvider::ExpectedHashes hashes { QByteArray(argv[2]), QByteArray(argv[3]) };
    const int total = argc > 4 ? QString::fromLatin1(argv[4]).toInt() : 1000;

    QTemporaryDir tmp;
    Database database(tmp.filePath(QStringLiteral("bench.db")), QStringLiteral("semantic-bench"));
    QString error;
    if (!database.open(&error)) {
        out << "database open failed\n";
        return 1;
    }
    ClipboardRepository repository(database.handle());
    SettingsRepository settingsRepository(database.handle());
    SettingsService settings(settingsRepository);
    settings.setMaxItems(100000);
    settings.setRetentionSeconds(0);
    HistoryService history(repository, settings);
    for (int i = 0; i < total; ++i)
        history.add(sampleContent(i));

    QElapsedTimer timer;
    {
        // direct provider timings on this thread
        timer.start();
        OnnxEmbeddingProvider provider(dir, hashes);
        if (provider.failure() != ProviderFailure::None) {
            out << "model refused\n";
            return 1;
        }
        out << "verify and load model    " << QString::number(timer.nsecsElapsed() / 1e6, 'f', 1) << " ms\n";
        constexpr int kSingles = 100;
        (void)provider.embed({ sampleContent(0) });
        timer.restart();
        for (int i = 0; i < kSingles; ++i)
            (void)provider.embed({ sampleContent(i) });
        out << "embed one clip, mean     " << QString::number(timer.nsecsElapsed() / 1e6 / kSingles, 'f', 2)
            << " ms\n";
        QStringList batch;
        for (int i = 0; i < SemanticIndex::kBatchSize; ++i)
            batch << sampleContent(i);
        timer.restart();
        for (int i = 0; i < 20; ++i)
            (void)provider.embed(batch);
        out << "embed batch of " << SemanticIndex::kBatchSize << ", mean "
            << QString::number(timer.nsecsElapsed() / 1e6 / 20, 'f', 2) << " ms\n";
    }

    EmbeddingRepository embeddings(database.handle());
    const long rssBefore = rssKb();
    SemanticIndex index(embeddings, repository);
    QEventLoop loop;
    QObject::connect(&index, &SemanticIndex::backfillIdle, &loop, &QEventLoop::quit);
    timer.restart();
    index.start([dir, hashes](ProviderFailure* failure) -> std::unique_ptr<EmbeddingProvider> {
        auto provider = std::make_unique<OnnxEmbeddingProvider>(dir, hashes);
        if (provider->failure() != ProviderFailure::None) {
            *failure = provider->failure();
            return nullptr;
        }
        return provider;
    });
    loop.exec();
    const double backfillMs = timer.nsecsElapsed() / 1e6;
    out << "backfill " << index.indexedCount() << " clips      "
        << QString::number(backfillMs / 1000.0, 'f', 2) << " s including model load ("
        << QString::number(index.indexedCount() / (backfillMs / 1000.0), 'f', 0) << " clips/s)\n";
    out << "rss delta after backfill " << (rssKb() - rssBefore) << " kB\n";

    const QStringList queries = { QStringLiteral("rescheduled meeting"), QStringLiteral("fix the sink"),
        QStringLiteral("database query"), QStringLiteral("container logs"),
        QStringLiteral("financial results") };
    QObject::connect(&index, &SemanticIndex::queryReady, &loop, &QEventLoop::quit);
    double queryMs = 0.0;
    int hits = 0;
    constexpr int kRounds = 4;
    for (int round = 0; round < kRounds; ++round) {
        for (const QString& query : queries) {
            QList<qint64> found;
            const auto connection = QObject::connect(&index, &SemanticIndex::queryReady,
                [&found](const QString&, const QList<qint64>& ids) { found = ids; });
            timer.restart();
            index.query(query);
            loop.exec();
            queryMs += timer.nsecsElapsed() / 1e6;
            hits += static_cast<int>(found.size());
            QObject::disconnect(connection);
        }
    }
    const int count = kRounds * static_cast<int>(queries.size());
    out << "query to ranked ids, mean " << QString::number(queryMs / count, 'f', 2) << " ms over "
        << index.indexedCount() << " vectors (" << hits / count << " hits each)\n";
    out << "resident memory          " << rssKb() << " kB\n";
    index.stop();
    database.close();
    return 0;
}
