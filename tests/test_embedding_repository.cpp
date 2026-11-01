#include "TestSupport.h"
#include "storage/EmbeddingRepository.h"

#include <QRegularExpression>
#include <QSqlQuery>
#include <QtTest>

using namespace clipvault;
using clipvault::test::Fixture;

namespace {

Embedding unit(int hot, int dim = 4)
{
    Embedding vector(dim, 0.0f);
    vector[hot] = 1.0f;
    return vector;
}

int rowCount(Fixture& fixture)
{
    QSqlQuery query(fixture.database().handle());
    if (!query.exec(QStringLiteral("SELECT count(*) FROM clip_embeddings")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

QString hashOf(Fixture& fixture, qint64 id)
{
    return fixture.repository().findById(id)->contentHash;
}

} // namespace

class TestEmbeddingRepository : public QObject {
    Q_OBJECT

private slots:
    void blobIsLittleEndianFloat32()
    {
        const QByteArray blob = EmbeddingRepository::toBlob({ 1.0f, -2.5f });
        QCOMPARE(blob, QByteArray::fromHex("0000803f000020c0"));
        QCOMPARE(EmbeddingRepository::fromBlob(blob, 2), (Embedding { 1.0f, -2.5f }));
        QVERIFY(EmbeddingRepository::fromBlob(blob, 3).isEmpty());
    }

    void foreignKeysAreEnforced()
    {
        Fixture fixture;
        QSqlQuery query(fixture.database().handle());
        QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys")) && query.next());
        QCOMPARE(query.value(0).toInt(), 1);

        EmbeddingRepository embeddings(fixture.database().handle());
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("embedding upsert failed")));
        QVERIFY(!embeddings.upsert(424242, QStringLiteral("m"), QStringLiteral("h"), unit(0)));
        QCOMPARE(rowCount(fixture), 0);
    }

    void storesAndLoadsPerModel()
    {
        Fixture fixture;
        EmbeddingRepository embeddings(fixture.database().handle());
        const qint64 id = fixture.insertRaw(QStringLiteral("alpha"), QDateTime::currentDateTimeUtc());
        QVERIFY(embeddings.upsert(id, QStringLiteral("m1"), hashOf(fixture, id), unit(1)));
        // replacing keeps one row per item
        QVERIFY(embeddings.upsert(id, QStringLiteral("m1"), hashOf(fixture, id), unit(2)));
        QCOMPARE(rowCount(fixture), 1);

        const auto loaded = embeddings.loadAll(QStringLiteral("m1"), 4);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.front().itemId, id);
        QCOMPARE(loaded.front().vector, unit(2));
        QVERIFY(embeddings.loadAll(QStringLiteral("m2"), 4).isEmpty());
        QVERIFY(embeddings.loadAll(QStringLiteral("m1"), 8).isEmpty());
        QCOMPARE(embeddings.itemIds(QStringLiteral("m1")), QSet<qint64> { id });

        QVERIFY(embeddings.remove(id));
        QCOMPARE(rowCount(fixture), 0);
        QVERIFY(embeddings.upsert(id, QStringLiteral("m1"), hashOf(fixture, id), unit(2)));
        QVERIFY(embeddings.removeAll());
        QCOMPARE(rowCount(fixture), 0);
    }

    void pendingListsMissingStaleAndOtherModel()
    {
        Fixture fixture;
        EmbeddingRepository embeddings(fixture.database().handle());
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 fresh = fixture.insertRaw(QStringLiteral("fresh"), now);
        const qint64 missing = fixture.insertRaw(QStringLiteral("missing ") + QString(5000, u'x'), now);
        const qint64 stale = fixture.insertRaw(QStringLiteral("stale"), now);
        const qint64 otherModel = fixture.insertRaw(QStringLiteral("other"), now);
        QVERIFY(embeddings.upsert(fresh, QStringLiteral("m1"), hashOf(fixture, fresh), unit(0)));
        QVERIFY(embeddings.upsert(stale, QStringLiteral("m1"), QStringLiteral("old hash"), unit(0)));
        QVERIFY(embeddings.upsert(otherModel, QStringLiteral("m0"), hashOf(fixture, otherModel), unit(0)));

        const auto pending = embeddings.pending(QStringLiteral("m1"), 10, 100);
        QList<qint64> ids;
        for (const auto& item : pending)
            ids << item.itemId;
        QCOMPARE(ids, (QList<qint64> { otherModel, stale, missing }));
        QCOMPARE(pending.at(2).text.size(), 100);
        QCOMPARE(pending.at(2).contentHash, hashOf(fixture, missing));
        QCOMPARE(embeddings.pending(QStringLiteral("m1"), 2, 100).size(), 2);
        // stale vectors are not served
        QCOMPARE(embeddings.itemIds(QStringLiteral("m1")), QSet<qint64> { fresh });
    }

    void secretRowsAreNeverPendingOrLoaded()
    {
        Fixture fixture;
        EmbeddingRepository embeddings(fixture.database().handle());
        const qint64 id = fixture.insertRaw(
            QStringLiteral("plain then pinned as secret"), QDateTime::currentDateTimeUtc());
        QVERIFY(embeddings.upsert(id, QStringLiteral("m1"), hashOf(fixture, id), unit(0)));

        // the same text pinned as a secret marks the existing row
        ClipboardItem secret = *fixture.repository().findById(id);
        secret.secretKind = QStringLiteral("password");
        QVERIFY(fixture.repository().upsert(secret));

        QVERIFY(embeddings.loadAll(QStringLiteral("m1"), 4).isEmpty());
        QVERIFY(embeddings.itemIds(QStringLiteral("m1")).isEmpty());
        QVERIFY(embeddings.pending(QStringLiteral("m1"), 10, 100).isEmpty());
        QCOMPARE(embeddings.removeSecrets(), 1);
        QCOMPARE(rowCount(fixture), 0);
    }

    void vectorsDieWithTheirItems()
    {
        Fixture fixture;
        fixture.settings().setMaxItems(10);
        EmbeddingRepository embeddings(fixture.database().handle());
        const QDateTime now = QDateTime::currentDateTimeUtc();
        auto embedded = [&](const QString& text, const QDateTime& at, bool pinned = false,
                            std::optional<QDateTime> expires = std::nullopt) {
            const qint64 id = fixture.insertRaw(text, at, pinned, expires);
            embeddings.upsert(id, QStringLiteral("m1"), hashOf(fixture, id), unit(0));
            return id;
        };

        const qint64 removed = embedded(QStringLiteral("removed"), now);
        QVERIFY(fixture.history().remove(removed));
        QCOMPARE(rowCount(fixture), 0);

        embedded(QStringLiteral("expired"), now.addSecs(-100), false, now.addSecs(-10));
        QCOMPARE(fixture.repository().deleteExpired(now), 1);
        QCOMPARE(rowCount(fixture), 0);

        for (int i = 0; i < 12; ++i)
            embedded(QStringLiteral("item %1").arg(i), now.addSecs(i));
        QCOMPARE(fixture.repository().enforceLimit(10), 2);
        QCOMPARE(rowCount(fixture), 10);

        embedded(QStringLiteral("pinned"), now, true);
        QVERIFY(fixture.history().clearAll(false));
        QCOMPARE(rowCount(fixture), 1);
        QVERIFY(fixture.history().clearAll(true));
        QCOMPARE(rowCount(fixture), 0);
    }
};

QTEST_GUILESS_MAIN(TestEmbeddingRepository)
#include "test_embedding_repository.moc"
