#include "OnnxEmbeddingProvider.h"

#include "../core/Logging.h"
#include "../core/WordPieceTokenizer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace clipvault {
namespace {

enum class Input {
    Ids,
    Mask,
    TypeIds,
};

// reads the whole file once so the bytes that were hashed are the bytes that get loaded
bool readHashed(const QString& path, QByteArray* bytes, QByteArray* hexDigest)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    bytes->clear();
    bytes->reserve(file.size());
    constexpr qint64 kChunk = 1048576;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunk);
        if (chunk.isEmpty())
            return false;
        hash.addData(chunk);
        bytes->append(chunk);
    }
    *hexDigest = hash.result().toHex();
    return true;
}

} // namespace

struct OnnxEmbeddingProvider::Runtime {
    Ort::Env env { ORT_LOGGING_LEVEL_ERROR, "clipvault" };
    Ort::Session session { nullptr };
    std::vector<std::string> inputNames;
    std::vector<Input> inputs;
    std::string outputName;
};

OnnxEmbeddingProvider::ExpectedHashes OnnxEmbeddingProvider::pinnedHashes()
{
    return { QByteArray(semantic_model::kModelSha256), QByteArray(semantic_model::kVocabSha256) };
}

OnnxEmbeddingProvider::OnnxEmbeddingProvider(const QString& modelDir, const ExpectedHashes& expected)
{
    const QDir dir(modelDir);
    const QString modelPath = dir.filePath(QLatin1String(semantic_model::kModelFile));
    const QString vocabPath = dir.filePath(QLatin1String(semantic_model::kVocabFile));
    if (modelDir.isEmpty() || !QFile::exists(modelPath) || !QFile::exists(vocabPath)) {
        fail(ProviderFailure::FilesMissing,
            QStringLiteral("model.onnx or vocab.txt is missing from the folder"));
        return;
    }

    QByteArray vocab;
    QByteArray vocabDigest;
    if (!readHashed(vocabPath, &vocab, &vocabDigest)) {
        fail(ProviderFailure::FilesMissing, QStringLiteral("vocab.txt could not be read"));
        return;
    }
    if (vocabDigest != expected.vocab.toLower()) {
        fail(
            ProviderFailure::HashMismatch, QStringLiteral("vocab.txt sha256 does not match the pinned hash"));
        return;
    }

    QByteArray model;
    QByteArray modelDigest;
    if (!readHashed(modelPath, &model, &modelDigest)) {
        fail(ProviderFailure::FilesMissing, QStringLiteral("model.onnx could not be read"));
        return;
    }
    if (modelDigest != expected.model.toLower()) {
        fail(ProviderFailure::HashMismatch,
            QStringLiteral("model.onnx sha256 does not match the pinned hash"));
        return;
    }

    m_tokenizer = std::make_unique<WordPieceTokenizer>(vocab);
    if (!m_tokenizer->isValid()) {
        fail(ProviderFailure::Failed, QStringLiteral("vocab.txt lacks the special tokens"));
        return;
    }

    try {
        m_runtime = std::make_unique<Runtime>();
        // telemetry is already a no op on linux and this keeps it off if that ever changes
        m_runtime->env.DisableTelemetryEvents();

        Ort::SessionOptions options;
        // one thread each means the caller runs every kernel and onnx runtime starts no pools
        // so the embedding worker stays the only extra thread in the process
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ORT_SEQUENTIAL);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        options.AddConfigEntry("session.inter_op.allow_spinning", "0");

        m_runtime->session
            = Ort::Session(m_runtime->env, model.constData(), static_cast<size_t>(model.size()), options);
        model.clear();
        model.squeeze();

        const Ort::AllocatorWithDefaultOptions allocator;
        for (size_t i = 0; i < m_runtime->session.GetInputCount(); ++i) {
            const std::string name = m_runtime->session.GetInputNameAllocated(i, allocator).get();
            if (name == "input_ids")
                m_runtime->inputs.push_back(Input::Ids);
            else if (name == "attention_mask")
                m_runtime->inputs.push_back(Input::Mask);
            else if (name == "token_type_ids")
                m_runtime->inputs.push_back(Input::TypeIds);
            else {
                fail(ProviderFailure::Failed, QStringLiteral("model has an unexpected input"));
                return;
            }
            m_runtime->inputNames.push_back(name);
        }
        for (size_t i = 0; i < m_runtime->session.GetOutputCount(); ++i) {
            const std::string name = m_runtime->session.GetOutputNameAllocated(i, allocator).get();
            if (i == 0 || name == "last_hidden_state")
                m_runtime->outputName = name;
        }
    } catch (const Ort::Exception& error) {
        fail(ProviderFailure::Failed,
            QStringLiteral("onnx runtime refused the model, code %1").arg(error.GetOrtErrorCode()));
        return;
    }

    m_modelId = QStringLiteral("all-MiniLM-L6-v2/") + QString::fromLatin1(modelDigest.left(16));
}

OnnxEmbeddingProvider::~OnnxEmbeddingProvider() = default;

void OnnxEmbeddingProvider::fail(ProviderFailure failure, QString reason)
{
    m_failure = failure;
    m_failureReason = std::move(reason);
    m_runtime.reset();
    m_tokenizer.reset();
    qCWarning(lcApp) << "semantic model refused:" << m_failureReason;
}

QList<Embedding> OnnxEmbeddingProvider::embed(const QStringList& texts)
{
    if (m_failure != ProviderFailure::None || !m_runtime || texts.isEmpty())
        return { };
    // one sequence per run because padding a short clip to a long neighbour costs more than the extra calls
    QList<Embedding> out;
    out.reserve(texts.size());
    for (const QString& text : texts) {
        QList<Embedding> one = run({ text });
        if (one.size() != 1)
            return { };
        out << one.front();
    }
    return out;
}

QList<Embedding> OnnxEmbeddingProvider::run(const QStringList& texts)
{
    std::vector<std::vector<std::int64_t>> encoded;
    encoded.reserve(static_cast<size_t>(texts.size()));
    size_t length = 0;
    for (const QString& text : texts) {
        encoded.push_back(m_tokenizer->encode(text));
        length = std::max(length, encoded.back().size());
    }

    const size_t batch = encoded.size();
    std::vector<std::int64_t> ids(batch * length, 0);
    std::vector<std::int64_t> mask(batch * length, 0);
    std::vector<std::int64_t> typeIds(batch * length, 0);
    for (size_t row = 0; row < batch; ++row) {
        for (size_t col = 0; col < encoded[row].size(); ++col) {
            ids[row * length + col] = encoded[row][col];
            mask[row * length + col] = 1;
        }
    }

    try {
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<std::int64_t, 2> shape { static_cast<std::int64_t>(batch),
            static_cast<std::int64_t>(length) };
        std::vector<Ort::Value> values;
        std::vector<const char*> names;
        for (size_t i = 0; i < m_runtime->inputs.size(); ++i) {
            std::vector<std::int64_t>& source = m_runtime->inputs[i] == Input::Ids
                ? ids
                : (m_runtime->inputs[i] == Input::Mask ? mask : typeIds);
            values.push_back(Ort::Value::CreateTensor<std::int64_t>(
                memory, source.data(), source.size(), shape.data(), shape.size()));
            names.push_back(m_runtime->inputNames[i].c_str());
        }
        const char* outputName = m_runtime->outputName.c_str();
        std::vector<Ort::Value> outputs = m_runtime->session.Run(
            Ort::RunOptions { nullptr }, names.data(), values.data(), values.size(), &outputName, 1);

        const std::vector<std::int64_t> outShape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
        if (outShape.size() != 3 || outShape[0] != static_cast<std::int64_t>(batch)
            || outShape[1] != static_cast<std::int64_t>(length)
            || outShape[2] != semantic_model::kDimension) {
            qCWarning(lcApp) << "semantic model returned an unexpected shape";
            return { };
        }
        const float* hidden = outputs.front().GetTensorData<float>();
        constexpr int kDim = semantic_model::kDimension;

        // mean over real tokens then unit length which is what sentence transformers does for this model
        QList<Embedding> out;
        out.reserve(static_cast<qsizetype>(batch));
        for (size_t row = 0; row < batch; ++row) {
            Embedding vector(kDim, 0.0f);
            const size_t tokens = encoded[row].size();
            for (size_t col = 0; col < tokens; ++col) {
                const float* token = hidden + (row * length + col) * kDim;
                for (int d = 0; d < kDim; ++d)
                    vector[d] += token[d];
            }
            double norm = 0.0;
            for (float& value : vector) {
                value /= static_cast<float>(tokens);
                norm += static_cast<double>(value) * value;
            }
            const auto scale = static_cast<float>(1.0 / std::max(std::sqrt(norm), 1e-12));
            for (float& value : vector)
                value *= scale;
            out << vector;
        }
        return out;
    } catch (const Ort::Exception& error) {
        qCWarning(lcApp) << "semantic inference failed, code" << error.GetOrtErrorCode();
        return { };
    }
}

} // namespace clipvault
