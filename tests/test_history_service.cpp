#include "TestSupport.h"

#include <QSignalSpy>
#include <QtTest>

using namespace clipvault;
using clipvault::test::Fixture;
using AddStatus = HistoryService::AddStatus;

class TestHistoryService : public QObject {
    Q_OBJECT

private slots:
    void adds_an_item_and_classifies_it();
    void ignores_empty_and_whitespace_content();
    void rejects_oversized_content();
    void duplicate_content_does_not_create_a_second_entry();
    void duplicate_content_refreshes_recency();
    void duplicate_content_keeps_the_pin();
    void pin_and_unpin();
    void delete_removes_the_entry();
    void clear_all_variants();
    void retention_sweep_removes_expired_unpinned_items();
    void retention_change_reapplies_to_existing_items();
    void history_limit_trims_oldest_first();
    void history_limit_keeps_every_pinned_item();
    void emits_history_changed_on_write();
};

void TestHistoryService::adds_an_item_and_classifies_it()
{
    Fixture fixture;
    QVERIFY2(fixture.isOpen(), qPrintable(fixture.openError()));

    const auto outcome = fixture.history().add(QStringLiteral("https://github.com"));
    QCOMPARE(outcome.status, AddStatus::Inserted);
    QVERIFY(outcome.id > 0);

    const auto stored = fixture.repository().findById(outcome.id);
    QVERIFY(stored.has_value());
    QCOMPARE(toString(stored->type), QStringLiteral("URL"));
    QCOMPARE(stored->contentHash, HistoryService::hashOf(QStringLiteral("https://github.com")));
    QCOMPARE(fixture.history().count(), 1);
}

void TestHistoryService::ignores_empty_and_whitespace_content()
{
    Fixture fixture;
    QCOMPARE(fixture.history().add(QString()).status, AddStatus::IgnoredEmpty);
    QCOMPARE(fixture.history().add(QStringLiteral("   \n\t  ")).status, AddStatus::IgnoredEmpty);
    QCOMPARE(fixture.history().count(), 0);
}

void TestHistoryService::rejects_oversized_content()
{
    Fixture fixture;
    QSignalSpy rejected(&fixture.history(), &HistoryService::captureRejected);

    const QString huge(HistoryService::kMaxContentChars + 1, QLatin1Char('x'));
    QCOMPARE(fixture.history().add(huge).status, AddStatus::RejectedTooLarge);
    QCOMPARE(fixture.history().count(), 0);
    QCOMPARE(rejected.count(), 1);

    // The boundary itself is still accepted
    const QString atLimit(HistoryService::kMaxContentChars, QLatin1Char('y'));
    QCOMPARE(fixture.history().add(atLimit).status, AddStatus::Inserted);
}

void TestHistoryService::duplicate_content_does_not_create_a_second_entry()
{
    Fixture fixture;
    const auto first = fixture.history().add(QStringLiteral("hello world"));
    const auto second = fixture.history().add(QStringLiteral("hello world"));

    QCOMPARE(first.status, AddStatus::Inserted);
    QCOMPARE(second.status, AddStatus::Refreshed);
    QCOMPARE(first.id, second.id);
    QCOMPARE(fixture.history().count(), 1);
}

void TestHistoryService::duplicate_content_refreshes_recency()
{
    Fixture fixture;
    // Seeded directly so the original timestamp is old
    const QDateTime old = QDateTime::currentDateTimeUtc().addSecs(-7200);
    const qint64 id = fixture.insertRaw(QStringLiteral("recycled"), old);

    const auto outcome = fixture.history().add(QStringLiteral("recycled"));
    QCOMPARE(outcome.status, AddStatus::Refreshed);
    QCOMPARE(outcome.id, id);

    const auto stored = fixture.repository().findById(id);
    QVERIFY(stored->createdAt > old);
    QCOMPARE(fixture.history().count(), 1);
}

void TestHistoryService::duplicate_content_keeps_the_pin()
{
    Fixture fixture;
    const auto first = fixture.history().add(QStringLiteral("keep me"));
    QVERIFY(fixture.history().setPinned(first.id, true));

    QCOMPARE(fixture.history().add(QStringLiteral("keep me")).status, AddStatus::Refreshed);
    QVERIFY(fixture.repository().findById(first.id)->pinned);
}

void TestHistoryService::pin_and_unpin()
{
    Fixture fixture;
    const auto item = fixture.history().add(QStringLiteral("pin target"));

    QVERIFY(fixture.history().setPinned(item.id, true));
    QVERIFY(fixture.repository().findById(item.id)->pinned);
    QVERIFY(fixture.history().setPinned(item.id, false));
    QVERIFY(!fixture.repository().findById(item.id)->pinned);
}

void TestHistoryService::delete_removes_the_entry()
{
    Fixture fixture;
    const auto item = fixture.history().add(QStringLiteral("temporary"));

    QVERIFY(fixture.history().remove(item.id));
    QCOMPARE(fixture.history().count(), 0);
    QVERIFY(!fixture.history().remove(item.id));
}

void TestHistoryService::clear_all_variants()
{
    Fixture fixture;
    fixture.history().add(QStringLiteral("ordinary entry"));
    const auto pinned = fixture.history().add(QStringLiteral("pinned entry"));
    QVERIFY(fixture.history().setPinned(pinned.id, true));

    QVERIFY(fixture.history().clearAll(/*includePinned=*/false));
    QCOMPARE(fixture.history().count(), 1);

    QVERIFY(fixture.history().clearAll(/*includePinned=*/true));
    QCOMPARE(fixture.history().count(), 0);
}

void TestHistoryService::retention_sweep_removes_expired_unpinned_items()
{
    Fixture fixture;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    fixture.insertRaw(QStringLiteral("stale"), now.addSecs(-100), false, now.addSecs(-10));
    const qint64 pinned = fixture.insertRaw(QStringLiteral("stale but pinned"), now.addSecs(-100),
                                            true, now.addSecs(-10));
    const qint64 fresh = fixture.insertRaw(QStringLiteral("fresh"), now, false, now.addSecs(600));

    QCOMPARE(fixture.history().runMaintenance(), 1);
    QCOMPARE(fixture.history().count(), 2);
    QVERIFY(fixture.repository().findById(pinned).has_value());
    QVERIFY(fixture.repository().findById(fresh).has_value());
}

void TestHistoryService::retention_change_reapplies_to_existing_items()
{
    Fixture fixture;
    const QDateTime old = QDateTime::currentDateTimeUtc().addSecs(-7200);
    fixture.insertRaw(QStringLiteral("two hours old"), old);
    const qint64 pinned = fixture.insertRaw(QStringLiteral("two hours old, pinned"),
                                            old.addSecs(1), /*pinned=*/true);
    QCOMPARE(fixture.history().count(), 2);

    // Shortening retention must retire the unpinned entry
    fixture.settings().setRetentionSeconds(3600);
    fixture.history().applyRetentionChange();

    QCOMPARE(fixture.history().count(), 1);
    QVERIFY(fixture.repository().findById(pinned).has_value());

    // Never clears stored expiry again
    fixture.settings().setRetentionSeconds(0);
    fixture.history().applyRetentionChange();
    QVERIFY(!fixture.repository().findById(pinned)->expiresAt.has_value());
}

void TestHistoryService::history_limit_trims_oldest_first()
{
    Fixture fixture;
    fixture.settings().setMaxItems(10);

    for (int i = 0; i < 15; ++i)
        fixture.history().add(QStringLiteral("entry %1").arg(i));

    QCOMPARE(fixture.history().count(), 10);
    QVERIFY(!fixture.repository().findByHash(HistoryService::hashOf(QStringLiteral("entry 0")))
                 .has_value());
    QVERIFY(fixture.repository().findByHash(HistoryService::hashOf(QStringLiteral("entry 14")))
                .has_value());
}

void TestHistoryService::history_limit_keeps_every_pinned_item()
{
    Fixture fixture;
    // Limit raised while seeding then lowered as a user would
    fixture.settings().setMaxItems(1000);

    QList<qint64> pinnedIds;
    for (int i = 0; i < 12; ++i) {
        const auto item = fixture.history().add(QStringLiteral("pinned %1").arg(i));
        QVERIFY(fixture.history().setPinned(item.id, true));
        pinnedIds << item.id;
    }
    for (int i = 0; i < 5; ++i)
        fixture.history().add(QStringLiteral("loose %1").arg(i));
    QCOMPARE(fixture.history().count(), 17);

    fixture.settings().setMaxItems(10);
    fixture.history().runMaintenance();

    // Pinned entries survive even above the configured limit
    for (const qint64 id : pinnedIds)
        QVERIFY(fixture.repository().findById(id).has_value());
    QCOMPARE(fixture.repository().pinnedCount(), 12);
    QCOMPARE(fixture.history().count(), 12);
}

void TestHistoryService::emits_history_changed_on_write()
{
    Fixture fixture;
    QSignalSpy changed(&fixture.history(), &HistoryService::historyChanged);

    const auto item = fixture.history().add(QStringLiteral("signal me"));
    QCOMPARE(changed.count(), 1);
    fixture.history().setPinned(item.id, true);
    QCOMPARE(changed.count(), 2);
    fixture.history().remove(item.id);
    QCOMPARE(changed.count(), 3);
}

QTEST_GUILESS_MAIN(TestHistoryService)
#include "test_history_service.moc"
