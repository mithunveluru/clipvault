#pragma once

#include "../core/ClipboardItem.h"
#include "../core/SearchFilter.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace clipvault {

// All history SQL lives here and every statement is bound
class ClipboardRepository {
public:
    explicit ClipboardRepository(QSqlDatabase db);

    struct Stored {
        qint64 id = -1;
        bool inserted = false;
    };

    // Inserts or refreshes recency keeping the existing id and pin
    std::optional<Stored> upsert(const ClipboardItem& item, QString* error = nullptr);

    std::optional<ClipboardItem> findById(qint64 id) const;
    std::optional<ClipboardItem> findByHash(const QString& hash) const;

    // Pinned first then newest first
    QList<ClipboardItem> search(const QString& query, int limit) const;
    [[nodiscard]] QList<ClipboardItem> search(const SearchFilter& filter, int limit) const;
    // rows among ids that pass the app title and date parts of filter and are not secrets in no set order
    [[nodiscard]] QList<ClipboardItem> findFiltered(
        const QList<qint64>& ids, const SearchFilter& filter) const;

    bool setPinned(qint64 id, bool pinned);
    bool setCopiedInto(qint64 id, const QString& app);

    // forgets every source field and copied into value
    bool clearProvenance();
    // forgets every window title and keeps the rest of the source
    bool clearSourceTitles();

    bool remove(qint64 id);

    // folds the wal into the main file so deleted rows leave no copy behind
    bool checkpoint();

    // Pinned rows are only removed when includePinned
    bool clearAll(bool includePinned);

    // Returns rows removed
    int deleteExpired(const QDateTime& now);

    // Never removes pinned rows so history may exceed the limit
    int enforceLimit(int maxItems);

    // Zero seconds means never expire
    bool recomputeExpiry(int retentionSeconds);

    int count() const;
    int pinnedCount() const;

private:
    QSqlDatabase m_db;
};

} // namespace clipvault
