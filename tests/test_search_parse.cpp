#include "TestSupport.h"

#include <QTest>

using namespace clipvault;

class TestSearchParse : public QObject {
    Q_OBJECT

private slots:
    void plainTextIsUnchanged();
    void appAndFromAreSynonyms();
    void titlePrefix();
    void quotedValues();
    void unterminatedQuoteTakesTheRest();
    void prefixesAreCaseInsensitive();
    void datesUseLocalDayBoundaries();
    void unknownDateStaysPlain();
    void urlsAndPathsStayPlain();
    void prefixInsideAWordStaysPlain();
    void emptyValuesFilterNothing();
    void mixedQuery();
    void mentionsTitle();
    void appQueryRoundTrips();
    void searchFiltersByAppAndDate();
};

namespace {

// a fixed local wall clock so day boundaries are predictable
const QDateTime kNow(QDate(2026, 3, 18), QTime(15, 30));

SearchFilter parse(const QString& query)
{
    return SearchService::parse(query, kNow);
}

} // namespace

void TestSearchParse::plainTextIsUnchanged()
{
    const SearchFilter filter = parse(QStringLiteral("  hello   world "));
    QCOMPARE(filter.text, QStringLiteral("hello   world"));
    QVERIFY(filter.app.isEmpty());
    QVERIFY(filter.title.isEmpty());
    QVERIFY(!filter.since);
    QVERIFY(!filter.until);
    QVERIFY(parse(QStringLiteral("   ")).isEmpty());
}

void TestSearchParse::appAndFromAreSynonyms()
{
    QCOMPARE(parse(QStringLiteral("app:firefox")).app, QStringLiteral("firefox"));
    QCOMPARE(parse(QStringLiteral("from:firefox")).app, QStringLiteral("firefox"));
    QVERIFY(parse(QStringLiteral("from:firefox")).text.isEmpty());
}

void TestSearchParse::titlePrefix()
{
    const SearchFilter filter = parse(QStringLiteral("title:readme"));
    QCOMPARE(filter.title, QStringLiteral("readme"));
    QVERIFY(filter.text.isEmpty());
}

void TestSearchParse::quotedValues()
{
    const SearchFilter filter = parse(QStringLiteral("title:\"my doc\" app:\"Visual Studio Code\" needle"));
    QCOMPARE(filter.title, QStringLiteral("my doc"));
    QCOMPARE(filter.app, QStringLiteral("Visual Studio Code"));
    QCOMPARE(filter.text, QStringLiteral("needle"));
}

void TestSearchParse::unterminatedQuoteTakesTheRest()
{
    const SearchFilter filter = parse(QStringLiteral("title:\"my do"));
    QCOMPARE(filter.title, QStringLiteral("my do"));
    QVERIFY(filter.text.isEmpty());
}

void TestSearchParse::prefixesAreCaseInsensitive()
{
    QCOMPARE(parse(QStringLiteral("APP:Code")).app, QStringLiteral("Code"));
    QVERIFY(parse(QStringLiteral("Date:TODAY")).since.has_value());
}

void TestSearchParse::datesUseLocalDayBoundaries()
{
    const QDateTime today(QDate(2026, 3, 18), QTime(0, 0));
    const QDateTime tomorrow(QDate(2026, 3, 19), QTime(0, 0));

    SearchFilter filter = parse(QStringLiteral("date:today"));
    QCOMPARE(*filter.since, today);
    QCOMPARE(*filter.until, tomorrow);

    filter = parse(QStringLiteral("date:yesterday"));
    QCOMPARE(*filter.since, QDateTime(QDate(2026, 3, 17), QTime(0, 0)));
    QCOMPARE(*filter.until, today);

    filter = parse(QStringLiteral("date:week"));
    QCOMPARE(*filter.since, QDateTime(QDate(2026, 3, 12), QTime(0, 0)));
    QCOMPARE(*filter.until, tomorrow);
    QVERIFY(filter.text.isEmpty());
}

void TestSearchParse::unknownDateStaysPlain()
{
    const SearchFilter filter = parse(QStringLiteral("date:2024"));
    QCOMPARE(filter.text, QStringLiteral("date:2024"));
    QVERIFY(!filter.since);
}

void TestSearchParse::urlsAndPathsStayPlain()
{
    for (const QString& query : { QStringLiteral("https://example.com/app:1"), QStringLiteral("http://x.org"),
             QStringLiteral("c:\\Users\\me"), QStringLiteral("mailto:a@b.c"), QStringLiteral("key:value") }) {
        const SearchFilter filter = parse(query);
        QCOMPARE(filter.text, query);
        QVERIFY(filter.app.isEmpty());
    }
}

void TestSearchParse::prefixInsideAWordStaysPlain()
{
    const SearchFilter filter = parse(QStringLiteral("myapp:thing"));
    QCOMPARE(filter.text, QStringLiteral("myapp:thing"));
    QVERIFY(filter.app.isEmpty());
}

void TestSearchParse::emptyValuesFilterNothing()
{
    const SearchFilter filter = parse(QStringLiteral("app: title:\"\" date: text"));
    QVERIFY(filter.app.isEmpty());
    QVERIFY(filter.title.isEmpty());
    QVERIFY(!filter.since);
    QCOMPARE(filter.text, QStringLiteral("text"));
}

void TestSearchParse::mixedQuery()
{
    const SearchFilter filter = parse(QStringLiteral("select from:term date:today * from"));
    QCOMPARE(filter.app, QStringLiteral("term"));
    QCOMPARE(filter.text, QStringLiteral("select * from"));
    QVERIFY(filter.since.has_value());
}

void TestSearchParse::mentionsTitle()
{
    QVERIFY(SearchService::mentionsTitle(QStringLiteral("title:")));
    QVERIFY(SearchService::mentionsTitle(QStringLiteral("foo Title:bar")));
    QVERIFY(!SearchService::mentionsTitle(QStringLiteral("subtitle:x")));
    QVERIFY(!SearchService::mentionsTitle(QStringLiteral("app:x")));
}

void TestSearchParse::appQueryRoundTrips()
{
    QCOMPARE(SearchService::appQuery(QStringLiteral("firefox")), QStringLiteral("app:firefox"));
    QCOMPARE(parse(SearchService::appQuery(QStringLiteral("My App"))).app, QStringLiteral("My App"));
}

void TestSearchParse::searchFiltersByAppAndDate()
{
    test::Fixture fx;
    QVERIFY2(fx.isOpen(), qPrintable(fx.openError()));
    Provenance firefox;
    firefox.app = QStringLiteral("org.mozilla.firefox");
    firefox.windowClass = QStringLiteral("Navigator");
    Provenance terminal;
    terminal.windowClass = QStringLiteral("gnome-terminal-server");
    QVERIFY(fx.history().add(QStringLiteral("browser clip"), firefox).stored());
    QVERIFY(fx.history().add(QStringLiteral("shell clip"), terminal).stored());
    fx.insertRaw(QStringLiteral("old clip"), QDateTime::currentDateTimeUtc().addDays(-30));

    QCOMPARE(fx.search().search(QStringLiteral("app:FIREFOX")).size(), 1);
    QCOMPARE(fx.search().search(QStringLiteral("from:navigator")).size(), 1);
    QCOMPARE(fx.search().search(QStringLiteral("from:terminal clip")).first().content,
        QStringLiteral("shell clip"));
    QCOMPARE(fx.search().search(QStringLiteral("date:week")).size(), 2);
    QCOMPARE(fx.search().search(QStringLiteral("clip")).size(), 3);
}

QTEST_GUILESS_MAIN(TestSearchParse)
#include "test_search_parse.moc"
