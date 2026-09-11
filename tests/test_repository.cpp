#include "TestSupport.h"

#include <QFile>
#include <QtTest>

using namespace clipvault;
using clipvault::test::Fixture;

class TestRepository : public QObject {
    Q_OBJECT

private slots:
    void initialises_schema();
    void database_file_is_readable_only_by_the_owner();
    void inserts_and_retrieves();
    void duplicate_hash_refreshes_instead_of_inserting();
    void duplicate_hash_preserves_pinned_state();
    void updates_pinned_flag();
    void deletes_a_row();
    void clear_all_can_keep_pinned();
    void deletes_expired_but_never_pinned();
    void enforces_history_limit_oldest_first();
    void enforces_limit_never_deletes_pinned();
    void survives_reopen();
    void search_escapes_like_wildcards();
};

void TestRepository::initialises_schema()
{
    Fixture fixture;
    QVERIFY2(fixture.isOpen(), qPrintable(fixture.openError()));
    QCOMPARE(fixture.repository().count(), 0);
}

void TestRepository::database_file_is_readable_only_by_the_owner()
{
    Fixture fixture;
    QVERIFY(fixture.isOpen());

    // History holds credentials so only the owner may read
    const auto forbidden = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

    for (const QString& suffix : { QString(), QStringLiteral("-wal"), QStringLiteral("-shm") }) {
        const QString path = fixture.databasePath() + suffix;
        if (!QFile::exists(path))
            continue;
        QVERIFY2(!(QFile::permissions(path) & forbidden), qPrintable(path));
    }
}

void TestRepository::inserts_and_retrieves()
{
    Fixture fixture;
    QVERIFY(fixture.isOpen());

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 id = fixture.insertRaw(QStringLiteral("hello world"), now);
    QVERIFY(id > 0);

    const auto stored = fixture.repository().findById(id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->content, QStringLiteral("hello world"));
    QCOMPARE(stored->pinned, false);
    QCOMPARE(stored->createdAt.toSecsSinceEpoch(), now.toSecsSinceEpoch());
    QCOMPARE(fixture.repository().count(), 1);
}

void TestRepository::duplicate_hash_refreshes_instead_of_inserting()
{
    Fixture fixture;
    QVERIFY(fixture.isOpen());

    const QDateTime old = QDateTime::currentDateTimeUtc().addSecs(-3600);
    const qint64 first = fixture.insertRaw(QStringLiteral("same text"), old);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 second = fixture.insertRaw(QStringLiteral("same text"), now);

    QCOMPARE(first, second);
    QCOMPARE(fixture.repository().count(), 1);
    QCOMPARE(fixture.repository().findById(first)->createdAt.toSecsSinceEpoch(),
             now.toSecsSinceEpoch());
}

void TestRepository::duplicate_hash_preserves_pinned_state()
{
    Fixture fixture;
    QVERIFY(fixture.isOpen());

    const qint64 id = fixture.insertRaw(QStringLiteral("pin me"),
                                        QDateTime::currentDateTimeUtc().addSecs(-60));
    QVERIFY(fixture.repository().setPinned(id, true));

    fixture.insertRaw(QStringLiteral("pin me"), QDateTime::currentDateTimeUtc());

    const auto stored = fixture.repository().findById(id);
    QVERIFY(stored.has_value());
    QVERIFY2(stored->pinned, "re-copying a pinned entry must not unpin it");
    QCOMPARE(fixture.repository().count(), 1);
}

void TestRepository::updates_pinned_flag()
{
    Fixture fixture;
    const qint64 id = fixture.insertRaw(QStringLiteral("entry"), QDateTime::currentDateTimeUtc());

    QVERIFY(fixture.repository().setPinned(id, true));
    QVERIFY(fixture.repository().findById(id)->pinned);
    QVERIFY(fixture.repository().setPinned(id, false));
    QVERIFY(!fixture.repository().findById(id)->pinned);
    QVERIFY2(!fixture.repository().setPinned(999999, true), "unknown id must report no update");
}

void TestRepository::deletes_a_row()
{
    Fixture fixture;
    const qint64 id = fixture.insertRaw(QStringLiteral("gone soon"), QDateTime::currentDateTimeUtc());

    QVERIFY(fixture.repository().remove(id));
    QVERIFY(!fixture.repository().findById(id).has_value());
    QCOMPARE(fixture.repository().count(), 0);
    QVERIFY(!fixture.repository().remove(id));
}

void TestRepository::clear_all_can_keep_pinned()
{
    Fixture fixture;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    fixture.insertRaw(QStringLiteral("ordinary"), now);
    const qint64 pinned = fixture.insertRaw(QStringLiteral("important"), now, /*pinned=*/true);

    QVERIFY(fixture.repository().clearAll(/*includePinned=*/false));
    QCOMPARE(fixture.repository().count(), 1);
    QVERIFY(fixture.repository().findById(pinned).has_value());

    QVERIFY(fixture.repository().clearAll(/*includePinned=*/true));
    QCOMPARE(fixture.repository().count(), 0);
}

void TestRepository::deletes_expired_but_never_pinned()
{
    Fixture fixture;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime past = now.addSecs(-10);

    fixture.insertRaw(QStringLiteral("expired"), now.addSecs(-100), false, past);
    const qint64 pinned = fixture.insertRaw(QStringLiteral("expired but pinned"),
                                            now.addSecs(-100), true, past);
    const qint64 fresh = fixture.insertRaw(QStringLiteral("still fresh"), now, false,
                                           now.addSecs(3600));
    const qint64 permanent = fixture.insertRaw(QStringLiteral("never expires"), now);

    QCOMPARE(fixture.repository().deleteExpired(now), 1);
    QCOMPARE(fixture.repository().count(), 3);
    QVERIFY(fixture.repository().findById(pinned).has_value());
    QVERIFY(fixture.repository().findById(fresh).has_value());
    QVERIFY(fixture.repository().findById(permanent).has_value());
}

void TestRepository::enforces_history_limit_oldest_first()
{
    Fixture fixture;
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(-1000);
    for (int i = 0; i < 10; ++i)
        fixture.insertRaw(QStringLiteral("entry %1").arg(i), base.addSecs(i));

    QCOMPARE(fixture.repository().enforceLimit(4), 6);
    QCOMPARE(fixture.repository().count(), 4);

    const auto remaining = fixture.repository().search(QString(), 100);
    QCOMPARE(remaining.size(), 4);
    QCOMPARE(remaining.first().content, QStringLiteral("entry 9"));
    QCOMPARE(remaining.last().content, QStringLiteral("entry 6"));
}

void TestRepository::enforces_limit_never_deletes_pinned()
{
    Fixture fixture;
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(-1000);
    for (int i = 0; i < 5; ++i)
        fixture.insertRaw(QStringLiteral("pinned %1").arg(i), base.addSecs(i), /*pinned=*/true);
    for (int i = 0; i < 3; ++i)
        fixture.insertRaw(QStringLiteral("loose %1").arg(i), base.addSecs(100 + i));

    // Limit below the pinned count leaves only pinned rows
    QCOMPARE(fixture.repository().enforceLimit(2), 3);
    QCOMPARE(fixture.repository().count(), 5);
    QCOMPARE(fixture.repository().pinnedCount(), 5);
}

void TestRepository::survives_reopen()
{
    Fixture fixture;
    const qint64 id = fixture.insertRaw(QStringLiteral("persist me"),
                                        QDateTime::currentDateTimeUtc(), /*pinned=*/true);
    QVERIFY(fixture.reopen());

    const auto stored = fixture.repository().findById(id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->content, QStringLiteral("persist me"));
    QVERIFY(stored->pinned);
}

void TestRepository::search_escapes_like_wildcards()
{
    Fixture fixture;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    fixture.insertRaw(QStringLiteral("discount 100% off"), now);
    fixture.insertRaw(QStringLiteral("plain entry"), now.addSecs(1));

    const auto results = fixture.repository().search(QStringLiteral("100%"), 50);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().content, QStringLiteral("discount 100% off"));
}

QTEST_GUILESS_MAIN(TestRepository)
#include "test_repository.moc"
