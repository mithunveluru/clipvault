#include "ClipboardRepository.h"

#include "../core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace clipvault {
namespace {

constexpr auto kColumns = "id, content, type, pinned, created_at, expires_at, content_hash,"
                          " secret_kind, source_app, source_class, source_title, source_path, copied_into";

ClipboardItem readRow(const QSqlQuery& query)
{
    ClipboardItem item;
    item.id = query.value(0).toLongLong();
    item.content = query.value(1).toString();
    item.type = clipboardTypeFromString(query.value(2).toString());
    item.pinned = query.value(3).toInt() != 0;
    item.createdAt = QDateTime::fromSecsSinceEpoch(query.value(4).toLongLong(), Qt::UTC);
    if (!query.value(5).isNull())
        item.expiresAt = QDateTime::fromSecsSinceEpoch(query.value(5).toLongLong(), Qt::UTC);
    item.contentHash = query.value(6).toString();
    item.secretKind = query.value(7).toString();
    item.source.app = query.value(8).toString();
    item.source.windowClass = query.value(9).toString();
    item.source.title = query.value(10).toString();
    item.source.path = query.value(11).toString();
    item.copiedInto = query.value(12).toString();
    return item;
}

// Wildcards in the query text must match literally
QString escapeLike(const QString& text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('%') || ch == QLatin1Char('_'))
            out += QLatin1Char('\\');
        out += ch;
    }
    return out;
}

QString likePattern(const QString& text)
{
    return QLatin1Char('%') + escapeLike(text) + QLatin1Char('%');
}

// the app title and date prefixes shared by search and findFiltered
void appendPrefixFilters(const SearchFilter& filter, QStringList& where, QVariantList& binds)
{
    const QString app = filter.app.trimmed();
    if (!app.isEmpty()) {
        where << QStringLiteral("(source_app LIKE ? ESCAPE '\\' OR source_class LIKE ? ESCAPE '\\')");
        binds << likePattern(app) << likePattern(app);
    }
    const QString title = filter.title.trimmed();
    if (!title.isEmpty()) {
        where << QStringLiteral("source_title LIKE ? ESCAPE '\\'");
        binds << likePattern(title);
    }
    if (filter.since) {
        where << QStringLiteral("created_at >= ?");
        binds << filter.since->toSecsSinceEpoch();
    }
    if (filter.until) {
        where << QStringLiteral("created_at < ?");
        binds << filter.until->toSecsSinceEpoch();
    }
}

// null keeps optional columns null rather than empty strings
QVariant orNull(const QString& text)
{
    return text.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(text);
}

bool reportFailure(const QSqlQuery& query, const char* what, QString* error = nullptr)
{
    const QString message = query.lastError().text();
    if (error)
        *error = message;
    qCCritical(lcDb) << what << "failed:" << message;
    return false;
}

} // namespace

ClipboardRepository::ClipboardRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

std::optional<ClipboardRepository::Stored> ClipboardRepository::upsert(const ClipboardItem& item, QString* error)
{
    const qint64 createdAt = item.createdAt.toSecsSinceEpoch();
    const QVariant expiresAt = item.expiresAt
        ? QVariant(item.expiresAt->toSecsSinceEpoch())
        : QVariant(QMetaType(QMetaType::LongLong));

    // Avoids RETURNING which older SQLite builds lack
    if (!m_db.transaction()) {
        if (error)
            *error = m_db.lastError().text();
        return std::nullopt;
    }

    auto rollback = [&](const QSqlQuery& query, const char* what) {
        reportFailure(query, what, error);
        m_db.rollback();
        return std::optional<Stored>{};
    };

    QSqlQuery lookup(m_db);
    lookup.prepare(QStringLiteral("SELECT id FROM clipboard_items WHERE content_hash = ?"));
    lookup.addBindValue(item.contentHash);
    if (!lookup.exec())
        return rollback(lookup, "hash lookup");

    if (lookup.next()) {
        const qint64 existingId = lookup.value(0).toLongLong();
        QSqlQuery update(m_db);
        // an empty source keeps the old one so a blocked app cannot wipe it
        update.prepare(QStringLiteral(
            "UPDATE clipboard_items SET created_at = ?, expires_at = ?, type = ?,"
            " source_app = CASE WHEN ? THEN ? ELSE source_app END,"
            " source_class = CASE WHEN ? THEN ? ELSE source_class END,"
            " source_title = CASE WHEN ? THEN ? ELSE source_title END,"
            " source_path = CASE WHEN ? THEN ? ELSE source_path END,"
            " secret_kind = COALESCE(?, secret_kind)"
            " WHERE id = ?"));
        update.addBindValue(createdAt);
        update.addBindValue(expiresAt);
        update.addBindValue(toString(item.type));
        const int replaceSource = item.source.isEmpty() ? 0 : 1;
        for (const QString* field : { &item.source.app, &item.source.windowClass, &item.source.title,
                                      &item.source.path }) {
            update.addBindValue(replaceSource);
            update.addBindValue(orNull(*field));
        }
        update.addBindValue(orNull(item.secretKind));
        update.addBindValue(existingId);
        if (!update.exec())
            return rollback(update, "recency update");
        if (!m_db.commit()) {
            m_db.rollback();
            return std::nullopt;
        }
        return Stored{ existingId, false };
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO clipboard_items (content, type, pinned, created_at, expires_at, content_hash,"
        " secret_kind, source_app, source_class, source_title, source_path)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(item.content);
    insert.addBindValue(toString(item.type));
    insert.addBindValue(item.pinned ? 1 : 0);
    insert.addBindValue(createdAt);
    insert.addBindValue(expiresAt);
    insert.addBindValue(item.contentHash);
    insert.addBindValue(orNull(item.secretKind));
    insert.addBindValue(orNull(item.source.app));
    insert.addBindValue(orNull(item.source.windowClass));
    insert.addBindValue(orNull(item.source.title));
    insert.addBindValue(orNull(item.source.path));
    if (!insert.exec())
        return rollback(insert, "insert");

    const qint64 newId = insert.lastInsertId().toLongLong();
    if (!m_db.commit()) {
        m_db.rollback();
        return std::nullopt;
    }
    return Stored{ newId, true };
}

std::optional<ClipboardItem> ClipboardRepository::findById(qint64 id) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT %1 FROM clipboard_items WHERE id = ?").arg(QLatin1String(kColumns)));
    query.addBindValue(id);
    if (!query.exec()) {
        reportFailure(query, "findById");
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return readRow(query);
}

std::optional<ClipboardItem> ClipboardRepository::findByHash(const QString& hash) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT %1 FROM clipboard_items WHERE content_hash = ?").arg(QLatin1String(kColumns)));
    query.addBindValue(hash);
    if (!query.exec()) {
        reportFailure(query, "findByHash");
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return readRow(query);
}

QList<ClipboardItem> ClipboardRepository::search(const QString& query, int limit) const
{
    SearchFilter filter;
    filter.text = query.trimmed();
    return search(filter, limit);
}

QList<ClipboardItem> ClipboardRepository::search(const SearchFilter& filter, int limit) const
{
    QStringList where;
    QVariantList binds;

    const QString text = filter.text.trimmed();
    if (!text.isEmpty()) {
        // LIKE is case insensitive for ASCII
        // pinned secrets stay masked so their text must not confirm a guess
        where << QStringLiteral("(secret_kind IS NULL AND content LIKE ? ESCAPE '\\')");
        binds << likePattern(text);
    }
    appendPrefixFilters(filter, where, binds);

    QString sqlText = QStringLiteral("SELECT %1 FROM clipboard_items").arg(QLatin1String(kColumns));
    if (!where.isEmpty())
        sqlText += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sqlText += QStringLiteral(" ORDER BY pinned DESC, created_at DESC LIMIT ?");

    QSqlQuery sql(m_db);
    sql.prepare(sqlText);
    for (const QVariant& value : binds)
        sql.addBindValue(value);
    sql.addBindValue(limit);

    QList<ClipboardItem> results;
    if (!sql.exec()) {
        reportFailure(sql, "search");
        return results;
    }
    while (sql.next())
        results.append(readRow(sql));
    return results;
}

QList<ClipboardItem> ClipboardRepository::findFiltered(
    const QList<qint64>& ids, const SearchFilter& filter) const
{
    QList<ClipboardItem> results;
    if (ids.isEmpty())
        return results;
    // pinned secrets stay masked so a meaning match must not surface them either
    QStringList where { QStringLiteral("secret_kind IS NULL") };
    QVariantList binds;
    appendPrefixFilters(filter, where, binds);
    QStringList marks;
    for (const qint64 id : ids) {
        marks << QStringLiteral("?");
        binds << id;
    }
    where << QStringLiteral("id IN (%1)").arg(marks.join(QLatin1Char(',')));

    QSqlQuery sql(m_db);
    sql.prepare(QStringLiteral("SELECT %1 FROM clipboard_items WHERE %2")
            .arg(QLatin1String(kColumns), where.join(QStringLiteral(" AND "))));
    for (const QVariant& value : binds)
        sql.addBindValue(value);
    if (!sql.exec()) {
        reportFailure(sql, "findFiltered");
        return results;
    }
    while (sql.next())
        results.append(readRow(sql));
    return results;
}

bool ClipboardRepository::setPinned(qint64 id, bool pinned)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE clipboard_items SET pinned = ? WHERE id = ?"));
    query.addBindValue(pinned ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec())
        return reportFailure(query, "setPinned");
    return query.numRowsAffected() > 0;
}

bool ClipboardRepository::setCopiedInto(qint64 id, const QString& app)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE clipboard_items SET copied_into = ? WHERE id = ?"));
    query.addBindValue(orNull(app));
    query.addBindValue(id);
    if (!query.exec())
        return reportFailure(query, "setCopiedInto");
    return query.numRowsAffected() > 0;
}

bool ClipboardRepository::clearProvenance()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("UPDATE clipboard_items SET source_app = NULL, source_class = NULL,"
                                   " source_title = NULL, source_path = NULL, copied_into = NULL")))
        return reportFailure(query, "clearProvenance");
    return true;
}

bool ClipboardRepository::clearSourceTitles()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("UPDATE clipboard_items SET source_title = NULL")))
        return reportFailure(query, "clearSourceTitles");
    return true;
}

bool ClipboardRepository::remove(qint64 id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM clipboard_items WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec())
        return reportFailure(query, "remove");
    return query.numRowsAffected() > 0;
}

bool ClipboardRepository::checkpoint()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")))
        return reportFailure(query, "checkpoint");
    // a busy checkpoint leaves deleted rows in the wal until the next one
    if (query.next() && query.value(0).toInt() != 0) {
        qCWarning(lcDb) << "wal checkpoint was busy";
        return false;
    }
    return true;
}

bool ClipboardRepository::clearAll(bool includePinned)
{
    QSqlQuery query(m_db);
    if (includePinned)
        query.prepare(QStringLiteral("DELETE FROM clipboard_items"));
    else
        query.prepare(QStringLiteral("DELETE FROM clipboard_items WHERE pinned = 0"));
    if (!query.exec())
        return reportFailure(query, "clearAll");
    return true;
}

int ClipboardRepository::deleteExpired(const QDateTime& now)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM clipboard_items"
                                 " WHERE pinned = 0 AND expires_at IS NOT NULL AND expires_at <= ?"));
    query.addBindValue(now.toSecsSinceEpoch());
    if (!query.exec()) {
        reportFailure(query, "deleteExpired");
        return 0;
    }
    return query.numRowsAffected();
}

int ClipboardRepository::enforceLimit(int maxItems)
{
    if (maxItems <= 0)
        return 0;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM clipboard_items WHERE id IN ("
        "  SELECT id FROM clipboard_items WHERE pinned = 0 ORDER BY created_at ASC, id ASC"
        "  LIMIT max(0, (SELECT count(*) FROM clipboard_items) - ?))"));
    query.addBindValue(maxItems);
    if (!query.exec()) {
        reportFailure(query, "enforceLimit");
        return 0;
    }
    return query.numRowsAffected();
}

bool ClipboardRepository::recomputeExpiry(int retentionSeconds)
{
    QSqlQuery query(m_db);
    if (retentionSeconds <= 0) {
        query.prepare(QStringLiteral("UPDATE clipboard_items SET expires_at = NULL"));
    } else {
        query.prepare(QStringLiteral("UPDATE clipboard_items SET expires_at = created_at + ?"));
        query.addBindValue(retentionSeconds);
    }
    if (!query.exec())
        return reportFailure(query, "recomputeExpiry");
    return true;
}

int ClipboardRepository::count() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT count(*) FROM clipboard_items")) || !query.next()) {
        reportFailure(query, "count");
        return 0;
    }
    return query.value(0).toInt();
}

int ClipboardRepository::pinnedCount() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT count(*) FROM clipboard_items WHERE pinned = 1")) || !query.next()) {
        reportFailure(query, "pinnedCount");
        return 0;
    }
    return query.value(0).toInt();
}

} // namespace clipvault
