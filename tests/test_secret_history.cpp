#include "TestSupport.h"

#include <QSignalSpy>
#include <QTest>

using namespace clipvault;
using namespace clipvault::test;
using AddStatus = HistoryService::AddStatus;

namespace {

// synthetic token in the shape of a github token
const QString kSecret = QStringLiteral("gh"
                                       "p_SyntheticFakeToken0000000000000000002");

} // namespace

class TestSecretHistory : public QObject {
    Q_OBJECT

private slots:
    void secretGoesToMemoryNotTheRepository()
    {
        Fixture fixture;
        QVERIFY2(fixture.isOpen(), qPrintable(fixture.openError()));
        QSignalSpy changed(&fixture.history(), &HistoryService::historyChanged);

        const auto outcome = fixture.history().add(kSecret);
        QCOMPARE(outcome.status, AddStatus::Inserted);
        QVERIFY(outcome.id < -1);
        QCOMPARE(fixture.repository().count(), 0);
        QVERIFY(!fixture.repository().findByHash(HistoryService::hashOf(kSecret)));
        QCOMPARE(fixture.history().ephemeral().count(), 1);
        QCOMPARE(fixture.history().count(), 1);
        QCOMPARE(changed.count(), 1);

        const auto held = fixture.history().ephemeral().find(outcome.id);
        QVERIFY(held.has_value());
        QCOMPARE(held->secretKind, QStringLiteral("github_token"));
        QVERIFY(held->contentHash.isEmpty());
    }

    void ordinaryTextStillStored()
    {
        Fixture fixture;
        const auto outcome = fixture.history().add(QStringLiteral("just a note"));
        QCOMPARE(outcome.status, AddStatus::Inserted);
        QVERIFY(outcome.id >= 0);
        QCOMPARE(fixture.history().ephemeral().count(), 0);
    }

    void radarOffStoresSecretsLikeAnythingElse()
    {
        Fixture fixture;
        fixture.settings().setSecretRadarEnabled(false);
        const auto outcome = fixture.history().add(kSecret);
        QCOMPARE(outcome.status, AddStatus::Inserted);
        QVERIFY(outcome.id >= 0);
        const auto stored = fixture.repository().findById(outcome.id);
        QVERIFY(stored.has_value());
        QVERIFY(stored->secretKind.isEmpty());
    }

    void repeatedSecretIsDedupedInMemory()
    {
        Fixture fixture;
        const auto first = fixture.history().add(kSecret);
        const auto second = fixture.history().add(kSecret);
        QCOMPARE(second.status, AddStatus::Refreshed);
        QCOMPARE(second.id, first.id);
        QCOMPARE(fixture.history().ephemeral().count(), 1);
    }

    void ttlComesFromSettings()
    {
        Fixture fixture;
        fixture.settings().setSecretTtlSeconds(120);
        const auto outcome = fixture.history().add(kSecret);
        QCOMPARE(fixture.history().ephemeral().remainingSeconds(outcome.id), 120);
    }

    void pinningPersistsWithKindAndLeavesMemory()
    {
        Fixture fixture;
        const auto outcome = fixture.history().add(kSecret);
        QVERIFY(fixture.history().setPinned(outcome.id, true));

        QCOMPARE(fixture.history().ephemeral().count(), 0);
        QCOMPARE(fixture.repository().count(), 1);
        const auto stored = fixture.repository().findByHash(HistoryService::hashOf(kSecret));
        QVERIFY(stored.has_value());
        QVERIFY(stored->pinned);
        QCOMPARE(stored->secretKind, QStringLiteral("github_token"));
        QCOMPARE(stored->content, kSecret);
        QVERIFY(stored->expiresAt.has_value());
    }

    void copyingAPinnedSecretAgainDoesNotDuplicate()
    {
        Fixture fixture;
        const auto outcome = fixture.history().add(kSecret);
        QVERIFY(fixture.history().setPinned(outcome.id, true));
        const qint64 storedId = fixture.repository().findByHash(HistoryService::hashOf(kSecret))->id;

        const auto again = fixture.history().add(kSecret);
        QCOMPARE(again.status, AddStatus::Refreshed);
        QCOMPARE(again.id, storedId);
        QCOMPARE(fixture.history().ephemeral().count(), 0);
        QCOMPARE(fixture.repository().count(), 1);
    }

    void unpinningMovesBackToMemoryWithFreshTtl()
    {
        Fixture fixture;
        fixture.settings().setSecretTtlSeconds(30);
        const auto outcome = fixture.history().add(kSecret);
        QVERIFY(fixture.history().setPinned(outcome.id, true));
        const qint64 storedId = fixture.repository().findByHash(HistoryService::hashOf(kSecret))->id;

        QVERIFY(fixture.history().setPinned(storedId, false));
        QCOMPARE(fixture.repository().count(), 0);
        QVERIFY(!fixture.repository().findById(storedId));
        const QList<ClipboardItem> held = fixture.history().ephemeral().items();
        QCOMPARE(held.size(), 1);
        QCOMPARE(held.first().content, kSecret);
        QCOMPARE(held.first().secretKind, QStringLiteral("github_token"));
        QCOMPARE(fixture.history().ephemeral().remainingSeconds(held.first().id), 30);
    }

    void unpinningAnOrdinaryEntryIsUnchanged()
    {
        Fixture fixture;
        const auto outcome = fixture.history().add(QStringLiteral("plain pinned note"));
        QVERIFY(fixture.history().setPinned(outcome.id, true));
        QVERIFY(fixture.history().setPinned(outcome.id, false));
        QVERIFY(fixture.repository().findById(outcome.id).has_value());
        QCOMPARE(fixture.history().ephemeral().count(), 0);
    }

    void removeAndClearReachMemory()
    {
        Fixture fixture;
        const auto first = fixture.history().add(kSecret);
        QVERIFY(fixture.history().remove(first.id));
        QCOMPARE(fixture.history().ephemeral().count(), 0);
        QVERIFY(!fixture.history().remove(first.id));

        fixture.history().add(kSecret);
        fixture.history().add(QStringLiteral("plain"));
        QVERIFY(fixture.history().clearAll(false));
        QCOMPARE(fixture.history().count(), 0);
    }

    void pinnedSecretFollowsRetentionUnlessKeptForever()
    {
        Fixture fixture;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto outcome = fixture.history().add(kSecret);
        QVERIFY(fixture.history().setPinned(outcome.id, true));
        // an ordinary pinned entry with the same past expiry stays
        const qint64 plain
            = fixture.insertRaw(QStringLiteral("pinned plain"), now.addSecs(-100), true, now.addSecs(-10));
        QVERIFY(fixture.repository().recomputeExpiry(1));
        QTest::qWait(1100);

        fixture.settings().setSecretPinnedNeverExpire(true);
        fixture.history().runMaintenance();
        QVERIFY(fixture.repository().findByHash(HistoryService::hashOf(kSecret)).has_value());

        fixture.settings().setSecretPinnedNeverExpire(false);
        QCOMPARE(fixture.history().runMaintenance(), 1);
        QVERIFY(!fixture.repository().findByHash(HistoryService::hashOf(kSecret)).has_value());
        QVERIFY(fixture.repository().findById(plain).has_value());
    }
};

QTEST_GUILESS_MAIN(TestSecretHistory)
#include "test_secret_history.moc"
