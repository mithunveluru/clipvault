#pragma once

#include "../core/Provenance.h"
#include "../platform/FocusTracker.h"
#include "HistoryService.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

namespace clipvault {

class SettingsService;

// decides what of the focused window is kept as the source of a clip
class ProvenanceService : public QObject {
    Q_OBJECT
public:
    using PathResolver = std::function<QString(const platform::WindowInfo&)>;

    // the resolver defaults to the platform one and tests swap it out
    ProvenanceService(platform::FocusTracker& tracker, const SettingsService& settings,
        HistoryService& history, PathResolver resolvePath = { }, QObject* parent = nullptr);

    // empty when tracking is off or the window is blocklisted or unknown
    [[nodiscard]] Provenance sourceFor(const QString& clipText) const;

    // adds the clip with its source and remembers the live window for activate
    HistoryService::AddOutcome capture(const QString& clipText);

    // the next foreign window to take focus is stored as where the item went
    void noteCopiedBack(qint64 id);

    bool clearProvenance();

    // only a local absolute path and never a url
    [[nodiscard]] static bool canOpen(const QString& path);
    [[nodiscard]] bool openSource(const QString& path) const;
    // only clips captured in this run know which window to raise
    [[nodiscard]] bool canActivate(qint64 id) const;
    bool activateSource(qint64 id, const Provenance& source);

    [[nodiscard]] QString mechanismDescription() const;

    // the clip itself when it is one http or https url and empty otherwise
    [[nodiscard]] static QString urlPathOf(const QString& clipText);

private:
    [[nodiscard]] bool isRecordable(const platform::WindowInfo& window) const;
    [[nodiscard]] Provenance sourceOf(const platform::WindowInfo& window, const QString& clipText) const;
    void onFocusChanged(const platform::WindowInfo& window);

    platform::FocusTracker& m_tracker;
    const SettingsService& m_settings;
    HistoryService& m_history;
    PathResolver m_resolvePath;
    qint64 m_pendingCopyId = -1;
    // item id to tracker window id seen this run and never stored or logged
    QHash<qint64, quint64> m_liveWindows;
};

} // namespace clipvault
