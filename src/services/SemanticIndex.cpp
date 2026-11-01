#include "SemanticIndex.h"

#include "../core/Logging.h"
#include "../storage/ClipboardRepository.h"
#include "../storage/EmbeddingRepository.h"
#include "OnnxEmbeddingProvider.h"
#include "SearchService.h"

#include <algorithm>

namespace clipvault {
namespace {

// short enough that a burst of copies costs one pass
constexpr int kSyncDelayMs = 50;

float dot(const Embedding& a, const Embedding& b)
{
    float sum = 0.0f;
    const qsizetype size = std::min(a.size(), b.size());
    for (qsizetype i = 0; i < size; ++i)
        sum += a[i] * b[i];
    return sum;
}

} // namespace

SemanticIndex::SemanticIndex(
    EmbeddingRepository& embeddings, const ClipboardRepository& items, QObject* parent)
    : QObject(parent)
    , m_embeddings(embeddings)
    , m_items(items)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(kSyncDelayMs);
    connect(&m_syncTimer, &QTimer::timeout, this, &SemanticIndex::sync);
}

SemanticIndex::~SemanticIndex() = default;

bool SemanticIndex::isCompiledIn()
{
#ifdef CLIPVAULT_SEMANTIC
    return true;
#else
    return false;
#endif
}

void SemanticIndex::configure(bool enabled, const QString& modelDir)
{
#ifdef CLIPVAULT_SEMANTIC
    if (!enabled) {
        stop();
        return;
    }
    if (modelDir.isEmpty()) {
        stop();
        setStatus(Status::FilesMissing);
        return;
    }
    if (modelDir == m_modelDir && (m_status == Status::Verifying || m_status == Status::Ready))
        return;
    start([modelDir](ProviderFailure* failure) -> std::unique_ptr<EmbeddingProvider> {
        auto provider = std::make_unique<OnnxEmbeddingProvider>(modelDir);
        if (provider->failure() != ProviderFailure::None) {
            *failure = provider->failure();
            return nullptr;
        }
        return provider;
    });
    m_modelDir = modelDir;
#else
    Q_UNUSED(enabled);
    Q_UNUSED(modelDir);
    stop();
#endif
}

void SemanticIndex::start(Factory factory)
{
    stop();
    m_engine = std::make_unique<EmbeddingEngine>();
    connect(m_engine.get(), &EmbeddingEngine::loaded, this, &SemanticIndex::onLoaded);
    connect(m_engine.get(), &EmbeddingEngine::embedded, this, &SemanticIndex::onEmbedded);
    setStatus(Status::Verifying);
    m_engine->load(std::move(factory));
}

void SemanticIndex::stop()
{
    m_syncTimer.stop();
    m_engine.reset();
    m_vectors.clear();
    m_modelDir.clear();
    m_modelId.clear();
    m_dimension = 0;
    m_backfillJob = 0;
    m_batchIds.clear();
    m_batchHashes.clear();
    m_queryJob = 0;
    m_queryText.clear();
    setStatus(Status::Off);
}

void SemanticIndex::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(status);
}

void SemanticIndex::onLoaded(int failure, const QString& modelId, int dimension)
{
    if (failure != int(ProviderFailure::None) || dimension <= 0) {
        switch (static_cast<ProviderFailure>(failure)) {
        case ProviderFailure::FilesMissing: setStatus(Status::FilesMissing); break;
        case ProviderFailure::HashMismatch: setStatus(Status::HashMismatch); break;
        default: setStatus(Status::Failed); break;
        }
        // the engine is the sender so it is released once this slot has returned
        QTimer::singleShot(0, this, [this] {
            if (m_status != Status::Verifying && m_status != Status::Ready)
                m_engine.reset();
        });
        return;
    }

    m_modelId = modelId;
    m_dimension = dimension;
    m_embeddings.removeSecrets();
    for (const EmbeddingRepository::Stored& stored : m_embeddings.loadAll(m_modelId, m_dimension))
        m_vectors.insert(stored.itemId, stored.vector);
    qCInfo(lcApp) << "semantic index ready with" << m_vectors.size() << "stored vectors";
    setStatus(Status::Ready);
    sync();
}

void SemanticIndex::scheduleSync()
{
    if (isReady())
        m_syncTimer.start();
}

void SemanticIndex::sync()
{
    if (!isReady())
        return;
    // a row pinned as a secret after it was embedded loses its vector here
    m_embeddings.removeSecrets();
    const QSet<qint64> live = m_embeddings.itemIds(m_modelId);
    for (auto it = m_vectors.begin(); it != m_vectors.end();) {
        if (live.contains(it.key()))
            ++it;
        else
            it = m_vectors.erase(it);
    }
    pumpBackfill();
}

void SemanticIndex::pumpBackfill()
{
    if (!isReady() || m_backfillJob != 0)
        return;
    const QList<EmbeddingRepository::Pending> pending
        = m_embeddings.pending(m_modelId, kBatchSize, kMaxChars);
    if (pending.isEmpty()) {
        emit backfillIdle();
        return;
    }
    QStringList texts;
    for (const EmbeddingRepository::Pending& item : pending) {
        m_batchIds << item.itemId;
        m_batchHashes << item.contentHash;
        texts << item.text;
    }
    m_backfillJob = m_engine->embed(texts);
}

void SemanticIndex::query(const QString& text)
{
    if (!isReady() || text.trimmed().isEmpty())
        return;
    m_queryText = text;
    m_queryJob = m_engine->embed({ text.left(kMaxChars) });
}

QList<SemanticIndex::Hit> SemanticIndex::rank(const Embedding& query, int limit) const
{
    QList<Hit> hits;
    for (auto it = m_vectors.constBegin(); it != m_vectors.constEnd(); ++it) {
        const float score = dot(query, it.value());
        if (score >= semantic_model::kMinSimilarity)
            hits.append({ it.key(), score });
    }
    // ties fall back to the newer item so the order is stable
    const auto better
        = [](const Hit& a, const Hit& b) { return a.score != b.score ? a.score > b.score : a.id > b.id; };
    if (hits.size() > limit) {
        std::partial_sort(hits.begin(), hits.begin() + limit, hits.end(), better);
        hits.resize(limit);
    } else {
        std::sort(hits.begin(), hits.end(), better);
    }
    return hits;
}

void SemanticIndex::onEmbedded(quint64 job, const QList<Embedding>& vectors)
{
    if (job == m_queryJob) {
        m_queryJob = 0;
        if (vectors.size() != 1)
            return;
        QList<qint64> ids;
        for (const Hit& hit : rank(vectors.front(), SearchService::kDefaultLimit))
            ids << hit.id;
        emit queryReady(m_queryText, ids);
        return;
    }
    if (job != m_backfillJob)
        return;

    m_backfillJob = 0;
    const QList<qint64> ids = std::exchange(m_batchIds, { });
    const QStringList hashes = std::exchange(m_batchHashes, { });
    if (vectors.size() != ids.size()) {
        // retried on the next history change rather than spinning on a bad batch
        qCWarning(lcApp) << "semantic backfill batch of" << ids.size() << "failed";
        return;
    }
    int stored = 0;
    int skipped = 0;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        // the row may have been deleted or pinned as a secret while the worker ran
        const std::optional<ClipboardItem> item = m_items.findById(ids[i]);
        if (!item || item->contentHash != hashes[i] || !item->secretKind.isEmpty()) {
            ++skipped;
            continue;
        }
        if (vectors[i].size() == m_dimension
            && m_embeddings.upsert(ids[i], m_modelId, hashes[i], vectors[i])) {
            m_vectors.insert(ids[i], vectors[i]);
            ++stored;
        }
    }
    // a batch where nothing could be written would come straight back so wait for the next sync
    if (stored + skipped < ids.size() && stored == 0) {
        qCWarning(lcApp) << "semantic backfill could not store a batch of" << ids.size();
        return;
    }
    pumpBackfill();
}

} // namespace clipvault
