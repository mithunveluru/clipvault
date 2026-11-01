#pragma once

#include "../core/EmbeddingProvider.h"

#include <QObject>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

namespace clipvault {

// lives on the embedding thread and only turns copied strings into vectors
class EmbeddingWorker : public QObject {
    Q_OBJECT
public:
    // runs on the embedding thread so hashing a large model never blocks the window
    using Factory = std::function<std::unique_ptr<EmbeddingProvider>(ProviderFailure*)>;

    void load(const Factory& factory);
    void embed(quint64 job, const QStringList& texts);

    // set from the main thread so jobs still queued at shutdown are skipped
    void stop() { m_stopping.store(true); }

signals:
    // failure is a ProviderFailure value
    void loaded(int failure, const QString& modelId, int dimension);
    void embedded(quint64 job, const QList<clipvault::Embedding>& vectors);

private:
    std::unique_ptr<EmbeddingProvider> m_provider;
    std::atomic<bool> m_stopping { false };
};

// owns the one embedding thread and hands results back to the main thread through queued signals
class EmbeddingEngine : public QObject {
    Q_OBJECT
public:
    using Factory = EmbeddingWorker::Factory;

    explicit EmbeddingEngine(QObject* parent = nullptr);
    // quits and waits so no job outlives the engine and queued jobs are dropped
    ~EmbeddingEngine() override;

    EmbeddingEngine(const EmbeddingEngine&) = delete;
    EmbeddingEngine& operator=(const EmbeddingEngine&) = delete;

    void load(Factory factory);
    // returns the job id echoed by embedded
    quint64 embed(const QStringList& texts);

signals:
    void loaded(int failure, const QString& modelId, int dimension);
    void embedded(quint64 job, const QList<clipvault::Embedding>& vectors);

private:
    QThread m_thread;
    std::unique_ptr<EmbeddingWorker> m_worker;
    quint64 m_nextJob = 0;
};

} // namespace clipvault
