#pragma once

#include "../core/EmbeddingProvider.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace clipvault {

class WordPieceTokenizer;

// the one upstream export the app accepts unless a caller passes other hashes
namespace semantic_model {
inline constexpr const char* kModelFile = "model.onnx";
inline constexpr const char* kVocabFile = "vocab.txt";
inline constexpr const char* kRepository = "sentence-transformers/all-MiniLM-L6-v2";
inline constexpr const char* kRevision = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41";
inline constexpr const char* kUpstreamModelPath = "onnx/model.onnx";
inline constexpr const char* kUpstreamVocabPath = "vocab.txt";
inline constexpr const char* kModelSha256
    = "6fd5d72fe4589f189f8ebc006442dbb529bb7ce38f8082112682524616046452";
inline constexpr const char* kVocabSha256
    = "07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3";
inline constexpr int kDimension = 384;
// tuned on tests fixtures semantic relevance json and explained in docs semantic search md
inline constexpr float kMinSimilarity = 0.35f;
} // namespace semantic_model

// minilm sentence embeddings through onnx runtime and only compiled when semantic search is on
class OnnxEmbeddingProvider final : public EmbeddingProvider {
public:
    // lowercase hex sha256
    struct ExpectedHashes {
        QByteArray model;
        QByteArray vocab;
    };

    [[nodiscard]] static ExpectedHashes pinnedHashes();

    // hashes both files before any session exists and refuses on mismatch
    explicit OnnxEmbeddingProvider(const QString& modelDir, const ExpectedHashes& expected = pinnedHashes());
    ~OnnxEmbeddingProvider() override;

    OnnxEmbeddingProvider(const OnnxEmbeddingProvider&) = delete;
    OnnxEmbeddingProvider& operator=(const OnnxEmbeddingProvider&) = delete;

    [[nodiscard]] ProviderFailure failure() const { return m_failure; }
    // content and path free so it may be logged and shown
    [[nodiscard]] QString failureReason() const { return m_failureReason; }

    [[nodiscard]] QString modelId() const override { return m_modelId; }
    [[nodiscard]] int dimension() const override { return semantic_model::kDimension; }
    [[nodiscard]] QList<Embedding> embed(const QStringList& texts) override;

private:
    struct Runtime;

    void fail(ProviderFailure failure, QString reason);

    std::unique_ptr<WordPieceTokenizer> m_tokenizer;
    std::unique_ptr<Runtime> m_runtime;
    ProviderFailure m_failure = ProviderFailure::None;
    QString m_failureReason;
    QString m_modelId;
};

} // namespace clipvault
