#include "FakeEmbeddingProvider.h"
#include "TestSupport.h"
#include "services/SemanticIndex.h"
#include "storage/EmbeddingRepository.h"

#include <QSignalSpy>
#include <QtTest>

using namespace clipvault;
using clipvault::test::FakeEmbeddingProbe;
using clipvault::test::FakeEmbeddingProvider;
using clipvault::test::Fixture;

namespace {

// synthetic token in the shape of a github token
const QString kSecret = QStringLiteral("gh"
                                       "p_SyntheticFakeToken0000000000000000003");

const QHash<QString, QString> kSynonyms { { QStringLiteral("automobile"), QStringLiteral("car") },
    { QStringLiteral("vehicle"), QStringLiteral("car") } };

SemanticIndex::Factory fakeFactory(
    std::shared_ptr<FakeEmbeddingProbe> probe, QString model = QStringLiteral("fake-1"))
{
    return [probe = std::move(probe), model](ProviderFailure*) -> std::unique_ptr<EmbeddingProvider> {
        return std::make_unique<FakeEmbeddingProvider>(probe, 0, kSynonyms, model);
    };
}

// one stack with an index wired to history changes like the controller does
struct Harness {
    Fixture fixture;
    EmbeddingRepository embeddings { fixture.database().handle() };
    SemanticIndex index { embeddings, fixture.repository() };
    std::shared_ptr<FakeEmbeddingProbe> probe = std::make_shared<FakeEmbeddingProbe>();

    Harness()
    {
        QObject::connect(
            &fixture.history(), &HistoryService::historyChanged, &index, &SemanticIndex::scheduleSync);
    }

    bool startAndSettle(const QString& model = QStringLiteral("fake-1"))
    {
        QSignalSpy idle(&index, &SemanticIndex::backfillIdle);
        index.start(fakeFactory(probe, model));
        return idle.wait(5000) && index.isReady();
    }

    bool settle()
    {
        QSignalSpy idle(&index, &SemanticIndex::backfillIdle);
        return idle.wait(5000);
    }
};

} // namespace

class TestSemanticIndex : public QObject {
    Q_OBJECT

private slots:
    void startsOffWithNoThread()
    {
        Harness h;
        QCOMPARE(h.index.status(), SemanticIndex::Status::Off);
        h.index.query(QStringLiteral("anything"));
        h.index.scheduleSync();
        QTest::qWait(100);
        QVERIFY(h.probe->texts().isEmpty());
    }

    void backfillsExistingClipsButNeverSecrets()
    {
        Harness h;
        h.fixture.history().add(QStringLiteral("first plain clip"));
        h.fixture.history().add(QStringLiteral("second plain clip"));
        // an in memory secret with a negative id
        const auto ephemeral = h.fixture.history().add(kSecret);
        QVERIFY(ephemeral.id < 0);
        // a pinned secret row on disk
        const QString pinnedSecret = kSecret + QStringLiteral("pinned");
        const auto toPin = h.fixture.history().add(pinnedSecret);
        QVERIFY(toPin.id < 0);
        QVERIFY(h.fixture.history().setPinned(toPin.id, true));
        QVERIFY(
            !h.fixture.repository().findByHash(HistoryService::hashOf(pinnedSecret))->secretKind.isEmpty());

        QSignalSpy status(&h.index, &SemanticIndex::statusChanged);
        QVERIFY(h.startAndSettle());
        QCOMPARE(status.at(0).at(0).value<SemanticIndex::Status>(), SemanticIndex::Status::Verifying);
        QCOMPARE(h.index.indexedCount(), 2);
        for (const QString& text : h.probe->texts())
            QVERIFY(!text.contains(QLatin1String("SyntheticFakeToken")));
    }

    void newClipsAreEmbeddedAndRemovedOnesDropped()
    {
        Harness h;
        QVERIFY(h.startAndSettle());
        QCOMPARE(h.index.indexedCount(), 0);

        const auto added = h.fixture.history().add(QStringLiteral("a freshly copied note"));
        QVERIFY(h.settle());
        QCOMPARE(h.index.indexedCount(), 1);
        h.fixture.history().add(QStringLiteral("another note"));
        QVERIFY(h.settle());
        QCOMPARE(h.index.indexedCount(), 2);

        QVERIFY(h.fixture.history().remove(added.id));
        QVERIFY(h.settle());
        QCOMPARE(h.index.indexedCount(), 1);

        QVERIFY(h.fixture.history().clearAll(true));
        QVERIFY(h.settle());
        QCOMPARE(h.index.indexedCount(), 0);
        QVERIFY(h.embeddings.itemIds(QStringLiteral("fake-1")).isEmpty());

        // copied secrets stay out even while running
        h.fixture.history().add(kSecret);
        QTest::qWait(150);
        for (const QString& text : h.probe->texts())
            QVERIFY(!text.contains(QLatin1String("SyntheticFakeToken")));
    }

    void rowPinnedAsSecretLaterLosesItsVector()
    {
        Harness h;
        h.fixture.settings().setSecretRadarEnabled(false);
        const auto plain = h.fixture.history().add(kSecret);
        QVERIFY(plain.id > 0);
        QVERIFY(h.startAndSettle());
        QCOMPARE(h.index.indexedCount(), 1);

        ClipboardItem marked = *h.fixture.repository().findById(plain.id);
        marked.secretKind = QStringLiteral("github_token");
        QVERIFY(h.fixture.repository().upsert(marked));
        h.index.scheduleSync();
        QVERIFY(h.settle());
        QCOMPARE(h.index.indexedCount(), 0);
        QVERIFY(h.embeddings.itemIds(QStringLiteral("fake-1")).isEmpty());
    }

    void queryFindsMeaningNotWords()
    {
        Harness h;
        const auto car = h.fixture.history().add(QStringLiteral("red car parked outside"));
        h.fixture.history().add(QStringLiteral("banana bread recipe"));
        QVERIFY(h.startAndSettle());

        QSignalSpy ready(&h.index, &SemanticIndex::queryReady);
        h.index.query(QStringLiteral("automobile"));
        QVERIFY(ready.wait());
        QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("automobile"));
        QCOMPARE(ready.at(0).at(1).value<QList<qint64>>(), QList<qint64> { car.id });

        // only the latest of two quick queries is answered
        ready.clear();
        h.index.query(QStringLiteral("vehicle bread"));
        h.index.query(QStringLiteral("vehicle"));
        QVERIFY(ready.wait());
        QTest::qWait(50);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("vehicle"));
    }

    void rankAppliesThresholdAndOrder()
    {
        Harness h;
        const auto both = h.fixture.history().add(QStringLiteral("car"));
        const auto half = h.fixture.history().add(QStringLiteral("car wash bucket"));
        h.fixture.history().add(QStringLiteral("unrelated words entirely"));
        QVERIFY(h.startAndSettle());
        const FakeEmbeddingProvider fake(nullptr, 0, kSynonyms);
        const QList<SemanticIndex::Hit> hits = h.index.rank(fake.vectorFor(QStringLiteral("automobile")), 10);
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits.at(0).id, both.id);
        QCOMPARE(hits.at(1).id, half.id);
        QVERIFY(hits.at(1).score >= 0.35f);
        QCOMPARE(h.index.rank(fake.vectorFor(QStringLiteral("automobile")), 1).size(), 1);
    }

    void storedVectorsAreReusedAndModelChangeReembeds()
    {
        Harness h;
        h.fixture.history().add(QStringLiteral("kept across restarts"));
        QVERIFY(h.startAndSettle());
        QCOMPARE(h.probe->texts().size(), 1);

        h.index.stop();
        QCOMPARE(h.index.indexedCount(), 0);
        QVERIFY(h.startAndSettle());
        QCOMPARE(h.index.indexedCount(), 1);
        QCOMPARE(h.probe->texts().size(), 1);

        QVERIFY(h.startAndSettle(QStringLiteral("fake-2")));
        QCOMPARE(h.index.indexedCount(), 1);
        QCOMPARE(h.probe->texts().size(), 2);
    }

    void failuresBecomeStatus()
    {
        Harness h;
        QSignalSpy status(&h.index, &SemanticIndex::statusChanged);
        h.index.start([](ProviderFailure* failure) -> std::unique_ptr<EmbeddingProvider> {
            *failure = ProviderFailure::HashMismatch;
            return nullptr;
        });
        QTRY_COMPARE(h.index.status(), SemanticIndex::Status::HashMismatch);
        h.index.query(QStringLiteral("nothing happens"));
        QTest::qWait(50);
        h.index.stop();
        QCOMPARE(h.index.status(), SemanticIndex::Status::Off);
    }

    void stoppingMidBackfillIsSafe()
    {
        Harness h;
        for (int i = 0; i < 40; ++i)
            h.fixture.history().add(QStringLiteral("clip number %1").arg(i));
        h.index.start([probe = h.probe](ProviderFailure*) -> std::unique_ptr<EmbeddingProvider> {
            return std::make_unique<FakeEmbeddingProvider>(probe, 30);
        });
        QTRY_VERIFY(!h.probe->texts().isEmpty());
        h.index.stop();
        QTest::qWait(100);
        QCOMPARE(h.index.indexedCount(), 0);
        QCOMPARE(h.probe->destroyedCount(), 1);
    }

    void compiledOutBuildStaysOff()
    {
        Harness h;
        h.index.configure(true, QString());
        if (SemanticIndex::isCompiledIn())
            QCOMPARE(h.index.status(), SemanticIndex::Status::FilesMissing);
        else
            QCOMPARE(h.index.status(), SemanticIndex::Status::Off);
        h.index.configure(false, QString());
        QCOMPARE(h.index.status(), SemanticIndex::Status::Off);
    }
};

QTEST_GUILESS_MAIN(TestSemanticIndex)
#include "test_semantic_index.moc"
