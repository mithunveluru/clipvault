#pragma once

#include "../core/ClipboardItem.h"

#include <QDeadlineTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <functional>
#include <optional>

namespace clipvault {

// holds detected secrets in memory only and wipes each one when its time runs out
class EphemeralStore : public QObject {
    Q_OBJECT
public:
    // called on expiry so the owner can clear the system clipboard if it still holds the text
    using ClipboardClearer = std::function<void(const QString& text)>;
    using Wiper = std::function<void(QString& text)>;

    explicit EphemeralStore(QObject* parent = nullptr);
    ~EphemeralStore() override;

    EphemeralStore(const EphemeralStore&) = delete;
    EphemeralStore& operator=(const EphemeralStore&) = delete;

    struct Added {
        qint64 id = 0;
        bool inserted = false;
    };

    // identical text refreshes the existing entry and its deadline
    Added add(const QString& content, const QString& kind, ClipboardType type, const Provenance& source,
        std::chrono::milliseconds ttl);

    // newest first with negative ids and the secret kind set
    [[nodiscard]] QList<ClipboardItem> items() const;
    [[nodiscard]] std::optional<ClipboardItem> find(qint64 id) const;
    [[nodiscard]] bool contains(qint64 id) const { return indexOf(id) >= 0; }
    [[nodiscard]] int count() const { return static_cast<int>(m_entries.size()); }

    // whole seconds left rounded up or minus one when absent
    [[nodiscard]] int remainingSeconds(qint64 id) const;

    // hands the text to the caller who must wipe it once done
    std::optional<ClipboardItem> take(qint64 id);

    bool remove(qint64 id);
    void clear();

    void setClipboardClearer(ClipboardClearer clearer) { m_clearClipboard = std::move(clearer); }
    // tests observe the wipe through this hook
    void setWiper(Wiper wiper) { m_wipe = std::move(wiper); }

signals:
    void changed();

private:
    struct Entry {
        ClipboardItem item;
        QDeadlineTimer deadline;
    };

    [[nodiscard]] qsizetype indexOf(qint64 id) const;
    void expireDue();
    void schedule();

    QList<Entry> m_entries;
    QTimer m_timer;
    qint64 m_nextId = -2;
    ClipboardClearer m_clearClipboard;
    Wiper m_wipe;
};

} // namespace clipvault
