#include "TestSupport.h"

#include <QTest>

using namespace clipvault;

namespace {

ClipboardItem makeItem(const QString& content, const QDateTime& createdAt, const QString& app = { },
    const QString& windowClass = { }, const QString& title = { }, const QString& path = { })
{
    Provenance source;
    source.app = app;
    source.windowClass = windowClass;
    source.title = title;
    source.path = path;
    ClipboardItem item;
    item.content = content;
    item.type = ClipboardType::Text;
    item.createdAt = createdAt;
    item.contentHash = HistoryService::hashOf(content);
    item.source = source;
    return item;
}

const QDateTime kBase = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);

} // namespace

class TestRepositoryFields : public QObject {
    Q_OBJECT

private slots:
    void provenanceRoundTrips();
    void refreshWithEmptySourceKeepsOldSource();
    void refreshWithNewSourceReplacesIt();
    void filterByAppMatchesAppOrClass();
    void filterByTitle();
    void filterByDateRange();
    void filterCombinesWithText();
    void appFilterWildcardsAreLiteral();
    void copiedIntoAndClearProvenance();
    void secretKindRoundTrips();
};

void TestRepositoryFields::provenanceRoundTrips()
{
    test::Fixture fx;
    QVERIFY(fx.isOpen());
    const auto stored = fx.repository().upsert(makeItem(QStringLiteral("a"), kBase, QStringLiteral("Firefox"),
        QStringLiteral("firefox"), QStringLiteral("Docs"), QStringLiteral("/tmp")));
    QVERIFY(stored);
    const auto item = fx.repository().findById(stored->id);
    QVERIFY(item);
    QCOMPARE(item->source.app, QStringLiteral("Firefox"));
    QCOMPARE(item->source.windowClass, QStringLiteral("firefox"));
    QCOMPARE(item->source.title, QStringLiteral("Docs"));
    QCOMPARE(item->source.path, QStringLiteral("/tmp"));
    QVERIFY(item->copiedInto.isEmpty());
    QVERIFY(item->secretKind.isEmpty());
}

void TestRepositoryFields::refreshWithEmptySourceKeepsOldSource()
{
    test::Fixture fx;
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("a"), kBase, QStringLiteral("Code"))));
    const auto refreshed = fx.repository().upsert(makeItem(QStringLiteral("a"), kBase.addSecs(5)));
    QVERIFY(refreshed && !refreshed->inserted);
    QCOMPARE(fx.repository().findById(refreshed->id)->source.app, QStringLiteral("Code"));
}

void TestRepositoryFields::refreshWithNewSourceReplacesIt()
{
    test::Fixture fx;
    QVERIFY(fx.repository().upsert(
        makeItem(QStringLiteral("a"), kBase, QStringLiteral("Code"), QString(), QStringLiteral("t"))));
    const auto refreshed
        = fx.repository().upsert(makeItem(QStringLiteral("a"), kBase.addSecs(5), QStringLiteral("Terminal")));
    const auto item = fx.repository().findById(refreshed->id);
    QCOMPARE(item->source.app, QStringLiteral("Terminal"));
    QVERIFY(item->source.title.isEmpty());
}

void TestRepositoryFields::filterByAppMatchesAppOrClass()
{
    test::Fixture fx;
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("one"), kBase, QStringLiteral("Firefox"))));
    QVERIFY(fx.repository().upsert(
        makeItem(QStringLiteral("two"), kBase, QString(), QStringLiteral("org.gnome.Terminal"))));
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("three"), kBase)));
    SearchFilter filter;
    filter.app = QStringLiteral("fire");
    QCOMPARE(fx.repository().search(filter, 10).size(), 1);
    filter.app = QStringLiteral("terminal");
    const auto hits = fx.repository().search(filter, 10);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().content, QStringLiteral("two"));
}

void TestRepositoryFields::filterByTitle()
{
    test::Fixture fx;
    QVERIFY(fx.repository().upsert(makeItem(
        QStringLiteral("one"), kBase, QStringLiteral("a"), QString(), QStringLiteral("Budget 2026"))));
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("two"), kBase, QStringLiteral("a"))));
    SearchFilter filter;
    filter.title = QStringLiteral("budget");
    QCOMPARE(fx.repository().search(filter, 10).size(), 1);
}

void TestRepositoryFields::filterByDateRange()
{
    test::Fixture fx;
    fx.insertRaw(QStringLiteral("old"), kBase);
    fx.insertRaw(QStringLiteral("mid"), kBase.addDays(1));
    fx.insertRaw(QStringLiteral("new"), kBase.addDays(2));
    SearchFilter filter;
    filter.since = kBase.addDays(1);
    filter.until = kBase.addDays(2);
    const auto hits = fx.repository().search(filter, 10);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().content, QStringLiteral("mid"));
}

void TestRepositoryFields::filterCombinesWithText()
{
    test::Fixture fx;
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("alpha"), kBase, QStringLiteral("Firefox"))));
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("beta"), kBase, QStringLiteral("Firefox"))));
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("alpha two"), kBase, QStringLiteral("Code"))));
    SearchFilter filter;
    filter.text = QStringLiteral("alpha");
    filter.app = QStringLiteral("firefox");
    const auto hits = fx.repository().search(filter, 10);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().content, QStringLiteral("alpha"));
}

void TestRepositoryFields::appFilterWildcardsAreLiteral()
{
    test::Fixture fx;
    QVERIFY(fx.repository().upsert(makeItem(QStringLiteral("x"), kBase, QStringLiteral("Firefox"))));
    SearchFilter filter;
    filter.app = QStringLiteral("%");
    QCOMPARE(fx.repository().search(filter, 10).size(), 0);
    filter.app = QStringLiteral("_irefox");
    QCOMPARE(fx.repository().search(filter, 10).size(), 0);
}

void TestRepositoryFields::copiedIntoAndClearProvenance()
{
    test::Fixture fx;
    const auto stored = fx.repository().upsert(makeItem(QStringLiteral("a"), kBase, QStringLiteral("Code"),
        QStringLiteral("code"), QStringLiteral("t"), QStringLiteral("/p")));
    QVERIFY(fx.repository().setCopiedInto(stored->id, QStringLiteral("Terminal")));
    QCOMPARE(fx.repository().findById(stored->id)->copiedInto, QStringLiteral("Terminal"));
    QVERIFY(fx.repository().clearProvenance());
    const auto item = fx.repository().findById(stored->id);
    QVERIFY(item->source.isEmpty());
    QVERIFY(item->copiedInto.isEmpty());
    QCOMPARE(item->content, QStringLiteral("a"));
}

void TestRepositoryFields::secretKindRoundTrips()
{
    test::Fixture fx;
    ClipboardItem item = makeItem(QStringLiteral("s"), kBase);
    item.pinned = true;
    item.secretKind = QStringLiteral("github-token");
    const auto stored = fx.repository().upsert(item);
    QCOMPARE(fx.repository().findById(stored->id)->secretKind, QStringLiteral("github-token"));
}

QTEST_GUILESS_MAIN(TestRepositoryFields)
#include "test_repository_fields.moc"
