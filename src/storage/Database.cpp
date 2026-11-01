#include "Database.h"

#include "../core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace clipvault {
namespace {

bool exec(QSqlDatabase& db, const QString& sql, QString* error)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;
    if (error)
        *error = query.lastError().text();
    qCCritical(lcDb) << "statement failed:" << query.lastError().text();
    return false;
}

// Clipboard history holds credentials so keep it owner only
bool restrictToOwner(const QString& path, bool directory)
{
    if (!QFile::exists(path))
        return true;
    QFileDevice::Permissions permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (directory)
        permissions |= QFileDevice::ExeOwner;
    if (QFile::setPermissions(path, permissions))
        return true;
    qCWarning(lcDb) << "could not restrict permissions on" << path;
    return false;
}

int userVersion(QSqlDatabase& db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

} // namespace

Database::Database(QString path, QString connectionName)
    : m_path(std::move(path))
    , m_connectionName(std::move(connectionName))
{
}

Database::~Database()
{
    close();
}

QString Database::defaultPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.local/share/ClipVault");
    return dir + QStringLiteral("/clipvault.db");
}

bool Database::open(QString* error)
{
    if (m_open)
        return true;

    const QFileInfo info(m_path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create the data directory:\n%1").arg(info.absolutePath());
        return false;
    }
    restrictToOwner(info.absolutePath(), true);

    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
        ? QSqlDatabase::database(m_connectionName, /*open=*/false)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);

    if (!db.isValid()) {
        if (error)
            *error = QStringLiteral("The Qt SQLite driver is not available. "
                                    "Install the Qt SQL SQLite plugin and try again.");
        return false;
    }

    db.setDatabaseName(m_path);
    if (!db.open()) {
        if (error)
            *error = QStringLiteral("Could not open the clipboard database:\n%1").arg(db.lastError().text());
        qCCritical(lcDb) << "open failed:" << db.lastError().text();
        return false;
    }

    // Tuning rationale in the design notes
    QString ignored;
    exec(db, QStringLiteral("PRAGMA journal_mode = WAL"), &ignored);
    exec(db, QStringLiteral("PRAGMA synchronous = NORMAL"), &ignored);
    exec(db, QStringLiteral("PRAGMA busy_timeout = 3000"), &ignored);
    exec(db, QStringLiteral("PRAGMA foreign_keys = ON"), &ignored);
    // deleted rows are overwritten so removed clips do not linger in free pages
    exec(db, QStringLiteral("PRAGMA secure_delete = ON"), &ignored);

    // Reapplied every start so existing installations are repaired
    restrictToOwner(m_path, false);
    restrictToOwner(m_path + QStringLiteral("-wal"), false);
    restrictToOwner(m_path + QStringLiteral("-shm"), false);

    m_open = true;
    if (!migrate(error)) {
        db.close();
        m_open = false;
        return false;
    }

    qCInfo(lcDb) << "database initialized, schema version" << kSchemaVersion;
    return true;
}

bool Database::migrate(QString* error)
{
    QSqlDatabase db = handle();
    const int version = userVersion(db);
    if (version < 0) {
        if (error)
            *error = QStringLiteral("The clipboard database appears to be corrupted "
                                    "and could not be read.");
        return false;
    }
    if (version > kSchemaVersion) {
        if (error)
            *error = QStringLiteral("This database was written by a newer version of "
                                    "ClipVault (schema %1) and cannot be opened.").arg(version);
        return false;
    }
    if (version == kSchemaVersion)
        return true;

    if (!db.transaction()) {
        if (error)
            *error = QStringLiteral("Could not start the schema migration.");
        return false;
    }

    auto fail = [&](QString message) {
        db.rollback();
        if (error)
            *error = std::move(message);
        return false;
    };

    if (version < 1) {
        static const char* const kV1[] = {
            "CREATE TABLE clipboard_items ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  content TEXT NOT NULL,"
            "  type TEXT NOT NULL,"
            "  pinned INTEGER NOT NULL DEFAULT 0,"
            "  created_at INTEGER NOT NULL,"
            "  expires_at INTEGER,"
            "  content_hash TEXT NOT NULL UNIQUE)",
            // Matches the ordering every query uses
            "CREATE INDEX idx_items_order ON clipboard_items(pinned DESC, created_at DESC)",
            // Retention only ever sweeps unpinned rows
            "CREATE INDEX idx_items_expiry ON clipboard_items(expires_at) WHERE pinned = 0",
            "CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
        };
        for (const char* sql : kV1) {
            QString message;
            if (!exec(db, QString::fromLatin1(sql), &message))
                return fail(QStringLiteral("Schema migration failed:\n%1").arg(message));
        }
    }

    if (version < 2) {
        // secret and provenance columns stay null for rows written before v2
        static const char* const kV2[] = {
            "ALTER TABLE clipboard_items ADD COLUMN secret_kind TEXT",
            "ALTER TABLE clipboard_items ADD COLUMN source_app TEXT",
            "ALTER TABLE clipboard_items ADD COLUMN source_class TEXT",
            "ALTER TABLE clipboard_items ADD COLUMN source_title TEXT",
            "ALTER TABLE clipboard_items ADD COLUMN source_path TEXT",
            "ALTER TABLE clipboard_items ADD COLUMN copied_into TEXT",
        };
        for (const char* sql : kV2) {
            QString message;
            if (!exec(db, QString::fromLatin1(sql), &message))
                return fail(QStringLiteral("Schema migration failed:\n%1").arg(message));
        }
    }

    if (version < 3) {
        // created even when semantic search is compiled out so the schema never forks
        static const char* const kV3[] = {
            "CREATE TABLE clip_embeddings ("
            "  item_id INTEGER PRIMARY KEY REFERENCES clipboard_items(id) ON DELETE CASCADE,"
            "  model TEXT NOT NULL,"
            "  dim INTEGER NOT NULL,"
            "  content_hash TEXT NOT NULL,"
            "  vector BLOB NOT NULL)",
        };
        for (const char* sql : kV3) {
            QString message;
            if (!exec(db, QString::fromLatin1(sql), &message))
                return fail(QStringLiteral("Schema migration failed:\n%1").arg(message));
        }
    }

    QString message;
    if (!exec(db, QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion), &message))
        return fail(QStringLiteral("Schema migration failed:\n%1").arg(message));

    if (!db.commit())
        return fail(QStringLiteral("Could not commit the schema migration."));

    qCInfo(lcDb) << "migrated schema from version" << version << "to" << kSchemaVersion;
    return true;
}

void Database::close()
{
    if (m_open) {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, /*open=*/false);
        if (db.isValid() && db.isOpen()) {
            // Fold the WAL back into the main file
            QSqlQuery(db).exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
            db.close();
        }
        m_open = false;
    }
    // The handle copy must leave scope before removal
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::isOpen() const
{
    return m_open;
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connectionName, /*open=*/false);
}

} // namespace clipvault
