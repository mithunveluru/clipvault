#include "TestSupport.h"

#include <QTest>

using namespace clipvault;

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void defaultsAreTheSafeOption();
    void ttlIsClamped();
    void listsAreNormalised();
    void flagsPersistAcrossRestart();
    void historyStoresGivenSource();
    void semanticSearchIsOffUntilChosen();
};

void TestSettings::defaultsAreTheSafeOption()
{
    test::Fixture fx;
    QVERIFY(fx.settings().secretRadarEnabled());
    QCOMPARE(fx.settings().secretTtlSeconds(), 45);
    QVERIFY(!fx.settings().secretPinnedNeverExpire());
    QVERIFY(fx.settings().secretCopyBackWarning());
    QVERIFY(fx.settings().secretRiskyApps().contains(QStringLiteral("slack")));
    QVERIFY(fx.settings().provenanceEnabled());
    QVERIFY(!fx.settings().provenanceTitles());
    QVERIFY(fx.settings().provenanceBlocklist().contains(QStringLiteral("keepassxc")));
}

void TestSettings::ttlIsClamped()
{
    test::Fixture fx;
    fx.settings().setSecretTtlSeconds(1);
    QCOMPARE(fx.settings().secretTtlSeconds(), 10);
    fx.settings().setSecretTtlSeconds(100000);
    QCOMPARE(fx.settings().secretTtlSeconds(), 300);
    fx.settings().setSecretTtlSeconds(60);
    QCOMPARE(fx.settings().secretTtlSeconds(), 60);
}

void TestSettings::listsAreNormalised()
{
    test::Fixture fx;
    fx.settings().setProvenanceBlocklist(
        { QStringLiteral(" KeePassXC "), QString(), QStringLiteral("a,b"), QStringLiteral("keepassxc") });
    QCOMPARE(fx.settings().provenanceBlocklist(),
        QStringList({ QStringLiteral("keepassxc"), QStringLiteral("ab") }));
    fx.settings().setSecretRiskyApps({ });
    QVERIFY(fx.settings().secretRiskyApps().isEmpty());
}

void TestSettings::flagsPersistAcrossRestart()
{
    test::Fixture fx;
    fx.settings().setProvenanceTitles(true);
    fx.settings().setSecretRadarEnabled(false);
    QVERIFY(fx.reopen());
    QVERIFY(fx.settings().provenanceTitles());
    QVERIFY(!fx.settings().secretRadarEnabled());
}

void TestSettings::historyStoresGivenSource()
{
    test::Fixture fx;
    Provenance source;
    source.app = QStringLiteral("Terminal");
    source.path = QStringLiteral("/home/u");
    const auto outcome = fx.history().add(QStringLiteral("ls -la"), source);
    QVERIFY(outcome.stored());
    const auto item = fx.repository().findById(outcome.id);
    QCOMPARE(item->source.app, QStringLiteral("Terminal"));
    QCOMPARE(item->source.path, QStringLiteral("/home/u"));
}

void TestSettings::semanticSearchIsOffUntilChosen()
{
    test::Fixture fx;
    QVERIFY(!fx.settings().semanticEnabled());
    QVERIFY(fx.settings().semanticModelDir().isEmpty());
    fx.settings().setSemanticEnabled(true);
    fx.settings().setSemanticModelDir(QStringLiteral("/models/minilm"));
    QVERIFY(fx.reopen());
    QVERIFY(fx.settings().semanticEnabled());
    QCOMPARE(fx.settings().semanticModelDir(), QStringLiteral("/models/minilm"));
}

QTEST_GUILESS_MAIN(TestSettings)
#include "test_settings.moc"
