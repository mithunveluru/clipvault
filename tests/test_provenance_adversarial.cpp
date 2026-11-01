#include "FakeFocusTracker.h"
#include "TestSupport.h"
#include "services/ProvenanceService.h"
#include "ui/ClipboardItemDelegate.h"
#include "ui/ClipboardListModel.h"
#include "ui/ProvenanceDialog.h"

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace clipvault;

class TestProvenanceAdversarial : public QObject {
    Q_OBJECT

private slots:
    void maskedSecretSourceLineDoesNotLeakSecret();
    void maskedSecretDialogDoesNotLeakSecret();
    void pinnedSecretStoresNoUrlPath();
    void titlesTurnedOffAreNoLongerSearchable();
    void titlesTurnedOffLeaveNoTitleOnSecretsInMemory();
    void urlPathIsNotOfferedForOpening();
    void dialogShowsTitlesAsPlainText();
    void appQueryWithQuoteRoundTrips();
    void dstDayIsTwentyThreeHours();
    void likeWildcardInAppIsLiteral();
    void injectionShapedQueryIsHarmless();
    void activateUsesTheLiveWindowId();
    void sourceWithoutLiveWindowIsNotOffered();
    void secretsInMemoryFollowSearchPrefixes();
    void secretCopyBackDoesNotBlockSavedCopyBack();
    void appChipStaysClearOfPinMarker();
};

namespace {

platform::WindowInfo browser()
{
    platform::WindowInfo window;
    window.app = QStringLiteral("org.mozilla.firefox");
    window.windowClass = QStringLiteral("firefox");
    window.title = QStringLiteral("<b>bold</b> <img src=\"file:///etc/hostname\">");
    window.windowId = 3;
    return window;
}

struct Stack {
    test::Fixture fx;
    test::FakeFocusTracker tracker;
    ProvenanceService service { tracker, fx.settings(), fx.history(),
        [](const platform::WindowInfo&) { return QString(); } };
};

constexpr auto kWebhook = "https://hooks.slack.com/services/T00000000/"
                          "B00000000/XXXXXXXXXXXXXXXXXXXXXXXX";

QPushButton* buttonNamed(const ProvenanceDialog& dialog, const QString& text)
{
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

bool anyLabelContains(const ProvenanceDialog& dialog, const QString& needle)
{
    for (const QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text().contains(needle))
            return true;
    }
    return false;
}

} // namespace

void TestProvenanceAdversarial::maskedSecretSourceLineDoesNotLeakSecret()
{
    Stack s;
    s.tracker.setFocused(browser());
    const QString secret = QString::fromLatin1(kWebhook);
    const auto outcome = s.fx.history().add(secret, s.service.sourceFor(secret));
    QVERIFY(outcome.stored());
    QVERIFY(outcome.id < 0);

    ClipboardListModel model;
    model.setItems(s.fx.history().ephemeral().items());
    const QModelIndex index = model.index(0, 0);
    QVERIFY(!index.data(Qt::DisplayRole).toString().contains(QStringLiteral("XXXXXXXX")));
    QVERIFY2(!index.data(ClipboardListModel::SourceRole).toString().contains(QStringLiteral("XXXXXXXX")),
        "the source line of a masked secret shows the secret");
    QVERIFY(
        !index.data(ClipboardListModel::SourceDetailRole).toString().contains(QStringLiteral("XXXXXXXX")));
}

void TestProvenanceAdversarial::maskedSecretDialogDoesNotLeakSecret()
{
    Stack s;
    ClipboardItem item;
    item.id = -2;
    item.content = QString::fromLatin1(kWebhook);
    item.secretKind = QStringLiteral("slack_webhook");
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.source.app = QStringLiteral("org.mozilla.firefox");
    item.source.title = QString::fromLatin1(kWebhook);
    item.source.path = QString::fromLatin1(kWebhook);

    const ProvenanceDialog masked(item, s.service);
    QVERIFY2(!anyLabelContains(masked, QStringLiteral("XXXXXXXX")), "the dialog shows a masked secret");
    QVERIFY(anyLabelContains(masked, item.source.app));
    QPushButton* open = buttonNamed(masked, QStringLiteral("Open source"));
    QVERIFY(open);
    QVERIFY(!open->isEnabled());

    const ProvenanceDialog revealed(item, s.service, true);
    QVERIFY(anyLabelContains(revealed, QStringLiteral("XXXXXXXX")));
}

void TestProvenanceAdversarial::pinnedSecretStoresNoUrlPath()
{
    Stack s;
    s.tracker.setFocused(browser());
    const QString secret = QString::fromLatin1(kWebhook);
    const auto outcome = s.service.capture(secret);
    QVERIFY(outcome.stored());
    QVERIFY(outcome.id < 0);
    QVERIFY(s.fx.history().ephemeral().items().at(0).source.path.isEmpty());

    QVERIFY(s.fx.history().setPinned(outcome.id, true));
    const QList<ClipboardItem> stored = s.fx.repository().search(SearchFilter { }, 10);
    QCOMPARE(stored.size(), 1);
    QVERIFY(!stored.at(0).secretKind.isEmpty());
    QVERIFY2(stored.at(0).source.path.isEmpty(), "pinning wrote the secret url to source_path");
    QCOMPARE(stored.at(0).source.app, browser().app);
}

void TestProvenanceAdversarial::titlesTurnedOffAreNoLongerSearchable()
{
    Stack s;
    s.fx.settings().setProvenanceTitles(true);
    s.tracker.setFocused(browser());
    QVERIFY(s.fx.history().add(QStringLiteral("clip"), s.service.sourceFor(QStringLiteral("clip"))).stored());
    QCOMPARE(s.fx.search().search(QStringLiteral("title:bold")).size(), 1);

    s.fx.settings().setProvenanceTitles(false);
    QVERIFY2(s.fx.search().search(QStringLiteral("title:bold")).isEmpty(),
        "an old title is still stored and searchable after titles were turned off");
}

void TestProvenanceAdversarial::titlesTurnedOffLeaveNoTitleOnSecretsInMemory()
{
    Stack s;
    s.fx.settings().setProvenanceTitles(true);
    s.tracker.setFocused(browser());
    QVERIFY(s.service.capture(QString::fromLatin1(kWebhook)).id < 0);
    QVERIFY(!s.fx.history().ephemeral().items().at(0).source.title.isEmpty());

    s.fx.settings().setProvenanceTitles(false);
    QVERIFY(s.fx.history().ephemeral().items().at(0).source.title.isEmpty());
    QCOMPARE(s.fx.history().ephemeral().items().at(0).source.app, browser().app);
}

void TestProvenanceAdversarial::urlPathIsNotOfferedForOpening()
{
    // the url is shown for reference and never handed to an opener
    Stack s;
    ClipboardItem item;
    item.id = 1;
    item.content = QStringLiteral("https://example.com/a");
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.source.app = QStringLiteral("org.mozilla.firefox");
    item.source.path = item.content;
    const ProvenanceDialog dialog(item, s.service);
    QPushButton* open = buttonNamed(dialog, QStringLiteral("Open source"));
    QVERIFY(open);
    QVERIFY2(!open->isEnabled(), "open source is enabled for a url path");
    QVERIFY(!s.service.openSource(item.source.path));

    item.source.path = QStringLiteral("/tmp");
    const ProvenanceDialog local(item, s.service);
    QVERIFY(buttonNamed(local, QStringLiteral("Open source"))->isEnabled());
    QVERIFY(!ProvenanceService::canOpen(QStringLiteral("file:///tmp")));
    QVERIFY(!ProvenanceService::canOpen(QStringLiteral("relative/path")));
}

void TestProvenanceAdversarial::dialogShowsTitlesAsPlainText()
{
    Stack s;
    ClipboardItem item;
    item.id = 1;
    item.content = QStringLiteral("clip");
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.source.app = QStringLiteral("org.mozilla.firefox");
    item.source.title = browser().title;
    ProvenanceDialog dialog(item, s.service);
    bool found = false;
    for (const QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text() != item.source.title)
            continue;
        found = true;
        QVERIFY2(label->textFormat() == Qt::PlainText, "a window title is rendered as rich text");
    }
    QVERIFY(found);
}

void TestProvenanceAdversarial::appQueryWithQuoteRoundTrips()
{
    const QString app = QStringLiteral("My \"Odd\" App");
    QCOMPARE(SearchService::parse(SearchService::appQuery(app), QDateTime::currentDateTime()).app, app);
}

void TestProvenanceAdversarial::dstDayIsTwentyThreeHours()
{
    const QTimeZone zone("America/New_York");
    QVERIFY(zone.isValid());
    const QDateTime now(QDate(2026, 3, 8), QTime(0, 30), zone);
    // parse works in the local zone so feed it the same wall clock moment
    const SearchFilter today = SearchService::parse(QStringLiteral("date:today"), now);
    QVERIFY(today.since && today.until);
    QVERIFY(*today.since <= now && now < *today.until);
    const SearchFilter week = SearchService::parse(QStringLiteral("date:week"), now);
    QCOMPARE(week.until, today.until);
    QVERIFY(week.since->daysTo(*week.until) == 7);
}

void TestProvenanceAdversarial::likeWildcardInAppIsLiteral()
{
    test::Fixture fx;
    Provenance a;
    a.app = QStringLiteral("abc");
    Provenance b;
    b.app = QStringLiteral("a_c");
    QVERIFY(fx.history().add(QStringLiteral("one"), a).stored());
    QVERIFY(fx.history().add(QStringLiteral("two"), b).stored());
    QCOMPARE(fx.search().search(QStringLiteral("app:a_c")).size(), 1);
    QCOMPARE(fx.search().search(QStringLiteral("app:%")).size(), 0);
    QCOMPARE(fx.search().search(QStringLiteral("app:\\")).size(), 0);
}

void TestProvenanceAdversarial::injectionShapedQueryIsHarmless()
{
    test::Fixture fx;
    QVERIFY(fx.history().add(QStringLiteral("keep me")).stored());
    const QString query
        = QStringLiteral("app:\"x') OR 1=1; DROP TABLE clipboard_items; --\" title:' date:today");
    QVERIFY(fx.search().search(query).isEmpty());
    QCOMPARE(fx.search().search(QStringLiteral("keep")).size(), 1);
}

void TestProvenanceAdversarial::activateUsesTheLiveWindowId()
{
    Stack s;
    s.tracker.setFocused(browser());
    const auto outcome = s.service.capture(QStringLiteral("clip"));
    QVERIFY(outcome.stored());
    QVERIFY(s.service.canActivate(outcome.id));

    // focus moves on so the id must come from capture time and not from now
    platform::WindowInfo other;
    other.windowClass = QStringLiteral("kitty");
    other.windowId = 9;
    s.tracker.setFocused(other);

    const auto item = s.fx.repository().findById(outcome.id);
    QVERIFY(item);
    ClipboardItem shown = *item;
    const ProvenanceDialog dialog(shown, s.service);
    QPushButton* activate = buttonNamed(dialog, QStringLiteral("Activate window"));
    QVERIFY(activate);
    QVERIFY(activate->isEnabled());
    activate->click();
    QCOMPARE(s.tracker.activated().windowId, browser().windowId);
    QCOMPARE(s.tracker.activated().windowClass, browser().windowClass);
}

void TestProvenanceAdversarial::sourceWithoutLiveWindowIsNotOffered()
{
    Stack s;
    s.tracker.setFocused(browser());
    // added straight to history as a clip from an earlier run would be
    const auto outcome
        = s.fx.history().add(QStringLiteral("old"), s.service.sourceFor(QStringLiteral("old")));
    QVERIFY(outcome.stored());
    QVERIFY(!s.service.canActivate(outcome.id));
    const auto item = s.fx.repository().findById(outcome.id);
    QVERIFY(item);
    QVERIFY(!s.service.activateSource(outcome.id, item->source));
    QCOMPARE(s.tracker.activated().windowId, quint64(0));
    QVERIFY(!s.tracker.activated().isValid());

    const ProvenanceDialog dialog(*item, s.service);
    QPushButton* activate = buttonNamed(dialog, QStringLiteral("Activate window"));
    QVERIFY(activate);
    QVERIFY(!activate->isEnabled());

    // a window without an id is not offered either and clearing forgets live ids
    platform::WindowInfo noId = browser();
    noId.windowId = 0;
    s.tracker.setFocused(noId);
    QVERIFY(!s.service.canActivate(s.service.capture(QStringLiteral("fresh")).id));
    s.tracker.setFocused(browser());
    const auto live = s.service.capture(QStringLiteral("live"));
    QVERIFY(s.service.canActivate(live.id));
    QVERIFY(s.service.clearProvenance());
    QVERIFY(!s.service.canActivate(live.id));
}

void TestProvenanceAdversarial::secretsInMemoryFollowSearchPrefixes()
{
    ClipboardItem secret;
    secret.id = -2;
    secret.content = QStringLiteral("ghp_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    secret.secretKind = QStringLiteral("github_token");
    secret.createdAt = QDateTime::currentDateTimeUtc();
    secret.source.app = QStringLiteral("org.mozilla.firefox");
    secret.source.windowClass = QStringLiteral("firefox");
    secret.source.title = QStringLiteral("Settings");

    const QDateTime now = QDateTime::currentDateTime();
    const auto matches = [&](const char* query) {
        return secretMatches(secret, SearchService::parse(QString::fromLatin1(query), now));
    };
    QVERIFY(matches(""));
    QVERIFY(matches("github"));
    QVERIFY2(matches("app:firefox"), "an app prefix hides a secret from that app");
    QVERIFY(matches("app:FIREFOX github token"));
    QVERIFY(matches("date:today"));
    QVERIFY(matches("title:sett"));
    QVERIFY(matches("app: date:"));
    QVERIFY(!matches("app:slack"));
    QVERIFY(!matches("app:firefox aws"));
    QVERIFY(!matches("date:yesterday"));
    QVERIFY(!matches("title:inbox"));
    // the text itself is never probed
    QVERIFY(!matches("ghp_"));
    QVERIFY(!matches("app:firefox ghp_"));
}

void TestProvenanceAdversarial::secretCopyBackDoesNotBlockSavedCopyBack()
{
    Stack s;
    s.tracker.setFocused(browser());
    const auto secret = s.service.capture(QString::fromLatin1(kWebhook));
    QVERIFY(secret.id < 0);
    const auto saved = s.service.capture(QStringLiteral("plain"));
    QVERIFY(saved.id > 0);

    platform::WindowInfo terminal;
    terminal.windowClass = QStringLiteral("kitty");
    s.service.noteCopiedBack(secret.id);
    s.tracker.setFocused(terminal);
    QVERIFY(s.fx.repository().findById(saved.id)->copiedInto.isEmpty());

    s.tracker.setFocused(browser());
    s.service.noteCopiedBack(saved.id);
    s.tracker.setFocused(terminal);
    QCOMPARE(s.fx.repository().findById(saved.id)->copiedInto, QStringLiteral("kitty"));
}

void TestProvenanceAdversarial::appChipStaysClearOfPinMarker()
{
    ClipboardItem item;
    item.id = 1;
    item.content = QStringLiteral("clip");
    item.pinned = true;
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.source.app = QStringLiteral("org.example.AVeryLongApplicationIdentifier");
    ClipboardListModel model;
    model.setItems({ item });
    const QModelIndex index = model.index(0, 0);

    ClipboardItemDelegate delegate;
    QSignalSpy clicked(&delegate, &ClipboardItemDelegate::appChipClicked);
    QStyleOptionViewItem option;
    option.font = QApplication::font();
    const auto clickAt = [&](int x, int y) {
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(x, y), QPointF(x, y), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        delegate.editorEvent(&release, &model, option, index);
    };

    // the star sits in the last kPadding plus 18 pixels of the row which is inset by 4
    option.rect = QRect(0, 0, 110, delegate.sizeHint(option, index).height());
    const int starRight = option.rect.right() - 4 - 10;
    for (int x = starRight - 18; x <= starRight; ++x) {
        for (int y = option.rect.top(); y <= option.rect.bottom(); ++y)
            clickAt(x, y);
    }
    QVERIFY2(clicked.isEmpty(), "the app chip runs under the pin marker");

    // a wide row still has a clickable chip next to the badge
    option.rect = QRect(0, 0, 600, delegate.sizeHint(option, index).height());
    for (int y = option.rect.top(); y <= option.rect.bottom() && clicked.isEmpty(); ++y)
        clickAt(4 + 10 + 28 + 12 + 2, y);
    QCOMPARE(clicked.size(), 1);
    QCOMPARE(clicked.at(0).at(0).toString(), item.source.app);
}

QTEST_MAIN(TestProvenanceAdversarial)
#include "test_provenance_adversarial.moc"
