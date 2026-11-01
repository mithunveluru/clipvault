#pragma once

#include "core/EmbeddingProvider.h"
#include "core/WordPieceTokenizer.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <cmath>
#include <memory>

namespace clipvault::test {

// what the fake saw on the worker thread so tests can inspect it from the main thread
struct FakeEmbeddingProbe {
    mutable QMutex mutex;
    QStringList embedded;
    QList<Qt::HANDLE> threads;
    int destroyed = 0;

    [[nodiscard]] QStringList texts() const
    {
        const QMutexLocker lock(&mutex);
        return embedded;
    }
    [[nodiscard]] int destroyedCount() const
    {
        const QMutexLocker lock(&mutex);
        return destroyed;
    }
};

// deterministic bag of words vectors where listed synonyms share a bucket
class FakeEmbeddingProvider final : public EmbeddingProvider {
public:
    static constexpr int kDim = 384;

    explicit FakeEmbeddingProvider(std::shared_ptr<FakeEmbeddingProbe> probe = { }, int delayMs = 0,
        QHash<QString, QString> synonyms = { }, QString modelId = QStringLiteral("fake-1"))
        : m_probe(std::move(probe))
        , m_delayMs(delayMs)
        , m_synonyms(std::move(synonyms))
        , m_modelId(std::move(modelId))
    {
    }

    ~FakeEmbeddingProvider() override
    {
        if (m_probe) {
            const QMutexLocker lock(&m_probe->mutex);
            ++m_probe->destroyed;
        }
    }

    FakeEmbeddingProvider(const FakeEmbeddingProvider&) = delete;
    FakeEmbeddingProvider& operator=(const FakeEmbeddingProvider&) = delete;

    [[nodiscard]] QString modelId() const override { return m_modelId; }
    [[nodiscard]] int dimension() const override { return kDim; }

    [[nodiscard]] QList<Embedding> embed(const QStringList& texts) override
    {
        if (m_delayMs > 0)
            QThread::msleep(static_cast<unsigned long>(m_delayMs));
        if (m_probe) {
            const QMutexLocker lock(&m_probe->mutex);
            m_probe->embedded << texts;
            m_probe->threads << QThread::currentThreadId();
        }
        QList<Embedding> out;
        for (const QString& text : texts)
            out << vectorFor(text);
        return out;
    }

    [[nodiscard]] Embedding vectorFor(const QString& text) const
    {
        Embedding vector(kDim, 0.0f);
        for (const QString& token : WordPieceTokenizer::basicTokens(text)) {
            const QString word = m_synonyms.value(token, token);
            const QByteArray digest = QCryptographicHash::hash(word.toUtf8(), QCryptographicHash::Sha1);
            const auto bucket
                = static_cast<unsigned char>(digest.at(0)) | (static_cast<unsigned char>(digest.at(1)) << 8);
            vector[bucket % kDim] += 1.0f;
        }
        float norm = 0.0f;
        for (const float value : vector)
            norm += value * value;
        norm = std::sqrt(norm);
        if (norm == 0.0f)
            vector[0] = 1.0f;
        else
            for (float& value : vector)
                value /= norm;
        return vector;
    }

private:
    std::shared_ptr<FakeEmbeddingProbe> m_probe;
    int m_delayMs = 0;
    QHash<QString, QString> m_synonyms;
    QString m_modelId;
};

} // namespace clipvault::test
