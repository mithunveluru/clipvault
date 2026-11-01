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

    void semanticTextDropsPrefixes()
    {
        QCOMPARE(SearchService::semanticText(
                     QStringLiteral("app:firefox meeting title:\"Team calendar\" notes date:today")),
            QStringLiteral("meeting notes"));
        // a query of prefixes alone leaves nothing to embed
        QVERIFY(SearchService::semanticText(QStringLiteral("app:firefox from:konsole date:week")).isEmpty());
        QVERIFY(SearchService::semanticText(QStringLiteral("title:")).isEmpty());
    }

    void semanticHitsObeyPrefixes()
    {
        Fixture fixture;
        auto add = [&fixture](const QString& text, const QString& app, const QString& windowClass,
                       const QString& title) {
            Provenance source;
            source.app = app;
            source.windowClass = windowClass;
            source.title = title;
            return fixture.history().add(text, source).id;
        };
        const qint64 calendar = add(QStringLiteral("meeting moved to friday"), QStringLiteral("firefox"),
            QStringLiteral("Navigator"), QStringLiteral("Team calendar"));
        const qint64 shell = add(QStringLiteral("kubectl rollout restart"), QStringLiteral("konsole"),
            QStringLiteral("konsole"), QStringLiteral("deploy shell"));
        const qint64 byClass = add(QStringLiteral("deadline is next week"), QString(),
            QStringLiteral("firefox-esr"), QStringLiteral("Mail"));
        const qint64 old = fixture.insertRaw(
            QStringLiteral("schedule from last month"), QDateTime::currentDateTimeUtc().addDays(-30));
        const QList<qint64> semantic { shell, calendar, old, byClass };

        const QList<QPair<QString, QList<qint64>>> cases {
            { QStringLiteral("app:firefox plans"), { calendar, byClass } },
            { QStringLiteral("from:konsole plans"), { shell } },
            { QStringLiteral("title:calendar plans"), { calendar } },
            { QStringLiteral("date:today plans"), { shell, calendar, byClass } },
            { QStringLiteral("app:firefox title:mail plans"), { byClass } },
            { QStringLiteral("plans"), semantic },
        };
        for (const auto& [query, expected] : cases) {
            QCOMPARE(idsOf(fixture.search().searchBlended(query, semantic)), expected);
            // the semantic side keeps exactly the rows the lexical side would allow under the same prefixes
            const QString prefixes = query.chopped(QStringLiteral("plans").size());
            const QList<qint64> allowed = idsOf(fixture.search().search(prefixes));
            for (const qint64 id : semantic)
                QCOMPARE(expected.contains(id), allowed.contains(id));
        }
    }
};

QTEST_GUILESS_MAIN(TestSearchBlend)
#include "test_search_blend.moc"
