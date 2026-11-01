#include "SearchService.h"

#include <QRegularExpression>
#include <QStringList>

namespace clipvault {
namespace {

// a prefix only counts at the start of a word so urls and drive letters stay plain text
const QRegularExpression& tokenPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"re((?:^|(?<=\s))(app|from|title|date):(?:"([^"]*)"?|(\S*)))re"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
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
        const QString value = match.hasCaptured(2) ? match.captured(2) : match.captured(3);

        // an empty value is a prefix still being typed and filters nothing
        if (key == QLatin1String("app") || key == QLatin1String("from")) {
            filter.app = value.trimmed();
        } else if (key == QLatin1String("title")) {
            filter.title = value.trimmed();
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
    if (app.contains(QRegularExpression(QStringLiteral(R"(\s)"))))
        return QStringLiteral("app:\"%1\"").arg(app);
    return QStringLiteral("app:") + app;
}

} // namespace clipvault
