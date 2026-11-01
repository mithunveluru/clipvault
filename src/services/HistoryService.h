#pragma once

#include "../core/ClipboardItem.h"
#include "../storage/ClipboardRepository.h"
#include "EphemeralStore.h"

#include <QObject>
#include <QString>

namespace clipvault {

class SettingsService;

// Turns raw clipboard text into a stored history entry
class HistoryService : public QObject {
    Q_OBJECT
public:
    enum class AddStatus {
        Inserted,
        Refreshed,
        IgnoredEmpty,
        RejectedTooLarge,
        Failed,
    };

    struct AddOutcome {
        AddStatus status = AddStatus::Failed;
        qint64 id = -1;
        bool stored() const { return status == AddStatus::Inserted || status == AddStatus::Refreshed; }
    };

    // Caps entry size so a runaway copy cannot stall
    static constexpr int kMaxContentChars = 2 * 1024 * 1024;

    HistoryService(ClipboardRepository& repository, const SettingsService& settings,
                   QObject* parent = nullptr);

    AddOutcome add(const QString& content, const Provenance& source = {});

    // pinning a secret writes it to disk and unpinning moves it back to memory
    bool setPinned(qint64 id, bool pinned);
    bool remove(qint64 id);

    // app the entry was pasted into after a copy back
    bool setCopiedInto(qint64 id, const QString& app);
    // forgets every source and copied into value
    bool clearProvenance();

    // Pinned entries are kept unless explicitly included
    bool clearAll(bool includePinned);

    // Retention sweep plus limit trim returns rows removed
    int runMaintenance();

    // Rewrites stored expiry after a retention change
    void applyRetentionChange();

    int count() const;

    static QString hashOf(const QString& content);

    // secrets held in memory only and listed with negative ids
    [[nodiscard]] EphemeralStore& ephemeral() { return m_ephemeral; }
    [[nodiscard]] const EphemeralStore& ephemeral() const { return m_ephemeral; }

signals:
    void historyChanged();
    // Content free reason suitable for the status bar
    void captureRejected(const QString& reason);

private:
    AddOutcome addSecret(const QString& content, const QString& kind, const Provenance& source);
    [[nodiscard]] QList<ClipboardItem> pinnedSecrets() const;
    bool pinSecret(qint64 id);
    bool unpinSecret(const ClipboardItem& item);

    ClipboardRepository& m_repository;
    const SettingsService& m_settings;
    EphemeralStore m_ephemeral;
};

} // namespace clipvault
