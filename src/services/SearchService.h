#pragma once

#include "../core/ClipboardItem.h"
#include "../storage/ClipboardRepository.h"

#include <QList>
#include <QString>

namespace clipvault {

// Owns result ordering and the result cap
class SearchService {
public:
    explicit SearchService(const ClipboardRepository& repository);

    // Pinned first then newest first
    QList<ClipboardItem> search(const QString& query, int limit = kDefaultLimit) const;

    // Quick pick surface not an archive browser
    static constexpr int kDefaultLimit = 200;

private:
    const ClipboardRepository& m_repository;
};

} // namespace clipvault
