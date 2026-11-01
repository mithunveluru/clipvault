#include "ProvenanceService.h"

#include "../core/Logging.h"
#include "../platform/SourceLocation.h"
#include "HistoryService.h"
#include "SettingsService.h"

#include <QRegularExpression>
#include <QUrl>

#include <utility>

namespace clipvault {
namespace {

bool sameWindow(const platform::WindowInfo& a, const platform::WindowInfo& b)
{
    return a.app == b.app && a.windowClass == b.windowClass && a.windowId == b.windowId;
}

QString displayApp(const platform::WindowInfo& window)
{
    return window.app.isEmpty() ? window.windowClass : window.app;
}

} // namespace

ProvenanceService::ProvenanceService(platform::FocusTracker& tracker, const SettingsService& settings,
    HistoryService& history, PathResolver resolvePath, QObject* parent)
    : QObject(parent)
    , m_tracker(tracker)
    , m_settings(settings)
    , m_history(history)
    , m_resolvePath(resolvePath ? std::move(resolvePath) : PathResolver(&platform::resolveSourcePath))
{
    connect(&m_tracker, &platform::FocusTracker::focusChanged, this, &ProvenanceService::onFocusChanged);
}

bool ProvenanceService::isRecordable(const platform::WindowInfo& window) const
{
    if (!m_settings.provenanceEnabled() || !window.isValid())
        return false;
    // substring so an entry also covers reverse domain app ids
    for (const QString& entry : m_settings.provenanceBlocklist()) {
        if (window.app.contains(entry, Qt::CaseInsensitive)
            || window.windowClass.contains(entry, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

Provenance ProvenanceService::sourceFor(const QString& clipText) const
{
    // the last foreign window so the popup itself is never the source
    const platform::WindowInfo window = m_tracker.lastForeignWindow();
    if (!isRecordable(window)) {
        qCDebug(lcFocus) << "clip recorded without a source";
        return { };
    }

    Provenance source;
    source.app = window.app;
    source.windowClass = window.windowClass;
    if (m_settings.provenanceTitles())
        source.title = window.title;
    source.path = urlPathOf(clipText);
    if (source.path.isEmpty())
        source.path = m_resolvePath(window);
    qCDebug(lcFocus) << "clip source recorded, title" << !source.title.isEmpty() << "path"
                     << !source.path.isEmpty();
    return source;
}

void ProvenanceService::noteCopiedBack(qint64 id)
{
    m_pendingCopyId = id;
}

void ProvenanceService::onFocusChanged(const platform::WindowInfo& window)
{
    if (m_pendingCopyId < 0 || !sameWindow(window, m_tracker.lastForeignWindow()))
        return;
    const qint64 id = std::exchange(m_pendingCopyId, -1);
    if (!isRecordable(window))
        return;
    m_history.setCopiedInto(id, displayApp(window));
}

bool ProvenanceService::clearProvenance()
{
    m_pendingCopyId = -1;
    return m_history.clearProvenance();
}

bool ProvenanceService::openSource(const QString& path) const
{
    return !path.isEmpty() && platform::openSourceLocation(path);
}

bool ProvenanceService::activateSource(const Provenance& source)
{
    platform::WindowInfo window;
    window.app = source.app;
    window.windowClass = source.windowClass;
    return window.isValid() && m_tracker.activate(window);
}

QString ProvenanceService::mechanismDescription() const
{
    return m_tracker.mechanismDescription();
}

QString ProvenanceService::urlPathOf(const QString& clipText)
{
    const QString text = clipText.trimmed();
    if (text.isEmpty() || text.contains(QRegularExpression(QStringLiteral(R"(\s)"))))
        return { };
    const QUrl url(text, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")))
        return { };
    return clipText.trimmed();
}

} // namespace clipvault
