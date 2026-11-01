#include "core/SecureZero.h"
#include "services/EphemeralStore.h"

#include <QSignalSpy>
#include <QTest>

using namespace clipvault;
using namespace std::chrono_literals;

namespace {

const QString kSecret = QStringLiteral("gh"
                                       "p_SyntheticFakeToken0000000000000000001");
const QString kOther = QStringLiteral("gl"
                                      "pat-SyntheticFake000000001");

} // namespace

class TestEphemeralStore : public QObject {
    Q_OBJECT

private slots:
    void addsWithNegativeIdsNewestFirst()
    {
        EphemeralStore store;
        QSignalSpy changed(&store, &EphemeralStore::changed);
        const auto first = store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 10s);
        const auto second = store.add(kOther, QStringLiteral("gitlab_token"), ClipboardType::Text, { }, 10s);
        QVERIFY(first.inserted && second.inserted);
        QVERIFY(first.id < -1);
        QVERIFY(second.id < -1);
        QVERIFY(first.id != second.id);
        QCOMPARE(changed.count(), 2);

        const QList<ClipboardItem> items = store.items();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).id, second.id);
        QCOMPARE(items.at(0).secretKind, QStringLiteral("gitlab_token"));
        QVERIFY(!items.at(0).pinned);
        QVERIFY(items.at(0).expiresAt.has_value());
    }

    void identicalTextRefreshesInsteadOfDuplicating()
    {
        EphemeralStore store;
        const auto first = store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 1s);
        store.add(kOther, QStringLiteral("gitlab_token"), ClipboardType::Text, { }, 10s);
        const auto again = store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 30s);
        QVERIFY(!again.inserted);
        QCOMPARE(again.id, first.id);
        QCOMPARE(store.count(), 2);
        QCOMPARE(store.items().first().id, first.id);
        QVERIFY(store.remainingSeconds(first.id) > 20);
    }

    void remainingSecondsCountsDown()
    {
        EphemeralStore store;
        const auto added = store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 45s);
        QCOMPARE(store.remainingSeconds(added.id), 45);
        QCOMPARE(store.remainingSeconds(-999), -1);
    }

    void expiresAfterTtlAndWipesTheOnlyCopy()
    {
        EphemeralStore store;
        QStringList wiped;
        bool wasDetached = false;
        bool emptiedAfter = false;
        store.setWiper([&](QString& text) {
            wiped << QString(text.constData(), text.size());
            // an unshared buffer means secure zero overwrites it rather than just dropping a reference
            wasDetached = text.isDetached();
            secureZero(text);
            emptiedAfter = text.isEmpty();
        });

        const QString copied = kSecret;
        const auto added = store.add(copied, QStringLiteral("github_token"), ClipboardType::Text, { }, 80ms);
        QVERIFY(store.contains(added.id));

        QSignalSpy changed(&store, &EphemeralStore::changed);
        QTRY_VERIFY_WITH_TIMEOUT(!store.contains(added.id), 2000);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(wiped, QStringList { kSecret });
        QVERIFY(wasDetached);
        QVERIFY(emptiedAfter);
        // the caller copy is untouched because the store kept its own buffer
        QCOMPARE(copied, kSecret);
    }

    void expiryAsksToClearTheClipboardWithTheText()
    {
        EphemeralStore store;
        QStringList cleared;
        store.setClipboardClearer([&](const QString& text) { cleared << text; });
        store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 50ms);
        const auto later = store.add(kOther, QStringLiteral("gitlab_token"), ClipboardType::Text, { }, 10s);
        QTRY_COMPARE_WITH_TIMEOUT(store.count(), 1, 2000);
        QCOMPARE(cleared, QStringList { kSecret });
        QVERIFY(store.contains(later.id));
    }

    void manualRemoveWipesButLeavesTheClipboard()
    {
        EphemeralStore store;
        int wipes = 0;
        int clears = 0;
        store.setWiper([&](QString& text) {
            ++wipes;
            text = QString();
        });
        store.setClipboardClearer([&](const QString&) { ++clears; });
        const auto added = store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 10s);
        QVERIFY(store.remove(added.id));
        QVERIFY(!store.remove(added.id));
        QCOMPARE(wipes, 1);
        QCOMPARE(clears, 0);
    }

    void takeHandsOverWithoutWiping()
    {
        EphemeralStore store;
        int wipes = 0;
        store.setWiper([&](QString& text) {
            ++wipes;
            text = QString();
        });
        const auto added = store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 10s);
        const auto taken = store.take(added.id);
        QVERIFY(taken.has_value());
        QCOMPARE(taken->content, kSecret);
        QCOMPARE(taken->secretKind, QStringLiteral("github_token"));
        QCOMPARE(store.count(), 0);
        QCOMPARE(wipes, 0);
    }

    void clearAndDestructionWipeEverything()
    {
        int wipes = 0;
        {
            EphemeralStore store;
            store.setWiper([&](QString& text) {
                ++wipes;
                text = QString();
            });
            store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 10s);
            store.add(kOther, QStringLiteral("gitlab_token"), ClipboardType::Text, { }, 10s);
            store.clear();
            QCOMPARE(store.count(), 0);
            QCOMPARE(wipes, 2);
            store.add(kSecret, QStringLiteral("github_token"), ClipboardType::Text, { }, 10s);
        }
        QCOMPARE(wipes, 3);
    }
};

QTEST_GUILESS_MAIN(TestEphemeralStore)
#include "test_ephemeral_store.moc"
