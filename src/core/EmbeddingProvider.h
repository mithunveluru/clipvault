#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace clipvault {

using Embedding = QList<float>;

// why a provider could not be created and shown as the settings status
enum class ProviderFailure {
    None,
    FilesMissing,
    HashMismatch,
    Failed,
};

// turns text into unit length vectors and may block so it only ever runs on the embedding worker
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    // changes whenever vectors from two providers are not comparable
    [[nodiscard]] virtual QString modelId() const = 0;
    [[nodiscard]] virtual int dimension() const = 0;

    // one vector per text in order or an empty list on failure
    [[nodiscard]] virtual QList<Embedding> embed(const QStringList& texts) = 0;
};

} // namespace clipvault
