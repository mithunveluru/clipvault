#pragma once

#include "../core/EmbeddingProvider.h"
#include "EmbeddingEngine.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

namespace clipvault {

class ClipboardRepository;
class EmbeddingRepository;

// main thread owner of the in memory vectors that never embeds a secret
class SemanticIndex : public QObject {
    Q_OBJECT
public:
    enum class Status {
        Off,
        Verifying,
        Ready,
        FilesMissing,
        HashMismatch,
        Failed,
    };
    Q_ENUM(Status)

    struct Hit {
        qint64 id = -1;
        float score = 0.0f;
    };

    using Factory = EmbeddingEngine::Factory;

    SemanticIndex(
        EmbeddingRepository& embeddings, const ClipboardRepository& items, QObject* parent = nullptr);
    ~SemanticIndex() override;

    SemanticIndex(const SemanticIndex&) = delete;
    SemanticIndex& operator=(const SemanticIndex&) = delete;

    // applies the two settings and stays off when the build has no semantic search
    void configure(bool enabled, const QString& modelDir);
    // the worker thread exists only between start and stop
    void start(Factory factory);
    void stop();

    [[nodiscard]] Status status() const { return m_status; }
    [[nodiscard]] bool isReady() const { return m_status == Status::Ready; }
    [[nodiscard]] int indexedCount() const { return static_cast<int>(m_vectors.size()); }

    // answered at most once through queryReady and only for the latest text
    void query(const QString& text);
    [[nodiscard]] QList<Hit> rank(const Embedding& query, int limit) const;

    // coalesces history changes into one reconcile and backfill pass
    void scheduleSync();

    [[nodiscard]] static bool isCompiledIn();

    static constexpr int kBatchSize = 8;
    // the tokenizer keeps 126 pieces so longer text is never read
    static constexpr int kMaxChars = 4000;

signals:
    void statusChanged(clipvault::SemanticIndex::Status status);
    // ids above the minimum similarity best first
    void queryReady(const QString& text, const QList<qint64>& ids);
    // nothing is left to embed
    void backfillIdle();

private:
    void setStatus(Status status);
    void onLoaded(int failure, const QString& modelId, int dimension);
    void onEmbedded(quint64 job, const QList<Embedding>& vectors);
    void sync();
    void pumpBackfill();

    EmbeddingRepository& m_embeddings;
    const ClipboardRepository& m_items;
    std::unique_ptr<EmbeddingEngine> m_engine;
    Status m_status = Status::Off;
    QString m_modelDir;
    QString m_modelId;
    int m_dimension = 0;
    QHash<qint64, Embedding> m_vectors;
    QTimer m_syncTimer;

    quint64 m_backfillJob = 0;
    QList<qint64> m_batchIds;
    QStringList m_batchHashes;
    quint64 m_queryJob = 0;
    QString m_queryText;
};

} // namespace clipvault
