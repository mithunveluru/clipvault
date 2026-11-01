#include "storage/Database.h"

#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using clipvault::Database;

namespace {

QString uniqueName()
{
    return QStringLiteral("migration-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
}

int scalar(const QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql) || !query.next())
        return -1;
    return query.value(0).toInt();
}

} // namespace

class TestMigration : public QObject {
    Q_OBJECT

private slots:
    void upgradesV1PreservingHistory();
    void reopenIsIdempotent();
    void freshDatabaseHasCurrentSchema();
    void embeddingsCascadeOnDelete();

private:
    QString copyFixture(QTemporaryDir& dir)
    {
        const QString target = dir.filePath(QStringLiteral("history.db"));
        QFile::copy(QStringLiteral(CLIPVAULT_FIXTURES "/v1.db"), target);
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
        return target;
    }
};

void TestMigration::upgradesV1PreservingHistory()
{
    QTemporaryDir dir;
    const QString path = copyFixture(dir);
    Database database(path, uniqueName());
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QSqlDatabase db = database.handle();

    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")), Database::kSchemaVersion);
    QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM clipboard_items")), 4);
    QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM settings")), 3);

    QSqlQuery rows(db);
    QVERIFY(rows.exec(QStringLiteral(
        "SELECT content, pinned, created_at, expires_at, secret_kind, source_app, source_class,"
        " source_title, source_path, copied_into FROM clipboard_items ORDER BY id")));
    QVERIFY(rows.next());
    QCOMPARE(rows.value(0).toString(), QStringLiteral("https://example.com/a?b=1"));
    QCOMPARE(rows.value(2).toLongLong(), 1700000000);
    QCOMPARE(rows.value(3).toLongLong(), 1700604800);
    for (int column = 4; column <= 9; ++column)
        QVERIFY(rows.value(column).isNull());
    QVERIFY(rows.next());
    QCOMPARE(rows.value(0).toString(), QString::fromUtf8("pinned note with ünïcödé ✓"));
    QCOMPARE(rows.value(1).toInt(), 1);
    QVERIFY(rows.value(3).isNull());
    QVERIFY(rows.next());
    // whitespace survives because content is stored exactly as copied
    QCOMPARE(rows.value(0).toString(), QStringLiteral("  leading and trailing spaces kept  \n"));

    QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'clip_embeddings'")), 1);
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA secure_delete")), 1);
    const QFileDevice::Permissions perms = QFile::permissions(path);
    QVERIFY(perms.testFlag(QFileDevice::ReadOwner) && perms.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                       | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
}

void TestMigration::reopenIsIdempotent()
{
    QTemporaryDir dir;
    const QString path = copyFixture(dir);
    const QString name = uniqueName();
    {
        Database first(path, name);
        QString error;
        QVERIFY2(first.open(&error), qPrintable(error));
    }
    Database second(path, name);
    QString error;
    QVERIFY2(second.open(&error), qPrintable(error));
    QCOMPARE(scalar(second.handle(), QStringLiteral("SELECT count(*) FROM clipboard_items")), 4);
}

void TestMigration::freshDatabaseHasCurrentSchema()
{
    QTemporaryDir dir;
    Database database(dir.filePath(QStringLiteral("fresh.db")), uniqueName());
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    QCOMPARE(scalar(database.handle(), QStringLiteral("PRAGMA user_version")), Database::kSchemaVersion);
    QSqlQuery query(database.handle());
    QVERIFY(query.exec(QStringLiteral("SELECT secret_kind, source_app, copied_into FROM clipboard_items")));
}

void TestMigration::embeddingsCascadeOnDelete()
{
    QTemporaryDir dir;
    const QString path = copyFixture(dir);
    Database database(path, uniqueName());
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    QSqlQuery query(database.handle());
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO clip_embeddings (item_id, model, dim, content_hash, vector) VALUES (1, 'm', 1, 'h', x'00')")));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM clipboard_items WHERE id = 1")));
    QCOMPARE(scalar(database.handle(), QStringLiteral("SELECT count(*) FROM clip_embeddings")), 0);
}

QTEST_GUILESS_MAIN(TestMigration)
#include "test_migration.moc"
