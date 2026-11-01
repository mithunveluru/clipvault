#pragma once

#include "../core/ClipboardItem.h"
#include "../core/SearchFilter.h"
#include "../storage/ClipboardRepository.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace clipvault {

// Owns result ordering and the result cap
class SearchService {
public:
    explicit SearchService(const ClipboardRepository& repository);

    // Pinned first then newest first
    QList<ClipboardItem> search(const QString& query, int limit = kDefaultLimit) const;

    // app from title and date prefixes become filters and the rest stays plain content text
    [[nodiscard]] static SearchFilter parse(const QString& query, const QDateTime& now);
    // true once the query holds a title prefix even with no value yet
    [[nodiscard]] static bool mentionsTitle(const QString& query);
    // query text that filters by one app quoting names with spaces
    [[nodiscard]] static QString appQuery(const QString& app);

    // Quick pick surface not an archive browser
    static constexpr int kDefaultLimit = 200;

private:
    const ClipboardRepository& m_repository;
};

} // namespace clipvault
