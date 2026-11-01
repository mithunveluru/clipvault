#include "EmbeddingEngine.h"

#include <QMetaObject>

namespace clipvault {

void EmbeddingWorker::load(const Factory& factory)
{
    if (m_stopping.load())
        return;
    m_provider.reset();
    ProviderFailure failure = ProviderFailure::None;
    m_provider = factory(&failure);
    if (!m_provider) {
        emit loaded(int(failure == ProviderFailure::None ? ProviderFailure::Failed : failure), QString(), 0);
        return;
    }
    emit loaded(int(ProviderFailure::None), m_provider->modelId(), m_provider->dimension());
}

void EmbeddingWorker::embed(quint64 job, const QStringList& texts)
{
    if (m_stopping.load())
        return;
    QList<Embedding> vectors;
    if (m_provider)
        vectors = m_provider->embed(texts);
    emit embedded(job, vectors);
}

EmbeddingEngine::EmbeddingEngine(QObject* parent)
    : QObject(parent)
    , m_worker(std::make_unique<EmbeddingWorker>())
{
    qRegisterMetaType<QList<clipvault::Embedding>>();
    m_thread.setObjectName(QStringLiteral("clipvault-embed"));
    m_worker->moveToThread(&m_thread);
    connect(m_worker.get(), &EmbeddingWorker::loaded, this, &EmbeddingEngine::loaded);
    connect(m_worker.get(), &EmbeddingWorker::embedded, this, &EmbeddingEngine::embedded);
    m_thread.start(QThread::LowPriority);
}

EmbeddingEngine::~EmbeddingEngine()
{
    m_worker->stop();
    m_thread.quit();
    m_thread.wait();
    // the thread has finished so deleting here cannot race a running job
    m_worker.reset();
}

void EmbeddingEngine::load(Factory factory)
{
    EmbeddingWorker* worker = m_worker.get();
    QMetaObject::invokeMethod(
        worker, [worker, factory = std::move(factory)] { worker->load(factory); }, Qt::QueuedConnection);
}

quint64 EmbeddingEngine::embed(const QStringList& texts)
{
    const quint64 job = ++m_nextJob;
    EmbeddingWorker* worker = m_worker.get();
    QMetaObject::invokeMethod(
        worker, [worker, job, texts] { worker->embed(job, texts); }, Qt::QueuedConnection);
    return job;
}

} // namespace clipvault
