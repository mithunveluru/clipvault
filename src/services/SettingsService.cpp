#include "SettingsService.h"

#include "../core/Logging.h"
#include "../platform/Autostart.h"

namespace clipvault {
namespace {
constexpr auto kRetention = "retention_seconds";
constexpr auto kMaxItems = "max_items";
constexpr auto kStartPaused = "start_paused";
constexpr auto kHotkey = "hotkey";
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
