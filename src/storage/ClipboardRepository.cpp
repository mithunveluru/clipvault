#include "ClipboardRepository.h"

#include "../core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace clipvault {
namespace {

constexpr auto kColumns = "id, content, type, pinned, created_at, expires_at, content_hash";

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
        update.prepare(QStringLiteral(
            "UPDATE clipboard_items SET created_at = ?, expires_at = ?, type = ? WHERE id = ?"));
        update.addBindValue(createdAt);
        update.addBindValue(expiresAt);
        update.addBindValue(toString(item.type));
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
        "INSERT INTO clipboard_items (content, type, pinned, created_at, expires_at, content_hash)"
        " VALUES (?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(item.content);
    insert.addBindValue(toString(item.type));
    insert.addBindValue(item.pinned ? 1 : 0);
    insert.addBindValue(createdAt);
    insert.addBindValue(expiresAt);
    insert.addBindValue(item.contentHash);
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
    const QString trimmed = query.trimmed();
    QSqlQuery sql(m_db);
    if (trimmed.isEmpty()) {
        sql.prepare(QStringLiteral("SELECT %1 FROM clipboard_items"
                                   " ORDER BY pinned DESC, created_at DESC LIMIT ?")
                        .arg(QLatin1String(kColumns)));
    } else {
        // LIKE is case insensitive for ASCII
        sql.prepare(QStringLiteral("SELECT %1 FROM clipboard_items"
                                   " WHERE content LIKE ? ESCAPE '\\'"
                                   " ORDER BY pinned DESC, created_at DESC LIMIT ?")
                        .arg(QLatin1String(kColumns)));
        sql.addBindValue(QLatin1Char('%') + escapeLike(trimmed) + QLatin1Char('%'));
    }
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

bool ClipboardRepository::remove(qint64 id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM clipboard_items WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec())
        return reportFailure(query, "remove");
    return query.numRowsAffected() > 0;
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
