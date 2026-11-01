#include "SearchService.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace clipvault {
namespace {

// a prefix only counts at the start of a word so urls and drive letters stay plain text
const QRegularExpression& tokenPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"re((?:^|(?<=\s))(app|from|title|date):(?:"((?:[^"\\]|\\.?)*)"?|(\S*)))re"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// inside quotes a backslash keeps the next quote or backslash literal
QString unescapeQuoted(const QString& value)
{
    static const QRegularExpression escaped(QStringLiteral(R"re(\\(["\\]))re"));
    QString result = value;
    return result.replace(escaped, QStringLiteral("\\1"));
}

bool applyDate(const QString& value, const QDateTime& now, SearchFilter& filter)
{
    const QDate today = now.toLocalTime().date();
    const QString key = value.toLower();
    if (key == QLatin1String("today")) {
        filter.since = today.startOfDay();
        filter.until = today.addDays(1).startOfDay();
    } else if (key == QLatin1String("yesterday")) {
        filter.since = today.addDays(-1).startOfDay();
        filter.until = today.startOfDay();
    } else if (key == QLatin1String("week")) {
        filter.since = today.addDays(-6).startOfDay();
        filter.until = today.addDays(1).startOfDay();
    } else {
        return false;
    }
    return true;
}

} // namespace

SearchService::SearchService(const ClipboardRepository& repository)
    : m_repository(repository)
{
}

QList<ClipboardItem> SearchService::search(const QString& query, int limit) const
{
    return m_repository.search(parse(query, QDateTime::currentDateTime()), qBound(1, limit, 5000));
}

SearchFilter SearchService::parse(const QString& query, const QDateTime& now)
{
    SearchFilter filter;
    QStringList plain;
    qsizetype consumedUpTo = 0;

    auto matches = tokenPattern().globalMatch(query);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString key = match.captured(1).toLower();
        const QString value = match.hasCaptured(2) ? unescapeQuoted(match.captured(2)) : match.captured(3);

        // an empty value is a prefix still being typed and filters nothing
        // values stay untrimmed so an app name round trips and the repository trims later
        if (key == QLatin1String("app") || key == QLatin1String("from")) {
            filter.app = value;
        } else if (key == QLatin1String("title")) {
            filter.title = value;
        } else if (!value.isEmpty() && !applyDate(value, now, filter)) {
            continue;
        }

        plain << query.mid(consumedUpTo, match.capturedStart() - consumedUpTo).trimmed();
        consumedUpTo = match.capturedEnd();
    }
    plain << query.mid(consumedUpTo).trimmed();
    plain.removeAll(QString());
    filter.text = plain.join(QLatin1Char(' '));
    return filter;
}

bool SearchService::mentionsTitle(const QString& query)
{
    auto matches = tokenPattern().globalMatch(query);
    while (matches.hasNext()) {
        if (matches.next().captured(1).compare(QLatin1String("title"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString SearchService::appQuery(const QString& app)
{
    if (!app.contains(QRegularExpression(QStringLiteral(R"re([\s"])re"))))
        return QStringLiteral("app:") + app;
    QString escaped = app;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("app:\"%1\"").arg(escaped);
}

QList<ClipboardItem> SearchService::searchBlended(
    const QString& query, const QList<qint64>& semanticIds, int limit) const
{
    const int cap = qBound(1, limit, 5000);
    const SearchFilter filter = parse(query, QDateTime::currentDateTime());
    QList<ClipboardItem> results = m_repository.search(filter, cap);
    QSet<qint64> seen;
    for (const ClipboardItem& item : results)
        seen.insert(item.id);
    // semantic hits obey the same prefixes as the lexical side
    QHash<qint64, ClipboardItem> candidates;
    for (ClipboardItem& item : m_repository.findFiltered(semanticIds, filter))
        candidates.insert(item.id, std::move(item));
    for (const qint64 id : semanticIds) {
        if (results.size() >= cap)
            break;
        const auto found = candidates.constFind(id);
        if (found == candidates.constEnd() || seen.contains(id))
            continue;
        seen.insert(id);
        results.append(*found);
    }
    return results;
}

QString SearchService::semanticText(const QString& query)
{
    return parse(query, QDateTime::currentDateTime()).text;
}

} // namespace clipvault
