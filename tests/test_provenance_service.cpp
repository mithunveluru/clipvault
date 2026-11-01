#include "FakeFocusTracker.h"
#include "TestSupport.h"
#include "services/ProvenanceService.h"

#include <QSignalSpy>
#include <QTest>

using namespace clipvault;

class TestProvenanceService : public QObject {
    Q_OBJECT

private slots:
    void recordsAppClassAndResolvedPath();
    void disabledRecordsNothing();
    void titlesOnlyWhenEnabled();
    void blocklistMatchesAppOrClassIgnoringCase();
    void selfWindowIsNeverTheSource();
    void unknownWindowRecordsNothing();
    void singleUrlClipIsThePath();
    void otherTextNeverGuessesAUrl();
    void copiedIntoRecordsNextForeignWindow();
    void copiedIntoRespectsPolicy();
    void clearProvenanceForgetsEverything();
};

namespace {

platform::WindowInfo editor()
{
    platform::WindowInfo window;
    window.app = QStringLiteral("org.gnome.TextEditor");
    window.windowClass = QStringLiteral("gnome-text-editor");
    window.title = QStringLiteral("notes.txt");
    window.windowId = 7;
    return window;
}

platform::WindowInfo self()
{
    platform::WindowInfo window;
    window.app = QStringLiteral("clipvault");
    window.windowClass = QStringLiteral("clipvault");
    window.windowId = 99;
    return window;
}

struct Stack {
    test::Fixture fx;
    test::FakeFocusTracker tracker;
    ProvenanceService service { tracker, fx.settings(), fx.history(),
        [](const platform::WindowInfo&) { return QStringLiteral("/home/u/project"); } };
};

} // namespace

void TestProvenanceService::recordsAppClassAndResolvedPath()
{
    Stack s;
    s.tracker.setFocused(editor());
    const Provenance source = s.service.sourceFor(QStringLiteral("some text"));
    QCOMPARE(source.app, QStringLiteral("org.gnome.TextEditor"));
    QCOMPARE(source.windowClass, QStringLiteral("gnome-text-editor"));
    QCOMPARE(source.path, QStringLiteral("/home/u/project"));
}

void TestProvenanceService::disabledRecordsNothing()
{
    Stack s;
    s.fx.settings().setProvenanceEnabled(false);
    s.tracker.setFocused(editor());
    QVERIFY(s.service.sourceFor(QStringLiteral("https://example.com")).isEmpty());
}

void TestProvenanceService::titlesOnlyWhenEnabled()
{
    Stack s;
    s.tracker.setFocused(editor());
    QVERIFY(s.service.sourceFor(QStringLiteral("x")).title.isEmpty());
    s.fx.settings().setProvenanceTitles(true);
    QCOMPARE(s.service.sourceFor(QStringLiteral("x")).title, QStringLiteral("notes.txt"));
}

void TestProvenanceService::blocklistMatchesAppOrClassIgnoringCase()
{
    Stack s;
    platform::WindowInfo vault;
    vault.app = QStringLiteral("org.keepassxc.KeePassXC");
    s.tracker.setFocused(vault);
    QVERIFY(s.service.sourceFor(QStringLiteral("hunter2")).isEmpty());

    platform::WindowInfo byClass;
    byClass.windowClass = QStringLiteral("Bitwarden");
    s.tracker.setFocused(byClass);
    QVERIFY(s.service.sourceFor(QStringLiteral("hunter2")).isEmpty());

    s.fx.settings().setProvenanceBlocklist({ QStringLiteral("TEXTEDITOR") });
    s.tracker.setFocused(editor());
    QVERIFY(s.service.sourceFor(QStringLiteral("x")).isEmpty());
}

void TestProvenanceService::selfWindowIsNeverTheSource()
{
    Stack s;
    s.tracker.setFocused(editor());
    s.tracker.setFocused(self(), false);
    QCOMPARE(s.service.sourceFor(QStringLiteral("x")).app, QStringLiteral("org.gnome.TextEditor"));
}

void TestProvenanceService::unknownWindowRecordsNothing()
{
    Stack s;
    QVERIFY(s.service.sourceFor(QStringLiteral("https://example.com")).isEmpty());
}

void TestProvenanceService::singleUrlClipIsThePath()
{
    Stack s;
    s.tracker.setFocused(editor());
    QCOMPARE(s.service.sourceFor(QStringLiteral("  https://example.com/a?b=1 ")).path,
        QStringLiteral("https://example.com/a?b=1"));
    QCOMPARE(
        s.service.sourceFor(QStringLiteral("HTTP://example.com")).path, QStringLiteral("HTTP://example.com"));
}

void TestProvenanceService::otherTextNeverGuessesAUrl()
{
    for (const QString& text : { QStringLiteral("see https://example.com now"), QStringLiteral("example.com"),
             QStringLiteral("ftp://example.com"), QStringLiteral("https://"),
             QStringLiteral("https://a.com\nhttps://b.com"), QStringLiteral("file:///etc/passwd") })
        QVERIFY2(ProvenanceService::urlPathOf(text).isEmpty(), qPrintable(text));

    // no url so the resolver decides
    Stack s;
    s.tracker.setFocused(editor());
    QCOMPARE(s.service.sourceFor(QStringLiteral("example.com")).path, QStringLiteral("/home/u/project"));
}

void TestProvenanceService::copiedIntoRecordsNextForeignWindow()
{
    Stack s;
    s.tracker.setFocused(editor());
    const auto outcome
        = s.fx.history().add(QStringLiteral("clip"), s.service.sourceFor(QStringLiteral("clip")));
    QVERIFY(outcome.stored());

    s.tracker.setFocused(self(), false);
    s.service.noteCopiedBack(outcome.id);
    // focus moving within clipvault records nothing
    s.tracker.setFocused(self(), false);
    QVERIFY(s.fx.repository().findById(outcome.id)->copiedInto.isEmpty());

    platform::WindowInfo terminal;
    terminal.windowClass = QStringLiteral("kitty");
    s.tracker.setFocused(terminal);
    QCOMPARE(s.fx.repository().findById(outcome.id)->copiedInto, QStringLiteral("kitty"));

    // only the first foreign window after the copy counts
    s.tracker.setFocused(editor());
    QCOMPARE(s.fx.repository().findById(outcome.id)->copiedInto, QStringLiteral("kitty"));
}

void TestProvenanceService::copiedIntoRespectsPolicy()
{
    Stack s;
    const qint64 id = s.fx.history().add(QStringLiteral("clip")).id;

    platform::WindowInfo vault;
    vault.app = QStringLiteral("keepassxc");
    s.service.noteCopiedBack(id);
    s.tracker.setFocused(vault);
    QVERIFY(s.fx.repository().findById(id)->copiedInto.isEmpty());

    s.fx.settings().setProvenanceEnabled(false);
    s.service.noteCopiedBack(id);
    s.tracker.setFocused(editor());
    QVERIFY(s.fx.repository().findById(id)->copiedInto.isEmpty());
}

void TestProvenanceService::clearProvenanceForgetsEverything()
{
    Stack s;
    s.fx.settings().setProvenanceTitles(true);
    s.tracker.setFocused(editor());
    const qint64 id
        = s.fx.history().add(QStringLiteral("clip"), s.service.sourceFor(QStringLiteral("clip"))).id;
    QVERIFY(s.fx.repository().setCopiedInto(id, QStringLiteral("kitty")));

    QSignalSpy changed(&s.fx.history(), &HistoryService::historyChanged);
    QVERIFY(s.service.clearProvenance());
    QCOMPARE(changed.count(), 1);
    const auto item = s.fx.repository().findById(id);
    QVERIFY(item->source.isEmpty());
    QVERIFY(item->copiedInto.isEmpty());
    QCOMPARE(item->content, QStringLiteral("clip"));
}

QTEST_GUILESS_MAIN(TestProvenanceService)
#include "test_provenance_service.moc"
