#include "FakeEmbeddingProvider.h"
#include "TestSupport.h"
#include "services/SemanticIndex.h"
#include "storage/EmbeddingRepository.h"
#include "ui/ClipboardListModel.h"
#include "ui/MainWindow.h"

#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QtTest>

using namespace clipvault;
using clipvault::test::FakeEmbeddingProbe;
using clipvault::test::FakeEmbeddingProvider;
using clipvault::test::Fixture;

namespace {

QList<qint64> shownIds(const QListView& list)
{
    QList<qint64> ids;
    const QAbstractItemModel* model = list.model();
    for (int row = 0; row < model->rowCount(); ++row)
        ids << model->index(row, 0).data(ClipboardListModel::IdRole).toLongLong();
    return ids;
}

qint64 addFrom(Fixture& fixture, const QString& text, const QString& app)
{
    Provenance source;
    source.app = app;
    return fixture.history().add(text, source).id;
}

} // namespace

// the search box drives the index the way a user would
class TestSemanticWindow : public QObject {
    Q_OBJECT

private slots:
    void prefixesFilterHitsAndNeverReachTheIndex()
    {
        Fixture fixture;
        EmbeddingRepository embeddings(fixture.database().handle());
        SemanticIndex index(embeddings, fixture.repository());
        const auto probe = std::make_shared<FakeEmbeddingProbe>();
        const qint64 firefoxCar
            = addFrom(fixture, QStringLiteral("red car parked outside"), QStringLiteral("firefox"));
        const qint64 konsoleCar
            = addFrom(fixture, QStringLiteral("vehicle registration"), QStringLiteral("konsole"));

        QSignalSpy idle(&index, &SemanticIndex::backfillIdle);
        index.start([probe](ProviderFailure*) -> std::unique_ptr<EmbeddingProvider> {
            return std::make_unique<FakeEmbeddingProvider>(probe, 0,
                QHash<QString, QString> { { QStringLiteral("automobile"), QStringLiteral("car") },
                    { QStringLiteral("vehicle"), QStringLiteral("car") } });
        });
        QVERIFY(idle.wait(5000));
        QVERIFY(index.isReady());

        MainWindow window(fixture.history(), fixture.search(), fixture.settings());
        window.setSemanticIndex(&index);
        auto* edit = window.findChild<QLineEdit*>();
        auto* list = window.findChild<QListView*>();
        QVERIFY(edit && list);

        const qsizetype embeddedBefore = probe->texts().size();
        edit->setText(QStringLiteral("app:firefox"));
        QTest::qWait(200);
        QCOMPARE(probe->texts().size(), embeddedBefore);
        QCOMPARE(shownIds(*list), QList<qint64> { firefoxCar });

        // both clips mean car yet only the firefox one passes the prefix
        QSignalSpy ready(&index, &SemanticIndex::queryReady);
        edit->setText(QStringLiteral("app:firefox automobile"));
        QVERIFY(ready.wait());
        QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("automobile"));
        QVERIFY(ready.at(0).at(1).value<QList<qint64>>().contains(konsoleCar));
        QTRY_COMPARE(shownIds(*list), QList<qint64> { firefoxCar });
        QCOMPARE(probe->texts().last(), QStringLiteral("automobile"));

        index.stop();
    }
};

QTEST_MAIN(TestSemanticWindow)
#include "test_semantic_window.moc"
