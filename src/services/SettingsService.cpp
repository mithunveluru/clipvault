#include "SettingsService.h"

#include "../core/Logging.h"
#include "../platform/Autostart.h"

namespace clipvault {
namespace {
constexpr auto kRetention = "retention_seconds";
constexpr auto kMaxItems = "max_items";
constexpr auto kStartPaused = "start_paused";
constexpr auto kHotkey = "hotkey";
constexpr auto kSecretRadar = "secret_radar_enabled";
constexpr auto kSecretTtl = "secret_ttl_seconds";
constexpr auto kSecretPinnedNeverExpire = "secret_pinned_never_expire";
constexpr auto kSecretCopyBackWarning = "secret_copyback_warning";
constexpr auto kSecretRiskyApps = "secret_risky_apps";
constexpr auto kProvenance = "provenance_enabled";
constexpr auto kProvenanceTitles = "provenance_titles";
constexpr auto kProvenanceBlocklist = "provenance_blocklist";
constexpr auto kSemanticEnabled = "semantic_enabled";
constexpr auto kSemanticModelDir = "semantic_model_dir";
} // namespace

SettingsService::SettingsService(SettingsRepository& repository, QObject* parent)
    : QObject(parent)
    , m_repository(repository)
{
}

QList<SettingsService::RetentionOption> SettingsService::retentionOptions()
{
    return {
        { QObject::tr("1 hour"),  60 * 60 },
        { QObject::tr("1 day"),   24 * 60 * 60 },
        { QObject::tr("7 days"),  7 * 24 * 60 * 60 },
        { QObject::tr("30 days"), 30 * 24 * 60 * 60 },
        { QObject::tr("Never"),   0 },
    };
}

QString SettingsService::read(const QString& key, const QString& fallback) const
{
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd())
        return cached.value();

    QString stored;
    switch (m_repository.value(key, &stored)) {
    case SettingsRepository::Lookup::Found:
        m_cache.insert(key, stored);
        return stored;
    case SettingsRepository::Lookup::Missing:
        m_cache.insert(key, fallback);
        return fallback;
    case SettingsRepository::Lookup::Failed:
        break;
    }

    // Uncached so a later read can recover the value
    qCCritical(lcApp) << "settings read failed, using default for" << key;
    return fallback;
}

void SettingsService::write(const QString& key, const QString& value)
{
    if (read(key, QString()) == value && m_cache.contains(key))
        return;
    if (!m_repository.setValue(key, value)) {
        qCWarning(lcApp) << "could not persist setting" << key;
        return;
    }
    m_cache.insert(key, value);
    emit changed(key);
}

int SettingsService::retentionSeconds() const
{
    bool ok = false;
    const int value = read(QLatin1String(kRetention), QString::number(kDefaultRetentionSeconds)).toInt(&ok);
    return (ok && value >= 0) ? value : kDefaultRetentionSeconds;
}

void SettingsService::setRetentionSeconds(int seconds)
{
    write(QLatin1String(kRetention), QString::number(qMax(0, seconds)));
}

int SettingsService::maxItems() const
{
    bool ok = false;
    const int value = read(QLatin1String(kMaxItems), QString::number(kDefaultMaxItems)).toInt(&ok);
    return (ok && value > 0) ? value : kDefaultMaxItems;
}

void SettingsService::setMaxItems(int maxItems)
{
    write(QLatin1String(kMaxItems), QString::number(qBound(10, maxItems, 1000000)));
}

bool SettingsService::startPaused() const
{
    return read(QLatin1String(kStartPaused), QStringLiteral("0")) == QLatin1String("1");
}

void SettingsService::setStartPaused(bool paused)
{
    write(QLatin1String(kStartPaused), paused ? QStringLiteral("1") : QStringLiteral("0"));
}

QString SettingsService::hotkey() const
{
    const QString value = read(QLatin1String(kHotkey), QLatin1String(kDefaultHotkey));
    return value.isEmpty() ? QLatin1String(kDefaultHotkey) : value;
}

void SettingsService::setHotkey(const QString& sequence)
{
    write(QLatin1String(kHotkey), sequence);
}

bool SettingsService::readFlag(const char* key, bool fallback) const
{
    return read(QLatin1String(key), fallback ? QStringLiteral("1") : QStringLiteral("0")) == QLatin1String("1");
}

void SettingsService::writeFlag(const char* key, bool value)
{
    write(QLatin1String(key), value ? QStringLiteral("1") : QStringLiteral("0"));
}

QStringList SettingsService::readList(const char* key, const char* fallback) const
{
    QStringList out;
    const QStringList parts = read(QLatin1String(key), QLatin1String(fallback)).split(QLatin1Char(','));
    for (const QString& part : parts) {
        const QString item = part.trimmed().toLower();
        if (!item.isEmpty())
            out << item;
    }
    return out;
}

void SettingsService::writeList(const char* key, const QStringList& values)
{
    QStringList cleaned;
    for (const QString& value : values) {
        const QString item = value.trimmed().toLower().remove(QLatin1Char(','));
        if (!item.isEmpty() && !cleaned.contains(item))
            cleaned << item;
    }
    write(QLatin1String(key), cleaned.join(QLatin1Char(',')));
}

bool SettingsService::secretRadarEnabled() const
{
    return readFlag(kSecretRadar, true);
}

void SettingsService::setSecretRadarEnabled(bool enabled)
{
    writeFlag(kSecretRadar, enabled);
}

int SettingsService::secretTtlSeconds() const
{
    bool ok = false;
    const int value = read(QLatin1String(kSecretTtl), QString::number(kDefaultSecretTtlSeconds)).toInt(&ok);
    return ok ? qBound(kMinSecretTtlSeconds, value, kMaxSecretTtlSeconds) : kDefaultSecretTtlSeconds;
}

void SettingsService::setSecretTtlSeconds(int seconds)
{
    write(QLatin1String(kSecretTtl), QString::number(qBound(kMinSecretTtlSeconds, seconds, kMaxSecretTtlSeconds)));
}

bool SettingsService::secretPinnedNeverExpire() const
{
    return readFlag(kSecretPinnedNeverExpire, false);
}

void SettingsService::setSecretPinnedNeverExpire(bool enabled)
{
    writeFlag(kSecretPinnedNeverExpire, enabled);
}

bool SettingsService::secretCopyBackWarning() const
{
    return readFlag(kSecretCopyBackWarning, true);
}

void SettingsService::setSecretCopyBackWarning(bool enabled)
{
    writeFlag(kSecretCopyBackWarning, enabled);
}

QStringList SettingsService::secretRiskyApps() const
{
    return readList(kSecretRiskyApps, kDefaultSecretRiskyApps);
}

void SettingsService::setSecretRiskyApps(const QStringList& apps)
{
    writeList(kSecretRiskyApps, apps);
}

bool SettingsService::provenanceEnabled() const
{
    return readFlag(kProvenance, true);
}

void SettingsService::setProvenanceEnabled(bool enabled)
{
    writeFlag(kProvenance, enabled);
}

bool SettingsService::provenanceTitles() const
{
    return readFlag(kProvenanceTitles, false);
}

void SettingsService::setProvenanceTitles(bool enabled)
{
    writeFlag(kProvenanceTitles, enabled);
}

QStringList SettingsService::provenanceBlocklist() const
{
    return readList(kProvenanceBlocklist, kDefaultProvenanceBlocklist);
}

void SettingsService::setProvenanceBlocklist(const QStringList& apps)
{
    writeList(kProvenanceBlocklist, apps);
}

bool SettingsService::semanticEnabled() const
{
    return readFlag(kSemanticEnabled, false);
}

void SettingsService::setSemanticEnabled(bool enabled)
{
    writeFlag(kSemanticEnabled, enabled);
}

QString SettingsService::semanticModelDir() const
{
    return read(QLatin1String(kSemanticModelDir), QString());
}

void SettingsService::setSemanticModelDir(const QString& dir)
{
    write(QLatin1String(kSemanticModelDir), dir);
}

// The desktop entry is the only source of truth
bool SettingsService::launchOnStartup() const
{
    return platform::isAutostartEnabled();
}

void SettingsService::setLaunchOnStartup(bool enabled)
{
    if (!platform::setAutostartEnabled(enabled))
        qCWarning(lcApp) << "could not update the autostart entry";
}

} // namespace clipvault
