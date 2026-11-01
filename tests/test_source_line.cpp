#include "ui/ClipboardListModel.h"

#include <QTest>

using namespace clipvault;

class TestSourceLine : public QObject {
    Q_OBJECT

private slots:
    void noSourceIsEmpty();
    void appWithPathPrefersPath();
    void appWithTitleWhenNoPath();
    void classStandsInForMissingApp();
    void pathAloneWithoutApp();
};

namespace {

QModelIndex modelWith(ClipboardListModel& model, const Provenance& source)
{
    ClipboardItem item;
    item.id = 1;
    item.content = QStringLiteral("clip");
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.source = source;
    model.setItems({ item });
    return model.index(0, 0);
}

QString role(const QModelIndex& index, ClipboardListModel::Role r)
{
    return index.data(r).toString();
}

} // namespace

void TestSourceLine::noSourceIsEmpty()
{
    ClipboardListModel model;
    const QModelIndex index = modelWith(model, { });
    QVERIFY(role(index, ClipboardListModel::SourceRole).isEmpty());
    QVERIFY(role(index, ClipboardListModel::SourceAppRole).isEmpty());
    QVERIFY(role(index, ClipboardListModel::SourceDetailRole).isEmpty());
}

void TestSourceLine::appWithPathPrefersPath()
{
    ClipboardListModel model;
    Provenance source;
    source.app = QStringLiteral("kitty");
    source.title = QStringLiteral("vim notes");
    source.path = QStringLiteral("/home/u");
    const QModelIndex index = modelWith(model, source);
    QCOMPARE(role(index, ClipboardListModel::SourceRole), QStringLiteral("kitty  ·  /home/u"));
    QCOMPARE(role(index, ClipboardListModel::SourceAppRole), QStringLiteral("kitty"));
    QCOMPARE(role(index, ClipboardListModel::SourceDetailRole), QStringLiteral("/home/u"));
}

void TestSourceLine::appWithTitleWhenNoPath()
{
    ClipboardListModel model;
    Provenance source;
    source.app = QStringLiteral("firefox");
    source.title = QStringLiteral("Docs");
    const QModelIndex index = modelWith(model, source);
    QCOMPARE(role(index, ClipboardListModel::SourceRole), QStringLiteral("firefox  ·  Docs"));
    QCOMPARE(role(index, ClipboardListModel::SourceDetailRole), QStringLiteral("Docs"));
}

void TestSourceLine::classStandsInForMissingApp()
{
    ClipboardListModel model;
    Provenance source;
    source.windowClass = QStringLiteral("Navigator");
    const QModelIndex index = modelWith(model, source);
    QCOMPARE(role(index, ClipboardListModel::SourceRole), QStringLiteral("Navigator"));
    QCOMPARE(role(index, ClipboardListModel::SourceAppRole), QStringLiteral("Navigator"));
}

void TestSourceLine::pathAloneWithoutApp()
{
    ClipboardListModel model;
    Provenance source;
    source.path = QStringLiteral("https://example.com");
    const QModelIndex index = modelWith(model, source);
    QCOMPARE(role(index, ClipboardListModel::SourceRole), QStringLiteral("https://example.com"));
    QVERIFY(role(index, ClipboardListModel::SourceAppRole).isEmpty());
}

QTEST_GUILESS_MAIN(TestSourceLine)
#include "test_source_line.moc"
