#pragma once

#include "../core/ClipboardItem.h"
#include "../storage/ClipboardRepository.h"

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

    bool setPinned(qint64 id, bool pinned);
    bool remove(qint64 id);

    // Pinned entries are kept unless explicitly included
    bool clearAll(bool includePinned);

    // Retention sweep plus limit trim returns rows removed
    int runMaintenance();

    // Rewrites stored expiry after a retention change
    void applyRetentionChange();

    int count() const;

    static QString hashOf(const QString& content);

signals:
    void historyChanged();
    // Content free reason suitable for the status bar
    void captureRejected(const QString& reason);

private:
    ClipboardRepository& m_repository;
    const SettingsService& m_settings;
};

} // namespace clipvault
