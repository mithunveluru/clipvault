#include "HistoryService.h"

#include "../core/Classifier.h"
#include "../core/Logging.h"
#include "../core/SecretDetector.h"
#include "../core/SecureZero.h"
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
    connect(&m_ephemeral, &EphemeralStore::changed, this, &HistoryService::historyChanged);
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

    // a secret never reaches the repository so neither its text nor its hash lands on disk
    if (m_settings.secretRadarEnabled()) {
        const SecretVerdict verdict = detectSecret(content);
        if (verdict.isSecret())
            return addSecret(content, verdict.kind, source);
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

HistoryService::AddOutcome HistoryService::addSecret(const QString& content, const QString& kind,
                                                     const Provenance& source)
{
    // compared directly because hashing would leave a guessable fingerprint
    for (const ClipboardItem& stored : pinnedSecrets()) {
        if (stored.content == content) {
            qCInfo(lcSecret) << "copied secret is already pinned, kind" << kind << "length" << content.size();
            return { AddStatus::Refreshed, stored.id };
        }
    }

    const auto added = m_ephemeral.add(content, kind, classify(content), source,
                                       std::chrono::seconds(m_settings.secretTtlSeconds()));
    qCInfo(lcSecret) << "secret kept in memory only, kind" << kind << "length" << content.size();
    return { added.inserted ? AddStatus::Inserted : AddStatus::Refreshed, added.id };
}

QList<ClipboardItem> HistoryService::pinnedSecrets() const
{
    const int pinned = m_repository.pinnedCount();
    if (pinned == 0)
        return { };
    // pinned rows sort first so this limit returns exactly them
    QList<ClipboardItem> secrets;
    for (ClipboardItem& item : m_repository.search(SearchFilter { }, pinned)) {
        if (item.pinned && !item.secretKind.isEmpty())
            secrets.append(std::move(item));
    }
    return secrets;
}

bool HistoryService::pinSecret(qint64 id)
{
    std::optional<ClipboardItem> item = m_ephemeral.find(id);
    if (!item)
        return false;

    // pinning is consent so from here the secret is stored like any pinned entry
    item->pinned = true;
    item->contentHash = hashOf(item->content);
    item->createdAt = QDateTime::currentDateTimeUtc();
    const int retention = m_settings.retentionSeconds();
    item->expiresAt = retention > 0 ? std::optional(item->createdAt.addSecs(retention)) : std::nullopt;

    QString error;
    const auto stored = m_repository.upsert(*item, &error);
    if (stored && !stored->inserted)
        m_repository.setPinned(stored->id, true);
    if (!stored) {
        secureZero(item->content);
        qCCritical(lcSecret) << "could not pin secret:" << error;
        emit captureRejected(tr("Could not save the clipboard entry."));
        return false;
    }

    qCInfo(lcSecret) << "secret pinned to disk, kind" << item->secretKind << "length" << item->content.size();
    m_ephemeral.remove(id);
    secureZero(item->content);
    return true;
}

bool HistoryService::unpinSecret(const ClipboardItem& item)
{
    if (!m_repository.remove(item.id))
        return false;
    m_repository.checkpoint();
    qCInfo(lcSecret) << "secret unpinned and moved back to memory, kind" << item.secretKind << "length"
                     << item.content.size();
    m_ephemeral.add(item.content, item.secretKind, item.type, item.source,
                    std::chrono::seconds(m_settings.secretTtlSeconds()));
    return true;
}

bool HistoryService::setPinned(qint64 id, bool pinned)
{
    if (id < 0)
        return pinned && pinSecret(id);
    if (!pinned) {
        std::optional<ClipboardItem> item = m_repository.findById(id);
        if (item && !item->secretKind.isEmpty()) {
            const bool moved = unpinSecret(*item);
            secureZero(item->content);
            return moved;
        }
    }

    if (!m_repository.setPinned(id, pinned))
        return false;
    qCDebug(lcApp) << (pinned ? "item pinned" : "item unpinned");
    emit historyChanged();
    return true;
}

bool HistoryService::remove(qint64 id)
{
    if (id < 0)
        return m_ephemeral.remove(id);
    if (!m_repository.remove(id))
        return false;
    // a deleted entry may be a pinned secret so its wal copy goes too
    m_repository.checkpoint();
    qCDebug(lcApp) << "history item deleted";
    emit historyChanged();
    return true;
}

bool HistoryService::setCopiedInto(qint64 id, const QString& app)
{
    if (!m_repository.setCopiedInto(id, app))
        return false;
    emit historyChanged();
    return true;
}

bool HistoryService::clearProvenance()
{
    if (!m_repository.clearProvenance())
        return false;
    qCInfo(lcApp) << "provenance cleared";
    emit historyChanged();
    return true;
}

bool HistoryService::clearAll(bool includePinned)
{
    m_ephemeral.clear();
    if (!m_repository.clearAll(includePinned))
        return false;
    m_repository.checkpoint();
    qCInfo(lcApp) << "history cleared" << (includePinned ? "(including pinned)" : "(pinned kept)");
    emit historyChanged();
    return true;
}

int HistoryService::runMaintenance()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int expired = m_repository.deleteExpired(now);
    // pinned secrets still follow retention unless the user asked them to stay
    if (!m_settings.secretPinnedNeverExpire()) {
        bool removedSecret = false;
        for (const ClipboardItem& item : pinnedSecrets()) {
            if (item.expiresAt && *item.expiresAt <= now && m_repository.remove(item.id)) {
                ++expired;
                removedSecret = true;
            }
        }
        if (removedSecret)
            m_repository.checkpoint();
    }
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
    return m_repository.count() + m_ephemeral.count();
}

} // namespace clipvault
