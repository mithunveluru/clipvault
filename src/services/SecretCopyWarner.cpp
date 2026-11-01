#include "SecretCopyWarner.h"

#include "../core/Logging.h"
#include "SettingsService.h"

namespace clipvault {

SecretCopyWarner::SecretCopyWarner(
    platform::FocusTracker& focus, const SettingsService& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_window.setSingleShot(true);
    connect(&m_window, &QTimer::timeout, this, &SecretCopyWarner::disarm);
    connect(&focus, &platform::FocusTracker::focusChanged, this, &SecretCopyWarner::onFocusChanged);
}

void SecretCopyWarner::secretWrittenBack(const QString& kind, std::chrono::seconds window)
{
    if (!m_settings.secretCopyBackWarning() || window.count() <= 0) {
        disarm();
        return;
    }
    m_kind = kind;
    m_armed = true;
    m_window.start(window);
}

void SecretCopyWarner::disarm()
{
    m_armed = false;
    m_kind.clear();
    m_window.stop();
}

bool SecretCopyWarner::isRisky(const platform::WindowInfo& window) const
{
    const QString app = window.app.toLower();
    const QString windowClass = window.windowClass.toLower();
    for (const QString& entry : m_settings.secretRiskyApps()) {
        const QString risky = entry.trimmed().toLower();
        if (risky.isEmpty())
            continue;
        // desktop ids such as com dot slack dot slack hold the plain name inside
        if (app.contains(risky) || windowClass.contains(risky))
            return true;
    }
    return false;
}

void SecretCopyWarner::onFocusChanged(const platform::WindowInfo& window)
{
    if (!m_armed || !isRisky(window))
        return;
    const QString label = QString(m_kind).replace(QLatin1Char('_'), QLatin1Char(' '));
    const QString app = window.app.isEmpty() ? window.windowClass : window.app;
    qCInfo(lcSecret) << "chat app focused while a secret is on the clipboard, kind" << m_kind;
    disarm();
    emit warning(tr("A %1 is on the clipboard and %2 is now focused. ClipVault cannot see pastes, "
                    "so check before you send.")
            .arg(label, app));
}

} // namespace clipvault
