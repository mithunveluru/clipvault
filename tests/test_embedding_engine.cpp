#include "FakeEmbeddingProvider.h"
#include "services/EmbeddingEngine.h"

#include <QSignalSpy>
#include <QtTest>

using namespace clipvault;
using clipvault::test::FakeEmbeddingProbe;
using clipvault::test::FakeEmbeddingProvider;

namespace {

EmbeddingEngine::Factory fakeFactory(std::shared_ptr<FakeEmbeddingProbe> probe, int delayMs = 0)
{
    return [probe = std::move(probe), delayMs](ProviderFailure*) -> std::unique_ptr<EmbeddingProvider> {
        return std::make_unique<FakeEmbeddingProvider>(probe, delayMs);
    };
}

} // namespace

class TestEmbeddingEngine : public QObject {
    Q_OBJECT

private slots:
    void embedsOnTheWorkerAndAnswersOnTheMainThread()
    {
        auto probe = std::make_shared<FakeEmbeddingProbe>();
        {
            EmbeddingEngine engine;
            QSignalSpy loaded(&engine, &EmbeddingEngine::loaded);
            engine.load(fakeFactory(probe));
            QVERIFY(loaded.wait());
            QCOMPARE(loaded.at(0).at(0).toInt(), int(ProviderFailure::None));
            QCOMPARE(loaded.at(0).at(1).toString(), QStringLiteral("fake-1"));
            QCOMPARE(loaded.at(0).at(2).toInt(), FakeEmbeddingProvider::kDim);

            QThread* answeredOn = nullptr;
            QList<Embedding> vectors;
            connect(&engine, &EmbeddingEngine::embedded, this, [&](quint64, const QList<Embedding>& result) {
                answeredOn = QThread::currentThread();
                vectors = result;
            });
            QSignalSpy embedded(&engine, &EmbeddingEngine::embedded);
            const quint64 job = engine.embed({ QStringLiteral("one"), QStringLiteral("two") });
            QVERIFY(embedded.wait());
            QCOMPARE(embedded.at(0).at(0).toULongLong(), job);
            QCOMPARE(vectors.size(), 2);
            QCOMPARE(vectors.front().size(), FakeEmbeddingProvider::kDim);
            QCOMPARE(answeredOn, QThread::currentThread());

            const QMutexLocker lock(&probe->mutex);
            QCOMPARE(probe->threads.size(), 1);
            QVERIFY(probe->threads.front() != QThread::currentThreadId());
        }
        QCOMPARE(probe->destroyedCount(), 1);
    }

    void failedLoadIsReported()
    {
        EmbeddingEngine engine;
        QSignalSpy loaded(&engine, &EmbeddingEngine::loaded);
        engine.load([](ProviderFailure* failure) -> std::unique_ptr<EmbeddingProvider> {
            *failure = ProviderFailure::HashMismatch;
            return nullptr;
        });
        QVERIFY(loaded.wait());
        QCOMPARE(loaded.at(0).at(0).toInt(), int(ProviderFailure::HashMismatch));

        // a job without a provider answers with no vectors rather than hanging
        QSignalSpy embedded(&engine, &EmbeddingEngine::embedded);
        engine.embed({ QStringLiteral("text") });
        QVERIFY(embedded.wait());
        QVERIFY(embedded.at(0).at(1).value<QList<Embedding>>().isEmpty());
    }

    void destructionWithQueuedJobsIsSafe()
    {
        auto probe = std::make_shared<FakeEmbeddingProbe>();
        int answers = 0;
        {
            auto engine = std::make_unique<EmbeddingEngine>();
            connect(engine.get(), &EmbeddingEngine::embedded, this, [&answers] { ++answers; });
            engine->load(fakeFactory(probe, 20));
            for (int i = 0; i < 50; ++i)
                engine->embed({ QStringLiteral("job %1").arg(i) });
            QTRY_VERIFY(!probe->texts().isEmpty());
            engine.reset();
        }
        // nothing queued for the dead engine may still arrive
        const int seen = answers;
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCOMPARE(answers, seen);
        QVERIFY(probe->texts().size() < 50);
        QCOMPARE(probe->destroyedCount(), 1);
    }

    void destructionBeforeLoadRunsIsSafe()
    {
        auto probe = std::make_shared<FakeEmbeddingProbe>();
        {
            EmbeddingEngine engine;
            engine.load(fakeFactory(probe));
            engine.embed({ QStringLiteral("never") });
        }
        QVERIFY(probe->destroyedCount() <= 1);
    }
};

QTEST_GUILESS_MAIN(TestEmbeddingEngine)
#include "test_embedding_engine.moc"
