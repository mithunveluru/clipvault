#include "TestSupport.h"

#include <QtTest>

using namespace clipvault;
using clipvault::test::Fixture;

class TestSearchService : public QObject {
    Q_OBJECT

private slots:
    void empty_query_returns_recent_history_newest_first();
    void search_is_case_insensitive();
    void partial_matches_are_found();
    void pinned_entries_rank_above_the_rest();
    void no_matches_returns_an_empty_list();
    void whitespace_only_query_is_treated_as_empty();
    void result_count_is_capped();
};

namespace {

QStringList contentsOf(const QList<ClipboardItem>& items)
{
    QStringList out;
    out.reserve(items.size());
    for (const ClipboardItem& item : items)
        out << item.content;
    return out;
}

} // namespace

void TestSearchService::empty_query_returns_recent_history_newest_first()
{
    Fixture fixture;
    QVERIFY2(fixture.isOpen(), qPrintable(fixture.openError()));

    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(-300);
    fixture.insertRaw(QStringLiteral("oldest"), base);
    fixture.insertRaw(QStringLiteral("middle"), base.addSecs(60));
    fixture.insertRaw(QStringLiteral("newest"), base.addSecs(120));

    QCOMPARE(contentsOf(fixture.search().search(QString())),
             QStringList({ QStringLiteral("newest"), QStringLiteral("middle"),
                           QStringLiteral("oldest") }));
}

void TestSearchService::search_is_case_insensitive()
{
    Fixture fixture;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    fixture.insertRaw(QStringLiteral("https://github.com/qt/qtbase"), now);

    QCOMPARE(fixture.search().search(QStringLiteral("GITHUB")).size(), 1);
    QCOMPARE(fixture.search().search(QStringLiteral("github")).size(), 1);
    QCOMPARE(fixture.search().search(QStringLiteral("GitHub")).size(), 1);
}

void TestSearchService::partial_matches_are_found()
{
    Fixture fixture;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    fixture.insertRaw(QStringLiteral("SELECT * FROM employees"), now);
    fixture.insertRaw(QStringLiteral("unrelated note"), now.addSecs(1));

    const auto results = fixture.search().search(QStringLiteral("employ"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().content, QStringLiteral("SELECT * FROM employees"));
}

void TestSearchService::pinned_entries_rank_above_the_rest()
{
    Fixture fixture;
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(-300);
    fixture.insertRaw(QStringLiteral("match old pinned"), base, /*pinned=*/true);
    fixture.insertRaw(QStringLiteral("match newer loose"), base.addSecs(200));

    // An older pinned entry outranks a newer unpinned one
    QCOMPARE(contentsOf(fixture.search().search(QString())).first(),
             QStringLiteral("match old pinned"));
    QCOMPARE(contentsOf(fixture.search().search(QStringLiteral("match"))).first(),
             QStringLiteral("match old pinned"));
}

void TestSearchService::no_matches_returns_an_empty_list()
{
    Fixture fixture;
    fixture.insertRaw(QStringLiteral("hello world"), QDateTime::currentDateTimeUtc());
    QVERIFY(fixture.search().search(QStringLiteral("nothing here")).isEmpty());
}

void TestSearchService::whitespace_only_query_is_treated_as_empty()
{
    Fixture fixture;
    fixture.insertRaw(QStringLiteral("hello world"), QDateTime::currentDateTimeUtc());
    QCOMPARE(fixture.search().search(QStringLiteral("   ")).size(), 1);
}

void TestSearchService::result_count_is_capped()
{
    Fixture fixture;
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(-1000);
    for (int i = 0; i < 30; ++i)
        fixture.insertRaw(QStringLiteral("entry %1").arg(i), base.addSecs(i));

    QCOMPARE(fixture.search().search(QString(), 5).size(), 5);
    QCOMPARE(fixture.search().search(QString()).size(), 30);
}

QTEST_GUILESS_MAIN(TestSearchService)
#include "test_search_service.moc"
