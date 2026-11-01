#pragma once

#include "../core/EmbeddingProvider.h"

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>

namespace clipvault {

// clip embeddings sql where rows die with their item through the foreign key cascade
class EmbeddingRepository {
public:
    explicit EmbeddingRepository(const QSqlDatabase& db);

    struct Stored {
        qint64 itemId = -1;
        Embedding vector;
    };

    struct Pending {
        qint64 itemId = -1;
        QString contentHash;
        // only the leading characters since the tokenizer keeps far fewer
        QString text;
    };

    // the vector is stored as little endian float32
    bool upsert(qint64 itemId, const QString& model, const QString& contentHash, const Embedding& vector);

    // fresh vectors for this model and never for a secret row
    [[nodiscard]] QList<Stored> loadAll(const QString& model, int dim) const;
    [[nodiscard]] QSet<qint64> itemIds(const QString& model) const;

    // non secret items with no vector or one from another model or older content newest first
    [[nodiscard]] QList<Pending> pending(const QString& model, int limit, int maxChars) const;

    bool remove(qint64 itemId);
    bool removeAll();
    // returns rows removed
    int removeSecrets();

    [[nodiscard]] static QByteArray toBlob(const Embedding& vector);
    // empty when the blob is not dim floats long
    [[nodiscard]] static Embedding fromBlob(const QByteArray& blob, int dim);

private:
    QSqlDatabase m_db;
};

} // namespace clipvault
