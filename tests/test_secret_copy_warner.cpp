#include "FakeFocusTracker.h"
#include "TestSupport.h"
#include "services/SecretCopyWarner.h"

#include <QSignalSpy>
#include <QTest>

using namespace clipvault;
using namespace clipvault::test;
using namespace std::chrono_literals;

namespace {

platform::WindowInfo window(const QString& app, const QString& windowClass = { })
{
    platform::WindowInfo info;
    info.app = app;
    info.windowClass = windowClass;
    return info;
}

} // namespace

class TestSecretCopyWarner : public QObject {
    Q_OBJECT

private slots:
    void warnsOnceWhenARiskyAppGetsFocus()
    {
        Fixture fixture;
        FakeFocusTracker focus;
        SecretCopyWarner warner(focus, fixture.settings());
        QSignalSpy warned(&warner, &SecretCopyWarner::warning);

        warner.secretWrittenBack(QStringLiteral("github_token"), 30s);
        focus.setFocused(window(QStringLiteral("org.gnome.TextEditor")));
        QCOMPARE(warned.count(), 0);

        focus.setFocused(window(QStringLiteral("com.slack.Slack")));
        QCOMPARE(warned.count(), 1);
        const QString message = warned.first().first().toString();
        QVERIFY(message.contains(QStringLiteral("github token")));
        QVERIFY(message.contains(QStringLiteral("cannot see pastes")));
        QVERIFY(!warner.isArmed());

        focus.setFocused(window(QStringLiteral("discord")));
        QCOMPARE(warned.count(), 1);
    }

    void matchesOnWindowClassToo()
    {
        Fixture fixture;
        FakeFocusTracker focus;
        SecretCopyWarner warner(focus, fixture.settings());
        QSignalSpy warned(&warner, &SecretCopyWarner::warning);
        warner.secretWrittenBack(QStringLiteral("api_key"), 30s);
        focus.setFocused(window(QString(), QStringLiteral("TelegramDesktop")));
        QCOMPARE(warned.count(), 1);
    }

    void silentWhenDisabledOrDisarmed()
    {
        Fixture fixture;
        FakeFocusTracker focus;
        SecretCopyWarner warner(focus, fixture.settings());
        QSignalSpy warned(&warner, &SecretCopyWarner::warning);

        focus.setFocused(window(QStringLiteral("slack")));
        QCOMPARE(warned.count(), 0);

        warner.secretWrittenBack(QStringLiteral("api_key"), 30s);
        warner.disarm();
        focus.setFocused(window(QStringLiteral("slack")));
        QCOMPARE(warned.count(), 0);

        fixture.settings().setSecretCopyBackWarning(false);
        warner.secretWrittenBack(QStringLiteral("api_key"), 30s);
        QVERIFY(!warner.isArmed());
        focus.setFocused(window(QStringLiteral("slack")));
        QCOMPARE(warned.count(), 0);
    }

    void customListAndWindowExpiry()
    {
        Fixture fixture;
        fixture.settings().setSecretRiskyApps({ QStringLiteral("mattermost") });
        FakeFocusTracker focus;
        SecretCopyWarner warner(focus, fixture.settings());
        QSignalSpy warned(&warner, &SecretCopyWarner::warning);

        warner.secretWrittenBack(QStringLiteral("api_key"), 30s);
        focus.setFocused(window(QStringLiteral("slack")));
        QCOMPARE(warned.count(), 0);
        focus.setFocused(window(QStringLiteral("Mattermost")));
        QCOMPARE(warned.count(), 1);

        warner.secretWrittenBack(QStringLiteral("api_key"), 1s);
        QTRY_VERIFY_WITH_TIMEOUT(!warner.isArmed(), 3000);
        focus.setFocused(window(QStringLiteral("mattermost")));
        QCOMPARE(warned.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSecretCopyWarner)
#include "test_secret_copy_warner.moc"
