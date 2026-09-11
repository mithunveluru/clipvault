#include "SearchService.h"

namespace clipvault {

SearchService::SearchService(const ClipboardRepository& repository)
    : m_repository(repository)
{
}

QList<ClipboardItem> SearchService::search(const QString& query, int limit) const
{
    return m_repository.search(query, qBound(1, limit, 5000));
}

} // namespace clipvault
