#include "EmbeddingRepository.h"

#include "../core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtEndian>

#include <cstring>

namespace clipvault {
namespace {

bool reportFailure(const QSqlQuery& query, const char* what)
{
    qCCritical(lcDb) << what << "failed:" << query.lastError().text();
    return false;
}

// a vector only counts when it belongs to this model and to the current content of a non secret row
constexpr auto kFresh = "FROM clip_embeddings e JOIN clipboard_items i ON i.id = e.item_id"
                        " WHERE e.model = ? AND e.content_hash = i.content_hash AND i.secret_kind IS NULL";

} // namespace

EmbeddingRepository::EmbeddingRepository(const QSqlDatabase& db)
    : m_db(db)
{
}

QByteArray EmbeddingRepository::toBlob(const Embedding& vector)
{
    QByteArray blob(vector.size() * qsizetype(sizeof(float)), Qt::Uninitialized);
    char* out = blob.data();
    for (const float value : vector) {
        quint32 bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        qToLittleEndian(bits, out);
        out += sizeof bits;
    }
    return blob;
}

Embedding EmbeddingRepository::fromBlob(const QByteArray& blob, int dim)
{
    if (dim <= 0 || blob.size() != dim * qsizetype(sizeof(float)))
        return { };
    Embedding vector(dim);
    const char* in = blob.constData();
    for (float& value : vector) {
        const quint32 bits = qFromLittleEndian<quint32>(in);
        std::memcpy(&value, &bits, sizeof value);
        in += sizeof bits;
    }
    return vector;
}

bool EmbeddingRepository::upsert(
    qint64 itemId, const QString& model, const QString& contentHash, const Embedding& vector)
{
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("INSERT OR REPLACE INTO clip_embeddings (item_id, model, dim, content_hash, vector)"
                       " VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(itemId);
    query.addBindValue(model);
    query.addBindValue(int(vector.size()));
    query.addBindValue(contentHash);
    query.addBindValue(toBlob(vector));
    if (!query.exec())
        return reportFailure(query, "embedding upsert");
    return true;
}

QList<EmbeddingRepository::Stored> EmbeddingRepository::loadAll(const QString& model, int dim) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT e.item_id, e.vector %1 AND e.dim = ?").arg(QLatin1String(kFresh)));
    query.addBindValue(model);
    query.addBindValue(dim);
    QList<Stored> out;
    if (!query.exec()) {
        reportFailure(query, "embedding load");
        return out;
    }
    while (query.next()) {
        Embedding vector = fromBlob(query.value(1).toByteArray(), dim);
        if (!vector.isEmpty())
            out.append({ query.value(0).toLongLong(), std::move(vector) });
    }
    return out;
}

QSet<qint64> EmbeddingRepository::itemIds(const QString& model) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT e.item_id %1").arg(QLatin1String(kFresh)));
    query.addBindValue(model);
    QSet<qint64> out;
    if (!query.exec()) {
        reportFailure(query, "embedding ids");
        return out;
    }
    while (query.next())
        out.insert(query.value(0).toLongLong());
    return out;
}

QList<EmbeddingRepository::Pending> EmbeddingRepository::pending(
    const QString& model, int limit, int maxChars) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT i.id, i.content_hash, substr(i.content, 1, ?) FROM clipboard_items i"
                                 " LEFT JOIN clip_embeddings e ON e.item_id = i.id"
                                 " WHERE i.secret_kind IS NULL AND (e.item_id IS NULL OR e.model != ? OR "
                                 "e.content_hash != i.content_hash)"
                                 " ORDER BY i.id DESC LIMIT ?"));
    query.addBindValue(maxChars);
    query.addBindValue(model);
    query.addBindValue(limit);
    QList<Pending> out;
    if (!query.exec()) {
        reportFailure(query, "embedding pending");
        return out;
    }
    while (query.next())
        out.append({ query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString() });
    return out;
}

bool EmbeddingRepository::remove(qint64 itemId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM clip_embeddings WHERE item_id = ?"));
    query.addBindValue(itemId);
    if (!query.exec())
        return reportFailure(query, "embedding remove");
    return query.numRowsAffected() > 0;
}

bool EmbeddingRepository::removeAll()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM clip_embeddings")))
        return reportFailure(query, "embedding clear");
    return true;
}

int EmbeddingRepository::removeSecrets()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM clip_embeddings WHERE item_id IN"
                                   " (SELECT id FROM clipboard_items WHERE secret_kind IS NOT NULL)"))) {
        reportFailure(query, "embedding secret purge");
        return 0;
    }
    return query.numRowsAffected();
}

} // namespace clipvault
