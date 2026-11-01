#include "TestSupport.h"

#include <QtTest>

using namespace clipvault;
using clipvault::test::Fixture;

namespace {

QList<qint64> idsOf(const QList<ClipboardItem>& items)
{
    QList<qint64> ids;
    for (const ClipboardItem& item : items)
        ids << item.id;
    return ids;
}

} // namespace

class TestSearchBlend : public QObject {
    Q_OBJECT

private slots:
    void noSemanticIdsMatchesPlainSearch()
    {
        Fixture fixture;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        fixture.insertRaw(QStringLiteral("alpha one"), now.addSecs(-3));
        fixture.insertRaw(QStringLiteral("alpha two"), now.addSecs(-2), true);
        fixture.insertRaw(QStringLiteral("beta"), now.addSecs(-1));
        for (const QString& query : { QString(), QStringLiteral("alpha"), QStringLiteral("zzz") })
            QCOMPARE(
                idsOf(fixture.search().searchBlended(query, { })), idsOf(fixture.search().search(query)));
    }

    void semanticOnlyHitsFollowLexicalOnes()
    {
        Fixture fixture;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 lexicalOld = fixture.insertRaw(QStringLiteral("car keys on the hook"), now.addSecs(-10));
        const qint64 lexicalNew = fixture.insertRaw(QStringLiteral("car insurance renewal"), now.addSecs(-5));
        const qint64 meaningA
            = fixture.insertRaw(QStringLiteral("automobile service booked"), now.addSecs(-8));
        const qint64 meaningB = fixture.insertRaw(QStringLiteral("vehicle registration"), now.addSecs(-7));

        ClipboardItem secret;
        secret.content = QStringLiteral("pinned secret that means car");
        secret.type = ClipboardType::Text;
        secret.pinned = true;
        secret.createdAt = now;
        secret.contentHash = HistoryService::hashOf(secret.content);
        secret.secretKind = QStringLiteral("password");
        const qint64 secretId = fixture.repository().upsert(secret)->id;

        // the semantic list repeats a lexical hit and names a deleted row and a secret
        const QList<qint64> semantic { meaningB, lexicalOld, 999999, secretId, meaningA };
        const QList<ClipboardItem> blended
            = fixture.search().searchBlended(QStringLiteral(" car "), semantic);
        QCOMPARE(idsOf(blended), (QList<qint64> { lexicalNew, lexicalOld, meaningB, meaningA }));

        QCOMPARE(idsOf(fixture.search().searchBlended(QStringLiteral("car"), semantic, 3)),
            (QList<qint64> { lexicalNew, lexicalOld, meaningB }));
    }

    void semanticTextIsTheTrimmedQuery()
    {
        QCOMPARE(SearchService::semanticText(QStringLiteral("  red car \n")), QStringLiteral("red car"));
        QVERIFY(SearchService::semanticText(QStringLiteral("   ")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSearchBlend)
#include "test_search_blend.moc"
