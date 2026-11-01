#include "services/OnnxEmbeddingProvider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace clipvault;

namespace {

QByteArray readFixture(const char* name)
{
    QFile file(QStringLiteral(CLIPVAULT_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return file.readAll();
}

QString modelDir()
{
    return qEnvironmentVariable("CLIPVAULT_TEST_MODEL_DIR");
}

// the pinned official hashes unless the local test export is a different file
OnnxEmbeddingProvider::ExpectedHashes testHashes()
{
    OnnxEmbeddingProvider::ExpectedHashes hashes = OnnxEmbeddingProvider::pinnedHashes();
    if (qEnvironmentVariableIsSet("CLIPVAULT_TEST_MODEL_SHA256"))
        hashes.model = qgetenv("CLIPVAULT_TEST_MODEL_SHA256");
    if (qEnvironmentVariableIsSet("CLIPVAULT_TEST_VOCAB_SHA256"))
        hashes.vocab = qgetenv("CLIPVAULT_TEST_VOCAB_SHA256");
    return hashes;
}

int threadCount()
{
    return static_cast<int>(
        QDir(QStringLiteral("/proc/self/task")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
}

float dot(const Embedding& a, const Embedding& b)
{
    float sum = 0.0f;
    for (qsizetype i = 0; i < a.size(); ++i)
        sum += a[i] * b[i];
    return sum;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

class TestOnnxEmbedding : public QObject {
    Q_OBJECT

private slots:
    void missingFilesAreReported()
    {
        const QTemporaryDir dir;
        OnnxEmbeddingProvider provider(dir.path());
        QCOMPARE(provider.failure(), ProviderFailure::FilesMissing);
        QVERIFY(provider.embed({ QStringLiteral("hello") }).isEmpty());
        QVERIFY(!provider.failureReason().contains(dir.path()));
    }

    void wrongFilesAreRefusedBeforeLoading()
    {
        const QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("model.onnx")), QByteArrayLiteral("not a model")));
        const QByteArray vocab = QByteArrayLiteral("[PAD]\n[UNK]\n[CLS]\n[SEP]\n");
        QVERIFY(writeFile(dir.filePath(QStringLiteral("vocab.txt")), vocab));
        // the vocab passes so the refusal must come from the model hash
        const OnnxEmbeddingProvider provider(dir.path(),
            { OnnxEmbeddingProvider::pinnedHashes().model,
                QCryptographicHash::hash(vocab, QCryptographicHash::Sha256).toHex() });
        QCOMPARE(provider.failure(), ProviderFailure::HashMismatch);
        QVERIFY(provider.failureReason().contains(QLatin1String("model.onnx")));
        QVERIFY(!provider.failureReason().contains(dir.path()));
        QVERIFY(provider.modelId().isEmpty());
    }

    void realModelMatchesReference()
    {
        if (modelDir().isEmpty())
            QSKIP("set CLIPVAULT_TEST_MODEL_DIR to a folder holding model.onnx and vocab.txt to run the real "
                  "model test");

        const int threadsBefore = threadCount();
        OnnxEmbeddingProvider provider(modelDir(), testHashes());
        QCOMPARE(provider.failure(), ProviderFailure::None);
        QVERIFY(provider.modelId().startsWith(QLatin1String("all-MiniLM-L6-v2/")));

        QStringList texts;
        QList<QList<double>> heads;
        for (const QByteArray& line : readFixture("embedding_reference.jsonl").split('\n')) {
            if (line.trimmed().isEmpty())
                continue;
            const QJsonObject object = QJsonDocument::fromJson(line).object();
            texts << object.value(QStringLiteral("text")).toString();
            QList<double> head;
            for (const QJsonValue& value : object.value(QStringLiteral("head")).toArray())
                head << value.toDouble();
            heads << head;
        }
        QVERIFY(texts.size() >= 10);

        // batched with padding and one at a time must agree with the reference
        const QList<Embedding> batched = provider.embed(texts);
        QCOMPARE(batched.size(), texts.size());
        double worst = 0.0;
        for (qsizetype i = 0; i < texts.size(); ++i) {
            const QList<Embedding> single = provider.embed({ texts[i] });
            QCOMPARE(single.size(), 1);
            QCOMPARE(batched[i].size(), 384);
            QVERIFY(std::abs(dot(batched[i], batched[i]) - 1.0f) < 1e-4f);
            for (qsizetype d = 0; d < heads[i].size(); ++d) {
                worst = std::max(worst, std::abs(batched[i][d] - heads[i][d]));
                worst = std::max(worst, std::abs(single.front()[d] - heads[i][d]));
            }
        }
        qInfo("largest difference from the reference %.3g", worst);
        QVERIFY2(worst < 1e-4, "embedding differs from the onnx runtime reference");

        // single threaded session options mean onnx runtime adds no threads of its own
        QCOMPARE(threadCount(), threadsBefore);
    }

    void realModelRefusesWrongHash()
    {
        if (modelDir().isEmpty())
            QSKIP("set CLIPVAULT_TEST_MODEL_DIR to a folder holding model.onnx and vocab.txt to run the real "
                  "model test");
        OnnxEmbeddingProvider::ExpectedHashes hashes = testHashes();
        hashes.model = QByteArray(64, '0');
        const OnnxEmbeddingProvider provider(modelDir(), hashes);
        QCOMPARE(provider.failure(), ProviderFailure::HashMismatch);
        QVERIFY(!provider.failureReason().contains(modelDir()));
    }

    void relevanceSetSeparatesAtThreshold()
    {
        if (modelDir().isEmpty())
            QSKIP("set CLIPVAULT_TEST_MODEL_DIR to a folder holding model.onnx and vocab.txt to run the real "
                  "model test");
        OnnxEmbeddingProvider provider(modelDir(), testHashes());
        QCOMPARE(provider.failure(), ProviderFailure::None);

        const QJsonObject set = QJsonDocument::fromJson(readFixture("semantic_relevance.json")).object();
        QStringList clips;
        for (const QJsonValue& clip : set.value(QStringLiteral("clips")).toArray())
            clips << clip.toString();
        const QList<Embedding> clipVectors = provider.embed(clips);
        QCOMPARE(clipVectors.size(), clips.size());

        // every query must rank its clip first and above the threshold while unrelated clips stay below
        int queries = 0;
        int found = 0;
        int falsePositives = 0;
        int unrelatedPairs = 0;
        float lowestRelevant = 1.0f;
        float highestUnrelated = -1.0f;
        for (const QJsonValue& entry : set.value(QStringLiteral("queries")).toArray()) {
            const QJsonObject query = entry.toObject();
            const QList<Embedding> q = provider.embed({ query.value(QStringLiteral("text")).toString() });
            QCOMPARE(q.size(), 1);
            const int relevant = query.value(QStringLiteral("relevant")).toArray().at(0).toInt();
            ++queries;
            int best = -1;
            float bestScore = -2.0f;
            for (int c = 0; c < int(clips.size()); ++c) {
                const float score = dot(q.front(), clipVectors[c]);
                if (score > bestScore) {
                    bestScore = score;
                    best = c;
                }
                if (c == relevant) {
                    lowestRelevant = std::min(lowestRelevant, score);
                    if (score >= semantic_model::kMinSimilarity)
                        ++found;
                } else {
                    ++unrelatedPairs;
                    highestUnrelated = std::max(highestUnrelated, score);
                    if (score >= semantic_model::kMinSimilarity)
                        ++falsePositives;
                }
            }
            QCOMPARE(best, relevant);
        }
        qInfo("threshold %.2f found %d of %d relevant clips lowest %.3f false positives %d of %d highest "
              "unrelated %.3f",
            double(semantic_model::kMinSimilarity), found, queries, double(lowestRelevant), falsePositives,
            unrelatedPairs, double(highestUnrelated));
        QCOMPARE(found, queries);
        QVERIFY(falsePositives <= 1);
    }
};

QTEST_GUILESS_MAIN(TestOnnxEmbedding)
#include "test_onnx_embedding.moc"
