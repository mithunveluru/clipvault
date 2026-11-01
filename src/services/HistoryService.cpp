#include "HistoryService.h"

#include "../core/Classifier.h"
#include "../core/Logging.h"
#include "SettingsService.h"

#include <QCryptographicHash>
#include <QDateTime>

namespace clipvault {
namespace {

bool isBlank(const QString& text)
{
    for (const QChar ch : text) {
        if (!ch.isSpace())
            return false;
    }
    return true;
}

} // namespace

HistoryService::HistoryService(ClipboardRepository& repository, const SettingsService& settings,
                              QObject* parent)
    : QObject(parent)
    , m_repository(repository)
    , m_settings(settings)
{
}

QString HistoryService::hashOf(const QString& content)
{
    // Exact bytes so paste back is faithful
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex());
}

HistoryService::AddOutcome HistoryService::add(const QString& content, const Provenance& source)
{
    if (isBlank(content))
        return { AddStatus::IgnoredEmpty, -1 };

    if (content.size() > kMaxContentChars) {
        qCWarning(lcApp) << "clipboard entry rejected, size" << content.size() << "chars";
        emit captureRejected(tr("Clipboard entry was too large to store (%1 MB limit).")
                                 .arg(kMaxContentChars / (1024 * 1024)));
        return { AddStatus::RejectedTooLarge, -1 };
    }

    ClipboardItem item;
    item.content = content;
    item.type = classify(content);
    item.contentHash = hashOf(content);
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.source = source;

    const int retention = m_settings.retentionSeconds();
    if (retention > 0)
        item.expiresAt = item.createdAt.addSecs(retention);

    QString error;
    const auto stored = m_repository.upsert(item, &error);
    if (!stored) {
        qCCritical(lcApp) << "could not store clipboard entry:" << error;
        emit captureRejected(tr("Could not save the clipboard entry."));
        return { AddStatus::Failed, -1 };
    }

    if (stored->inserted)
        m_repository.enforceLimit(m_settings.maxItems());

    qCDebug(lcApp) << (stored->inserted ? "history entry stored" : "history entry refreshed")
                   << redact(content) << "type" << toString(item.type);

    emit historyChanged();
    return { stored->inserted ? AddStatus::Inserted : AddStatus::Refreshed, stored->id };
}

bool HistoryService::setPinned(qint64 id, bool pinned)
{
    if (!m_repository.setPinned(id, pinned))
        return false;
    qCDebug(lcApp) << (pinned ? "item pinned" : "item unpinned");
    emit historyChanged();
    return true;
}

bool HistoryService::remove(qint64 id)
{
    if (!m_repository.remove(id))
        return false;
    qCDebug(lcApp) << "history item deleted";
    emit historyChanged();
    return true;
}

bool HistoryService::clearAll(bool includePinned)
{
    if (!m_repository.clearAll(includePinned))
        return false;
    qCInfo(lcApp) << "history cleared" << (includePinned ? "(including pinned)" : "(pinned kept)");
    emit historyChanged();
    return true;
}

int HistoryService::runMaintenance()
{
    const int expired = m_repository.deleteExpired(QDateTime::currentDateTimeUtc());
    const int trimmed = m_repository.enforceLimit(m_settings.maxItems());
    const int removed = expired + trimmed;
    if (removed > 0) {
        qCInfo(lcApp) << "maintenance removed" << expired << "expired and" << trimmed << "over-limit entries";
        emit historyChanged();
    }
    return removed;
}

void HistoryService::applyRetentionChange()
{
    m_repository.recomputeExpiry(m_settings.retentionSeconds());
    runMaintenance();
    emit historyChanged();
}

int HistoryService::count() const
{
    return m_repository.count();
}

} // namespace clipvault
