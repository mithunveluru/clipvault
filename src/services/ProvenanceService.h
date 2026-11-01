#pragma once

#include "../core/Provenance.h"
#include "../platform/FocusTracker.h"

#include <QObject>
#include <QString>

#include <functional>

namespace clipvault {

class HistoryService;
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

    // the next foreign window to take focus is stored as where the item went
    void noteCopiedBack(qint64 id);

    bool clearProvenance();

    [[nodiscard]] bool openSource(const QString& path) const;
    bool activateSource(const Provenance& source);

    [[nodiscard]] QString mechanismDescription() const;

    // the clip itself when it is one http or https url and empty otherwise
    [[nodiscard]] static QString urlPathOf(const QString& clipText);

private:
    [[nodiscard]] bool isRecordable(const platform::WindowInfo& window) const;
    void onFocusChanged(const platform::WindowInfo& window);

    platform::FocusTracker& m_tracker;
    const SettingsService& m_settings;
    HistoryService& m_history;
    PathResolver m_resolvePath;
    qint64 m_pendingCopyId = -1;
};

} // namespace clipvault
